# innet — 轻量级进程内发布/订阅库

`innet` 是一个专为**解耦模块通信**和**线程间事件同步**设计的轻量级消息库。它不搞复杂协议，不跨进程，只做一件事：让线程之间高效传递消息。


> **⚠️** `innet` 不支持跨进程、不支持序列化、不支持 QoS — 它只解决“同一个进程内，线程之间怎么高效传消息”的问题，为多线程应用设计的极简 Pub/Sub 通信库，C 接口，C++11 实现，零第三方依赖。。

---

## ✨ 核心特性

- **纯 C 接口，C++11 实现** — 易集成，跨语言友好。
- **线程安全，无锁设计优先** — 多线程并发无忧。
- **Latching（闩锁）支持** — 新订阅者自动收到最新状态，省去“拉取”逻辑。
- **收件箱策略可配**：
  - `DROP_NEW`（默认）— 队列满，丢新消息。
  - `DROP_OLD` — 队列满，丢最老消息。
  - `BLOCK` — 队列满，阻塞发布者。
- **Pending 订阅** — 支持订阅不存在的主题，等目标主题上线自动连。
- **零依赖** — 只用 pthread，不拖泥带水。

---

## 🛠 编译 & 运行

### 环境要求

- GCC / Clang（支持 C++11）
- `make`
- `pthreads`（系统自带）

### 编译库

```bash
make
# 输出: lib/libinnet.a, lib/libinnet.so
```

### 编译并运行示例

```bash
make demo
LD_LIBRARY_PATH=lib/ ./bin/demo_industry     # 工业控制模拟
LD_LIBRARY_PATH=lib/ ./bin/demo_pubsub       # 基础 Pub/Sub 示例
```

### 清理

```bash
make clean
```

---

## 🚀 快速上手

下面是一个最简示例：发布者先发一条“闩锁”消息，订阅者后启动也能收到它。

```c
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "innet.h"

void* publisher(void* arg) {
    innet_id_t pub_id;
    innet_node_conf_t conf = {
        .cache_size = sizeof(int),
        .flags = INN_CONF_LATCHED,  // 启用闩锁
        .inbox_capacity = 16,
        .event_mask = 0,
    };

    innet_create_node(&pub_id, "counter", &conf);
    printf("[Pub] Node created: %u\n", pub_id);

    int val = 42;
    printf("[Pub] Publishing latched value: %d\n", val);
    innet_publish(pub_id, &val, sizeof(val), 0); // 闩锁生效

    sleep(2); // 等待订阅者上线

    for (int i = 1; i <= 3; i++) {
        val = 100 + i;
        innet_publish(pub_id, &val, sizeof(val), 0);
        usleep(500000);
    }

    innet_remove_node(pub_id);
    return NULL;
}

void* subscriber(void* arg) {
    innet_id_t sub_id;
    innet_node_conf_t conf = {
        .inbox_capacity = 8,
        .event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED,
    };

    innet_create_node(&sub_id, NULL, &conf);
    printf("[Sub] Node created: %u\n", sub_id);

    sleep(1); // 模拟延迟启动
    innet_subscribe(sub_id, "counter"); // 订阅时自动收到闩锁消息！

    for (int i = 0; i < 4; i++) {
        innet_event_t ev;
        int buf;
        if (innet_receive(sub_id, &ev, &buf, sizeof(buf), 2000) == INN_OK) {
            const char* type = (ev.event == INN_EVENT_LATCHED) ? "LATCHED" : "PUBLISH";
            printf("[Sub] [%s] %d (from %u)\n", type, buf, ev.sender);
        }
    }

    innet_remove_node(sub_id);
    return NULL;
}

int main() {
    innet_init();

    pthread_t p, s;
    pthread_create(&p, NULL, publisher, NULL);
    pthread_create(&s, NULL, subscriber, NULL);

    pthread_join(p, NULL);
    pthread_join(s, NULL);

    innet_deinit();
    printf("Done.\n");
    return 0;
}
```

---

## 📖 API 列表

### 初始化

```c
int innet_init(void);
void innet_deinit(void);
```

### 创建/销毁节点

```c
int innet_create_node(innet_id_t *id, const char *name, const innet_node_conf_t *conf);
int innet_remove_node(innet_id_t id);
```

### 订阅/发布

```c
int innet_subscribe(innet_id_t subscriber, const char *pub_name);
int innet_unsubscribe(innet_id_t subscriber, innet_id_t publisher);
int innet_publish(innet_id_t pub, const void *data, size_t size, uint32_t timeout_ms);
```

### 接收消息

```c
int innet_receive(innet_id_t receiver, innet_event_t *ev, void *buf, size_t buf_cap, int timeout_ms);
```

### 工具函数

```c
const char *innet_strerr(int err);      // 错误码转字符串
int innet_node_num(void);               // 当前节点总数
int innet_inbox_len(innet_id_t id);     // 收件箱待处理消息数
int innet_subscriber_num(innet_id_t id); // 获取订阅者数量
```

---
