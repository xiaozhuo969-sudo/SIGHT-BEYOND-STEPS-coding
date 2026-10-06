#include "control_output.h"
#include "gesture.h"

void control_output_altitude(bool yaw_axis, unsigned signal, bool valid, uint16_t channels[8])
{
    channels[0]=channels[1]=channels[2]=channels[3]=1500;
    channels[4]=2000;
    channels[5]=channels[6]=channels[7]=1000;
    if(!valid) return;
    unsigned lateral=yaw_axis?3:0;
    if(signal==GESTURE_UP) channels[2]=1800;
    else if(signal==GESTURE_DOWN) channels[2]=1100;
    else if(signal==GESTURE_LEFT) channels[lateral]=1300;
    else if(signal==GESTURE_RIGHT) channels[lateral]=1700;
    else if(signal==GESTURE_FLAT) channels[1]=1700;
    // Palm press (FIST) and undecided transitions keep all four channels neutral.
}
