"""Run the real FIFO reader and rolling history against an emulated I2C FIFO."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'esp_err.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_STATE 3
''',
    'driver/gpio.h': '#pragma once\ntypedef int gpio_num_t;\n#define GPIO_NUM_40 40\n',
    'driver/i2c_master.h': '''#pragma once
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int scl_speed_hz, device_address; } i2c_device_config_t;
esp_err_t i2c_master_bus_add_device(void*,const i2c_device_config_t*,void**);
esp_err_t i2c_master_transmit(void*,const uint8_t*,size_t,int);
esp_err_t i2c_master_transmit_receive(void*,const uint8_t*,size_t,uint8_t*,size_t,int);
''',
    'esp_log.h': '''#pragma once
#include <stdio.h>
#define ESP_LOGW(tag, ...) do { (void)(tag); if(0) printf(__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, ...) do { (void)(tag); if(0) printf(__VA_ARGS__); } while(0)
''',
    'freertos/FreeRTOS.h': '''#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
''',
    'freertos/task.h': '''#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
''',
}
SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include "esp_max30102.c"
#include "ppg_window.h"
static uint32_t fifo[32][2];
static uint8_t wr, rd, overflow;
static unsigned delays, resets;
static int fail;
TickType_t xTaskGetTickCount(void) { return 1000; }
void vTaskDelay(TickType_t n) { (void)n; ++delays; }
esp_err_t i2c_master_bus_add_device(void *bus,const i2c_device_config_t *c,void **h)
{ (void)bus; (void)c; *h=(void*)1; return ESP_OK; }
esp_err_t i2c_master_transmit(void *h,const uint8_t *b,size_t n,int timeout)
{
 (void)h; assert(n==2 && timeout==100);
 if(b[0]==REG_FIFO_WR_PTR)wr=b[1];
 if(b[0]==REG_FIFO_RD_PTR)rd=b[1];
 if(b[0]==REG_OVF_COUNTER){overflow=b[1];++resets;}
 return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(void *h,const uint8_t *b,size_t n,uint8_t *out,size_t size,int timeout)
{
 (void)h; assert(n==1 && timeout==100); if(fail)return ESP_FAIL;
 if(b[0]==REG_FIFO_DATA) {
  assert(size==6);
  for(int k=0;k<2;k++){uint32_t v=fifo[rd][k];out[3*k]=v>>16;out[3*k+1]=v>>8;out[3*k+2]=v;}
  rd=(rd+1)&31;
 } else {
  assert(size==1);*out=b[0]==REG_FIFO_WR_PTR?wr:b[0]==REG_FIFO_RD_PTR?rd:b[0]==REG_OVF_COUNTER?overflow:0;
 }
 return ESP_OK;
}
static void push(uint32_t red,uint32_t ir){fifo[wr][0]=red;fifo[wr][1]=ir;wr=(wr+1)&31;}
int main(void)
{
 struct i2c_MAX30102_t sensor={.i2c_dev=(void*)1};int worn=1,got=99;uint32_t red[32],ir[32];
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==ESP_OK && got==0 && delays==0);
 push(0x7abcd,0x6fedc);
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==ESP_OK && got==1 && delays==0);
 assert(red[0]==0x3abcd && ir[0]==0x2fedc);
 // Single-sample reads preserve the original 32-sample wear evidence duration.
 for(unsigned i=0;i<31;i++){push(21000+i,25000);assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==ESP_OK && got==1);}
 assert(worn==0 && delays==0);
 for(unsigned i=0;i<7;i++)push(100+i,200+i);
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,3,&got)==ESP_OK && got==3 && red[2]==102);
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==ESP_OK && got==4 && red[0]==103 && ir[3]==206);
 overflow=1;
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==MAX30102_ERR_FIFO_OVERFLOW && got==0 && resets==1);
 fail=1;
 assert(i2c_MAX30102_read_spo2_ppg(&sensor,40,&worn,red,ir,32,&got)==ESP_FAIL && got==0 && resets==2);
 fail=0;ppg_window_t window={0};
 for(unsigned i=0;i<1007;i++) {
  uint32_t r=i,inf=10000+i;ppg_window_append(&window,&r,&inf,1);
  assert(window.count==(i<500?i+1:500));
  assert(window.red[window.count-1]==i && window.ir[window.count-1]==10000+i);
  assert(window.red[0]==(i<500?0:i-499));
 }
 window.count=0;red[0]=77;ir[0]=88;ppg_window_append(&window,red,ir,1);
 assert(window.count==1 && window.red[0]==77 && window.ir[0]==88);
 uint32_t large[520];for(unsigned i=0;i<520;++i)large[i]=i;
 ppg_window_append(&window,large,large,520);
 assert(window.count==500 && window.red[0]==20 && window.red[499]==519);
 // A normal small pulse remains continuous; a pressure/contact step resets.
 ppg_contact_t contact={0};
 for(unsigned i=0;i<500;++i) {
  unsigned phase=i%100, pulse=phase<50?phase:100-phase;
  assert(!ppg_contact_step(&contact,24000+2*pulse,27000+3*pulse));
 }
 assert(ppg_contact_step(&contact,8000,9000));
 assert(!ppg_contact_step(&contact,8010,9010));
 assert(ppg_contact_step(&contact,24000,27000));
 // Weak/flat PPG must not toggle contact while optical contact stays present.
 max30102_wear_t wear={.detect_flag=1};uint32_t flat[32];
 for(unsigned i=0;i<32;++i)flat[i]=10000;
 for(unsigned i=0;i<40;++i)max30102_wear_update(&wear,flat,32,15000*32);
 assert(wear.detect_flag==0);
 for(unsigned i=0;i<2;++i)max30102_wear_update(&wear,flat,32,1000*32);
 assert(wear.detect_flag==0);
 max30102_wear_update(&wear,flat,32,15000*32); // Contact recovers before off debounce.
 for(unsigned i=0;i<3;++i)max30102_wear_update(&wear,flat,32,1000*32);
 assert(wear.detect_flag==1);
 // Fresh -> invalid hold -> fresh replacement -> off clears -> next session empty.
 ppg_result_t result={0};
 ppg_result_update(&result,true,false,0,1000);
 assert(!result.valid && !result.held && result.updated_us==0);
 ppg_result_update(&result,true,true,97.8f,2000);
 assert(result.valid && !result.held && result.value==97.8f);
 for(int i=0;i<100;++i)ppg_result_update(&result,true,false,0,3000+i);
 assert(!result.valid && result.held && result.value==97.8f && result.updated_us==2000);
 ppg_result_update(&result,true,true,NAN,5000);
 assert(result.held && result.value==97.8f && result.updated_us==2000);
 ppg_result_update(&result,true,true,105,6000);
 assert(result.held && result.value==97.8f);
 ppg_result_update(&result,true,true,96.9f,7000);
 assert(result.valid && !result.held && result.value==96.9f && result.updated_us==7000);
 ppg_result_update(&result,false,false,0,8000);
 assert(!result.valid && !result.held && result.value==0 && result.updated_us==0);
 ppg_result_update(&result,true,false,0,9000);
 assert(!result.valid && !result.held && result.updated_us==0);
 puts("PASS: FIFO stream/history, contact steps, weak-pulse contact retention, valid-result hold/update/removal");
}
'''

with tempfile.TemporaryDirectory(prefix='ppg-stream-test-') as d:
    folder=Path(d)
    for name, body in HEADERS.items():
        p=folder/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body)
    source=folder/'test.c';source.write_text(SOURCE)
    binary=folder/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                    '-I'+str(folder),'-I'+str(ROOT/'components/max30102'),'-I'+str(ROOT/'main'),
                    str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
