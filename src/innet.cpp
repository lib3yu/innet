#include "innet.h"
#include <stdbool.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <pthread.h>
#include <sys/time.h> // for gettimeofday

struct Event {
    uint32_t event;
    innet_id_t sender;
    innet_id_t receiver;
    std::vector<uint8_t> payload; /* Payload is copied */
};

struct Node {
    innet_id_t id;
    std::string name;
    uint32_t flags;

    /* Cache */
    std::vector<uint8_t> cache;
    bool cache_valid;
    pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

    /* Subscribers */
    std::unordered_set<innet_id_t> subscribers;
    pthread_rwlock_t subs_lock = PTHREAD_RWLOCK_INITIALIZER;

    /* Inbox */
    std::deque<Event> inbox;
    pthread_mutex_t inbox_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t inbox_cv = PTHREAD_COND_INITIALIZER;
    uint32_t inbox_capacity;
    uint32_t inbox_policy;

    /* Config */
    uint32_t notify_size_check;
    innet_event_mask_t event_mask;

    /* State */
    bool closed = false;
};

/* Global state */
static std::unordered_map<innet_id_t, std::unique_ptr<Node>> s_nodes_by_id;
static std::unordered_map<std::string, innet_id_t> s_nodes_by_name;
static std::unordered_map<std::string, std::vector<innet_id_t>> s_pendins_subs_by_name;
static pthread_rwlock_t s_nodes_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static std::atomic<innet_id_t> s_next_id{1};

/* Async Signal Task */
struct SignalTask {
    innet_id_t pub_id;
};

static std::deque<SignalTask> s_signal_task_queue;
static pthread_mutex_t s_signal_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_signal_queue_cv = PTHREAD_COND_INITIALIZER;
static bool s_signal_thread_shutdown = false;
static std::thread s_signal_thread;


//   █████████   █████               █████     ███              █████   █████          ████                               
//  ███░░░░░███ ░░███               ░░███     ░░░              ░░███   ░░███          ░░███                               
// ░███    ░░░  ███████    ██████   ███████   ████   ██████     ░███    ░███   ██████  ░███  ████████   ██████  ████████  
// ░░█████████ ░░░███░    ░░░░░███ ░░░███░   ░░███  ███░░███    ░███████████  ███░░███ ░███ ░░███░░███ ███░░███░░███░░███ 
//  ░░░░░░░░███  ░███      ███████   ░███     ░███ ░███ ░░░     ░███░░░░░███ ░███████  ░███  ░███ ░███░███████  ░███ ░░░  
//  ███    ░███  ░███ ███ ███░░███   ░███ ███ ░███ ░███  ███    ░███    ░███ ░███░░░   ░███  ░███ ░███░███░░░   ░███      
// ░░█████████   ░░█████ ░░████████  ░░█████  █████░░██████     █████   █████░░██████  █████ ░███████ ░░██████  █████     
//  ░░░░░░░░░     ░░░░░   ░░░░░░░░    ░░░░░  ░░░░░  ░░░░░░     ░░░░░   ░░░░░  ░░░░░░  ░░░░░  ░███░░░   ░░░░░░  ░░░░░      
//                                                                                           ░███                         
//                                                                                           █████                        
//                                                                                          ░░░░░                         

/* Get current time in milliseconds */
/* Returns 0 on success */
static int get_time_ms(struct timespec *ts, int timeout_ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + timeout_ms / 1000;
    ts->tv_nsec = (tv.tv_usec + (timeout_ms % 1000) * 1000) * 1000;
    if (ts->tv_nsec >= 1000000000)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
    return 0;
}

/* Check if event type is enabled in mask */
/* Returns true if enabled */
static bool is_event_type_enabled(innet_event_mask_t mask, uint32_t event_type)
{
    return (mask & event_type) != 0;
}


//  ██████████                                             █████████               █████                                                                                                                           
// ░░███░░░░░█                                            ███░░░░░███             ░░███                                                                                                                            
//  ░███  █ ░  ████████  ████████   ██████  ████████     ███     ░░░   ██████   ███████   ██████   █████                                                                                                           
//  ░██████   ░░███░░███░░███░░███ ███░░███░░███░░███   ░███          ███░░███ ███░░███  ███░░███ ███░░                                                                                                            
//  ░███░░█    ░███ ░░░  ░███ ░░░ ░███ ░███ ░███ ░░░    ░███         ░███ ░███░███ ░███ ░███████ ░░█████                                                                                                           
//  ░███ ░   █ ░███      ░███     ░███ ░███ ░███        ░░███     ███░███ ░███░███ ░███ ░███░░░   ░░░░███                                                                                                          
//  ██████████ █████     █████    ░░██████  █████        ░░█████████ ░░██████ ░░████████░░██████  ██████                                                                                                           
// ░░░░░░░░░░ ░░░░░     ░░░░░      ░░░░░░  ░░░░░          ░░░░░░░░░   ░░░░░░   ░░░░░░░░  ░░░░░░  ░░░░░░                                                                                                            
                                                                                                                                                                                                                
