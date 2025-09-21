/* Simplified pub/sub demo with sensor publisher and display subscriber */
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
typedef struct {
    double temperature;
    long timestamp;
} TemperatureData;

// ====================================================================
// 3. Sensor publisher thread
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

        usleep(110 * 1000);
    }
    print_safe("TempSensor Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 4. Display subscriber thread
// ====================================================================

void *display_thread_func(void *arg)
{
    innet_id_t display_id = *(innet_id_t *)arg;

    print_safe("Display Thread", "Thread started.");

    char buffer[256];
    innet_event_t event;
    char event_buffer[256];

    while (s_system_running)
    {
        int res = innet_receive(display_id, &event, event_buffer, sizeof(event_buffer), 1000);
        if (res == INN_OK)
        {
            if ((event.event == INN_EVENT_PUBLISH || event.event == INN_EVENT_LATCHED) &&
                event.size == sizeof(TemperatureData))
            {
                const TemperatureData *data = (const TemperatureData *)event_buffer;
                const char *event_type = (event.event == INN_EVENT_LATCHED) ? "LATCHED" : "PUBLISH";
                snprintf(buffer, sizeof(buffer), "DISPLAY: Temp is %.2f C. (Event: %s, from ID: %u)",
                        data->temperature, event_type, event.sender);
                print_safe("Display Thread", buffer);
            }
        }
        else if (res == INN_ERR_TIMEOUT)
        {
            /* Timeout handling */
        }
        else if (res == INN_ERR_CLOSED)
        {
            print_safe("Display Thread", "Node closed. Shutting down.");
            break;
        }
    }
    print_safe("Display Thread", "Thread shutting down.");
    return NULL;
}

// ====================================================================
// 5. Main function - system orchestration
// ====================================================================

int main()
{
    pthread_t sensor_tid, display_tid;
    innet_id_t sensor_id, display_id;
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
    /* Display subscriber node */
    innet_node_conf_t display_param = {0};
    display_param.flags = INN_CONF_NONE;
    display_param.event_mask = INN_EVENT_PUBLISH | INN_EVENT_LATCHED;
    display_param.inbox_capacity = 15;
    display_param.inbox_policy = INN_INBOX_POLICY_DROP_NEW;
    ret = innet_create_node(&display_id, NULL, &display_param);
    if (ret != INN_OK)
    {
        print_safe("Main", "Failed to create display node!");
        innet_deinit();
        return -1;
    }
    snprintf(buffer, sizeof(buffer), "Created display node %u:%s!",
                display_id, "NULL");
    print_safe("Main", buffer);

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
    snprintf(buffer, sizeof(buffer), "Created sensor node %u:%s!",
                sensor_id, "sensor/temp");
    print_safe("Main", buffer);

    /* --- Step 3: Establish subscriptions --- */
    print_safe("Main", "--- Establishing Subscriptions ---");
    int res = innet_subscribe(display_id, "sensor/temp");
    snprintf(buffer, sizeof(buffer), "Subscription result: %s", innet_strerr(res));
    print_safe("Main", buffer);

    /* --- Step 4: Start threads --- */
    print_safe("Main", "--- Starting Node Threads ---");

    pthread_create(&sensor_tid, NULL, sensor_thread_func, &sensor_id);
    pthread_create(&display_tid, NULL, display_thread_func, &display_id);

    print_safe("Main", "System is running. Simulating for 10 seconds.");

    /* --- Step 5: Run simulation --- */
    sleep(10);

    /* --- Step 6: System shutdown --- */
    print_safe("Main", "--- System Shutting Down ---");
    s_system_running = false;

    pthread_join(sensor_tid, NULL);
    pthread_join(display_tid, NULL);
    print_safe("Main", "All threads joined.");

    innet_deinit();
    pthread_mutex_destroy(&s_print_mutex);
    print_safe("Main", "innet de-initialized. Exiting.");

    return 0;
}
