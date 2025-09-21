#include "innet.h"
#include "innet_lock.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <atomic>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h> // for gettimeofday


// --- BEGIN: C++11 compatibility ---
#if __cplusplus < 201402L
#include <memory>
#include <utility>

namespace std {

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

} // namespace std
#endif
// --- END: C++11 compatibility ---


static int get_time_ms(struct timespec *ts, int timeout_ms);
static bool is_event_type_enabled(innet_event_mask_t mask, uint32_t event_type);


struct Event {
    uint32_t event;
    innet_id_t sender;
    innet_id_t receiver;
    std::vector<uint8_t> payload; /* Payload is copied */
};


class Inbox {
public:
    /* Constructs an Inbox with a given capacity and policy. */
    Inbox(size_t cap = 16, int pol = INN_INBOX_POLICY_DEFAULT)
        : capacity(cap), policy(pol), closed(false) {
        m_size.store(0);
    }

    /* Pushes an event into the inbox. */
    bool push(const Event &ev, int timeout_ms = -1) {
        UniqueLock lk(mtx);
        if (closed) {
            return false;
        }

        if (queue.size() < capacity) {
            queue.push_back(ev);
            m_size.fetch_add(1, std::memory_order_relaxed);
            cv.signal();
            return true;
        }

        switch (policy) {
            case INN_INBOX_POLICY_DROP_OLD:
                if (!queue.empty()) queue.pop_front();
                queue.push_back(ev);
                cv.signal();
                return true;

            case INN_INBOX_POLICY_BLOCK: {
                if (timeout_ms <= 0) return false;

                timespec ts;
                get_time_ms(&ts, timeout_ms);
                while (!closed && queue.size() >= capacity) {
                    if (cv.timedwait(lk, &ts) == ETIMEDOUT) {
                        return false; // Timeout
                    }
                }
                if (closed) return false;

                queue.push_back(ev);
                m_size.fetch_add(1, std::memory_order_relaxed);
                cv.signal();
                return true;
            }

            case INN_INBOX_POLICY_DROP_NEW:
            default:
                return false;
        }
    }

    /* Peeks at the event at the front of the queue without removing it. */
    bool peek(Event &out, int timeout_ms = -1) {
        UniqueLock lk(mtx);

        auto condition_check = [this]() {
            return !queue.empty() || closed;
        };

        if (!condition_check()) {
            if (timeout_ms < 0) {
                while (!condition_check()) {
                    cv.wait(lk);
                }
            } else if (timeout_ms == 0) {
                return false; // Non-blocking, fail immediately
            } else {
                timespec ts;
                get_time_ms(&ts, timeout_ms);
                while (!condition_check()) {
                    if (cv.timedwait(lk, &ts) == ETIMEDOUT) {
                        return false; // Timeout
                    }
                }
            }
        }
        
        if (!queue.empty()) {
            out = queue.front(); // Copy only, do not remove
            return true;
        }

        return false;
    }

    /* Consumes (removes) the first event from the front of the queue. */
    void consume() {
        UniqueLock lk(mtx);
        if (!queue.empty()) {
            queue.pop_front();
            m_size.fetch_sub(1, std::memory_order_relaxed);
            cv.signal();
        }
    }

    /* Closes the inbox, preventing any new events from being pushed. */
    void close() {
        UniqueLock lk(mtx);
        closed = true;
        cv.broadcast();
    }

    void set_policy(int p) { policy = p; }
    void set_capacity(size_t cap) { capacity = cap; }

    /* Returns the current number of events in the inbox. */
    size_t size() const {
        return m_size.load(std::memory_order_relaxed);
    }

    /** Checks if the inbox is closed. */
    bool is_closed() {
        UniqueLock lk(mtx);
        return closed;
    }

private:
    size_t capacity;
    int policy;

    std::deque<Event> queue;
    std::atomic<size_t> m_size;
    bool closed;

    Mutex mtx;
    CondVar cv;
};


struct Node {
    innet_id_t id;
    std::string name;
    uint32_t flags;

    /* Cache */
    std::vector<uint8_t> cache;
    bool cache_valid;
    Mutex cache_lock;

    /* Subscribers */
    std::unordered_set<innet_id_t> subscribers;
    RWLock subs_lock;

    /* Inbox */
    std::unique_ptr<Inbox> inbox;

    /* Config */
    innet_event_mask_t event_mask;

};

class PendingMgr {
public:
    static int add_pending(const std::string& name, innet_id_t subscriber) {
        WriteGuard wr(s_pending_rwlock);
        s_pending_subs_map[name].push_back(subscriber);
        return INN_OK;
    }