/* Convert error code to string */
/* Returns error description string */
const char *innet_strerr(int err)
{
    switch (err)
    {
    case INN_OK:
        return "Success";
    case INN_INFO_PENDING:
        return "Pending";
    case INN_INFO_CACHE_PULLED:
        return "Pulled from cache";
    case INN_ERR_FAIL:
        return "General failure";
    case INN_ERR_TIMEOUT:
        return "Timeout";
    case INN_ERR_NOMEM:
        return "No memory";
    case INN_ERR_NOTFOUND:
        return "Not found";
    case INN_ERR_NOSUPPORT:
        return "Not supported";
    case INN_ERR_BUSY:
        return "Busy";
    case INN_ERR_INVALID:
        return "Invalid argument";
    case INN_ERR_ACCESS:
        return "Access denied";
    case INN_ERR_EXIST:
        return "Already exists";
    case INN_ERR_NODATA:
        return "No data available";
    case INN_ERR_INITIALIZED:
        return "Already initialized";
    case INN_ERR_NOTINITIALIZED:
        return "Not initialized";
    case INN_ERR_CLOSED:
        return "Node closed";
    case INN_ERR_NULL_POINTER:
        return "Null pointer";
    default:
        return "Unknown error";
    }
}

//  █████        ███     ██████                                        ████           
// ░░███        ░░░     ███░░███                                      ░░███           
//  ░███        ████   ░███ ░░░   ██████   ██████  █████ ████  ██████  ░███   ██████  
//  ░███       ░░███  ███████    ███░░███ ███░░███░░███ ░███  ███░░███ ░███  ███░░███ 
//  ░███        ░███ ░░░███░    ░███████ ░███ ░░░  ░███ ░███ ░███ ░░░  ░███ ░███████  
//  ░███      █ ░███   ░███     ░███░░░  ░███  ███ ░███ ░███ ░███  ███ ░███ ░███░░░   
//  ███████████ █████  █████    ░░██████ ░░██████  ░░███████ ░░██████  █████░░██████  
// ░░░░░░░░░░░ ░░░░░  ░░░░░      ░░░░░░   ░░░░░░    ░░░░░███  ░░░░░░  ░░░░░  ░░░░░░   
//                                                  ███ ░███                          
//                                                 ░░██████                           
//                                                  ░░░░░░                            

/* Initialize the innet library */
/* Returns INN_OK on success */
int innet_init(void)
{
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&s_nodes_rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);

    s_signal_thread = std::thread([]() {
        while (true) {
            SignalTask task;
            {
                pthread_mutex_lock(&s_signal_queue_lock);
                while (s_signal_task_queue.empty() && !s_signal_thread_shutdown) {
                    pthread_cond_wait(&s_signal_queue_cv, &s_signal_queue_lock);
                }
                if (s_signal_thread_shutdown) {
                    pthread_mutex_unlock(&s_signal_queue_lock);
                    break;
                }
                task = s_signal_task_queue.front();
                s_signal_task_queue.pop_front();
                pthread_mutex_unlock(&s_signal_queue_lock);
            }

            Node* pub_node = nullptr;
            {
                pthread_rwlock_rdlock(&s_nodes_rwlock);
                auto it = s_nodes_by_id.find(task.pub_id);
                if (it != s_nodes_by_id.end()) {
                    pub_node = it->second.get();
                }
                pthread_rwlock_unlock(&s_nodes_rwlock);
            }

            if (!pub_node) continue; // Publisher gone
            
            /* make a copy */
            std::unordered_set<innet_id_t> local_subs;
            {
                pthread_rwlock_rdlock(&pub_node->subs_lock);
                local_subs = pub_node->subscribers;
                pthread_rwlock_unlock(&pub_node->subs_lock);
            }

            for (innet_id_t sub_id : local_subs) 
            {
                Node* sub_node = nullptr;
                {
                    pthread_rwlock_rdlock(&s_nodes_rwlock);
                    auto it = s_nodes_by_id.find(sub_id);
                    if (it != s_nodes_by_id.end()) {
                        sub_node = it->second.get();
                    }
                    pthread_rwlock_unlock(&s_nodes_rwlock);
                }

                if (!sub_node || sub_node->closed) 
                    continue;

                if (!is_event_type_enabled(sub_node->event_mask, INN_EVENT_PUBLISH_SIG))
                    continue;

                pthread_mutex_lock(&sub_node->inbox_lock);
                if (sub_node->closed) {
                    pthread_mutex_unlock(&sub_node->inbox_lock);
                    continue;
                }

                /* Check for coalescing: if last event is a PUBLISH_SIG from same sender, skip */
                bool should_add = true;
                if (!sub_node->inbox.empty()) {
                    const Event& last_ev = sub_node->inbox.back();
                    if (last_ev.event == INN_EVENT_PUBLISH_SIG && last_ev.sender == task.pub_id) {
                        should_add = false; // Coalesce
                    }
                }

                if (should_add) {
                    if (sub_node->inbox.size() >= sub_node->inbox_capacity) {
                        if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_NEW) {
                            // Drop the new one
                            pthread_mutex_unlock(&sub_node->inbox_lock);
                            continue;
                        } else if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_OLD) {
                            sub_node->inbox.pop_front();
                        } else if (sub_node->inbox_policy == INN_INBOX_POLICY_BLOCK) {
                            // This is tricky in async context, we drop instead to avoid blocking the signal thread
                            pthread_mutex_unlock(&sub_node->inbox_lock);
                            continue; // Drop new
                        }
                    }
                    Event ev;
                    ev.event = INN_EVENT_PUBLISH_SIG;
                    ev.sender = task.pub_id;
                    ev.receiver = sub_id;
                    ev.payload.clear();
                    sub_node->inbox.push_back(std::move(ev));
                    pthread_cond_signal(&sub_node->inbox_cv);
                }
                pthread_mutex_unlock(&sub_node->inbox_lock);
            }
        } 
    });

    return INN_OK;
}

