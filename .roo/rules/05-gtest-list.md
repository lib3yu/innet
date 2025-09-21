### GTest 单元测试设计列表

我们将测试分为几个逻辑组，每个组关注一部分核心功能。

#### 1. 基础 API 与节点管理 (`NodeManagement`
)

这个测试组主要验证库的生命周期和节点的基本增、删、查操作。

* **`InitDeinitCycle`**:
    * 测试 `innet_init()` 和 `innet_deinit()` 能否被成功调用并配对使用，无内存泄漏或崩溃。
* **`CreateSingleNode`**:
    * 创建一个节点，验证 `innet_node_num()` 返回值为 1。
* **`CreateNodeWithAndWithoutName`**:
    * 创建一个具名节点，验证 `innet_find_node()` 能找到它。
    * 创建一个匿名节点 (`name = NULL`)，验证它不会出现在具名查找中。
* **`CreateDuplicateNameFails`**:
    * 创建一个具名节点 "node_A"。
    * 再次尝试创建名为 "node_A" 的节点，验证返回 `INN_ERR_EXIST` 错误。
* **`FindNode`**:
    * 创建一个具名节点，用 `innet_find_node()` 查找，验证返回的 ID 与创建时一致。
* **`FindNonExistentNodeFails`**:
    * 尝试查找一个不存在的节点名，验证返回 `INN_ERR_NOTFOUND`。
* **`CreateAndRemoveNode`**:
    * 创建节点，然后调用 `innet_remove_node()`。
    * 验证 `innet_node_num()` 返回值变回 0。
    * 再次查找该节点应失败。
* **`RemoveNonExistentNodeFails`**:
    * 尝试使用一个无效的 ID (如 99999) 调用 `innet_remove_node()`，验证返回错误。

#### 2. 核心发布/订阅逻辑 (`PubSubLogic`)

这是库的核心功能，需要详尽测试。

* **`SinglePubSub`**:
    * **Happy Path**: 创建一个发布者和一个订阅者，订阅者订阅发布者。
    * 发布者发送一条消息，验证订阅者能接收到，且内容、大小、发送方ID完全一致。
* **`OnePubMultipleSubs`**:
    * 创建一个发布者和三个订阅者，三个订阅者都订阅同一个发布者。
    * 发布者发送一条消息，验证**所有三个**订阅者都能收到该消息。
* **`MultiplePubsOneSub`**:
    * 创建发布者 "topic_A" 和 "topic_B"。
    * 创建一个订阅者，只订阅 "topic_A"。
    * 验证订阅者只能收到来自 "topic_A" 的消息，收不到 "topic_B" 的。
* **`UnsubscribeWorks`**:
    * 建立订阅关系后，调用 `innet_unsubscribe()`。
    * 发布者再发送消息，验证已取消订阅的节点不会收到新消息。
* **`PublishWithNoSubscribers`**:
    * 创建一个发布者，但不创建任何订阅者。
    * 调用 `innet_publish()`，验证程序不会崩溃，并成功返回。
* **`ReceiveTimeout`**:
    * 创建一个订阅者，不进行任何发布操作。
    * 调用 `innet_receive()` 并设置一个较短的超时（如 50ms），验证函数返回 `INN_ERR_TIMEOUT`。
* **`NonBlockingReceive`**:
    * 在收件箱为空时，使用超时 `0` 调用 `innet_receive()`，验证其立即返回 `INN_ERR_TIMEOUT`。
* **`BufferTooSmallForReceive`**:
    * 发布者发送一个大小为 32 字节的消息。
    * 订阅者使用一个 16 字节的 buffer 调用 `innet_receive()`，验证返回 `INN_ERR_NOMEM` 或 `INN_ERR_MSG_TOO_LARGE` 等相关错误。

#### 3. 收件箱策略测试 (`InboxPolicy`)

这部分专门验证不同的队列溢出策略是否按预期工作。

* **`DropNewPolicy` (默认)**:
    * 设置订阅者收件箱容量为 2。
    * 发送消息1, 2。
    * 再发送消息3。
    * 验证接收到的消息是 1 和 2，消息 3 被丢弃。
