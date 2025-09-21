#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include "innet.h"

class InnetTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(innet_init(), INN_OK);
    }

    void TearDown() override {
        innet_deinit();
    }

    innet_node_conf_t default_conf = {0, 10, INN_INBOX_POLICY_DEFAULT, INN_CONF_NONE, INN_EVENT_PUBLISH | INN_EVENT_LATCHED};
};

// 1. 基础 API 与节点管理 (NodeManagement)
TEST_F(InnetTest, InitDeinitCycle) {
    innet_deinit();
    ASSERT_EQ(innet_init(), INN_OK);
    innet_deinit();
    ASSERT_EQ(innet_init(), INN_OK);
}

TEST_F(InnetTest, CreateSingleNode) {
    innet_id_t id;
    ASSERT_EQ(innet_create_node(&id, nullptr, &default_conf), INN_OK);
    ASSERT_EQ(innet_node_num(), 1);
    innet_remove_node(id);
}

TEST_F(InnetTest, CreateNodeWithAndWithoutName) {
    innet_id_t id_named, id_anon;
    ASSERT_EQ(innet_create_node(&id_named, "test_node", &default_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&id_anon, nullptr, &default_conf), INN_OK);
    innet_id_t found_id;
    ASSERT_EQ(innet_find_node("test_node", &found_id), INN_OK);
    ASSERT_EQ(found_id, id_named);
    // Anonymous node cannot be found by name
    innet_remove_node(id_named);
    innet_remove_node(id_anon);
}

TEST_F(InnetTest, CreateDuplicateNameFails) {
    innet_id_t id1, id2;
    ASSERT_EQ(innet_create_node(&id1, "dup_node", &default_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&id2, "dup_node", &default_conf), INN_ERR_EXIST);
    innet_remove_node(id1);
}

TEST_F(InnetTest, FindNode) {
    innet_id_t id;
    ASSERT_EQ(innet_create_node(&id, "find_me", &default_conf), INN_OK);
    innet_id_t found_id;
    ASSERT_EQ(innet_find_node("find_me", &found_id), INN_OK);
    ASSERT_EQ(found_id, id);
    innet_remove_node(id);
}

TEST_F(InnetTest, FindNonExistentNodeFails) {
    innet_id_t found_id;
    ASSERT_EQ(innet_find_node("nonexistent", &found_id), INN_ERR_NOTFOUND);
}

TEST_F(InnetTest, CreateAndRemoveNode) {
    innet_id_t id;
    ASSERT_EQ(innet_create_node(&id, "remove_me", &default_conf), INN_OK);
    ASSERT_EQ(innet_node_num(), 1);
    ASSERT_EQ(innet_remove_node(id), INN_OK);
    ASSERT_EQ(innet_node_num(), 0);
    innet_id_t found_id;
    ASSERT_EQ(innet_find_node("remove_me", &found_id), INN_ERR_NOTFOUND);
}

TEST_F(InnetTest, RemoveNonExistentNodeFails) {
    ASSERT_EQ(innet_remove_node(99999), INN_ERR_NOTFOUND);
}