/* Deinitialize the innet library */
void innet_deinit(void)
{
    {
        pthread_mutex_lock(&s_signal_queue_lock);
        s_signal_thread_shutdown = true;
        pthread_cond_signal(&s_signal_queue_cv);
        pthread_mutex_unlock(&s_signal_queue_lock);
    }
    
    if (s_signal_thread.joinable()) {
        s_signal_thread.join();
    }

    pthread_rwlock_wrlock(&s_nodes_rwlock);
    for (auto &pair : s_nodes_by_id)
    {
        Node *node = pair.second.get();
        pthread_mutex_lock(&node->inbox_lock);
        node->closed = true;
        pthread_cond_broadcast(&node->inbox_cv);
        pthread_mutex_unlock(&node->inbox_lock);
    }
    s_nodes_by_id.clear();
    s_nodes_by_name.clear();
    s_pendins_subs_by_name.clear();
    pthread_rwlock_unlock(&s_nodes_rwlock);

    pthread_rwlock_destroy(&s_nodes_rwlock);
    pthread_mutex_destroy(&s_signal_queue_lock);
    pthread_cond_destroy(&s_signal_queue_cv);
}



//  ██████   █████              █████             ██████   ██████                                                                                       █████                                                      
// ░░██████ ░░███              ░░███             ░░██████ ██████                                                                                       ░░███                                                       
//  ░███░███ ░███   ██████   ███████   ██████     ░███░█████░███   ██████   ████████    ██████    ███████  ██████  █████████████    ██████  ████████   ███████                                                     
//  ░███░░███░███  ███░░███ ███░░███  ███░░███    ░███░░███ ░███  ░░░░░███ ░░███░░███  ░░░░░███  ███░░███ ███░░███░░███░░███░░███  ███░░███░░███░░███ ░░░███░                                                      
//  ░███ ░░██████ ░███ ░███░███ ░███ ░███████     ░███ ░░░  ░███   ███████  ░███ ░███   ███████ ░███ ░███░███████  ░███ ░███ ░███ ░███████  ░███ ░███   ░███                                                       
//  ░███  ░░█████ ░███ ░███░███ ░███ ░███░░░      ░███      ░███  ███░░███  ░███ ░███  ███░░███ ░███ ░███░███░░░   ░███ ░███ ░███ ░███░░░   ░███ ░███   ░███ ███                                                   
//  █████  ░░█████░░██████ ░░████████░░██████     █████     █████░░████████ ████ █████░░████████░░███████░░██████  █████░███ █████░░██████  ████ █████  ░░█████                                                    
// ░░░░░    ░░░░░  ░░░░░░   ░░░░░░░░  ░░░░░░     ░░░░░     ░░░░░  ░░░░░░░░ ░░░░ ░░░░░  ░░░░░░░░  ░░░░░███ ░░░░░░  ░░░░░ ░░░ ░░░░░  ░░░░░░  ░░░░ ░░░░░    ░░░░░                                                     
//                                                                                               ███ ░███                                                                                                          
//                                                                                              ░░██████                                                                                                           
//                                                                                               ░░░░░░                                                                                                            

/* Create a new node */
/* Returns INN_OK on success */
int innet_create_node(innet_id_t *id, const char *name, const innet_node_conf_t *conf)
{
    if (!id || !conf)
        return INN_ERR_NULL_POINTER;

    auto node = std::make_unique<Node>();
    innet_id_t node_id = s_next_id.fetch_add(1);
    node->id = node_id;
    if (name) node->name = name;
    node->flags = conf->flags;
    node->inbox_capacity = conf->inbox_capacity > 0 ? conf->inbox_capacity : 100;
    node->inbox_policy = conf->inbox_policy;
    node->notify_size_check = conf->notify_size_check;
    node->event_mask = conf->event_mask;
    node->cache_valid = false;
    if (conf->cache_size > 0)
    {
        node->cache.resize(conf->cache_size);
    }

    std::string node_name = node->name;

    pthread_rwlock_wrlock(&s_nodes_rwlock);
    if (!node_name.empty() && s_nodes_by_name.find(node_name) != s_nodes_by_name.end())
    {
        pthread_rwlock_unlock(&s_nodes_rwlock);
        return INN_ERR_EXIST;
    }
    s_nodes_by_id[node_id] = std::move(node);

    if (!node_name.empty()) {
        s_nodes_by_name[node_name] = node_id;

        /* Handle pending subscriptions */
        auto pendins_it = s_pendins_subs_by_name.find(node_name);
        if (pendins_it != s_pendins_subs_by_name.end())
        {
            for (innet_id_t sub_id : pendins_it->second)
            {
                auto sub_it = s_nodes_by_id.find(sub_id);

                if (sub_it == s_nodes_by_id.end())
                    continue;

                Node *sub_node = sub_it->second.get();
                pthread_rwlock_wrlock(&sub_node->subs_lock);
                sub_node->subscribers.insert(node_id);
                pthread_rwlock_unlock(&sub_node->subs_lock);

                /* If latched, send latched event */
                if ((node->flags & INN_CONF_LATCHED) && node->cache_valid)
                {
                    pthread_mutex_lock(&sub_node->inbox_lock);
                    if (sub_node->inbox.size() < sub_node->inbox_capacity || sub_node->inbox_policy != INN_INBOX_POLICY_DROP_NEW)
                    {
                        Event ev;
                        ev.event = INN_EVENT_LATCHED;
                        ev.sender = node_id;
                        ev.receiver = sub_id;
                        ev.payload.assign(node->cache.begin(), node->cache.end());
                        sub_node->inbox.push_back(std::move(ev));
                        pthread_cond_signal(&sub_node->inbox_cv);
                    }
                    pthread_mutex_unlock(&sub_node->inbox_lock);
                }
            }
            s_pendins_subs_by_name.erase(pendins_it);
        }
    }
    pthread_rwlock_unlock(&s_nodes_rwlock);

    *id = node_id;
    return INN_OK;
}

