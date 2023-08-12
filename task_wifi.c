#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "thpool.h"
#include "wifi.h"

static void task_wifi_scan_handler(task_t *task)
{
    if (!task)
        return;

    wifi_t* wifi = (wifi_t*)task->user_data;
    printf("wifi scanning...\n");
    wifi_scan(wifi);
    usleep(1000 * 1000);
    printf("wifi scan OK...\n");
}

static void task_push_wifi_scan(thpool_t *thpool, task_handler_t handler, void *wifi)
{
    task_t *task = task_init();
    task->handler = handler;
    task->user_data = wifi;
    task_queue_push(thpool_taskqueue(thpool), task);
}

int main()
{
    printf("Making %d thread pool\n", 1);
    thpool_t *thpool = thpool_init(1);

    wifi_t *wifi;
    if ((wifi = wifi_new()) == NULL) {
        printf("wifi_new() fail\n");
        return -1;
    }
    if (wifi_open(wifi, NULL) != 0) {
        printf("wifi_open() fail\n");
        wifi_free(wifi);
        return -1;
    }
    task_push_wifi_scan(thpool, task_wifi_scan_handler, wifi);
    task_push_wifi_scan(thpool, task_wifi_scan_handler, wifi);

    while(1) {
        printf("main...\n");
        usleep(1000 * 1000);
    }
    thpool_wait(thpool);
    
    wifi_free(wifi);
    puts("Killing threadpool");
    thpool_destroy(thpool);

    return 0;
}
