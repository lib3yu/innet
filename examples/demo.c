#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>    // for sleep()
#include <stdatomic.h> // for atomic_bool (C11 standard)
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "innet.h"

// ====================================================================
// 1. 全局控制变量和线程安全打印
// ====================================================================

/* static bool s_system_running = true; */
static atomic_bool s_system_running = true;
static pthread_mutex_t s_print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 线程安全的打印函数 */
void print_safe(const char *source, const char *message)
{
    pthread_mutex_lock(&s_print_mutex);
    printf("[%-25s] %s\n", source, message);
    pthread_mutex_unlock(&s_print_mutex);
}

// ====================================================================
// 2. 消息数据结构定义 (纯C)
// ====================================================================
typedef struct
{
    double temperature;
    long timestamp;
} TemperatureData;

typedef struct
{
    int turn_on; // C语言中没有bool，通常用int
    int fan_speed;
} FanCommand;

// ====================================================================
// 3. 节点工作线程函数 (取代回调函数)
// ====================================================================

/* --- 控制器节点工作线程 --- */
void *controller_thread_func(void *arg)
{
    innet_id_t controller_id = *(innet_id_t *)arg;
    free(arg); // 释放传递过来的数据

    print_safe("CoolingController Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256]; // Buffer for event payload

    while (s_system_running)
    {
        /* 阻塞等待事件 */
        int res = innet_receive(controller_id, &event, event_buffer, sizeof(event_buffer), 1000); // 1秒超时
        if (res == INN_OK)
        {
            /* 处理来自传感器的温度发布 */
            if (event.event == INN_EVENT_PUBLISH && event.size == sizeof(TemperatureData))
            {
                const TemperatureData *data = (const TemperatureData *)event_buffer;
                snprintf(buffer, sizeof(buffer), "Received temperature: %.2f C", data->temperature);
                print_safe("CoolingController Thread", buffer);

                /* 控制逻辑：温度高于28度则开启风扇 */
                if (data->temperature > 28.0)
                {
                    FanCommand cmd = {1, 80}; // turn_on = true, speed = 80%
                    print_safe("CoolingController Thread", "Temp HIGH! Publishing FAN ON command.");
                    innet_publish(controller_id, &cmd, sizeof(cmd));
                }
            }
            /* 处理来自HMI的紧急通知 */
            else if (event.event == INN_EVENT_NOTIFY && event.size == sizeof(FanCommand))
            {
                const FanCommand *cmd = (const FanCommand *)event_buffer;
                if (cmd->turn_on)
                {
                    print_safe("CoolingController Thread", "Received URGENT NOTIFY from HMI. Forcing FAN ON.");
                    innet_publish(controller_id, cmd, sizeof(*cmd));
                }
            }
            /* 处理传感器的信号 (新功能演示) */
            else if (event.event == INN_EVENT_PUBLISH_SIG)
            {
                print_safe("CoolingController Thread", "Received PUBLISH_SIGNAL from sensor. Checking latest temp...");
                /* 收到信号后主动拉取最新数据 */
                TemperatureData latest_temp;
                size_t sz = sizeof(latest_temp);
                int pull_res = innet_pull(controller_id, event.sender, &latest_temp, &sz, 0); // 0 timeout
                if (pull_res == INN_INFO_CACHE_PULLED)
                {
                    snprintf(buffer, sizeof(buffer), "Pulled latest temp after signal: %.2f C", latest_temp.temperature);
                    print_safe("CoolingController Thread", buffer);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "Failed to pull after signal: %s", innet_strerr(pull_res));
                    print_safe("CoolingController Thread", buffer);
                }
            }
        }
        else if (res == INN_ERR_TIMEOUT)
        {
            /* 超时，可以做一些周期性检查 */
            /* print_safe("CoolingController Thread", "Timed out waiting for event..."); */
        }
        else if (res == INN_ERR_CLOSED)
        {
            print_safe("CoolingController Thread", "Node closed. Shutting down.");
            break;
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Error in receive: %s", innet_strerr(res));
            print_safe("CoolingController Thread", buffer);
        }
    }
    print_safe("CoolingController Thread", "Thread shutting down.");
    return NULL;
}

/* --- 风扇执行器节点工作线程 --- */
void *fan_actuator_thread_func(void *arg)
{
    innet_id_t fan_id = *(innet_id_t *)arg;
    free(arg);

    print_safe("FanActuator Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256];

    while (s_system_running)
    {
        int res = innet_receive(fan_id, &event, event_buffer, sizeof(event_buffer), 1000);
        if (res == INN_OK)
        {
            if (event.event == INN_EVENT_PUBLISH && event.size == sizeof(FanCommand))
            {
                const FanCommand *cmd = (const FanCommand *)event_buffer;
                if (cmd->turn_on)
                {
                    snprintf(buffer, sizeof(buffer), "Command received. Turning ON fan at speed %d%%", cmd->fan_speed);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "Command received. Turning OFF fan.");
                }
                print_safe("FanActuator Thread", buffer);
            }
        }
        else if (res == INN_ERR_TIMEOUT)
        {
            /* 超时处理 */
        }
        else if (res == INN_ERR_CLOSED)
        {
            print_safe("FanActuator Thread", "Node closed. Shutting down.");
            break;
        }
    }
    print_safe("FanActuator Thread", "Thread shutting down.");
    return NULL;
}