/* Remove a node */
/* Returns INN_OK on success */
int innet_remove_node(innet_id_t id)
{
    pthread_rwlock_wrlock(&s_nodes_rwlock);
    auto it = s_nodes_by_id.find(id);
    if (it == s_nodes_by_id.end())
    {
        pthread_rwlock_unlock(&s_nodes_rwlock);
        return INN_ERR_NOTFOUND;
    }
    Node *node = it->second.get();
    std::string name = node->name;

    /* Remove from name map */
    if (!name.empty())
    {
        s_nodes_by_name.erase(name);
    }

    /* Mark as closed and wake up all waiters */
    pthread_mutex_lock(&node->inbox_lock);
    node->closed = true;
    pthread_cond_broadcast(&node->inbox_cv);
    pthread_mutex_unlock(&node->inbox_lock);

    s_nodes_by_id.erase(it);
    pthread_rwlock_unlock(&s_nodes_rwlock);
    return INN_OK;
}

/* Get number of nodes */
/* Returns number of nodes */
int innet_node_num(void)
{
    pthread_rwlock_rdlock(&s_nodes_rwlock);
    int num = s_nodes_by_id.size();
    pthread_rwlock_unlock(&s_nodes_rwlock);
    return num;
}

/* Find node by name */
/* Returns INN_OK on success */
int innet_find_node(const char *name, innet_id_t *id)
{
    if (!name || !id)
        return INN_ERR_NULL_POINTER;
    pthread_rwlock_rdlock(&s_nodes_rwlock);
    auto it = s_nodes_by_name.find(name);
    if (it == s_nodes_by_name.end())
    {
        pthread_rwlock_unlock(&s_nodes_rwlock);
        return INN_ERR_NOTFOUND;
    }
    *id = it->second;
    pthread_rwlock_unlock(&s_nodes_rwlock);
    return INN_OK;
}

//   █████████             █████                                 ███             █████     ███                      
//  ███░░░░░███           ░░███                                 ░░░             ░░███     ░░░                       
// ░███    ░░░  █████ ████ ░███████   █████   ██████  ████████  ████  ████████  ███████   ████   ██████  ████████   
// ░░█████████ ░░███ ░███  ░███░░███ ███░░   ███░░███░░███░░███░░███ ░░███░░███░░░███░   ░░███  ███░░███░░███░░███  
//  ░░░░░░░░███ ░███ ░███  ░███ ░███░░█████ ░███ ░░░  ░███ ░░░  ░███  ░███ ░███  ░███     ░███ ░███ ░███ ░███ ░███  
//  ███    ░███ ░███ ░███  ░███ ░███ ░░░░███░███  ███ ░███      ░███  ░███ ░███  ░███ ███ ░███ ░███ ░███ ░███ ░███  
// ░░█████████  ░░████████ ████████  ██████ ░░██████  █████     █████ ░███████   ░░█████  █████░░██████  ████ █████ 
//  ░░░░░░░░░    ░░░░░░░░ ░░░░░░░░  ░░░░░░   ░░░░░░  ░░░░░     ░░░░░  ░███░░░     ░░░░░  ░░░░░  ░░░░░░  ░░░░ ░░░░░  
//                                                                    ░███                                          
//                                                                    █████                                         
//                                                                   ░░░░░                                          

