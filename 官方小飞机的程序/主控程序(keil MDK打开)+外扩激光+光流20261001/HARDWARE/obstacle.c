#include "stm32f10x.h"
#include "obstacle.h"
#include "hand_link.h"
#include "../../../shared/hand_protocol.h"

volatile ObstacleState obstacle;
static volatile uint8_t rx_dma[128];
static uint8_t frame[7],used;
static uint16_t consumed;

/* Captured sensor protocol: 5A 5A 45 02 high low sum8, centimetres.
 * A sliding window recovers after noise, dropped bytes and embedded headers. */
void obstacle_receive_byte(uint8_t byte) {
 unsigned i;uint8_t sum=0;
 ++obstacle.bytes;
 frame[used++]=byte;
 if(used<7)return;
 if(frame[0]==0x5a&&frame[1]==0x5a&&frame[2]==0x45&&frame[3]==2) {
  for(i=0;i<6;i++)sum=(uint8_t)(sum+frame[i]);
  if(sum==frame[6]) {
   uint16_t cm=((uint16_t)frame[4]<<8)|frame[5];
   obstacle.raw_cm=cm;obstacle.last_ms=hand_milliseconds;
   obstacle.valid=cm>=HC_DISTANCE_MIN_CM&&cm<=HC_DISTANCE_MAX_CM;
   ++obstacle.frames;used=0;return;
  }
  ++obstacle.bad_checksum;
 }
 for(i=0;i<6;i++)frame[i]=frame[i+1];
 used=6;
}
uint16_t obstacle_distance_cm(void) {
 return obstacle.valid&&obstacle.frames&&
  (uint32_t)(hand_milliseconds-obstacle.last_ms)<=600
  ?(uint16_t)obstacle.raw_cm:HC_DISTANCE_UNKNOWN;
}
void obstacle_init(void) {
 GPIO_InitTypeDef gpio;
 USART_InitTypeDef uart;
 DMA_InitTypeDef dma;
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);
 RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1,ENABLE);
 /* Receive only on PB11: do not configure or drive the module's command pin. */
 gpio.GPIO_Pin=GPIO_Pin_11;gpio.GPIO_Speed=GPIO_Speed_50MHz;
 gpio.GPIO_Mode=GPIO_Mode_IN_FLOATING;GPIO_Init(GPIOB,&gpio);
 USART_StructInit(&uart);uart.USART_BaudRate=9600;uart.USART_Mode=USART_Mode_Rx;
 USART_Init(USART3,&uart);
 DMA_DeInit(DMA1_Channel3);DMA_StructInit(&dma);
 dma.DMA_PeripheralBaseAddr=(uint32_t)&USART3->DR;
 dma.DMA_MemoryBaseAddr=(uint32_t)rx_dma;
 dma.DMA_DIR=DMA_DIR_PeripheralSRC;dma.DMA_BufferSize=sizeof(rx_dma);
 dma.DMA_MemoryInc=DMA_MemoryInc_Enable;dma.DMA_Mode=DMA_Mode_Circular;
 DMA_Init(DMA1_Channel3,&dma);DMA_Cmd(DMA1_Channel3,ENABLE);
 USART_DMACmd(USART3,USART_DMAReq_Rx,ENABLE);USART_Cmd(USART3,ENABLE);
}
/* Run in the 3ms scheduler. DMA keeps receiving during the existing IMU work;
 * at 9600 baud a 128-byte ring covers >130ms. No new interrupt competes with it. */
void obstacle_poll(void) {
 uint32_t status=USART3->SR;
 uint16_t end=(sizeof(rx_dma)-DMA1_Channel3->CNDTR)%sizeof(rx_dma);
 if(status&(USART_SR_ORE|USART_SR_NE|USART_SR_FE|USART_SR_PE)) {
  volatile uint32_t discard=USART3->DR;(void)discard;
  ++obstacle.uart_errors;obstacle.valid=0;used=0;consumed=end;return;
 }
 while(consumed!=end) {
  obstacle_receive_byte(rx_dma[consumed]);
  consumed=(consumed+1)%sizeof(rx_dma);
 }
}
