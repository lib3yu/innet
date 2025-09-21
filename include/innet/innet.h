#ifndef INNET_H
#define INNET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INNET_API 


typedef uint32_t innet_id_t;

/* invalid id */
#define INN_INVALID_ID ((innet_id_t)-1)


/* return code */
#define INN_INFO_CACHE_PULLED     2
#define INN_INFO_PENDING          1
#define INN_OK                    0
#define INN_ERR_FAIL              -1
#define INN_ERR_TIMEOUT           -2
#define INN_ERR_NOMEM             -3
#define INN_ERR_NOTFOUND          -4
#define INN_ERR_NOSUPPORT         -5
#define INN_ERR_BUSY              -6
#define INN_ERR_INVALID           -7
#define INN_ERR_ACCESS            -8
#define INN_ERR_MSG_TOO_LARGE     -9
#define INN_ERR_EXIST             -10
#define INN_ERR_NODATA            -11
#define INN_ERR_INITIALIZED       -12
#define INN_ERR_NOTINITIALIZED    -13
#define INN_ERR_CLOSED            -14
#define INN_ERR_NULL_POINTER      -15



/* node flag */
#define INN_CONF_NONE             (0 << 0)
#define INN_CONF_CACHED           (1 << 0)
#define INN_CONF_LATCHED          ((1 << 1) | INN_CONF_CACHED)


/* inbox policy */
#define INN_INBOX_POLICY_NONE      0 // no inbox for pure publish nodes
#define INN_INBOX_POLICY_DROP_NEW  1 // default
#define INN_INBOX_POLICY_DROP_OLD  2
#define INN_INBOX_POLICY_BLOCK     3

#define INN_INBOX_POLICY_DEFAULT   INN_INBOX_POLICY_DROP_NEW


/* receive event type */
#define INN_EVENT_PUBLISH         (0x01)
#define INN_EVENT_LATCHED         (0x10)

typedef uint8_t innet_event_mask_t;


/* node config struct */
typedef struct {
    uint32_t cache_size;
    uint32_t inbox_capacity;
    uint32_t inbox_policy;
    uint32_t flags; // INN_CONF_CACHED, INN_CONF_LATCHED
    innet_event_mask_t event_mask; // events that care about
} innet_node_conf_t;


/* node event struct */
typedef struct {
    uint32_t event; // INN_EVENT_*
    innet_id_t sender;
    innet_id_t receiver;
    size_t size;
} innet_event_t;


INNET_API int innet_init(void);
INNET_API void innet_deinit(void);
INNET_API int innet_create_node(innet_id_t *id, const char *name, const innet_node_conf_t *conf);
INNET_API int innet_remove_node(innet_id_t id);
INNET_API int innet_subscribe(innet_id_t subscriber, const char *pub_name);
INNET_API int innet_unsubscribe(innet_id_t subscriber, innet_id_t publisher);
INNET_API int innet_publish(innet_id_t pub, const void *data, size_t size, uint32_t timeout_ms);
INNET_API int innet_receive(innet_id_t receiver, innet_event_t *ev, void *buf, size_t buf_cap, int timeout_ms);
INNET_API const char *innet_strerr(int err);
INNET_API int innet_node_num(void);
INNET_API int innet_find_node(const char *name, innet_id_t *id);
INNET_API int innet_subscriber_num(innet_id_t id);
INNET_API int innet_inbox_len(innet_id_t id);


#ifdef __cplusplus
}
#endif

#endif /* INNET_H */