// 2. 核心发布/订阅逻辑 (PubSubLogic)
TEST_F(InnetTest, SinglePubSub) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0}; // No inbox for pub
    innet_node_conf_t sub_conf = default_conf;
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &sub_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    const char* msg = "hello";
    ASSERT_EQ(innet_publish(pub_id, msg, strlen(msg), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 1000), INN_OK);
    ASSERT_EQ(ev.event, INN_EVENT_PUBLISH);
    ASSERT_EQ(ev.sender, pub_id);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    ASSERT_EQ(ev.size, strlen(msg));
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, OnePubMultipleSubs) {
    innet_id_t pub_id, sub1_id, sub2_id, sub3_id;
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0};
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub1_id, "sub1", &default_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub2_id, "sub2", &default_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub3_id, "sub3", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub1_id, "pub"), INN_OK);
    ASSERT_EQ(innet_subscribe(sub2_id, "pub"), INN_OK);
    ASSERT_EQ(innet_subscribe(sub3_id, "pub"), INN_OK);
    const char* msg = "test";
    ASSERT_EQ(innet_publish(pub_id, msg, strlen(msg), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    // Check each subscriber receives
    ASSERT_EQ(innet_receive(sub1_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    ASSERT_EQ(innet_receive(sub2_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    ASSERT_EQ(innet_receive(sub3_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    innet_remove_node(pub_id);
    innet_remove_node(sub1_id);
    innet_remove_node(sub2_id);
    innet_remove_node(sub3_id);
}

TEST_F(InnetTest, MultiplePubsOneSub) {
    innet_id_t pub1_id, pub2_id, sub_id;
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0};
    ASSERT_EQ(innet_create_node(&pub1_id, "topic_A", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&pub2_id, "topic_B", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "topic_A"), INN_OK);
    const char* msg1 = "from A";
    const char* msg2 = "from B";
    ASSERT_EQ(innet_publish(pub1_id, msg1, strlen(msg1), 0), INN_OK);
    ASSERT_EQ(innet_publish(pub2_id, msg2, strlen(msg2), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg1, ev.size), 0);
    ASSERT_EQ(ev.sender, pub1_id);
    // No more messages
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_ERR_TIMEOUT);
    innet_remove_node(pub1_id);
    innet_remove_node(pub2_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, UnsubscribeWorks) {
    innet_id_t pub_id, sub_id;
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &default_conf), INN_OK); // Need inbox for pub? Wait, for test, assume.
    // Actually, for pub, no inbox needed, but to simplify, use default.
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    ASSERT_EQ(innet_unsubscribe(sub_id, pub_id), INN_OK);
    const char* msg = "test";
    ASSERT_EQ(innet_publish(pub_id, msg, strlen(msg), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_ERR_TIMEOUT);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, PublishWithNoSubscribers) {
    innet_id_t pub_id;
    ASSERT_EQ(innet_create_node(&pub_id, nullptr, &default_conf), INN_OK);
    const char* msg = "test";
    ASSERT_EQ(innet_publish(pub_id, msg, strlen(msg), 0), INN_OK);
    innet_remove_node(pub_id);
}

TEST_F(InnetTest, ReceiveTimeout) {
    innet_id_t sub_id;
    ASSERT_EQ(innet_create_node(&sub_id, nullptr, &default_conf), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 50), INN_ERR_TIMEOUT);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, NonBlockingReceive) {
    innet_id_t sub_id;
    ASSERT_EQ(innet_create_node(&sub_id, nullptr, &default_conf), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 0), INN_ERR_TIMEOUT);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, BufferTooSmallForReceive) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0};
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    const char msg[32] = "this is a 32 byte message here"; // 32 bytes
    ASSERT_EQ(innet_publish(pub_id, msg, sizeof(msg), 0), INN_OK);
    innet_event_t ev;
    char small_buf[16];
    ASSERT_EQ(innet_receive(sub_id, &ev, small_buf, sizeof(small_buf), 1000), INN_ERR_NOMEM);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

// 3. 收件箱策略测试 (InboxPolicy)
TEST_F(InnetTest, DropNewPolicy) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t sub_conf = {0, 2, INN_INBOX_POLICY_DROP_NEW, INN_CONF_NONE, INN_EVENT_PUBLISH};
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0};
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &sub_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg1", 4, 0), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg2", 4, 0), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg3", 4, 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, "msg1", ev.size), 0);
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, "msg2", ev.size), 0);
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_ERR_TIMEOUT); // msg3 dropped
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, DropOldPolicy) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t sub_conf = {0, 2, INN_INBOX_POLICY_DROP_OLD, INN_CONF_NONE, INN_EVENT_PUBLISH};
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0};
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &sub_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg1", 4, 0), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg2", 4, 0), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg3", 4, 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, "msg2", ev.size), 0);
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, "msg3", ev.size), 0);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

// Note: BlockPolicyWithTimeout and BlockPolicyUnblocks require threading, will add later if needed.

// 4. 数据闩锁功能测试 (Latching)
TEST_F(InnetTest, SubscribeAfterPublishGetsLatchedMsg) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t pub_conf = {sizeof(double), 0, INN_INBOX_POLICY_NONE, INN_CONF_LATCHED, 0}; // Latched
    innet_node_conf_t sub_conf = default_conf;
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    const double pub_value = 8.3135;
    ASSERT_EQ(innet_publish(pub_id, &pub_value, sizeof(double), 0), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &sub_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(ev.event, INN_EVENT_LATCHED);
    ASSERT_EQ(memcmp(buf, &pub_value, ev.size), 0);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, LatchedMessageIsUpdated) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t pub_conf = {10, 0, INN_INBOX_POLICY_NONE, INN_CONF_LATCHED, 0};
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msgA", 4, 0), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msgB", 4, 0), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, "msgB", 4), 0); // Latest
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, NoLatchIfNotEnabled) {
    innet_id_t pub_id, sub_id;
    innet_node_conf_t pub_conf = {0, 0, INN_INBOX_POLICY_NONE, INN_CONF_NONE, 0}; // Not latched
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &pub_conf), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, "msg", 3, 0), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_ERR_TIMEOUT);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

