/* Simplified demo based on simplified innet library */
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
// 1. Global control variables and thread-safe print
// ====================================================================

static atomic_bool s_system_running = true;
static pthread_mutex_t s_print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Thread-safe print function */
void print_safe(const char *source, const char *message)
{
    pthread_mutex_lock(&s_print_mutex);
    printf("[%-25s] %s\n", source, message);
    pthread_mutex_unlock(&s_print_mutex);
}

// ====================================================================
// 2. Message data structures (pure C)
// ====================================================================
typedef struct
{
    double temperature;
    long timestamp;
} TemperatureData;

typedef struct
{
    int turn_on; // 1 for on, 0 for off
    int fan_speed; // percentage
} FanCommand;

// ====================================================================
// 3. Node worker thread functions
// ====================================================================

/* --- Controller node worker thread --- */
void *controller_thread_func(void *arg)
{
    innet_id_t controller_id = *(innet_id_t *)arg;

    print_safe("CoolingController Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256];

    while (s_system_running)
    {
        /* Wait for events */
        int res = innet_receive(controller_id, &event, event_buffer, sizeof(event_buffer), 100);
        if (res == INN_OK)
        {
            /* Handle temperature publish from sensor */
            if ((event.event == INN_EVENT_PUBLISH || event.event == INN_EVENT_LATCHED) &&
                event.size == sizeof(TemperatureData))
            {
                const TemperatureData *data = (const TemperatureData *)event_buffer;
                snprintf(buffer, sizeof(buffer), "Received temperature: %.2f C", data->temperature);
                print_safe("CoolingController Thread", buffer);

                /* Control logic: turn on fan if temp > 28 */
                if (data->temperature > 28.0)
                {
                    FanCommand cmd = {1, 80}; // turn_on = 1, speed = 80%
                    print_safe("CoolingController Thread", "Temp HIGH! Publishing FAN ON command.");
                    innet_publish(controller_id, &cmd, sizeof(cmd), 10);
                }
                else
                {
                    FanCommand cmd = {0, 0}; // turn off
                    print_safe("CoolingController Thread", "Temp OK. Publishing FAN OFF command.");
                    innet_publish(controller_id, &cmd, sizeof(cmd), 10);
                }
            }
        } else if (res == INN_ERR_TIMEOUT) {
            /* Debug: Controller is waiting */
            print_safe("CoolingController Thread", "Waiting for events...");
        } else if (res == INN_ERR_CLOSED) {
            print_safe("CoolingController Thread", "Node closed. Shutting down.");
            break;
        } else {
            snprintf(buffer, sizeof(buffer), "Error in receive: %s", innet_strerr(res));
            print_safe("CoolingController Thread", buffer);
        }
    }

    print_safe("CoolingController Thread", "Thread shutting down.");
    return NULL;
}

/* --- Fan actuator node worker thread --- */
void *fan_actuator_thread_func(void *arg)
{
    innet_id_t fan_id = *(innet_id_t *)arg;

    print_safe("FanActuator Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256];

    while (s_system_running)
    {
        int res = innet_receive(fan_id, &event, event_buffer, sizeof(event_buffer), 1000);
        if (res == INN_OK)
        {
            if ((event.event == INN_EVENT_PUBLISH || event.event == INN_EVENT_LATCHED) &&
                event.size == sizeof(FanCommand))
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
            /* Timeout handling */
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

/* --- HMI node worker thread --- */
void *hmi_thread_func(void *arg)
{
    innet_id_t hmi_id = *(innet_id_t *)arg;

    print_safe("HMI Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256];

    while (s_system_running)
    {
        int res = innet_receive(hmi_id, &event, event_buffer, sizeof(event_buffer), 1000);
        if (res == INN_OK)
        {
            if ((event.event == INN_EVENT_PUBLISH || event.event == INN_EVENT_LATCHED) &&
                event.size == sizeof(TemperatureData))
            {
                const TemperatureData *data = (const TemperatureData *)event_buffer;
                const char *event_type = (event.event == INN_EVENT_LATCHED) ? "LATCHED" : "PUBLISH";
                snprintf(buffer, sizeof(buffer), "MONITOR: Temp is %.2f C. (Event: %s, from ID: %u)",
                        data->temperature, event_type, event.sender);
                print_safe("HMI Thread", buffer);
            }
        }
        else if (res == INN_ERR_TIMEOUT)
        {
            /* Periodic tasks if needed */
        }
        else if (res == INN_ERR_CLOSED)
        {
            print_safe("HMI Thread", "Node closed. Shutting down.");
            break;
        }
    }
    print_safe("HMI Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 4. Publisher thread
// ====================================================================

void *sensor_thread_func(void *arg)
{
    innet_id_t sensor_id = *(innet_id_t *)arg;

    char buffer[100];
    print_safe("TempSensor Thread", "Thread started.");

    while (s_system_running)
    {
        TemperatureData temp_data;
        temp_data.temperature = 20.0 + (rand() % 150) / 10.0; // 20.0 - 34.9 C
        temp_data.timestamp = time(NULL);

        snprintf(buffer, sizeof(buffer), "Publishing temperature: %.2f C", temp_data.temperature);
        print_safe("TempSensor Thread", buffer);
        innet_publish(sensor_id, &temp_data, sizeof(temp_data), 10);

        // sleep(2);
        usleep(110*1000);
    }
    print_safe("TempSensor Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 5. Main function - system orchestration
// ====================================================================

int main()
{
    pthread_t sensor_tid, controller_tid, fan_tid, hmi_tid;
    innet_id_t sensor_id, controller_id, fan_id, hmi_id;
    int ret;
    char buffer[256];

    srand(time(NULL));

    /* --- Step 1: Initialize innet library --- */
    print_safe("Main", "--- System Initializing ---");
    ret = innet_init();
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to initialize innet!");
        return -1;
    }

    /* --- Step 2: Create nodes --- */
    /* Controller node */
    innet_node_conf_t controller_param = {0};
    controller_param.flags = INN_CONF_NONE;
    controller_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED;
    controller_param.inbox_capacity = 20;
    controller_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&controller_id, "controller/cooling", &controller_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create controller node!");
        innet_deinit();
        return -1;
    }

    /* Debug: Check controller configuration */
    size_t ctrl_inbox_len = innet_inbox_len(controller_id);
    printf("[Main] Controller inbox length: %zu\n", ctrl_inbox_len);

    /* Fan actuator node */
    innet_node_conf_t fan_param = {0};
    fan_param.flags = INN_CONF_NONE;
    fan_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED;
    fan_param.inbox_capacity = 10;
    fan_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&fan_id, "actuator/fan", &fan_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create fan node!");
        innet_deinit();
        return -1;
    }

    /* HMI node */
    innet_node_conf_t hmi_param = {0};
    hmi_param.flags = INN_CONF_NONE;
    hmi_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED;
    hmi_param.inbox_capacity = 15;
    hmi_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&hmi_id, NULL, &hmi_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create HMI node!");
        innet_deinit();
        return -1;
    }

    /* Debug: Check HMI configuration */
    size_t hmi_inbox_len = innet_inbox_len(hmi_id);
    printf("[Main] HMI inbox length: %zu\n", hmi_inbox_len);

    int res2 = innet_subscribe(hmi_id, "sensor/temp");
    int res3 = innet_subscribe(controller_id, "sensor/temp");

    /* Temperature sensor node (with cache and latching) */
    innet_node_conf_t sensor_param = {0};
    sensor_param.cache_size = sizeof(TemperatureData);
    sensor_param.flags = INN_CONF_LATCHED;
    sensor_param.event_mask = 0; // sensor doesn't receive events
    sensor_param.inbox_capacity = 0;
    sensor_param.inbox_policy = INN_INBOX_POLICY_NONE;

    ret = innet_create_node(&sensor_id, "sensor/temp", &sensor_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create sensor node!");
        innet_deinit();
        return -1;
    }

    /* Check subscriber counts */
    int sensor_subs = innet_subscriber_num(sensor_id);
    int controller_subs = innet_subscriber_num(controller_id);
    snprintf(buffer, sizeof(buffer), "Subscriber counts: sensor=%d, controller=%d",
             sensor_subs, controller_subs);
    print_safe("Main", buffer);

    /* --- Step 3: Establish subscriptions --- */
    print_safe("Main", "--- Establishing Subscriptions ---");
    int res1 = innet_subscribe(fan_id, "controller/cooling");

    snprintf(buffer, sizeof(buffer), "Subscription results: fan=%s, hmi=%s, controller=%s",
             innet_strerr(res1), innet_strerr(res2), innet_strerr(res3));
    print_safe("Main", buffer);

    /* --- Step 4: Start threads --- */
    print_safe("Main", "--- Starting Node Threads ---");

    pthread_create(&sensor_tid, NULL, sensor_thread_func, &sensor_id);
    pthread_create(&controller_tid, NULL, controller_thread_func, &controller_id);
    pthread_create(&fan_tid, NULL, fan_actuator_thread_func, &fan_id);
    pthread_create(&hmi_tid, NULL, hmi_thread_func, &hmi_id);

    print_safe("Main", "System is running. Simulating for 15 seconds.");

    /* --- Step 5: Run simulation --- */
    sleep(20);

    /* --- Step 6: System shutdown --- */
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
