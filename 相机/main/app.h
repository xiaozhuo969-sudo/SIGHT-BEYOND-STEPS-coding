#pragma once
#include <stddef.h>
void network_start(void);
const char *network_ap_name(void);
void network_status(char *ip, size_t len, int *clients);
void camera_server_start(void);