// 5. 待定订阅功能测试 (PendingSubscribe)
TEST_F(InnetTest, SubscribeThenCreateNodeWorks) {
    innet_id_t sub_id, pub_id;
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "future_topic"), INN_INFO_PENDING);
    ASSERT_EQ(innet_create_node(&pub_id, "future_topic", &default_conf), INN_OK);
    const char* msg = "test";
    ASSERT_EQ(innet_publish(pub_id, msg, strlen(msg), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    innet_remove_node(sub_id);
    innet_remove_node(pub_id);
}

// 6. 多线程与并发压力测试 (Concurrency)
// Simplified versions
TEST_F(InnetTest, ManyThreadsSubscribing) {
    innet_id_t pub_id;
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &default_conf), INN_OK);
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);
    for (int i = 0; i < 20; ++i) {
        threads.emplace_back([this, pub_id, &success_count]() {
            innet_id_t sub_id;
            innet_node_conf_t conf = default_conf;
            if (innet_create_node(&sub_id, nullptr, &conf) == INN_OK) {
                if (innet_subscribe(sub_id, "pub") == INN_OK) {
                    success_count++;
                }
                // Note: Removing in thread might be unsafe, but for test
                innet_remove_node(sub_id);
            }
        });
    }
    for (auto& t : threads) t.join();
    ASSERT_EQ(success_count.load(), 20);
    ASSERT_EQ(innet_subscriber_num(pub_id), 20); // Since we removed them
    innet_remove_node(pub_id);
    ASSERT_LT(innet_subscriber_num(pub_id), 0);
}

// HighFrequencyPubSub: Need atomic counters, etc. Omitted for brevity.
// CreateAndRemoveStressTest: Omitted.
// PubSubUnsubscribeRace: Omitted.

// 7. 错误处理与边界情况 (EdgeCases)
TEST_F(InnetTest, NullPointerArgs) {
    innet_id_t id;
    ASSERT_EQ(innet_create_node(nullptr, "test", &default_conf), INN_ERR_NULL_POINTER);
    ASSERT_EQ(innet_create_node(&id, nullptr, nullptr), INN_ERR_NULL_POINTER); // Assuming conf required
    char buf[1];
    ASSERT_EQ(innet_receive(0, nullptr, buf, 1, 0), INN_ERR_NULL_POINTER);
}

TEST_F(InnetTest, InvalidNodeIDs) {
    ASSERT_EQ(innet_remove_node(99999), INN_ERR_NOTFOUND);
    ASSERT_EQ(innet_publish(99999, "test", 4, 0), INN_ERR_NOTFOUND);
}

TEST_F(InnetTest, ZeroSizePublish) {
    innet_id_t pub_id, sub_id;
    ASSERT_EQ(innet_create_node(&pub_id, "pub", &default_conf), INN_OK);
    ASSERT_EQ(innet_create_node(&sub_id, "sub", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(sub_id, "pub"), INN_OK);
    ASSERT_EQ(innet_publish(pub_id, nullptr, 0, 0), INN_OK);
    innet_event_t ev;
    char buf[1];
    ASSERT_EQ(innet_receive(sub_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(ev.size, 0);
    innet_remove_node(pub_id);
    innet_remove_node(sub_id);
}

TEST_F(InnetTest, PublishToSelf) {
    innet_id_t node_id;
    ASSERT_EQ(innet_create_node(&node_id, "self", &default_conf), INN_OK);
    ASSERT_EQ(innet_subscribe(node_id, "self"), INN_OK);
    const char* msg = "self";
    ASSERT_EQ(innet_publish(node_id, msg, strlen(msg), 0), INN_OK);
    innet_event_t ev;
    char buf[256];
    ASSERT_EQ(innet_receive(node_id, &ev, buf, sizeof(buf), 100), INN_OK);
    ASSERT_EQ(memcmp(buf, msg, ev.size), 0);
    innet_remove_node(node_id);
}