    static int get_pending(const std::string& name, std::vector<innet_id_t>& out_subs) {
        ReadGuard rd(s_pending_rwlock);
        auto it = s_pending_subs_map.find(name);
        if (it == s_pending_subs_map.end()) {
            return INN_ERR_NOTFOUND;
        }
        out_subs = it->second;
        return INN_OK;
    }

    static int remove_pending(const std::string& name) {
        WriteGuard wr(s_pending_rwlock);
        if (s_pending_subs_map.erase(name) == 0) {
            return INN_ERR_NOTFOUND;
        }
        return INN_OK;
    }

    static int clear_pending(const std::string& name) {
        WriteGuard wr(s_pending_rwlock);
        auto it = s_pending_subs_map.find(name);
        if (it != s_pending_subs_map.end()) {
            it->second.clear();
        }
        return INN_OK;
    }

    static void clear_all() {
        WriteGuard wr(s_pending_rwlock);
        s_pending_subs_map.clear();
    }

private:
    static std::unordered_map<std::string, std::vector<innet_id_t>> s_pending_subs_map;
    static RWLock s_pending_rwlock;
};


class NodeMgr {
public:
    static int create_node(innet_id_t* out_id, const std::string& name, const innet_node_conf_t& conf) {
        if (!out_id) return INN_ERR_NULL_POINTER;

        auto node = std::make_shared<Node>();
        innet_id_t node_id = s_next_id.fetch_add(1);
        node->id = node_id;
        if (!name.empty()) node->name = name;
        node->flags = conf.flags;
        if (conf.inbox_policy != INN_INBOX_POLICY_NONE) {
            node->inbox = std::make_unique<Inbox>(conf.inbox_capacity, conf.inbox_policy);
        }
        node->event_mask = conf.event_mask;
        node->cache_valid = false;
        if (conf.cache_size > 0) {
            node->cache.resize(conf.cache_size);
        }

        WriteGuard wr(s_nodes_rwlock);
        if (!name.empty() && s_nodes_name_map.find(name) != s_nodes_name_map.end()) {
            return INN_ERR_EXIST;
        }

        if (!name.empty()) {
            s_nodes_name_map[name] = node_id;
        }

        s_nodes_map[node_id] = node;
        *out_id = node_id;
        return INN_OK;
    }

    static int remove_node(innet_id_t id) {
        WriteGuard wr(s_nodes_rwlock);
        auto it = s_nodes_map.find(id);
        if (it == s_nodes_map.end()) {
            return INN_ERR_NOTFOUND;
        }
        Node* node = it->second.get();
        std::string name = node->name;

        if (!name.empty()) {
            s_nodes_name_map.erase(name);
        }

        if (node->inbox)
            node->inbox->close();

        s_nodes_map.erase(it);
        return INN_OK;
    }

    static std::shared_ptr<Node> get_node(innet_id_t id) {
        ReadGuard rd(s_nodes_rwlock);
        auto it = s_nodes_map.find(id);
        if (it != s_nodes_map.end()) {
            return it->second;
        }
        return nullptr;
    }

    static int find_node_id(const std::string& name, innet_id_t* out_id) {
        if (!out_id) return INN_ERR_NULL_POINTER;
        ReadGuard rd(s_nodes_rwlock);
        auto it = s_nodes_name_map.find(name);
        if (it == s_nodes_name_map.end()) {
            return INN_ERR_NOTFOUND;
        }
        *out_id = it->second;
        return INN_OK;
    }

    static size_t node_count() {
        ReadGuard rd(s_nodes_rwlock);
        return s_nodes_map.size();
    }

    static void clear_all() {
        WriteGuard wr(s_nodes_rwlock);
        for (auto& pair : s_nodes_map) {
            Node* node = pair.second.get();
            if (node->inbox)
                node->inbox->close();
        }
        s_nodes_map.clear();
        s_nodes_name_map.clear();
    }

private:
    static std::unordered_map<innet_id_t, std::shared_ptr<Node>> s_nodes_map;
    static std::unordered_map<std::string, innet_id_t> s_nodes_name_map;
    static RWLock s_nodes_rwlock;
    static std::atomic<innet_id_t> s_next_id;
};

/* NodeMgr */
std::unordered_map<innet_id_t, std::shared_ptr<Node>> NodeMgr::s_nodes_map;
std::unordered_map<std::string, innet_id_t> NodeMgr::s_nodes_name_map;
RWLock NodeMgr::s_nodes_rwlock;
std::atomic<innet_id_t> NodeMgr::s_next_id{1};
/* PendingMgr */
std::unordered_map<std::string, std::vector<innet_id_t>> PendingMgr::s_pending_subs_map;
RWLock PendingMgr::s_pending_rwlock;