* **`DropOldPolicy`**:
    * 设置策略为 `INN_INBOX_POLICY_DROP_OLD`，容量为 2。
    * 发送消息1, 2。
    * 再发送消息3。
    * 验证接收到的消息是 2 和 3，消息 1 被丢弃。
* **`BlockPolicyWithTimeout`**:
    * 设置策略为 `INN_INBOX_POLICY_BLOCK`，容量为 1。
    * 发送消息1。
    * 在**另一个线程**中调用 `innet_publish()` 发送消息2，并设置超时（如 50ms）。
    * 验证发布调用被阻塞，并在超时后返回错误（如 `INN_ERR_TIMEOUT` 或 `INN_ERR_BUSY`）。
* **`BlockPolicyUnblocks`**:
    * 与上一个测试类似，但在发布线程被阻塞后，主线程调用 `innet_receive()`消费一个消息。
    * 验证发布线程解除阻塞，并成功返回 `INN_OK`。

#### 4. 数据闩锁功能测试 (`Latching`)

* **`SubscribeAfterPublishGetsLatchedMsg`**:
    * 创建一个配置为 `INN_CONF_LATCHED` 的发布者。
    * 发布者**先**发送一条消息。
    * **然后**创建订阅者并订阅。
    * 验证订阅者能立即收到那条被闩锁的消息，且事件类型为 `INN_EVENT_LATCHED`。
* **`LatchedMessageIsUpdated`**:
    * Latching 发布者发送消息 A，然后发送消息 B。
    * 之后，订阅者才进行订阅。
    * 验证订阅者收到的是最后一条消息 B，而不是 A。
* **`NoLatchIfNotEnabled`**:
    * 创建一个**未开启** Latching 功能的发布者。
    * 发布者发送消息，然后订阅者订阅。
    * 验证订阅者**不会**收到任何旧消息。

#### 5. 待定订阅功能测试 (`PendingSubscribe`)

* **`SubscribeThenCreateNodeWorks`**:
    * 创建一个订阅者，订阅一个尚不存在的主题 "future_topic"，验证返回 `INN_INFO_PENDING`。
    * **之后**，创建名为 "future_topic" 的发布者节点。
    * 该发布者发送一条消息。
    * 验证最初的订阅者能成功接收到这条消息。

#### 6. 多线程与并发压力测试 (`Concurrency`)

这是确保库稳定性的关键测试。

* **`ManyThreadsSubscribing`**:
    * 创建一个发布者。
    * 并发启动 20 个线程，每个线程都去订阅同一个发布者。
    * 等待所有线程结束后，验证发布者的订阅者数量为 20 (`innet_subscriber_num`)。
* **`HighFrequencyPubSub`**:
    * 一个线程中，发布者以极高频率（例如循环100万次）发送消息。
    * 另一个线程中，订阅者持续接收消息。
    * 使用原子计数器验证接收到的消息总数与发送的总数是否一致，以及消息内容是否损坏。
* **`CreateAndRemoveStressTest`**:
    * 在多个线程中，并发地、随机地创建和删除大量具名节点。
    * 目的是检测 `NodeMgr` 的全局锁是否存在死锁或竞争条件问题。
* **`PubSubUnsubscribeRace`**:
    * 一个线程高频发布消息。
    * 多个其他线程随机地、反复地对该发布者进行订阅和取消订阅。
    * 验证程序全程无崩溃、无死锁。

#### 7. 错误处理与边界情况 (`EdgeCases`)

* **`NullPointerArgs`**:
    * 向所有接受指针参数的 API (如 `innet_create_node`, `innet_receive`) 传入 `NULL`。
    * 验证函数能优雅地处理，返回 `INN_ERR_NULL_POINTER` 错误且不崩溃。
* **`InvalidNodeIDs`**:
    * 向 `innet_publish`, `innet_remove_node` 等函数传入一个无效的、已删除的或不存在的节点ID。
    * 验证返回 `INN_ERR_NOTFOUND` 或其他合适的错误。
* **`ZeroSizePublish`**:
    * 调用 `innet_publish` 时 `size = 0` 且 `data = NULL`。这是一个合法的“信号”用例。
    * 验证订阅者能收到事件，且 `ev->size` 为 0。
* **`PublishToSelf`**:
    * 创建一个具名节点，然后自己订阅自己的名字。
    * 验证它可以给自己发送和接收消息。
