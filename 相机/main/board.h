#pragma once
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32S3
#error This pin map is for the ESP32-S3 board in docs/esp32s3_cam_sch.pdf
#endif
// nulllab ESP32S3-CAM V1.0, PDF 2025-11-03; D0..D7 = DVP_Y2..Y9.
#define CAM_D0 11
#define CAM_D1 9
#define CAM_D2 8
#define CAM_D3 10
#define CAM_D4 12
#define CAM_D5 18
#define CAM_D6 17
#define CAM_D7 16
#define CAM_XCLK 15
#define CAM_PCLK 13
#define CAM_VSYNC 6
#define CAM_HREF 7
#define CAM_SDA 4
#define CAM_SCL 5
#define CAM_PWDN -1
#define CAM_RESET -1
