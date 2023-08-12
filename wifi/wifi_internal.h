#ifndef __WIFI_INTERNAL_H__
#define __WIFI_INTERNAL_H__

#include <stdbool.h>

#include "wifi.h"

typedef struct wifi_backend
{
    void* (*init)(void);
    void (*free)(void *handle);
    bool (*is_available)(void *handle);
    bool (*enable)(void *handle, bool enabled);
    bool (*connection_info)(void *handle, wifi_network_info_t *network);
    void (*scan)(void *handle);
    bool (*connect_ssid)(void *handle, wifi_network_info_t *network);
    bool (*disconnect_ssid)(void *handle, wifi_network_info_t *network);
    const char *ident;
} wifi_backend_t;

extern wifi_backend_t wifi_nmcli;

#endif