/* Subscribe to a publisher */
/* Returns INN_OK on success */
int innet_subscribe(innet_id_t subscriber, innet_id_t publisher)
{
    Node *pub_node = nullptr;
    Node *sub_node = nullptr;

    pthread_rwlock_rdlock(&s_nodes_rwlock);
    auto pub_it = s_nodes_by_id.find(publisher);
    auto sub_it = s_nodes_by_id.find(subscriber);
    if (pub_it == s_nodes_by_id.end() || sub_it == s_nodes_by_id.end())
    {
        pthread_rwlock_unlock(&s_nodes_rwlock);
        return INN_ERR_NOTFOUND;
    }
    pub_node = pub_it->second.get();
    sub_node = sub_it->second.get();
    pthread_rwlock_unlock(&s_nodes_rwlock);

    pthread_rwlock_wrlock(&pub_node->subs_lock);
    pub_node->subscribers.insert(subscriber);
    pthread_rwlock_unlock(&pub_node->subs_lock);

    /* If latched and has data, send latched event immediately */
    if ((pub_node->flags & INN_CONF_LATCHED))
    {
        pthread_mutex_lock(&pub_node->cache_lock);
        bool has_cache = pub_node->cache_valid;
        std::vector<uint8_t> cache_data = pub_node->cache;
        pthread_mutex_unlock(&pub_node->cache_lock);

        if (has_cache) {
            pthread_mutex_lock(&sub_node->inbox_lock);
            if (!sub_node->closed && sub_node->inbox.size() < sub_node->inbox_capacity) {
                Event ev;
                ev.event = INN_EVENT_LATCHED;
                ev.sender = publisher;
                ev.receiver = subscriber;
                ev.payload = std::move(cache_data);
                sub_node->inbox.push_back(std::move(ev));
                pthread_cond_signal(&sub_node->inbox_cv);
            }
            pthread_mutex_unlock(&sub_node->inbox_lock);
        }
    }

    return INN_OK;
}

/* Subscribe to publisher by name */
/* Returns INN_OK, INN_INFO_PENDING, or error */
int innet_subscribe_name(innet_id_t subscriber, const char *pub_name)
{
    if (!pub_name)
        return INN_ERR_NULL_POINTER;

    innet_id_t pub_id = INN_INVALID_ID;
    int res = innet_find_node(pub_name, &pub_id);
    if (res == INN_OK)
    {
        return innet_subscribe(subscriber, pub_id);
    }
    else if (res == INN_ERR_NOTFOUND)
    {
        /* Publisher not found, add to pending */
        pthread_rwlock_wrlock(&s_nodes_rwlock);
        s_pendins_subs_by_name[pub_name].push_back(subscriber);
        pthread_rwlock_unlock(&s_nodes_rwlock);
        return INN_INFO_PENDING;
    }
    return res;
}

/* Unsubscribe from publisher */
/* Returns INN_OK on success */
int innet_unsubscribe(innet_id_t subscriber, innet_id_t publisher)
{
    Node *pub_node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto pub_it = s_nodes_by_id.find(publisher);
        if (pub_it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        pub_node = pub_it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    pthread_rwlock_wrlock(&pub_node->subs_lock);
    pub_node->subscribers.erase(subscriber);
    pthread_rwlock_unlock(&pub_node->subs_lock);
    return INN_OK;
}


//   █████████                          █████  ███                                                  █████    ███████████                               ███               ███                                       
//  ███░░░░░███                        ░░███  ░░░                                                  ░░███    ░░███░░░░░███                             ░░░               ░░░                                        
// ░███    ░░░   ██████  ████████    ███████  ████  ████████    ███████     ██████   ████████    ███████     ░███    ░███   ██████   ██████   ██████  ████  █████ █████ ████  ████████    ███████                  
// ░░█████████  ███░░███░░███░░███  ███░░███ ░░███ ░░███░░███  ███░░███    ░░░░░███ ░░███░░███  ███░░███     ░██████████   ███░░███ ███░░███ ███░░███░░███ ░░███ ░░███ ░░███ ░░███░░███  ███░░███                  
//  ░░░░░░░░███░███████  ░███ ░███ ░███ ░███  ░███  ░███ ░███ ░███ ░███     ███████  ░███ ░███ ░███ ░███     ░███░░░░░███ ░███████ ░███ ░░░ ░███████  ░███  ░███  ░███  ░███  ░███ ░███ ░███ ░███                  
//  ███    ░███░███░░░   ░███ ░███ ░███ ░███  ░███  ░███ ░███ ░███ ░███    ███░░███  ░███ ░███ ░███ ░███     ░███    ░███ ░███░░░  ░███  ███░███░░░   ░███  ░░███ ███   ░███  ░███ ░███ ░███ ░███                  
// ░░█████████ ░░██████  ████ █████░░████████ █████ ████ █████░░███████   ░░████████ ████ █████░░████████    █████   █████░░██████ ░░██████ ░░██████  █████  ░░█████    █████ ████ █████░░███████                  
//  ░░░░░░░░░   ░░░░░░  ░░░░ ░░░░░  ░░░░░░░░ ░░░░░ ░░░░ ░░░░░  ░░░░░███    ░░░░░░░░ ░░░░ ░░░░░  ░░░░░░░░    ░░░░░   ░░░░░  ░░░░░░   ░░░░░░   ░░░░░░  ░░░░░    ░░░░░    ░░░░░ ░░░░ ░░░░░  ░░░░░███                  
//                                                             ███ ░███                                                                                                                  ███ ░███                  
//                                                            ░░██████                                                                                                                  ░░██████                   
//                                                             ░░░░░░                                                                                                                    ░░░░░░                    

/* Publish data to subscribers */
/* Returns INN_OK on success */
int innet_publish(innet_id_t pub, const void *data, size_t size)
{
    Node *pub_node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(pub);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        pub_node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    /* Update cache if enabled */
    if (pub_node->flags & INN_CONF_CACHED)
    {
        pthread_mutex_lock(&pub_node->cache_lock);
        if (size <= pub_node->cache.size())
        {
            if (data)
                memcpy(pub_node->cache.data(), data, size);
            pub_node->cache_valid = true;
        }
        pthread_mutex_unlock(&pub_node->cache_lock);
    }

    /* Make a copy */
    std::unordered_set<innet_id_t> local_subs;
    {
        pthread_rwlock_rdlock(&pub_node->subs_lock);
        local_subs = pub_node->subscribers;
        pthread_rwlock_unlock(&pub_node->subs_lock);
    }

    /* Dispatch */
        for (innet_id_t sub_id : local_subs) {
        Node *sub_node = nullptr;
        {
            pthread_rwlock_rdlock(&s_nodes_rwlock);
            auto sub_it = s_nodes_by_id.find(sub_id);
            if (sub_it != s_nodes_by_id.end())
            {
                sub_node = sub_it->second.get();
            }
            pthread_rwlock_unlock(&s_nodes_rwlock);
        }

        if (!sub_node || sub_node->closed)
            continue;

        if (!is_event_type_enabled(sub_node->event_mask, INN_EVENT_PUBLISH)) {
            continue;
        }

        pthread_mutex_lock(&sub_node->inbox_lock);
        if (sub_node->closed) {
            pthread_mutex_unlock(&sub_node->inbox_lock);
            continue;
        }

        if (sub_node->inbox.size() >= sub_node->inbox_capacity)
        {
            if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_NEW)
            {
                pthread_mutex_unlock(&sub_node->inbox_lock);
                continue;
            }
            else if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_OLD)
            {
                sub_node->inbox.pop_front();
            }
            else if (sub_node->inbox_policy == INN_INBOX_POLICY_BLOCK)
            {
                /* Wait until space is available or node is closed */
                struct timespec ts;
                get_time_ms(&ts, 1000); // 1 second timeout for blocking
                while (sub_node->inbox.size() >= sub_node->inbox_capacity && !sub_node->closed)
                {
                    int res = pthread_cond_timedwait(&sub_node->inbox_cv, &sub_node->inbox_lock, &ts);
                    if (res == ETIMEDOUT)
                        break;
                }
                if (sub_node->inbox.size() >= sub_node->inbox_capacity || sub_node->closed)
                {
                    pthread_mutex_unlock(&sub_node->inbox_lock);
                    continue;
                }
            }
        }

        Event ev;
        ev.event = INN_EVENT_PUBLISH;
        ev.sender = pub;
        ev.receiver = sub_id;
        if (data && size > 0)
        {
            ev.payload.assign((const uint8_t *)data, (const uint8_t *)data + size);
        }
        sub_node->inbox.push_back(std::move(ev));
        pthread_cond_signal(&sub_node->inbox_cv);
        pthread_mutex_unlock(&sub_node->inbox_lock);
    }

    return INN_OK;
}

