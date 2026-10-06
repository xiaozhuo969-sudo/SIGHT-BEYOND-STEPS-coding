#ifndef OBSTACLE_H
#define OBSTACLE_H
#include <stdint.h>
typedef struct {
 uint32_t bytes,frames,bad_checksum,uart_errors,last_ms,raw_cm,valid;
} ObstacleState;
extern volatile ObstacleState obstacle;
void obstacle_init(void);
void obstacle_poll(void);
void obstacle_receive_byte(uint8_t byte);
uint16_t obstacle_distance_cm(void);
#endif