/* Get current time with timeout_ms offset */
/* Returns 0 on success */
static int get_time_ms(struct timespec *ts, int timeout_ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + timeout_ms / 1000;
    ts->tv_nsec = (long)tv.tv_usec * 1000 + (long)(timeout_ms % 1000) * 1000000;

    ts->tv_sec += ts->tv_nsec / 1000000000L;
    ts->tv_nsec %= 1000000000L;
    return 0;
}

/* Check if event type is enabled in mask */
/* Returns true if enabled */
static bool is_event_type_enabled(innet_event_mask_t mask, uint32_t event_type)
{
    return (mask & event_type) != 0;
}


/* Convert error code to string */
/* Returns error description string */
const char *innet_strerr(int err)
{
    switch (err)
    {
        case INN_OK: return "Success";
        case INN_INFO_PENDING: return "Pending";
        case INN_INFO_CACHE_PULLED: return "Pulled from cache";
        case INN_ERR_FAIL: return "General failure";
        case INN_ERR_TIMEOUT: return "Timeout";
        case INN_ERR_NOMEM: return "No memory";
        case INN_ERR_NOTFOUND: return "Not found";
        case INN_ERR_NOSUPPORT: return "Not supported";
        case INN_ERR_BUSY: return "Busy";
        case INN_ERR_INVALID: return "Invalid argument";
        case INN_ERR_ACCESS: return "Access denied";
        case INN_ERR_MSG_TOO_LARGE: return "Message size too large for cache";
        case INN_ERR_EXIST: return "Already exists";
        case INN_ERR_NODATA: return "No data available";
        case INN_ERR_INITIALIZED: return "Already initialized";
        case INN_ERR_NOTINITIALIZED: return "Not initialized";
        case INN_ERR_CLOSED: return "Node closed";
        case INN_ERR_NULL_POINTER: return "Null pointer";
        default: return "Unknown error";
    }
}


/* Initialize the innet library */
/* Returns INN_OK on success */
int innet_init(void)
{
    /* none */
    return INN_OK;
}

/* Deinitialize the innet library */
void innet_deinit(void)
{
    NodeMgr::clear_all();
    PendingMgr::clear_all();
}


/* Create a new node */
/* Returns INN_OK on success */
int innet_create_node(innet_id_t *id, const char *name, const innet_node_conf_t *conf)
{
    if (!id || !conf)
        return INN_ERR_NULL_POINTER;

    std::string node_name;
    if (name) node_name = name;

    int res = NodeMgr::create_node(id, node_name, *conf);
    if (res != INN_OK) return res;

    if (!node_name.empty()) {
        std::vector<innet_id_t> pending_subs;
        if (PendingMgr::get_pending(node_name, pending_subs) == INN_OK) {
            auto node = NodeMgr::get_node(*id);
            if (node) {
                WriteGuard wr(node->subs_lock);
                for (auto sub : pending_subs) {
                    node->subscribers.insert(sub);
                }
            }
            PendingMgr::remove_pending(node_name);
        }
    }

    return INN_OK;
}

/* Remove a node */
/* Returns INN_OK on success */
int innet_remove_node(innet_id_t id)
{
    return NodeMgr::remove_node(id);
}

/* Get number of nodes */
/* Returns number of nodes */
int innet_node_num(void)
{
    return (int)NodeMgr::node_count();
}

/* Find node by name */
/* Returns INN_OK on success */
int innet_find_node(const char *name, innet_id_t *id)
{
    return NodeMgr::find_node_id(name, id);
}


/* Subscribe to a publisher */
/* Returns INN_OK on success */
int innet_subscribe(innet_id_t subscriber, const char *pub_name)
{
    if (!pub_name)
        return INN_ERR_NULL_POINTER;

    innet_id_t pub_id = INN_INVALID_ID;
    int res = innet_find_node(pub_name, &pub_id);

    /* add to pending list */
    if (res == INN_ERR_NOTFOUND)
    {
        PendingMgr::add_pending(pub_name, subscriber);
        return INN_INFO_PENDING;
    }
    /* not a valid return res */
    if (res != INN_OK)
        return res;

    auto pub_node = NodeMgr::get_node(pub_id);
    auto sub_node = NodeMgr::get_node(subscriber);
    if (!pub_node || !sub_node ) 
        return INN_ERR_NOTFOUND;

    {
        WriteGuard wr(pub_node->subs_lock);
        pub_node->subscribers.insert(subscriber);
    }

    /* If latched and has data, send latched event immediately (skip for pure publish nodes) */
    bool should_do_latch = \
        (pub_node->flags & INN_CONF_LATCHED) &&
        (pub_node->cache_valid) &&
        (sub_node->inbox) &&
        (sub_node->event_mask & INN_EVENT_LATCHED);

    if (should_do_latch)
    {
        /* make a copy */
        std::vector<uint8_t> cache_data;
        {
            LockGuard lk(pub_node->cache_lock);
            cache_data = pub_node->cache;
        }

        Event ev;
        ev.event = INN_EVENT_LATCHED;
        ev.sender = pub_id;
        ev.receiver = subscriber;
        ev.payload = std::move(cache_data);
        if (sub_node->inbox) {
            sub_node->inbox->push(ev); // 对于latch事件，使用非阻塞push
        }
    }

    return INN_OK;
}


