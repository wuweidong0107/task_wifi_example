#ifndef __WIFI_H__
#define __WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "list.h"

enum wifi_error_code {
    WIFI_ERROR_OPEN  = -1,
};

typedef struct wifi_network_info {
    char ssid[64];
    char password[64];
    bool connected;
    uint8_t signal;
    struct list_head list;
} wifi_network_info_t;

typedef struct wifi_handle wifi_t;

/* Primary Functions */
wifi_t *wifi_new(void);
void wifi_free(wifi_t *wifi);
int wifi_open(wifi_t *wifi, const char *backend);
void wifi_close(wifi_t *wifi);
bool wifi_connection_info(wifi_t *wifi, wifi_network_info_t *network);
void wifi_scan(wifi_t *wifi);
bool wifi_connect_ssid(wifi_t *wifi, wifi_network_info_t *network);
bool wifi_disconnect_ssid(wifi_t *wifi, wifi_network_info_t *network);
const char *wifi_errmsg(wifi_t *wifi);

#ifdef __cplusplus
}
#endif

#endif