/* Publish signal to subscribers */
/* Returns INN_OK on success */
int innet_publish_signal(innet_id_t pub)
{
    Node *pub_node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(pub);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        pub_node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    /* Make a copy */
    std::unordered_set<innet_id_t> local_subs;
    {
        pthread_rwlock_rdlock(&pub_node->subs_lock);
        local_subs = pub_node->subscribers;
        pthread_rwlock_unlock(&pub_node->subs_lock);
    }

    /* Dispatch */
    for (innet_id_t sub_id : local_subs) 
    {
        Node *sub_node = nullptr;
        {
            pthread_rwlock_rdlock(&s_nodes_rwlock);
            auto sub_it = s_nodes_by_id.find(sub_id);
            if (sub_it != s_nodes_by_id.end())
            {
                sub_node = sub_it->second.get();
            }
            pthread_rwlock_unlock(&s_nodes_rwlock);
        }

        if (!sub_node || sub_node->closed)
            continue;

        if (!is_event_type_enabled(sub_node->event_mask, INN_EVENT_PUBLISH_SIG))
        {
            continue;
        }

        pthread_mutex_lock(&sub_node->inbox_lock);
        if (sub_node->closed)
        {
            pthread_mutex_unlock(&sub_node->inbox_lock);
            continue;
        }

        /* Check for coalescing: if last event is a PUBLISH_SIG from same sender, skip */
        bool should_add = true;
        if (!sub_node->inbox.empty())
        {
            const Event &last_ev = sub_node->inbox.back();
            if (last_ev.event == INN_EVENT_PUBLISH_SIG && last_ev.sender == pub)
            {
                should_add = false; // Coalesce
            }
        }

        if (should_add)
        {
            if (sub_node->inbox.size() >= sub_node->inbox_capacity)
            {
                if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_NEW)
                {
                    pthread_mutex_unlock(&sub_node->inbox_lock);
                    continue;
                }
                else if (sub_node->inbox_policy == INN_INBOX_POLICY_DROP_OLD)
                {
                    sub_node->inbox.pop_front();
                }
                else if (sub_node->inbox_policy == INN_INBOX_POLICY_BLOCK)
                {
                    struct timespec ts;
                    get_time_ms(&ts, 1000);
                    while (sub_node->inbox.size() >= sub_node->inbox_capacity && !sub_node->closed)
                    {
                        int res = pthread_cond_timedwait(&sub_node->inbox_cv, &sub_node->inbox_lock, &ts);
                        if (res == ETIMEDOUT)
                            break;
                    }
                    if (sub_node->inbox.size() >= sub_node->inbox_capacity || sub_node->closed)
                    {
                        pthread_mutex_unlock(&sub_node->inbox_lock);
                        continue;
                    }
                }
            }

            Event ev;
            ev.event = INN_EVENT_PUBLISH_SIG;
            ev.sender = pub;
            ev.receiver = sub_id;
            ev.payload.clear();
            sub_node->inbox.push_back(std::move(ev));
            pthread_cond_signal(&sub_node->inbox_cv);
        }
        pthread_mutex_unlock(&sub_node->inbox_lock);
    }

    return INN_OK;
}

