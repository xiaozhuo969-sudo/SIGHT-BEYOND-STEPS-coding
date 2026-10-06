#ifndef _REMOTE_H
#define _REMOTE_H
#include <stdint.h>

extern void RC_Analy(void);
void remote_reset_session(void);
void remote_timeout_tick(void);
extern volatile uint32_t legacy_received,legacy_last_ms,legacy_active,legacy_timeouts;

#endif

