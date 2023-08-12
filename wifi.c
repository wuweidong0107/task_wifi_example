#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "wifi_internal.h"
#include "wifi.h"

struct wifi_handle {
    const wifi_backend_t *backend;
    void *backend_handle;

    struct {
        int c_errno;
        char errmsg[128];
    } error;
};

static const wifi_backend_t *wifi_backends[] = {
    &wifi_nmcli,
    NULL,
};

static int _wifi_error(wifi_t *wifi, int code, int c_errno, const char *fmt, ...)
{
    va_list ap;
    
    wifi->error.c_errno = c_errno;
    va_start(ap, fmt);
    vsnprintf(wifi->error.errmsg, sizeof(wifi->error.errmsg), fmt, ap);
    va_end(ap);

    if (c_errno) {
        char buf[64];
        strerror_r(c_errno, buf, sizeof(buf));
        snprintf(wifi->error.errmsg + strlen(wifi->error.errmsg),
                 sizeof(wifi->error.errmsg) - strlen(wifi->error.errmsg),
                 ": %s [errno %d]", buf, c_errno);
    }

    return code;
}

void wifi_scan(wifi_t *wifi)
{
    if (wifi && wifi->backend && wifi->backend->scan) {
        wifi->backend->scan(wifi->backend_handle);
    }
}

bool wifi_connect_ssid(wifi_t *wifi, wifi_network_info_t *network)
{
    if (wifi && wifi->backend && wifi->backend->connect_ssid) {
        return wifi->backend->connect_ssid(wifi->backend_handle, network);
    }
    
    return false;
}

bool wifi_disconnect_ssid(wifi_t *wifi, wifi_network_info_t *network)
{
    if (wifi && wifi->backend && wifi->backend->disconnect_ssid) {
        return wifi->backend->disconnect_ssid(wifi->backend_handle, network);
    }
    
    return false;
}

bool wifi_connection_info(wifi_t *wifi, wifi_network_info_t *network)
{
    if (wifi && wifi->backend && wifi->backend->connection_info)
        return wifi->backend->connection_info(wifi->backend_handle, network);
    
    return false;
}

wifi_t *wifi_new(void)
{
    wifi_t *wifi = calloc(1, sizeof(wifi_t));
    if (wifi == NULL)
        return NULL;
    
    return wifi;
}

void wifi_free(wifi_t *wifi)
{
    free(wifi);
}

int wifi_open(wifi_t *wifi, const char *backend)
{
    int i;

    if (backend == NULL) {
        for (i=0; wifi_backends[i] != NULL; i++) {
            if (wifi_backends[i]->is_available && wifi_backends[i]->is_available(wifi)) {
                wifi->backend = wifi_backends[i];
                break;
            }
        }
    } else {
        for(i=0; wifi_backends[i]; i++) {
            if (!strncmp(backend, wifi_backends[i]->ident, strlen(backend))) {
                wifi->backend = wifi_backends[i];
                break;
            }
        }
    }
    if (wifi->backend == NULL)
        return _wifi_error(wifi, WIFI_ERROR_OPEN, 0, "WiFi backend %s not found", backend);

    if(wifi->backend->init) {
        wifi->backend_handle = wifi->backend->init();
        if (wifi->backend_handle == NULL)
            return _wifi_error(wifi, WIFI_ERROR_OPEN, 0, "WiFi backend %s init fail", backend);
    } else {
        return _wifi_error(wifi, WIFI_ERROR_OPEN, 0, "WiFi backend %s not implemented yet", backend);
    }

    return 0;
}

void wifi_close(wifi_t *wifi)
{
    if (wifi == NULL || wifi->backend == NULL)
        return;
    
    if (wifi->backend->free)
        wifi->backend->free(wifi->backend_handle);
}

const char *wifi_errmsg(wifi_t *wifi)
{
    return wifi->error.errmsg;
}