/* Publish signal asynchronously */
/* Returns INN_OK on success */
int innet_publish_signal_async(innet_id_t pub)
{
    pthread_mutex_lock(&s_signal_queue_lock);
    /* Simple coalescing: if last task is from same publisher, don't add */
    bool should_add = true;
    if (!s_signal_task_queue.empty() && s_signal_task_queue.back().pub_id == pub) {
        should_add = false;
    }
    if (should_add) {
        SignalTask task{pub};
        s_signal_task_queue.push_back(task);
        pthread_cond_signal(&s_signal_queue_cv);
    }
    pthread_mutex_unlock(&s_signal_queue_lock);
    return INN_OK;
}

/* Send notification to target node */
/* Returns INN_OK on success */
int innet_notify(innet_id_t sender, innet_id_t target, const void *data, size_t size)
{
    if (!data) return INN_ERR_NULL_POINTER;

    Node *target_node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(target);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        target_node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    if (target_node->notify_size_check > 0 && size > target_node->notify_size_check)
    {
        return INN_ERR_INVALID;
    }

    pthread_mutex_lock(&target_node->inbox_lock);
    if (target_node->closed)
    {
        pthread_mutex_unlock(&target_node->inbox_lock);
        return INN_ERR_CLOSED;
    }

    if (target_node->inbox.size() >= target_node->inbox_capacity) {
        if (target_node->inbox_policy == INN_INBOX_POLICY_DROP_NEW) {
            pthread_mutex_unlock(&target_node->inbox_lock);
            return INN_ERR_BUSY;
        } else if (target_node->inbox_policy == INN_INBOX_POLICY_DROP_OLD) {
            target_node->inbox.pop_front();
        } else if (target_node->inbox_policy == INN_INBOX_POLICY_BLOCK) {
            struct timespec ts;
            get_time_ms(&ts, 1000);
            while (target_node->inbox.size() >= target_node->inbox_capacity && !target_node->closed) {
                int res = pthread_cond_timedwait(&target_node->inbox_cv, &target_node->inbox_lock, &ts);
                if (res == ETIMEDOUT)
                    break;
            }
            if (target_node->inbox.size() >= target_node->inbox_capacity || target_node->closed) {
                pthread_mutex_unlock(&target_node->inbox_lock);
                return INN_ERR_BUSY;
            }
        }
    }

    Event ev;
    ev.event = INN_EVENT_NOTIFY;
    ev.sender = sender;
    ev.receiver = target;
    ev.payload.assign((const uint8_t *)data, (const uint8_t *)data + size);
    target_node->inbox.push_back(std::move(ev));
    pthread_cond_signal(&target_node->inbox_cv);
    pthread_mutex_unlock(&target_node->inbox_lock);

    return INN_OK;
}

/* Pull cached data from target node */
/* Returns INN_INFO_CACHE_PULLED or error */
int innet_pull(innet_id_t requester, innet_id_t target, void *buf, size_t *inout_size, int timeout_ms)
{
    if (!buf || !inout_size)
        return INN_ERR_NULL_POINTER;

    Node *target_node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(target);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        target_node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    if (!(target_node->flags & INN_CONF_CACHED)) {
        return INN_ERR_NOSUPPORT;
    }

    pthread_mutex_lock(&target_node->cache_lock);
    if (!target_node->cache_valid) {
        pthread_mutex_unlock(&target_node->cache_lock);
        return INN_ERR_NODATA;
    }

    size_t copy_size = (target_node->cache.size() < *inout_size) ? target_node->cache.size() : *inout_size;
    memcpy(buf, target_node->cache.data(), copy_size);
    *inout_size = copy_size;
    pthread_mutex_unlock(&target_node->cache_lock);

    return INN_INFO_CACHE_PULLED;
}