/* --- HMI节点工作线程 --- */
void *hmi_thread_func(void *arg)
{
    innet_id_t hmi_id = *(innet_id_t *)arg;
    free(arg);

    print_safe("HMI Thread", "Thread started.");

    char buffer[256];

    /* 1. 启动后立即PULL一次传感器的初始值 */
    sleep(1); // 等待传感器可能发布第一个值
    TemperatureData initial_temp;
    size_t sz = sizeof(initial_temp);
    print_safe("HMI Thread", "Attempting to PULL initial temperature...");
    int ret = innet_pull(hmi_id, INN_INVALID_ID, &initial_temp, &sz, 0); // 我们需要知道传感器的ID
    /* 为了演示，我们假设知道名字，先查找ID */
    innet_id_t sensor_id;
    if (innet_find_node("sensor/temp", &sensor_id) == INN_OK)
    {
        sz = sizeof(initial_temp);
        ret = innet_pull(hmi_id, sensor_id, &initial_temp, &sz, 0);
        if (ret == INN_INFO_CACHE_PULLED)
        {
            snprintf(buffer, sizeof(buffer), "PULL successful! Initial temperature: %.2f C", initial_temp.temperature);
            print_safe("HMI Thread", buffer);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "PULL failed. Error: %s.", innet_strerr(ret));
            print_safe("HMI Thread", buffer);
        }
    }
    else
    {
        print_safe("HMI Thread", "Could not find sensor node ID for pull.");
    }

    /* 2. 模拟HMI在5秒后发送一个紧急通知 */
    sleep(5);
    print_safe("HMI Thread", "Sending urgent NOTIFY to controller.");
    FanCommand urgent_cmd = {1, 100}; // turn_on = true, speed = 100%
    innet_id_t controller_id;
    if (innet_find_node("controller/cooling", &controller_id) == INN_OK)
    {
        innet_notify(hmi_id, controller_id, &urgent_cmd, sizeof(urgent_cmd));
    }

    /* 3. HMI线程继续运行，等待关闭信号 */
    innet_event_t event;
    char event_buffer[256];
    while (s_system_running)
    {
        int res = innet_receive(hmi_id, &event, event_buffer, sizeof(event_buffer), 1000);
        if (res == INN_OK)
        {
            if (event.event == INN_EVENT_PUBLISH || event.event == INN_EVENT_LATCHED)
            {
                if (event.size == sizeof(TemperatureData))
                {
                    const TemperatureData *data = (const TemperatureData *)event_buffer;
                    const char *event_type = (event.event == INN_EVENT_LATCHED) ? "LATCHED" : "PUBLISH";
                    snprintf(buffer, sizeof(buffer), "MONITOR: Temp is %.2f C. (Event: %s, from ID: %u)", data->temperature, event_type, event.sender);
                    print_safe("HMI Thread", buffer);
                }
            }
        }
        else if (res == INN_ERR_TIMEOUT)
        {
            /* 可以周期性地做一些事情 */
        }
        else if (res == INN_ERR_CLOSED)
        {
            print_safe("HMI Thread", "Node closed. Shutting down.");
            break;
        }
        /* 简单的周期性心跳信号发送演示 */
        static int counter = 0;
        counter++;
        if (counter % 10 == 0)
        { // 每10秒左右
            innet_id_t ctrl_id;
            if (innet_find_node("controller/cooling", &ctrl_id) == INN_OK)
            {
                /* 发送一个异步信号给控制器，通知它我们还活着 */
                innet_publish_signal_async(ctrl_id);
                print_safe("HMI Thread", "Sent async PUBLISH_SIGNAL to controller.");
            }
        }
    }
    print_safe("HMI Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 4. 线程函数 (发布者)
// ====================================================================

void *sensor_thread_func(void *arg)
{
    innet_id_t sensor_id = *(innet_id_t *)arg;
    free(arg); // 释放传递过来的数据

    char buffer[100];
    print_safe("TempSensor Thread", "Thread started.");

    int count = 0;
    while (s_system_running)
    {
        TemperatureData temp_data;
        temp_data.temperature = 20.0 + (rand() % 150) / 10.0; // 20.0 - 34.9 C
        temp_data.timestamp = time(NULL);

        snprintf(buffer, sizeof(buffer), "Publishing temperature: %.2f C", temp_data.temperature);
        print_safe("TempSensor Thread", buffer);
        innet_publish(sensor_id, &temp_data, sizeof(temp_data));

        /* 每发布3次，发送一个信号 */
        count++;
        if (count % 3 == 0)
        {
            print_safe("TempSensor Thread", "Publishing a PUBLISH_SIGNAL.");
            innet_publish_signal(sensor_id); // 同步信号
        }

        sleep(2);
    }
    print_safe("TempSensor Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 5. 主函数 - 系统编排
// ====================================================================

int main()
{
    pthread_t sensor_tid, controller_tid, fan_tid, hmi_tid;
    innet_id_t sensor_id, controller_id, fan_id, hmi_id;
    int ret;

    srand(time(NULL));

    /* --- 步骤 1: 初始化innet库 --- */
    print_safe("Main", "--- System Initializing ---");
    ret = innet_init();
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to initialize innet!");
        return -1;
    }

    /* --- 步骤 2: 创建节点 --- */
    /* 创建温度传感器节点 (带缓存和latching) */
    innet_node_conf_t sensor_param = {0}; // 使用初始化器清零
    sensor_param.cache_size = sizeof(TemperatureData);
    sensor_param.flags = INN_CONF_CACHED | INN_CONF_LATCHED; // 启用缓存和闩锁
    sensor_param.event_mask = 0;                             // 传感器本身不接收事件
    sensor_param.inbox_capacity = 10;
    sensor_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;

    ret = innet_create_node(&sensor_id, "sensor/temp", &sensor_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create sensor node!");
        innet_deinit();
        return -1;
    }

    /* 创建控制器节点 */
    innet_node_conf_t controller_param = {0};
    controller_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_NOTIFY | INN_EVENT_PUBLISH_SIG; // 关心发布、通知和信号
    controller_param.inbox_capacity = 20;
    controller_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&controller_id, "controller/cooling", &controller_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create controller node!");
        innet_deinit();
        return -1;
    }

    /* 创建风扇执行器节点 */
    innet_node_conf_t fan_param = {0};
    fan_param.event_mask = INN_EVENT_PUBLISH;
    fan_param.inbox_capacity = 10;
    fan_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&fan_id, "actuator/fan", &fan_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create fan node!");
        innet_deinit();
        return -1;
    }

    /* 创建HMI节点 */
    innet_node_conf_t hmi_param = {0};
    hmi_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED;
    hmi_param.inbox_capacity = 15;
    hmi_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    /* ret = innet_create_node(&hmi_id, "hmi/monitor", &hmi_param); */
    ret = innet_create_node(&hmi_id, NULL, &hmi_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create HMI node!");
        innet_deinit();
        return -1;
    }

    /* --- 步骤 3: 建立订阅关系 --- */
    print_safe("Main", "--- Establishing Subscriptions ---");
    /* 使用名称订阅，演示 pending subscription 功能 */
    innet_subscribe_name(controller_id, "sensor/temp");
    innet_subscribe_name(fan_id, "controller/cooling");
    innet_subscribe_name(hmi_id, "sensor/temp");

    /* --- 步骤 4: 启动线程 --- */
    print_safe("Main", "--- Starting Node Threads ---");

    /* 为每个工作线程传递节点ID */
    innet_id_t *controller_id_ptr = malloc(sizeof(innet_id_t));
    *controller_id_ptr = controller_id;
    pthread_create(&controller_tid, NULL, controller_thread_func, controller_id_ptr);

    innet_id_t *fan_id_ptr = malloc(sizeof(innet_id_t));
    *fan_id_ptr = fan_id;
    pthread_create(&fan_tid, NULL, fan_actuator_thread_func, fan_id_ptr);

    innet_id_t *hmi_id_ptr = malloc(sizeof(innet_id_t));
    *hmi_id_ptr = hmi_id;
    pthread_create(&hmi_tid, NULL, hmi_thread_func, hmi_id_ptr);

    innet_id_t *sensor_id_ptr = malloc(sizeof(innet_id_t));
    *sensor_id_ptr = sensor_id;
    pthread_create(&sensor_tid, NULL, sensor_thread_func, sensor_id_ptr);

    print_safe("Main", "System is running. Simulating for 25 seconds.");
    print_safe("Main", "==================================================");

    /* --- 步骤 5: 模拟动态变化 --- */
    sleep(8);
    print_safe("Main", "==================================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: Fan actuator hardware failure. Removing node.");
    ret = innet_remove_node(fan_id);
    if (ret == INN_OK)
    {
        print_safe("Main", "Node 'actuator/fan' removed successfully.");
    }
    else
    {
        print_safe("Main", "Failed to remove fan node.");
    }

    sleep(6);
    print_safe("Main", "==================================================");
    print_safe("Main", "!!! DYNAMIC CHANGE: HMI user closes temperature view.");
    ret = innet_unsubscribe(hmi_id, sensor_id);
    if (ret == INN_OK)
    {
        print_safe("Main", "HMI unsubscribed from 'sensor/temp' successfully.");
    }
    else
    {
        print_safe("Main", "Failed to unsubscribe HMI.");
    }

    /* --- 步骤 6: 系统关闭 --- */
    sleep(11); // 总共25秒
    print_safe("Main", "==================================================");
    print_safe("Main", "--- System Shutting Down ---");
    s_system_running = false;

    pthread_join(sensor_tid, NULL);
    pthread_join(controller_tid, NULL);
    pthread_join(fan_tid, NULL);
    pthread_join(hmi_tid, NULL);
    print_safe("Main", "All threads joined.");

    innet_deinit();
    pthread_mutex_destroy(&s_print_mutex);
    print_safe("Main", "innet de-initialized. Exiting.");

    return 0;
}