/* Unsubscribe from publisher */
/* Returns INN_OK on success */
int innet_unsubscribe(innet_id_t subscriber, innet_id_t publisher)
{
    auto pub_node = NodeMgr::get_node(publisher);
    if (!pub_node) 
        return INN_ERR_NOTFOUND;

    WriteGuard wr(pub_node->subs_lock);
    pub_node->subscribers.erase(subscriber);
    return INN_OK;
}


/* Publish data to subscribers */
/* Returns INN_OK on success */
int innet_publish(innet_id_t pub, const void *data, size_t size, uint32_t timeout_ms /* only work for block node */)
{
    auto pub_node = NodeMgr::get_node(pub);
    if (!pub_node) return INN_ERR_NOTFOUND;

    /* Update cache if enabled */
    if (pub_node->flags & INN_CONF_CACHED)
    {
        LockGuard lk(pub_node->cache_lock);
        if (pub_node->cache.size() >= size) {
            if (data)
                memcpy(pub_node->cache.data(), data, size);
            pub_node->cache_valid = true;
        }
        else {
            return INN_ERR_MSG_TOO_LARGE;
        }
    }

    /* Make a copy */
    std::unordered_set<innet_id_t> local_subs;
    {
        ReadGuard rd(pub_node->subs_lock);
        local_subs = pub_node->subscribers;
    }

    /* Dispatch */
    for (innet_id_t sub_id : local_subs)
    {
        auto sub_node = NodeMgr::get_node(sub_id);

        if (!sub_node)
            continue;

        if (!is_event_type_enabled(sub_node->event_mask, INN_EVENT_PUBLISH))
            continue;

        /* Skip inbox operations for pure publish nodes */
        if (!sub_node->inbox)
            continue;

        Event ev;
        ev.event = INN_EVENT_PUBLISH;
        ev.sender = pub;
        ev.receiver = sub_id;
        if (data && size > 0) {
            ev.payload.assign((const uint8_t *)data, (const uint8_t *)data + size);
        }

        sub_node->inbox->push(ev, timeout_ms);
    }

    return INN_OK;
}

/* Receive event from inbox */
/* Returns INN_OK on success */
int innet_receive(innet_id_t receiver, innet_event_t *ev, void *buf, size_t buf_cap, int timeout_ms)
{
    if (!ev || !buf) return INN_ERR_NULL_POINTER;

    auto node = NodeMgr::get_node(receiver);
    if (!node) return INN_ERR_NOTFOUND;

    if (!node->inbox) {
        return INN_ERR_NOSUPPORT;
    }

    Event internal_ev;

    // Step 1: "peek" at the message without removing it from the queue.
    // This gives us a safe copy to inspect.
    if (!node->inbox->peek(internal_ev, timeout_ms)) {
        return node->inbox->is_closed() ? INN_ERR_CLOSED : INN_ERR_TIMEOUT;
    }

    /* Step 2: Check if the user's buffer is sufficient. */ 
    if (buf_cap < internal_ev.payload.size()) {
        /* buffer is too small */
        return INN_ERR_NOMEM;
    }

    /* Step 3: All checks passed. Now, we commit by "consuming" the message. */
    node->inbox->consume();

    /* Step 4: Copy the data to the user's buffers. */ 
    ev->event = internal_ev.event;
    ev->sender = internal_ev.sender;
    ev->receiver = internal_ev.receiver;
    ev->size = internal_ev.payload.size();

    /* Copy payload */
    if (!internal_ev.payload.empty())
        memcpy(buf, internal_ev.payload.data(), internal_ev.payload.size());

    return INN_OK;
}


/* Get number of subscribers for node */
/* Returns number of subscribers */
int innet_subscriber_num(innet_id_t id)
{
    auto node = NodeMgr::get_node(id);
    if (!node) return INN_ERR_NOTFOUND;

    ReadGuard rd(node->subs_lock);
    int num = node->subscribers.size();
    return num;
}


/* Get inbox length for node */
/* Returns INN_OK on success */
int innet_inbox_len(innet_id_t id)
{
    auto node = NodeMgr::get_node(id);
    if (!node) return INN_ERR_INVALID;

    /* Pure publish nodes have no inbox */
    if (!node->inbox)
        return 0;

    return node->inbox->size();
}