/* Receive event from inbox */
/* Returns INN_OK on success */
int innet_receive(innet_id_t receiver, innet_event_t *ev, void *buf, size_t buf_cap, int timeout_ms)
{
    if (!ev || !buf) return INN_ERR_NULL_POINTER;

    Node *node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(receiver);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    pthread_mutex_lock(&node->inbox_lock);
    if (node->closed)
    {
        pthread_mutex_unlock(&node->inbox_lock);
        return INN_ERR_CLOSED;
    }

    /* Wait for event */
    while (node->inbox.empty() && !node->closed)
    {
        if (timeout_ms == 0)
        {
            pthread_mutex_unlock(&node->inbox_lock);
            return INN_ERR_TIMEOUT;
        }
        else if (timeout_ms > 0)
        {
            struct timespec ts;
            get_time_ms(&ts, timeout_ms);
            int res = pthread_cond_timedwait(&node->inbox_cv, &node->inbox_lock, &ts);
            if (res == ETIMEDOUT)
            {
                pthread_mutex_unlock(&node->inbox_lock);
                return INN_ERR_TIMEOUT;
            }
        }
        else
        { // timeout_ms < 0, wait forever
            pthread_cond_wait(&node->inbox_cv, &node->inbox_lock);
        }
    }

    if (node->closed)
    {
        pthread_mutex_unlock(&node->inbox_lock);
        return INN_ERR_CLOSED;
    }

    Event internal_ev = std::move(node->inbox.front());
    node->inbox.pop_front();
    pthread_mutex_unlock(&node->inbox_lock);

    /* Copy to user event struct */
    ev->event = internal_ev.event;
    ev->sender = internal_ev.sender;
    ev->receiver = internal_ev.receiver;
    ev->size = internal_ev.payload.size();

    /* Copy payload */
    if (internal_ev.payload.size() > buf_cap)
    {
        return INN_ERR_NOMEM;
    }
    if (!internal_ev.payload.empty())
    {
        memcpy(buf, internal_ev.payload.data(), internal_ev.payload.size());
    }

    return INN_OK;
}


//   █████████   █████               █████     ███           █████     ███                                               █████    █████   █████          ████                                                      
//  ███░░░░░███ ░░███               ░░███     ░░░           ░░███     ░░░                                               ░░███    ░░███   ░░███          ░░███                                                      
// ░███    ░░░  ███████    ██████   ███████   ████   █████  ███████   ████   ██████   █████      ██████   ████████    ███████     ░███    ░███   ██████  ░███  ████████                                            
// ░░█████████ ░░░███░    ░░░░░███ ░░░███░   ░░███  ███░░  ░░░███░   ░░███  ███░░███ ███░░      ░░░░░███ ░░███░░███  ███░░███     ░███████████  ███░░███ ░███ ░░███░░███                                           
//  ░░░░░░░░███  ░███      ███████   ░███     ░███ ░░█████   ░███     ░███ ░███ ░░░ ░░█████      ███████  ░███ ░███ ░███ ░███     ░███░░░░░███ ░███████  ░███  ░███ ░███                                           
//  ███    ░███  ░███ ███ ███░░███   ░███ ███ ░███  ░░░░███  ░███ ███ ░███ ░███  ███ ░░░░███    ███░░███  ░███ ░███ ░███ ░███     ░███    ░███ ░███░░░   ░███  ░███ ░███                                           
// ░░█████████   ░░█████ ░░████████  ░░█████  █████ ██████   ░░█████  █████░░██████  ██████    ░░████████ ████ █████░░████████    █████   █████░░██████  █████ ░███████                                            
//  ░░░░░░░░░     ░░░░░   ░░░░░░░░    ░░░░░  ░░░░░ ░░░░░░     ░░░░░  ░░░░░  ░░░░░░  ░░░░░░      ░░░░░░░░ ░░░░ ░░░░░  ░░░░░░░░    ░░░░░   ░░░░░  ░░░░░░  ░░░░░  ░███░░░                                             
//                                                                                                                                                             ░███                                                
//                                                                                                                                                             █████                                               
//                                                                                                                                                            ░░░░░                                                

/* Get number of subscribers for node */
/* Returns number of subscribers */
int innet_pub_num(innet_id_t id)
{
    Node *node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(id);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    pthread_rwlock_rdlock(&node->subs_lock);
    int num = node->subscribers.size();
    pthread_rwlock_unlock(&node->subs_lock);
    return num;
}

/* Get inbox length for node */
/* Returns INN_OK on success */
int innet_inbox_len(innet_id_t id, size_t *len)
{
    if (!len)
        return INN_ERR_NULL_POINTER;
    
    Node *node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(id);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    pthread_mutex_lock(&node->inbox_lock);
    *len = node->inbox.size();
    pthread_mutex_unlock(&node->inbox_lock);
    return INN_OK;
}

/* Get cache size and validity for node */
/* Returns INN_OK on success */
int innet_cache_size(innet_id_t id, size_t *size, int *has_data)
{
    if (!size || !has_data)
        return INN_ERR_NULL_POINTER;
    
    Node *node = nullptr;
    {
        pthread_rwlock_rdlock(&s_nodes_rwlock);
        auto it = s_nodes_by_id.find(id);
        if (it == s_nodes_by_id.end())
        {
            pthread_rwlock_unlock(&s_nodes_rwlock);
            return INN_ERR_NOTFOUND;
        }
        node = it->second.get();
        pthread_rwlock_unlock(&s_nodes_rwlock);
    }

    if (!(node->flags & INN_CONF_CACHED)) {
        return INN_ERR_NOSUPPORT;
    }

    pthread_mutex_lock(&node->cache_lock);
    *size = node->cache.size();
    *has_data = node->cache_valid ? 1 : 0;
    pthread_mutex_unlock(&node->cache_lock);
    return INN_OK;
}

