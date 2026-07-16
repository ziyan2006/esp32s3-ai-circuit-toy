#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

/**
 * @brief Initialize Wi-Fi in Station mode and connect to access point.
 */
void wifi_init_sta(void);

/**
 * @brief Check if Wi-Fi is currently connected and has an IP address.
 * 
 * @return true if connected, false otherwise.
 */
bool wifi_is_connected(void);

#endif // WIFI_H
