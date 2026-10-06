/* Ghidra 11.0.3 ARM Cortex-M3 decompilation.
 * Addresses and inferred names retained for comparison.
 * This file is pseudocode; build the manually reconstructed C sources.
 * Source SHA256: cbe3d9ff29cbe4ed8b158dc277fd6280a572135573af9f70b42d96420f9ad43d
 */


/* 080000f4 | FUN_080000f4 | 44 bytes */

void FUN_080000f4(void)

{
  int iVar1;
  code *UNRECOVERED_JUMPTABLE;
  int iVar2;
  undefined4 *puVar3;
  
  iVar1 = DAT_08000120;
  puVar3 = (undefined4 *)((int)&DAT_08000120 + DAT_08000120);
  iVar2 = DAT_08000120 + 0x800011f;
  if (puVar3 == (undefined4 *)((int)&DAT_08000120 + DAT_08000124)) {
    FUN_08000168();
  }
  UNRECOVERED_JUMPTABLE = *(code **)(iVar1 + 0x800012c);
  if (((uint)UNRECOVERED_JUMPTABLE & 1) != 0) {
    UNRECOVERED_JUMPTABLE = (code *)(iVar2 - (int)UNRECOVERED_JUMPTABLE);
  }
                    /* WARNING: Could not recover jumptable at 0x0800011e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*UNRECOVERED_JUMPTABLE)
            (*puVar3,*(undefined4 *)((int)&DAT_08000124 + iVar1),
             *(undefined4 *)(scatter_copy + iVar1));
  return;
}


/* 08000128 | scatter_copy | 26 bytes */

void scatter_copy(undefined4 *param_1,undefined4 *param_2,uint param_3)

{
  uint uVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  undefined4 uVar4;
  undefined4 uVar5;
  
  do {
    uVar1 = param_3;
    param_3 = uVar1 - 0x10;
    if (0xf < uVar1) {
      uVar2 = *param_1;
      uVar3 = param_1[1];
      uVar4 = param_1[2];
      uVar5 = param_1[3];
      param_1 = param_1 + 4;
      *param_2 = uVar2;
      param_2[1] = uVar3;
      param_2[2] = uVar4;
      param_2[3] = uVar5;
      param_2 = param_2 + 4;
    }
  } while (0xf < uVar1 && param_3 != 0);
  if ((param_3 & 8) != 0) {
    uVar2 = *param_1;
    uVar3 = param_1[1];
    param_1 = param_1 + 2;
    *param_2 = uVar2;
    param_2[1] = uVar3;
    param_2 = param_2 + 2;
  }
  if ((int)(uVar1 * 0x20000000) < 0) {
    *param_2 = *param_1;
  }
  return;
}


/* 08000144 | scatter_zero | 28 bytes */

void scatter_zero(undefined4 param_1,undefined4 *param_2,uint param_3)

{
  uint uVar1;
  
  do {
    uVar1 = param_3;
    param_3 = uVar1 - 0x10;
    if (0xf < uVar1) {
      *param_2 = 0;
      param_2[1] = 0;
      param_2[2] = 0;
      param_2[3] = 0;
      param_2 = param_2 + 4;
    }
  } while (0xf < uVar1 && param_3 != 0);
  if ((param_3 & 8) != 0) {
    *param_2 = 0;
    param_2[1] = 0;
    param_2 = param_2 + 2;
  }
  if ((int)(uVar1 * 0x20000000) < 0) {
    *param_2 = 0;
  }
  return;
}


/* 08000160 | FUN_08000160 | 4 bytes */

undefined8 FUN_08000160(undefined4 param_1,undefined4 param_2)

{
  return CONCAT44(param_2,param_1);
}


/* 08000164 | FUN_08000164 | 4 bytes */

void FUN_08000164(void)

{
  return;
}


/* 08000168 | FUN_08000168 | 18 bytes */

void FUN_08000168(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  undefined8 uVar2;
  
  uVar1 = FUN_080001ce();
  FUN_08000160(uVar1,param_3);
  main();
  uVar2 = FUN_08000218();
  FUN_08000164();
  FUN_0800022c((int)uVar2,(int)((ulonglong)uVar2 >> 0x20));
  (*DAT_080001b0)();
                    /* WARNING: Could not recover jumptable at 0x0800018e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*DAT_080001b4)();
  return;
}


/* 0800017a | FUN_0800017a | 14 bytes */

void FUN_0800017a(undefined4 param_1,undefined4 param_2)

{
  FUN_08000164();
  FUN_0800022c(param_1,param_2);
  (*DAT_080001b0)();
                    /* WARNING: Could not recover jumptable at 0x0800018e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*DAT_080001b4)();
  return;
}


/* 08000188 | Reset_Handler | 8 bytes */

void Reset_Handler(void)

{
  (*DAT_080001b0)();
                    /* WARNING: Could not recover jumptable at 0x0800018e. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*DAT_080001b4)();
  return;
}


/* 080001a2 | Default_Handler | 2 bytes */

void Default_Handler(void)

{
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 080001a4 | FUN_080001a4 | 10 bytes */

undefined8 FUN_080001a4(void)

{
  return CONCAT44(DAT_080001bc,DAT_080001b8);
}


/* 080001ce | FUN_080001ce | 74 bytes */

void FUN_080001ce(void)

{
  uint uVar1;
  undefined4 *puVar2;
  undefined4 in_lr;
  uint *puVar3;
  
  uVar1 = FUN_08000224();
  *(undefined4 *)((uVar1 & 0xfffffff8) + 0x5c) = in_lr;
  puVar3 = (uint *)((uVar1 & 0xfffffff8) + 0x58);
  *puVar3 = uVar1;
  FUN_080001a4();
  puVar2 = (undefined4 *)*puVar3;
  *puVar2 = 0;
  puVar2[1] = 0;
  puVar2[2] = 0;
  puVar2[3] = 0;
  puVar2[4] = 0;
  puVar2[5] = 0;
  puVar2[6] = 0;
  puVar2[7] = 0;
  puVar2[8] = 0;
  puVar2[9] = 0;
  puVar2[10] = 0;
  puVar2[0xb] = 0;
  puVar2[0xc] = 0;
  puVar2[0xd] = 0;
  puVar2[0xe] = 0;
  puVar2[0xf] = 0;
  return;
}


/* 08000218 | FUN_08000218 | 12 bytes */

undefined4 FUN_08000218(undefined4 param_1)

{
  FUN_0800017a(param_1);
  return DAT_08000228;
}


/* 08000224 | FUN_08000224 | 4 bytes */

undefined4 FUN_08000224(void)

{
  return DAT_08000228;
}


/* 0800022c | FUN_0800022c | 8 bytes */

void FUN_0800022c(void)

{
  software_bkpt(0xab);
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 0800023c | adc_gpio_init | 42 bytes */

void adc_gpio_init(void)

{
  uint uVar1;
  undefined4 in_r3;
  undefined4 local_8;
  
  local_8 = in_r3;
  RCC_AHBPeriphClockCmd(1);
  RCC_APB2PeriphClockCmd(0x204,1);
  uVar1 = (uint)local_8 >> 0x10;
  local_8 = CONCAT22((ushort)uVar1 & 0xff,0xf);
  GPIO_Init(DAT_08000268,&local_8);
  return;
}


/* 0800026c | adc_dma_init | 252 bytes */

void adc_dma_init(void)

{
  int iVar1;
  undefined4 local_44;
  undefined local_40;
  undefined local_3f;
  undefined4 local_3c;
  undefined4 local_38;
  undefined local_34;
  int local_30;
  undefined4 local_2c;
  undefined4 local_28;
  undefined4 local_24;
  undefined4 local_20;
  undefined4 local_1c;
  undefined4 local_18;
  undefined4 local_14;
  undefined4 local_10;
  undefined4 local_c;
  undefined4 local_8;
  
  FUN_08000550(DAT_08000368);
  local_30 = DAT_0800036c;
  local_2c = DAT_08000370;
  local_28 = 0;
  local_24 = 4;
  local_20 = 0;
  local_1c = 0x80;
  local_18 = 0x100;
  local_14 = 0x400;
  local_10 = 0x20;
  local_c = 0x2000;
  local_8 = 0;
  FUN_0800069c(DAT_08000368,&local_30);
  FUN_08000538(DAT_08000368,1);
  local_44 = 0;
  local_40 = 1;
  local_3f = 1;
  local_3c = 0xe0000;
  local_38 = 0;
  local_34 = 4;
  FUN_080003c8(DAT_0800036c + -0x4c,&local_44);
  RCC_ADCCLKConfig(0xc000);
  FUN_08000418(DAT_0800036c + -0x4c,0,1,5);
  FUN_08000418(DAT_0800036c + -0x4c,1,2,5);
  FUN_08000418(DAT_0800036c + -0x4c,2,3,5);
  FUN_08000418(DAT_0800036c + -0x4c,3,4,5);
  FUN_0800038a(DAT_0800036c + -0x4c,1);
  FUN_08000374(DAT_0800036c + -0x4c,1);
  FUN_080004d0(DAT_0800036c + -0x4c);
  do {
    iVar1 = FUN_080003b4(DAT_0800036c + -0x4c);
  } while (iVar1 != 0);
  FUN_080004f0(DAT_0800036c + -0x4c);
  do {
    iVar1 = FUN_080003a0(DAT_0800036c + -0x4c);
  } while (iVar1 != 0);
  FUN_080004da(DAT_0800036c + -0x4c,1);
  return;
}


/* 08000374 | FUN_08000374 | 22 bytes */

void FUN_08000374(int param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) & 0xfffffffe;
  }
  else {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 1;
  }
  return;
}


/* 0800038a | FUN_0800038a | 22 bytes */

void FUN_0800038a(int param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) & 0xfffffeff;
  }
  else {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 0x100;
  }
  return;
}


/* 080003a0 | FUN_080003a0 | 20 bytes */

bool FUN_080003a0(int param_1)

{
  return (*(uint *)(param_1 + 8) & 4) != 0;
}


/* 080003b4 | FUN_080003b4 | 20 bytes */

bool FUN_080003b4(int param_1)

{
  return (*(uint *)(param_1 + 8) & 8) != 0;
}


/* 080003c8 | FUN_080003c8 | 70 bytes */

void FUN_080003c8(int param_1,uint *param_2)

{
  *(uint *)(param_1 + 4) =
       *(uint *)(param_1 + 4) & DAT_08000410 | *param_2 | (uint)*(byte *)(param_2 + 1) << 8;
  *(uint *)(param_1 + 8) =
       *(uint *)(param_1 + 8) & DAT_08000414 |
       param_2[3] | param_2[2] | (uint)*(byte *)((int)param_2 + 5) << 1;
  *(uint *)(param_1 + 0x2c) =
       *(uint *)(param_1 + 0x2c) & 0xff0fffff | (*(byte *)(param_2 + 4) - 1 & 0xff) << 0x14;
  return;
}


/* 08000418 | FUN_08000418 | 184 bytes */

void FUN_08000418(int param_1,int param_2,int param_3,int param_4)

{
  if (param_2 < 10) {
    *(uint *)(param_1 + 0x10) =
         *(uint *)(param_1 + 0x10) & ~(7 << (param_2 * 3 & 0xffU)) |
         param_4 << (param_2 * 3 & 0xffU);
  }
  else {
    *(uint *)(param_1 + 0xc) =
         *(uint *)(param_1 + 0xc) & ~(7 << ((param_2 + -10) * 3 & 0xffU)) |
         param_4 << ((param_2 + -10) * 3 & 0xffU);
  }
  if (param_3 < 7) {
    *(uint *)(param_1 + 0x34) =
         *(uint *)(param_1 + 0x34) & ~(0x1f << ((param_3 + -1) * 5 & 0xffU)) |
         param_2 << ((param_3 + -1) * 5 & 0xffU);
  }
  else if (param_3 < 0xd) {
    *(uint *)(param_1 + 0x30) =
         *(uint *)(param_1 + 0x30) & ~(0x1f << ((param_3 + -7) * 5 & 0xffU)) |
         param_2 << ((param_3 + -7) * 5 & 0xffU);
  }
  else {
    *(uint *)(param_1 + 0x2c) =
         *(uint *)(param_1 + 0x2c) & ~(0x1f << ((param_3 + -0xd) * 5 & 0xffU)) |
         param_2 << ((param_3 + -0xd) * 5 & 0xffU);
  }
  return;
}


/* 080004d0 | FUN_080004d0 | 10 bytes */

void FUN_080004d0(int param_1)

{
  *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 8;
  return;
}


/* 080004da | FUN_080004da | 22 bytes */

void FUN_080004da(int param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) & 0xffafffff;
  }
  else {
    *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 0x500000;
  }
  return;
}


/* 080004f0 | FUN_080004f0 | 10 bytes */

void FUN_080004f0(int param_1)

{
  *(uint *)(param_1 + 8) = *(uint *)(param_1 + 8) | 4;
  return;
}


/* 080004fc | status_gpio_init | 50 bytes */

void status_gpio_init(void)

{
  undefined4 in_r3;
  undefined4 local_8 [2];
  
  local_8[0] = in_r3;
  RCC_APB2PeriphClockCmd(4,1);
  local_8[0] = 0x10031000;
  GPIO_Init(DAT_08000530,local_8);
  *(undefined4 *)(DAT_08000530 + 0x10) = 0x1000;
  return;
}


/* 08000534 | BusFault_Handler | 4 bytes */

void BusFault_Handler(void)

{
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 08000538 | FUN_08000538 | 24 bytes */

void FUN_08000538(uint *param_1,int param_2)

{
  if (param_2 == 0) {
    *param_1 = *param_1 & 0xfffe;
  }
  else {
    *param_1 = *param_1 | 1;
  }
  return;
}


/* 08000550 | FUN_08000550 | 324 bytes */

void FUN_08000550(uint *param_1)

{
  *param_1 = *param_1 & 0xfffe;
  *param_1 = 0;
  param_1[1] = 0;
  param_1[2] = 0;
  param_1[3] = 0;
  if (param_1 == DAT_08000694) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf;
  }
  else if (param_1 == DAT_08000694 + 5) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf0;
  }
  else if (param_1 == DAT_08000694 + 10) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf00;
  }
  else if (param_1 == DAT_08000694 + 0xf) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf000;
  }
  else if (param_1 == DAT_08000694 + 0x14) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf0000;
  }
  else if (param_1 == DAT_08000694 + 0x19) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf00000;
  }
  else if (param_1 == DAT_08000694 + 0x1e) {
    DAT_08000694[-1] = DAT_08000694[-1] | 0xf000000;
  }
  else if (param_1 == DAT_08000698) {
    param_1[-1] = param_1[-1] | 0xf;
  }
  else if (param_1 == DAT_08000698 + 5) {
    DAT_08000698[-1] = DAT_08000698[-1] | 0xf0;
  }
  else if (param_1 == DAT_08000698 + 10) {
    DAT_08000698[-1] = DAT_08000698[-1] | 0xf00;
  }
  else if (param_1 == DAT_08000698 + 0xf) {
    DAT_08000698[-1] = DAT_08000698[-1] | 0xf000;
  }
  else if (param_1 == DAT_08000698 + 0x14) {
    DAT_08000698[-1] = DAT_08000698[-1] | 0xf0000;
  }
  return;
}


/* 0800069c | FUN_0800069c | 60 bytes */

void FUN_0800069c(uint *param_1,uint *param_2)

{
  *param_1 = *param_1 & 0xffff800f |
             param_2[2] | param_2[8] | param_2[4] | param_2[5] | param_2[6] | param_2[7] |
             param_2[9] | param_2[10];
  param_1[1] = param_2[3];
  param_1[2] = *param_2;
  param_1[3] = param_2[1];
  return;
}


/* 080006d8 | DebugMon_Handler | 2 bytes */

void DebugMon_Handler(void)

{
  return;
}


/* 080006dc | FUN_080006dc | 22 bytes */

void FUN_080006dc(uint param_1)

{
  uint *puVar1;
  
  puVar1 = DAT_080006f4;
  *DAT_080006f4 = *DAT_080006f4 & 0xffffffef;
  *puVar1 = *puVar1 | param_1;
  return;
}


/* 080006f8 | FUN_080006f8 | 18 bytes */

void FUN_080006f8(uint param_1)

{
  *DAT_0800070c = *DAT_0800070c & 0x38 | param_1;
  return;
}


/* 08000710 | GPIO_Init | 278 bytes */

uint GPIO_Init(uint *param_1,ushort *param_2)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  uint uVar4;
  
  uVar1 = 0;
  uVar4 = *(byte *)((int)param_2 + 3) & 0xf;
  if ((*(byte *)((int)param_2 + 3) & 0x10) != 0) {
    uVar4 = *(byte *)(param_2 + 1) | uVar4;
  }
  if (*(char *)param_2 != '\0') {
    uVar3 = *param_1;
    for (uVar1 = 0; uVar1 < 8; uVar1 = uVar1 + 1) {
      uVar2 = 1 << (uVar1 & 0xff);
      if ((*param_2 & uVar2) == uVar2) {
        uVar3 = uVar4 << (uVar1 << 2 & 0xff) | uVar3 & ~(0xf << (uVar1 << 2 & 0xff));
        if (*(char *)((int)param_2 + 3) == '(') {
          param_1[5] = 1 << (uVar1 & 0xff);
        }
        else if (*(char *)((int)param_2 + 3) == 'H') {
          param_1[4] = 1 << (uVar1 & 0xff);
        }
      }
    }
    *param_1 = uVar3;
  }
  if (0xff < *param_2) {
    uVar3 = param_1[1];
    for (uVar1 = 0; uVar1 < 8; uVar1 = uVar1 + 1) {
      uVar2 = 1 << (uVar1 + 8 & 0xff);
      if ((*param_2 & uVar2) == uVar2) {
        uVar3 = uVar4 << (uVar1 << 2 & 0xff) | uVar3 & ~(0xf << (uVar1 << 2 & 0xff));
        if (*(char *)((int)param_2 + 3) == '(') {
          param_1[5] = 1 << (uVar1 + 8 & 0xff);
        }
        if (*(char *)((int)param_2 + 3) == 'H') {
          param_1[4] = 1 << (uVar1 + 8 & 0xff);
        }
      }
    }
    param_1[1] = uVar3;
  }
  return uVar1;
}


/* 08000826 | GPIO_SetBits | 4 bytes */

void GPIO_SetBits(int param_1,undefined4 param_2)

{
  *(undefined4 *)(param_1 + 0x10) = param_2;
  return;
}


/* 0800082c | micros | 58 bytes */

int micros(void)

{
  dword dVar1;
  
  do {
    dVar1 = SYSTICK_VAL;
  } while (*DAT_08000868 != *DAT_08000868);
  return (*DAT_0800086c * 1000 - dVar1) / *DAT_0800086c + *DAT_08000868 * 1000;
}


/* 08000870 | HardFault_Handler | 4 bytes */

void HardFault_Handler(void)

{
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 08000874 | soft_i2c_ack | 198 bytes */

void soft_i2c_ack(void)

{
  char cVar1;
  
  *DAT_0800093c = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  *DAT_0800093c = 0x80;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  DAT_0800093c[-1] = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  *DAT_0800093c = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  return;
}


/* 08000940 | soft_i2c_nack | 200 bytes */

void soft_i2c_nack(void)

{
  char cVar1;
  
  *DAT_08000a08 = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  DAT_08000a08[-1] = 0x80;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  DAT_08000a08[-1] = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  *DAT_08000a08 = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  return;
}


/* 08000a0c | soft_i2c_read_byte | 188 bytes */

uint soft_i2c_read_byte(void)

{
  uint uVar1;
  char cVar2;
  char cVar3;
  bool bVar4;
  
  cVar3 = '\b';
  uVar1 = 0;
  *DAT_08000ac8 = 0x80;
  while (bVar4 = cVar3 != '\0', cVar3 = cVar3 + -1, bVar4) {
    uVar1 = (uVar1 << 0x19) >> 0x18;
    DAT_08000ac8[1] = 0x40;
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    *DAT_08000ac8 = 0x40;
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    if ((DAT_08000ac8[-2] & 0x80) != 0) {
      uVar1 = uVar1 | 1;
    }
  }
  DAT_08000ac8[1] = 0x40;
  return uVar1;
}


/* 08000acc | soft_i2c_write_byte | 186 bytes */

void soft_i2c_write_byte(uint param_1)

{
  char cVar1;
  char cVar2;
  bool bVar3;
  
  cVar2 = '\b';
  while (bVar3 = cVar2 != '\0', cVar2 = cVar2 + -1, bVar3) {
    *DAT_08000b88 = 0x40;
    for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
    }
    if ((param_1 & 0x80) == 0) {
      *DAT_08000b88 = 0x80;
    }
    else {
      DAT_08000b88[-1] = 0x80;
    }
    param_1 = (param_1 << 0x19) >> 0x18;
    for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
    }
    DAT_08000b88[-1] = 0x40;
    for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
    }
    for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
    }
    for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
    }
  }
  *DAT_08000b88 = 0x40;
  return;
}


/* 08000b8c | soft_i2c_start | 144 bytes */

/* WARNING: Removing unreachable block (ram,0x08000bc4) */

undefined4 soft_i2c_start(void)

{
  undefined4 *puVar1;
  char cVar2;
  undefined4 uVar3;
  
  puVar1 = DAT_08000c1c;
  *DAT_08000c1c = 0x80;
  *puVar1 = 0x40;
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  DAT_08000c1c[1] = 0x80;
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  if ((DAT_08000c1c[-2] & 0x80) == 0) {
    DAT_08000c1c[1] = 0x40;
    for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
    }
    uVar3 = 0;
  }
  else {
    uVar3 = 1;
  }
  return uVar3;
}


/* 08000c20 | soft_i2c_stop | 152 bytes */

void soft_i2c_stop(void)

{
  char cVar1;
  
  *DAT_08000cb8 = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  *DAT_08000cb8 = 0x80;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  DAT_08000cb8[-1] = 0x40;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  DAT_08000cb8[-1] = 0x80;
  for (cVar1 = '\x05'; cVar1 != '\0'; cVar1 = cVar1 + -1) {
  }
  return;
}


/* 08000cbc | soft_i2c_wait_ack | 176 bytes */

bool soft_i2c_wait_ack(void)

{
  bool bVar1;
  char cVar2;
  
  *DAT_08000d6c = 0x40;
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  DAT_08000d6c[-1] = 0x80;
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  DAT_08000d6c[-1] = 0x40;
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  for (cVar2 = '\x05'; cVar2 != '\0'; cVar2 = cVar2 + -1) {
  }
  bVar1 = (DAT_08000d6c[-3] & 0x80) == 0;
  if (bVar1) {
    *DAT_08000d6c = 0x40;
  }
  else {
    *DAT_08000d6c = 0x40;
  }
  return !bVar1;
}


/* 08000d70 | soft_i2c_gpio_init | 56 bytes */

void soft_i2c_gpio_init(void)

{
  undefined4 local_8 [2];
  
  local_8[0] = 0x10030040;
  GPIO_Init(DAT_08000da8,local_8);
  local_8[0] = 0x140300c0;
  GPIO_Init(DAT_08000da8,local_8);
  return;
}


/* 08000dac | i2c_read_register | 100 bytes */

int i2c_read_register(int param_1,undefined4 param_2)

{
  char cVar1;
  int iVar2;
  
  iVar2 = soft_i2c_start();
  if (iVar2 != 1) {
    soft_i2c_write_byte(param_1);
    iVar2 = soft_i2c_wait_ack();
    if (iVar2 == 1) {
      soft_i2c_stop();
      iVar2 = 1;
    }
    else {
      soft_i2c_write_byte(param_2);
      soft_i2c_wait_ack();
      soft_i2c_stop();
      soft_i2c_start();
      soft_i2c_write_byte(param_1 + 1U & 0xff);
      iVar2 = soft_i2c_wait_ack();
      if (iVar2 == 1) {
        soft_i2c_stop();
        iVar2 = 1;
      }
      else {
        cVar1 = soft_i2c_read_byte();
        soft_i2c_nack();
        soft_i2c_stop();
        iVar2 = (int)cVar1;
      }
    }
  }
  return iVar2;
}


/* 08000e10 | i2c_write_buffer | 96 bytes */

int i2c_write_buffer(undefined4 param_1,undefined4 param_2,int param_3,int param_4)

{
  int iVar1;
  int iVar2;
  
  iVar1 = soft_i2c_start();
  if (iVar1 != 1) {
    soft_i2c_write_byte(param_1);
    iVar1 = soft_i2c_wait_ack();
    if (iVar1 == 1) {
      soft_i2c_stop();
      iVar1 = 1;
    }
    else {
      soft_i2c_write_byte(param_2);
      soft_i2c_wait_ack();
      for (iVar1 = 0; iVar1 < param_4; iVar1 = iVar1 + 1) {
        soft_i2c_write_byte(*(undefined *)(param_3 + iVar1));
        iVar2 = soft_i2c_wait_ack();
        if (iVar2 == 1) {
          soft_i2c_stop();
          return 1;
        }
      }
      soft_i2c_stop();
      iVar1 = 0;
    }
  }
  return iVar1;
}


/* 08000e70 | i2c_write_register | 68 bytes */

int i2c_write_register(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  int iVar1;
  
  iVar1 = soft_i2c_start();
  if (iVar1 != 1) {
    soft_i2c_write_byte(param_1);
    iVar1 = soft_i2c_wait_ack();
    if (iVar1 == 1) {
      soft_i2c_stop();
      iVar1 = 1;
    }
    else {
      soft_i2c_write_byte(param_2);
      soft_i2c_wait_ack();
      soft_i2c_write_byte(param_3);
      soft_i2c_wait_ack();
      soft_i2c_stop();
      iVar1 = 0;
    }
  }
  return iVar1;
}


/* 08000eb4 | i2c_read_buffer | 128 bytes */

int i2c_read_buffer(int param_1,undefined4 param_2,undefined *param_3,uint param_4)

{
  undefined uVar1;
  int iVar2;
  
  iVar2 = soft_i2c_start();
  if (iVar2 != 1) {
    soft_i2c_write_byte(param_1);
    iVar2 = soft_i2c_wait_ack();
    if (iVar2 == 1) {
      soft_i2c_stop();
      iVar2 = 1;
    }
    else {
      soft_i2c_write_byte(param_2);
      soft_i2c_wait_ack();
      soft_i2c_stop();
      soft_i2c_start();
      soft_i2c_write_byte(param_1 + 1U & 0xff);
      iVar2 = soft_i2c_wait_ack();
      if (iVar2 == 1) {
        soft_i2c_stop();
        iVar2 = 1;
      }
      else {
        for (; param_4 != 0; param_4 = param_4 - 1 & 0xff) {
          uVar1 = soft_i2c_read_byte();
          *param_3 = uVar1;
          if (param_4 == 1) {
            soft_i2c_nack();
          }
          else {
            soft_i2c_ack();
          }
          param_3 = param_3 + 1;
        }
        soft_i2c_stop();
        iVar2 = 0;
      }
    }
  }
  return iVar2;
}


/* 08000f34 | MemManage_Handler | 4 bytes */

void MemManage_Handler(void)

{
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 08000f38 | NMI_Handler | 2 bytes */

void NMI_Handler(void)

{
  return;
}


/* 08000f3c | nrf_check | 68 bytes */

bool nrf_check(undefined4 param_1,undefined4 param_2)

{
  uint uVar1;
  undefined4 local_18 [4];
  
  local_18[2] = DAT_08000f80;
  local_18[3] = DAT_08000f84;
  local_18[0] = param_1;
  local_18[1] = param_2;
  nrf_write_buffer(0x30,local_18 + 2,5);
  nrf_read_buffer(0x10,local_18,5);
  for (uVar1 = 0; (uVar1 < 5 && (*(char *)((int)local_18 + uVar1) == -0x5b));
      uVar1 = uVar1 + 1 & 0xff) {
  }
  return uVar1 != 5;
}


/* 08000f88 | nrf_gpio_init | 152 bytes */

void nrf_gpio_init(void)

{
  undefined4 in_r3;
  undefined4 local_8 [2];
  
  local_8[0] = in_r3;
  RCC_APB2PeriphClockCmd(4,1);
  local_8[0] = 0x10030800;
  GPIO_Init(DAT_08001020,local_8);
  RCC_APB2PeriphClockCmd(8,1);
  local_8[0] = 0x10031000;
  GPIO_Init(DAT_08001024,local_8);
  *(undefined4 *)(DAT_08001020 + 0x10) = 0x800;
  *(undefined4 *)(DAT_08001024 + 0x10) = 0x1000;
  local_8[0] = 0x48030100;
  GPIO_Init(DAT_08001020,local_8);
  GPIO_SetBits(DAT_08001020,0x100);
  soft_spi_gpio_init();
  *(undefined4 *)(DAT_08001020 + 0x14) = 0x800;
  *(undefined4 *)(DAT_08001024 + 0x10) = 0x1000;
  return;
}


/* 08001028 | nrf_read_buffer | 62 bytes */

undefined4 nrf_read_buffer(undefined4 param_1,int param_2,int param_3)

{
  undefined uVar1;
  undefined4 uVar2;
  uint uVar3;
  
  *DAT_08001068 = 0x1000;
  uVar2 = soft_spi_transfer(param_1);
  for (uVar3 = 0; (int)uVar3 < param_3; uVar3 = uVar3 + 1 & 0xff) {
    uVar1 = soft_spi_transfer(0xff);
    *(undefined *)(param_2 + uVar3) = uVar1;
  }
  DAT_08001068[-1] = 0x1000;
  return uVar2;
}


/* 0800106c | nrf_read_register | 40 bytes */

undefined4 nrf_read_register(undefined4 param_1)

{
  undefined4 uVar1;
  
  *DAT_08001094 = 0x1000;
  soft_spi_transfer(param_1);
  uVar1 = soft_spi_transfer(0xff);
  DAT_08001094[-1] = 0x1000;
  return uVar1;
}


/* 08001098 | nrf_transmit_32 | 96 bytes */

undefined4 nrf_transmit_32(undefined4 param_1)

{
  uint uVar1;
  undefined4 uVar2;
  
  *DAT_080010f8 = 0x800;
  nrf_write_buffer(0xa0,param_1,0x20);
  DAT_080010f8[-1] = 0x800;
  do {
  } while ((DAT_080010f8[-3] & 0x100) != 0);
  uVar1 = nrf_read_register(7);
  nrf_write_register(0x27,uVar1);
  if ((uVar1 & 0x10) == 0) {
    if ((uVar1 & 0x20) == 0) {
      uVar2 = 0xff;
    }
    else {
      uVar2 = 0x20;
    }
  }
  else {
    nrf_write_register(0xe1,0xff);
    uVar2 = 0x10;
  }
  return uVar2;
}


/* 080010fc | nrf_write_buffer | 62 bytes */

undefined4 nrf_write_buffer(undefined4 param_1,undefined *param_2,int param_3)

{
  undefined4 uVar1;
  uint uVar2;
  
  *DAT_0800113c = 0x1000;
  uVar1 = soft_spi_transfer(param_1);
  for (uVar2 = 0; (int)uVar2 < param_3; uVar2 = uVar2 + 1 & 0xff) {
    soft_spi_transfer(*param_2);
    param_2 = param_2 + 1;
  }
  DAT_0800113c[-1] = 0x1000;
  return uVar1;
}


/* 08001140 | nrf_write_register | 42 bytes */

undefined4 nrf_write_register(undefined4 param_1,undefined4 param_2)

{
  undefined4 uVar1;
  
  *DAT_0800116c = 0x1000;
  uVar1 = soft_spi_transfer(param_1);
  soft_spi_transfer(param_2);
  DAT_0800116c[-1] = 0x1000;
  return uVar1;
}


/* 08001170 | nrf_init | 22 bytes */

void nrf_init(void)

{
  int iVar1;
  
  nrf_gpio_init();
  do {
    iVar1 = nrf_check();
  } while (iVar1 != 0);
  nrf_tx_mode();
  return;
}


/* 08001188 | build_and_send_packet | 106 bytes */

void build_and_send_packet(void)

{
  int iVar1;
  uint uVar2;
  
  for (uVar2 = 4; iVar1 = DAT_080011f8, uVar2 < 0x1a; uVar2 = uVar2 + 2 & 0xff) {
    *(undefined *)(DAT_080011f8 + uVar2) = *(undefined *)(DAT_080011f4 + uVar2 + -3);
    *(char *)(DAT_080011f8 + 0x1f) = *(char *)(iVar1 + 0x1f) + *(char *)(iVar1 + uVar2);
    iVar1 = DAT_080011f8;
    *(undefined *)(DAT_080011f8 + uVar2 + 1) = *(undefined *)(DAT_080011f4 + uVar2 + -4);
    *(char *)(iVar1 + 0x1f) = *(char *)(iVar1 + 0x1f) + *(char *)(iVar1 + uVar2);
  }
  *(undefined *)(DAT_080011f8 + 0x1f) = 0;
  for (uVar2 = 0; uVar2 < 0x1f; uVar2 = uVar2 + 1 & 0xff) {
    *(char *)(DAT_080011f8 + 0x1f) =
         *(char *)(DAT_080011f8 + 0x1f) + *(char *)(DAT_080011f8 + uVar2);
  }
  nrf_transmit_32(DAT_080011f8);
  return;
}


/* 080011fc | interrupts_init | 70 bytes */

void interrupts_init(void)

{
  undefined4 in_r3;
  undefined4 local_8 [2];
  
  local_8[0] = in_r3;
  NVIC_PriorityGroupConfig(0x700);
  local_8[0] = 0x1000025;
  NVIC_Init(local_8);
  local_8[0] = 0x103021d;
  NVIC_Init(local_8);
  return;
}


/* 08001244 | NVIC_Init | 100 bytes */

void NVIC_Init(byte *param_1)

{
  uint uVar1;
  
  if (param_1[3] == 0) {
    *(int *)(DAT_080012b0 + ((int)(uint)*param_1 >> 5) * 4) = 1 << (*param_1 & 0x1f);
  }
  else {
    uVar1 = 0x700 - (*DAT_080012a8 & 0x700) >> 8;
    *(char *)(DAT_080012ac + (uint)*param_1) =
         (char)(((uint)param_1[1] << (4 - uVar1 & 0xff) | (uint)param_1[2] & 0xfU >> (uVar1 & 0xff))
               << 4);
    *(int *)(&DAT_e000e100 + ((int)(uint)*param_1 >> 5) * 4) = 1 << (*param_1 & 0x1f);
  }
  return;
}


/* 080012b4 | NVIC_PriorityGroupConfig | 10 bytes */

void NVIC_PriorityGroupConfig(uint param_1)

{
  *DAT_080012c4 = DAT_080012c0 | param_1;
  return;
}


/* 080012c8 | PendSV_Handler | 2 bytes */

void PendSV_Handler(void)

{
  return;
}


/* 080012cc | RCC_ADCCLKConfig | 18 bytes */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void RCC_ADCCLKConfig(uint arg0)

{
  *(uint *)(DAT_080012e0 + 4) = *(uint *)(DAT_080012e0 + 4) & 0xffff3fff | arg0;
  return;
}


/* 080012e4 | RCC_AHBPeriphClockCmd | 26 bytes */

void RCC_AHBPeriphClockCmd(uint param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(DAT_08001300 + 0x14) = *(uint *)(DAT_08001300 + 0x14) & ~param_1;
  }
  else {
    *(uint *)(DAT_08001300 + 0x14) = *(uint *)(DAT_08001300 + 0x14) | param_1;
  }
  return;
}


/* 08001304 | RCC_APB1PeriphClockCmd | 26 bytes */

void RCC_APB1PeriphClockCmd(uint param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(DAT_08001320 + 0x1c) = *(uint *)(DAT_08001320 + 0x1c) & ~param_1;
  }
  else {
    *(uint *)(DAT_08001320 + 0x1c) = *(uint *)(DAT_08001320 + 0x1c) | param_1;
  }
  return;
}


/* 08001324 | RCC_APB2PeriphClockCmd | 26 bytes */

void RCC_APB2PeriphClockCmd(uint param_1,int param_2)

{
  if (param_2 == 0) {
    *(uint *)(DAT_08001340 + 0x18) = *(uint *)(DAT_08001340 + 0x18) & ~param_1;
  }
  else {
    *(uint *)(DAT_08001340 + 0x18) = *(uint *)(DAT_08001340 + 0x18) | param_1;
  }
  return;
}


/* 08001344 | clock_init | 120 bytes */

void clock_init(void)

{
  int iVar1;
  
  RCC_DeInit();
  FUN_08001544(1);
  do {
    iVar1 = FUN_080014e0(0x21);
  } while (iVar1 == 0);
  FUN_080006dc(0x10);
  FUN_080006f8(2);
  FUN_0800152c(0);
  FUN_08001550(0x400);
  FUN_08001568(0);
  FUN_0800158c(0,0x280000);
  FUN_08001580(1);
  do {
    iVar1 = FUN_080014e0(0x39);
  } while (iVar1 == 0);
  FUN_080015a8(2);
  do {
    iVar1 = FUN_0800151c();
  } while (iVar1 != 8);
  RCC_APB2PeriphClockCmd(0x521d,1);
  RCC_APB1PeriphClockCmd(DAT_080013bc,1);
  return;
}


/* 080013c0 | RCC_DeInit | 64 bytes */

void RCC_DeInit(void)

{
  uint *puVar1;
  uint *puVar2;
  
  puVar1 = DAT_08001400;
  *DAT_08001400 = *DAT_08001400 | 1;
  puVar2 = DAT_08001400;
  DAT_08001400[1] = puVar1[1] & DAT_08001404;
  puVar1 = DAT_08001400;
  *DAT_08001400 = *puVar2 & DAT_08001408;
  *puVar1 = *puVar1 & 0xfffbffff;
  puVar1[1] = puVar1[1] & 0xff80ffff;
  puVar1[2] = 0x9f0000;
  return;
}


/* 0800140c | RCC_GetClocksFreq | 192 bytes */

void RCC_GetClocksFreq(uint *param_1)

{
  uint uVar1;
  int iVar2;
  
  uVar1 = *(uint *)(DAT_080014cc + 4) & 0xc;
  if (uVar1 == 0) {
    *param_1 = DAT_080014d0;
  }
  else if (uVar1 == 4) {
    *param_1 = DAT_080014d0;
  }
  else if (uVar1 == 8) {
    iVar2 = ((*(uint *)(DAT_080014cc + 4) & 0x3c0000) >> 0x12) + 2;
    if ((*(uint *)(DAT_080014cc + 4) & 0x10000) == 0) {
      *param_1 = iVar2 * DAT_080014d4;
    }
    else if ((*(uint *)(DAT_080014cc + 4) & 0x20000) == 0) {
      *param_1 = iVar2 * DAT_080014d0;
    }
    else {
      *param_1 = iVar2 * DAT_080014d4;
    }
  }
  else {
    *param_1 = DAT_080014d0;
  }
  param_1[1] = *param_1 >> *(sbyte *)(DAT_080014d8 + ((*(uint *)(DAT_080014cc + 4) & 0xf0) >> 4));
  param_1[2] = param_1[1] >> *(sbyte *)(DAT_080014d8 + ((*(uint *)(DAT_080014cc + 4) & 0x700) >> 8))
  ;
  param_1[3] = param_1[1] >>
               *(sbyte *)(DAT_080014d8 + ((*(uint *)(DAT_080014cc + 4) & 0x3800) >> 0xb));
  param_1[4] = param_1[3] /
               (uint)*(byte *)(DAT_080014dc + ((*(uint *)(DAT_080014cc + 4) & 0xc000) >> 0xe));
  return;
}


/* 080014e0 | FUN_080014e0 | 56 bytes */

bool FUN_080014e0(uint param_1)

{
  uint uVar1;
  
  if ((int)param_1 >> 5 == 1) {
    uVar1 = *DAT_08001518;
  }
  else if ((int)param_1 >> 5 == 2) {
    uVar1 = DAT_08001518[8];
  }
  else {
    uVar1 = DAT_08001518[9];
  }
  return (1 << (param_1 & 0x1f) & uVar1) != 0;
}


/* 0800151c | FUN_0800151c | 10 bytes */

uint FUN_0800151c(void)

{
  return *(uint *)(DAT_08001528 + 4) & 0xc;
}


/* 0800152c | FUN_0800152c | 18 bytes */

void FUN_0800152c(uint param_1)

{
  *(uint *)(DAT_08001540 + 4) = *(uint *)(DAT_08001540 + 4) & 0xffffff0f | param_1;
  return;
}


/* 08001544 | FUN_08001544 | 6 bytes */

void FUN_08001544(undefined4 param_1)

{
  *DAT_0800154c = param_1;
  return;
}


/* 08001550 | FUN_08001550 | 18 bytes */

void FUN_08001550(uint param_1)

{
  *(uint *)(DAT_08001564 + 4) = *(uint *)(DAT_08001564 + 4) & 0xfffff8ff | param_1;
  return;
}


/* 08001568 | FUN_08001568 | 20 bytes */

void FUN_08001568(int param_1)

{
  *(uint *)(DAT_0800157c + 4) = *(uint *)(DAT_0800157c + 4) & 0xffffc7ff | param_1 << 3;
  return;
}


/* 08001580 | FUN_08001580 | 6 bytes */

void FUN_08001580(undefined4 param_1)

{
  *(undefined4 *)(DAT_08001588 + 0x60) = param_1;
  return;
}


/* 0800158c | FUN_0800158c | 24 bytes */

void FUN_0800158c(uint param_1,uint param_2)

{
  *(uint *)(DAT_080015a4 + 4) = *(uint *)(DAT_080015a4 + 4) & 0xffc0ffff | param_1 | param_2;
  return;
}


/* 080015a8 | FUN_080015a8 | 18 bytes */

void FUN_080015a8(uint param_1)

{
  *(uint *)(DAT_080015bc + 4) = *(uint *)(DAT_080015bc + 4) & 0xfffffffc | param_1;
  return;
}


/* 080015c0 | update_controls | 504 bytes */

void update_controls(void)

{
  uint uVar1;
  uint uVar2;
  
  uVar1 = soft_uint_to_float((uint)DAT_080017b8[1]);
  uVar1 = soft_float_multiply(uVar1,DAT_080017bc);
  uVar1 = soft_float_to_uint(uVar1);
  DAT_080017c4[2] = (short)uVar1 + 1000 + *(short *)(DAT_080017c0 + 6);
  uVar1 = soft_uint_to_float((uint)DAT_080017b8[2]);
  uVar1 = soft_float_multiply(uVar1,0x3e800000);
  uVar1 = soft_float_to_uint(uVar1);
  DAT_080017c4[1] = (short)uVar1 + 1000 + *(short *)(DAT_080017c0 + 4);
  uVar1 = soft_uint_to_float((uint)*DAT_080017c8);
  uVar1 = soft_float_multiply(uVar1,0x3f400000);
  uVar2 = soft_uint_to_float((uint)DAT_080017c4[1]);
  uVar2 = soft_float_multiply(uVar2,0x3e800000);
  uVar1 = soft_float_add(uVar2,uVar1);
  uVar1 = soft_float_to_uint(uVar1);
  DAT_080017c4[1] = (ushort)uVar1;
  *DAT_080017c8 = (ushort)uVar1;
  uVar1 = soft_uint_to_float((uint)*DAT_080017b8);
  uVar1 = soft_float_multiply(uVar1,0x3e800000);
  uVar1 = soft_float_to_uint(uVar1);
  DAT_080017c4[3] = (short)uVar1 + 1000 + *(short *)(DAT_080017c0 + 8);
  uVar1 = soft_uint_to_float((uint)*DAT_080017cc);
  uVar1 = soft_float_multiply(uVar1,0x3f400000);
  uVar2 = soft_uint_to_float((uint)DAT_080017c4[3]);
  uVar2 = soft_float_multiply(uVar2,0x3e800000);
  uVar1 = soft_float_add(uVar2,uVar1);
  uVar1 = soft_float_to_uint(uVar1);
  DAT_080017c4[3] = (ushort)uVar1;
  *DAT_080017cc = (ushort)uVar1;
  uVar1 = soft_uint_to_float((uint)DAT_080017b8[3]);
  uVar1 = soft_float_multiply(uVar1,0x3e800000);
  uVar1 = soft_float_to_uint(uVar1);
  *DAT_080017c4 = (short)uVar1 + 1000 + *(short *)(DAT_080017c0 + 2);
  uVar1 = soft_uint_to_float((uint)*DAT_080017d0);
  uVar1 = soft_float_multiply(uVar1,0x3f400000);
  uVar2 = soft_uint_to_float((uint)*DAT_080017c4);
  uVar2 = soft_float_multiply(uVar2,0x3e800000);
  uVar1 = soft_float_add(uVar2,uVar1);
  uVar1 = soft_float_to_uint(uVar1);
  *DAT_080017c4 = (ushort)uVar1;
  *DAT_080017d0 = (ushort)uVar1;
  if (DAT_080017c4[2] < 0x7d1) {
    if (DAT_080017c4[2] < 1000) {
      DAT_080017c4[2] = 1000;
    }
  }
  else {
    DAT_080017c4[2] = 2000;
  }
  if (DAT_080017c4[1] < 0x7d1) {
    if (DAT_080017c4[1] < 1000) {
      DAT_080017c4[1] = 1000;
    }
  }
  else {
    DAT_080017c4[1] = 2000;
  }
  if (*DAT_080017c4 < 0x7d1) {
    if (*DAT_080017c4 < 1000) {
      *DAT_080017c4 = 1000;
    }
  }
  else {
    *DAT_080017c4 = 2000;
  }
  if (DAT_080017c4[3] < 0x7d1) {
    if (DAT_080017c4[3] < 1000) {
      DAT_080017c4[3] = 1000;
    }
  }
  else {
    DAT_080017c4[3] = 2000;
  }
  scan_keys();
  build_and_send_packet();
  return;
}


/* 080017d4 | load_trim_and_init_keys | 32 bytes */

void load_trim_and_init_keys(void)

{
  undefined2 uVar1;
  
  uVar1 = eeprom_read_u16(0);
  *(undefined2 *)(DAT_080017f4 + 2) = uVar1;
  uVar1 = eeprom_read_u16(4);
  *(undefined2 *)(DAT_080017f4 + 4) = uVar1;
  keys_gpio_init();
  return;
}


/* 080017f8 | soft_spi_gpio_init | 104 bytes */

void soft_spi_gpio_init(void)

{
  int iVar1;
  undefined4 in_r3;
  undefined4 local_8;
  
  local_8 = in_r3;
  RCC_APB2PeriphClockCmd(8,1);
  GPIO_SetBits(DAT_08001860,0x1000);
  local_8 = 0x10032000;
  GPIO_Init(DAT_08001860,&local_8);
  local_8._0_2_ = 0x8000;
  GPIO_Init(DAT_08001860,&local_8);
  local_8 = CONCAT22(local_8._2_2_,0x4000);
  local_8 = CONCAT13(4,(undefined3)local_8);
  GPIO_Init(DAT_08001860,&local_8);
  iVar1 = DAT_08001860;
  *(undefined4 *)(DAT_08001860 + 0x14) = 0x2000;
  *(undefined4 *)(iVar1 + 0x10) = 0x8000;
  return;
}


/* 08001864 | soft_spi_transfer | 94 bytes */

uint soft_spi_transfer(uint param_1)

{
  undefined4 *puVar1;
  uint uVar2;
  byte bVar3;
  
  uVar2 = 0;
  for (bVar3 = 0; puVar1 = DAT_080018c4, bVar3 < 8; bVar3 = bVar3 + 1) {
    *DAT_080018c4 = 0x2000;
    if ((param_1 & 0x80) == 0) {
      *DAT_080018c4 = 0x8000;
    }
    else {
      puVar1[-1] = 0x8000;
    }
    param_1 = (param_1 << 0x19) >> 0x18;
    uVar2 = (uVar2 << 0x19) >> 0x18;
    DAT_080018c4[-1] = 0x2000;
    if ((int)(DAT_080018c4[-3] << 0x11) < 0) {
      uVar2 = uVar2 + 1 & 0xff;
    }
    *DAT_080018c4 = 0x2000;
  }
  return uVar2;
}


/* 080018c8 | SVC_Handler | 2 bytes */

void SVC_Handler(void)

{
  return;
}


/* 080018cc | SysTick_Handler | 40 bytes */

void SysTick_Handler(void)

{
  byte bVar1;
  byte *pbVar2;
  
  *DAT_080018f4 = *DAT_080018f4 + 1;
  pbVar2 = DAT_080018f8;
  bVar1 = *DAT_080018f8;
  *DAT_080018f8 = *DAT_080018f8 + 1;
  if (9 < bVar1) {
    *pbVar2 = 0;
    update_controls();
  }
  return;
}


/* 080018fc | SystemInit | 14 bytes */

void SystemInit(void)

{
  *DAT_0800190c = *DAT_0800190c | 1;
  return;
}


/* 08001910 | nrf_tx_mode | 90 bytes */

void nrf_tx_mode(void)

{
  *DAT_0800196c = 0x800;
  nrf_write_buffer(0x30,DAT_08001970,5);
  nrf_write_buffer(0x2a,DAT_08001974,5);
  nrf_write_register(0x21,0);
  nrf_write_register(0x22,1);
  nrf_write_register(0x24,0x1a);
  nrf_write_register(0x25,1);
  nrf_write_register(0x26,7);
  nrf_write_register(0x20,0xe);
  DAT_0800196c[-1] = 0x800;
  return;
}


/* 08001978 | USART1_IRQHandler | 10 bytes */

undefined2 USART1_IRQHandler(void)

{
  return *DAT_08001984;
}


/* 08001988 | UsageFault_Handler | 4 bytes */

void UsageFault_Handler(void)

{
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 0800198c | board_init | 34 bytes */

void board_init(void)

{
  int iVar1;
  
  soft_i2c_gpio_init();
  nrf_init();
  do {
    iVar1 = eeprom_check();
  } while (iVar1 != 0);
  adc_gpio_init();
  adc_dma_init();
  load_trim_and_init_keys();
  return;
}


/* 080019b0 | timebase_init | 26 bytes */

void timebase_init(void)

{
  uint local_18 [6];
  
  RCC_GetClocksFreq(local_18);
  *DAT_080019d0 = local_18[0] / DAT_080019cc;
  return;
}


/* 080019d4 | delay_ms | 34 bytes */

void delay_ms(int param_1)

{
  int iVar1;
  int iVar2;
  
  iVar1 = micros();
  do {
    iVar2 = micros();
  } while ((uint)(iVar2 - iVar1) < (uint)(param_1 * 1000));
  return;
}


/* 080019f8 | scan_keys | 454 bytes */

void scan_keys(void)

{
  char cVar1;
  int iVar2;
  int iVar3;
  
  iVar2 = DAT_08001bdc;
  cVar1 = *DAT_08001bc0;
  if (cVar1 == '\0') {
    if (0x1e < (uint)(*DAT_08001bc4 - *DAT_08001bc8)) {
      if ((((*DAT_08001bcc & 0x80c0) == 0x80c0) && ((*DAT_08001bd0 & 0x803) == 0x803)) &&
         ((*(uint *)(DAT_08001bd4 + 8) & 0xc000) == 0xc000)) {
        *DAT_08001bc0 = '\x01';
      }
      DAT_08001bcc[3] = 0x1000;
    }
  }
  else if (cVar1 == '\x01') {
    if ((((*DAT_08001bcc & 0x80c0) != 0x80c0) || ((*DAT_08001bd0 & 0x803) != 0x803)) ||
       ((*(uint *)(DAT_08001bd4 + 8) & 0xc000) != 0xc000)) {
      *DAT_08001bc0 = '\x02';
    }
  }
  else if (cVar1 == '\x02') {
    if ((*DAT_08001bcc & 0x8000) == 0) {
      *(ushort *)(DAT_08001bd8 + 8) = *(ushort *)(DAT_08001bd8 + 8) ^ 0x438;
    }
    else if ((*DAT_08001bd0 & 0x800) == 0) {
      *(ushort *)(DAT_08001bd8 + 10) = *(ushort *)(DAT_08001bd8 + 10) ^ 0x438;
    }
    else if ((*(uint *)(DAT_08001bd4 + 8) & 0x4000) == 0) {
      *(ushort *)(DAT_08001bd8 + 0xc) = *(ushort *)(DAT_08001bd8 + 0xc) ^ 0x438;
    }
    else if ((*(uint *)(DAT_08001bd4 + 8) & 0x8000) == 0) {
      *(ushort *)(DAT_08001bd8 + 0xe) = *(ushort *)(DAT_08001bd8 + 0xe) ^ 0x438;
    }
    else if ((*DAT_08001bcc & 0x40) == 0) {
      iVar3 = (int)*(short *)(DAT_08001bdc + 2);
      if (iVar3 != -0xfa && iVar3 + 0xfa < 0 == SCARRY4(iVar3,0xfa)) {
        *(short *)(DAT_08001bdc + 2) = *(short *)(DAT_08001bdc + 2) + -5;
        eeprom_write_u16(0,*(undefined2 *)(iVar2 + 2));
      }
    }
    else if ((*DAT_08001bcc & 0x80) == 0) {
      if (*(short *)(DAT_08001bdc + 2) < 0xfa) {
        *(short *)(DAT_08001bdc + 2) = *(short *)(DAT_08001bdc + 2) + 5;
        eeprom_write_u16(0,*(undefined2 *)(iVar2 + 2));
      }
    }
    else if ((*DAT_08001bd0 & 1) == 0) {
      iVar3 = (int)*(short *)(DAT_08001bdc + 4);
      if (iVar3 != -0xfa && iVar3 + 0xfa < 0 == SCARRY4(iVar3,0xfa)) {
        *(short *)(DAT_08001bdc + 4) = *(short *)(DAT_08001bdc + 4) + -5;
        eeprom_write_u16(4,*(undefined2 *)(iVar2 + 4));
      }
    }
    else if (((*DAT_08001bd0 & 2) == 0) && (*(short *)(DAT_08001bdc + 4) < 0xfa)) {
      *(short *)(DAT_08001bdc + 4) = *(short *)(DAT_08001bdc + 4) + 5;
      eeprom_write_u16(4,*(undefined2 *)(iVar2 + 4));
    }
    *DAT_08001bc0 = '\0';
    DAT_08001bcc[2] = 0x1000;
    *DAT_08001bc8 = *DAT_08001bc4;
  }
  return;
}


/* 08001be0 | keys_gpio_init | 80 bytes */

void keys_gpio_init(void)

{
  undefined4 in_r3;
  undefined4 local_8;
  
  *(undefined4 *)(DAT_08001c30 + 4) = 0x2000000;
  local_8 = in_r3;
  RCC_APB2PeriphClockCmd(0x1c,1);
  local_8 = 0x480380d0;
  GPIO_Init(DAT_08001c34,&local_8);
  local_8._0_2_ = 0x803;
  GPIO_Init(DAT_08001c38,&local_8);
  local_8 = CONCAT22(local_8._2_2_,0xc000);
  GPIO_Init(DAT_08001c3c,&local_8);
  return;
}


/* 08001c40 | main | 116 bytes */

/* WARNING: Removing unreachable block (ram,0x08001c90) */
/* WARNING: Removing unreachable block (ram,0x08001c6a) */

void main(void)

{
  bool bVar1;
  int iVar2;
  
  clock_init();
  timebase_init();
  status_gpio_init();
  iVar2 = DAT_08001cb4;
  do {
    bVar1 = 0 < iVar2;
    iVar2 = iVar2 + -1;
  } while (bVar1);
  *DAT_08001cb8 = 0x1000;
  SYSTICK_LOAD = 47999;
  *(undefined *)(DAT_08001cbc + 0xb) = 0xf0;
  SYSTICK_VAL = 0;
  SYSTICK_CTRL = 7;
  board_init();
  interrupts_init();
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


/* 08001cc4 | eeprom_read_u16 | 22 bytes */

uint eeprom_read_u16(undefined4 param_1,undefined4 param_2,undefined4 param_3,uint param_4)

{
  uint local_10;
  
  local_10 = param_4;
  i2c_read_buffer(0xa0,param_1,&local_10,2);
  return local_10 & 0xffff;
}


/* 08001cda | eeprom_check | 44 bytes */

bool eeprom_check(void)

{
  char cVar1;
  
  i2c_write_register(0xa0,0x55,0xaa);
  delay_ms(1000);
  cVar1 = i2c_read_register(0xa0,0x55);
  return cVar1 != -0x56;
}


/* 08001d06 | eeprom_write_u16 | 24 bytes */

void eeprom_write_u16(undefined4 param_1,undefined4 param_2)

{
  undefined4 uVar1;
  undefined4 uStack_c;
  
  uVar1 = param_1;
  uStack_c = param_2;
  delay_ms(10);
  i2c_write_buffer(0xa0,param_1,&uStack_c,2,uVar1);
  return;
}


/* 08001d20 | soft_float_add | 412 bytes */

/* WARNING: Control flow encountered bad instruction data */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

uint soft_float_add(uint arg0,uint arg1)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  uint extraout_r1;
  int iVar4;
  uint uVar5;
  uint uVar6;
  uint uVar7;
  code *UNRECOVERED_JUMPTABLE;
  bool bVar8;
  bool bVar9;
  
  if ((int)(arg0 ^ arg1) < 0) {
    uVar2 = arg1 ^ 0x80000000;
    if (arg0 < uVar2 || arg0 - uVar2 == 0) {
      uVar5 = arg0 - uVar2 ^ 0x80000000;
      arg0 = arg0 - uVar5;
      uVar2 = uVar2 + uVar5;
    }
    uVar5 = arg0 >> 0x17;
    bVar8 = (uVar2 & 0x7f800000) == 0;
    uVar1 = uVar5 - (uVar2 >> 0x17);
    if (!bVar8) {
      bVar8 = (uVar5 & 0xff) == 0xff;
    }
    if (bVar8) {
      if ((uVar5 & 0xff) != 0xff) {
        if ((arg0 & 0x7f800000) == 0) {
          arg0 = 0;
        }
        return arg0;
      }
      UNRECOVERED_JUMPTABLE = (code *)0x80020c4;
      FUN_08001f4e();
      software_interrupt(0x40);
                    /* WARNING: Could not recover jumptable at 0x080020cc. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      uVar2 = (*UNRECOVERED_JUMPTABLE)(extraout_r1 ^ 0x80000000);
      return uVar2;
    }
    uVar2 = uVar2 << 8 | 0x80000000;
    uVar3 = (arg0 << 8 | 0x80000000) - (uVar2 >> (uVar1 & 0xff));
    if (-1 < (int)uVar3) {
      if ((uVar5 & 0xfe) == 0) {
        if (uVar3 >> 7 == 0) {
          return 0;
        }
      }
      else {
        uVar6 = uVar3 * 2;
        if ((int)uVar6 < 0) {
          bVar8 = ((int)uVar6 >> 7 & 1U) != 0;
          uVar5 = ((int)uVar6 >> 8) + (arg0 & 0xff800000) + (uint)bVar8;
          if (!bVar8) {
            return uVar5;
          }
          if ((uVar3 & 0x3f) != 0) {
            return uVar5;
          }
          goto LAB_08002066;
        }
        uVar6 = uVar6 >> 2;
        if (uVar6 == 0) {
          return 0;
        }
        uVar1 = uVar5 - LZCOUNT(uVar6);
        uVar2 = 0x28U - LZCOUNT(uVar6) & 0x1f;
        if ((uVar5 ^ uVar1) >> 8 == 0) {
          return (uVar6 >> uVar2 | uVar6 << 0x20 - uVar2) + uVar1 * 0x800000;
        }
      }
      return arg0 & 0x80000000;
    }
    bVar8 = (uVar3 >> 7 & 1) != 0;
    uVar5 = (uVar3 >> 8) + (uVar5 - 1) * 0x800000 + (uint)bVar8;
    if (!bVar8) {
      return uVar5;
    }
    if ((uVar3 & 0x7f) != 0) {
      return uVar5;
    }
LAB_08002066:
    if (uVar2 << (0x20 - uVar1 & 0xff) == 0) {
      uVar5 = uVar5 & 0xfffffffe;
    }
    else {
      uVar5 = uVar5 - 1;
    }
    return uVar5;
  }
  iVar4 = arg0 - arg1;
  if (arg0 < arg1) {
    arg0 = arg0 - iVar4;
    arg1 = arg1 + iVar4;
  }
  uVar2 = arg0 >> 0x17;
  bVar8 = (arg1 & 0x7f800000) == 0;
  uVar5 = uVar2 - (arg1 >> 0x17);
  if (!bVar8) {
    bVar8 = (uVar2 & 0xff) == 0xff;
  }
  if (bVar8) {
    if ((uVar2 & 0xff) != 0xff) {
      if ((arg0 & 0x7f800000) == 0) {
        arg0 = arg0 & 0x80000000;
      }
      return arg0;
    }
    FUN_08001f4e();
                    /* WARNING: Bad instruction - Truncating control flow here */
    halt_baddata();
  }
  uVar1 = arg0 << 8 | 0x80000000;
  uVar3 = arg1 << 8 | 0x80000000;
  uVar6 = uVar3 >> (uVar5 & 0xff);
  bVar8 = CARRY4(uVar1,uVar6);
  uVar1 = uVar1 + uVar6;
  if (bVar8 == false) {
    bVar8 = (uVar1 >> 7 & 1) != 0;
    uVar6 = (uVar1 >> 8) + (uVar2 - 1) * 0x800000 + (uint)bVar8;
    if (!bVar8) {
      return uVar6;
    }
    uVar7 = uVar6 * 2;
    if ((uVar1 & 0x7f) != 0) {
      bVar8 = 0xfeffffff < uVar7;
    }
    if (!bVar8) {
      return uVar6;
    }
  }
  else {
    bVar9 = (uVar1 >> 1 & 0x80) != 0;
    uVar2 = ((uint)bVar8 << 0x1f | uVar1 >> 1) >> 8;
    uVar6 = uVar2 + (arg0 & 0xff800000) + (uint)bVar9;
    if (bVar9) {
      uVar2 = uVar1 & 0xff;
    }
    uVar7 = uVar6 * 2;
    if (uVar2 != 0) {
      if (uVar2 != 0) {
        bVar9 = 0xfeffffff < uVar7;
      }
      if (!bVar9) {
        return uVar6;
      }
      goto LAB_08001d9c;
    }
  }
  if (uVar3 << (0x20 - uVar5 & 0xff) == 0) {
    uVar6 = uVar6 & 0xfffffffe;
  }
  if (uVar7 < 0xff000000) {
    return uVar6;
  }
LAB_08001d9c:
  return (uVar6 + 0xa0000000 >> 0x17 | 0xff) << 0x17;
}


/* 08001de4 | soft_float_to_uint | 56 bytes */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

uint soft_float_to_uint(uint arg0)

{
  uint uVar1;
  undefined2 *unaff_r4;
  code *UNRECOVERED_JUMPTABLE;
  uint uStack0000011c;
  
  uVar1 = 0x9e - (arg0 >> 0x17);
  if (arg0 >> 0x17 < 0x9f) {
    return (arg0 << 8 | 0x80000000) >> (uVar1 & 0xff);
  }
  if (((int)arg0 < 0) && (arg0 << 1 < 0x7f000000)) {
    return 0;
  }
  UNRECOVERED_JUMPTABLE = (code *)0x8001e14;
  FUN_08001f4e();
  *unaff_r4 = (short)unaff_r4;
  uStack0000011c = uVar1;
                    /* WARNING: Could not recover jumptable at 0x08001e1a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  uVar1 = (*UNRECOVERED_JUMPTABLE)(0);
  return uVar1;
}


/* 08001e24 | soft_uint_to_float | 38 bytes */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

uint soft_uint_to_float(uint arg0)

{
  uint uVar1;
  uint uVar2;
  
  uVar2 = arg0 << LZCOUNT(arg0);
  if (uVar2 != 0) {
    uVar1 = (0x9d - LZCOUNT(arg0)) * 0x800000 + (uVar2 >> 8);
    if ((uVar2 & 0x80) == 0) {
      return uVar1;
    }
    arg0 = uVar1 + 1;
    if ((uVar2 & 0x7f) == 0) {
      arg0 = arg0 & 0xfffffffe;
    }
  }
  return arg0;
}


/* 08001e4c | soft_float_multiply | 234 bytes */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

uint soft_float_multiply(uint arg0,uint arg1)

{
  longlong lVar1;
  uint uVar2;
  int iVar3;
  uint in_r3;
  uint uVar4;
  uint uVar5;
  bool bVar6;
  undefined8 uVar7;
  
  uVar2 = arg0 >> 7 & 0xff0000;
  bVar6 = uVar2 == 0;
  if (!bVar6) {
    in_r3 = arg1 >> 7 & 0xff0000;
    bVar6 = in_r3 == 0;
  }
  if (!bVar6) {
    bVar6 = uVar2 == 0xff0000;
  }
  if (!bVar6) {
    bVar6 = in_r3 == 0xff0000;
  }
  if (bVar6) {
    if ((int)(arg0 ^ arg1) < 0) {
      uVar2 = uVar2 | 0x100;
    }
    if (uVar2 < 0xff0000 && (arg1 >> 7 & 0xff0000) < 0xff0000) {
      return (arg0 ^ arg1) & 0x80000000;
    }
    uVar7 = FUN_08001f4e();
    uVar2 = (uint)uVar7 ^ (int)((ulonglong)uVar7 >> 0x20) << 2;
  }
  else {
    if ((int)(arg0 ^ arg1) < 0) {
      uVar2 = uVar2 | 0x100;
    }
    lVar1 = (ulonglong)(arg0 << 8 | 0x80000000) * (ulonglong)(arg1 << 8 | 0x80000000);
    uVar4 = (uint)((ulonglong)lVar1 >> 0x20);
    iVar3 = uVar2 + in_r3 + -0x800000;
    if ((int)lVar1 != 0) {
      uVar4 = uVar4 | 1;
    }
    bVar6 = (uVar4 & 0x80000000) != 0;
    uVar5 = uVar4 << 1;
    if (bVar6) {
      uVar5 = (uint)bVar6 << 0x1f | uVar4 & 0x7fffffff;
    }
    uVar4 = iVar3 + (iVar3 >> 0x10) + (uint)bVar6;
    bVar6 = (uVar5 >> 7 & 1) != 0;
    uVar2 = (uVar5 >> 8) + uVar4 * 0x800000 + (uint)bVar6;
    if ((!bVar6 || uVar5 >> 8 < uVar5 << 0x19) && uVar4 < 0xfc0000) {
      return uVar2;
    }
    if ((uVar5 & 0xff) == 0x80) {
      uVar2 = uVar2 & 0xfffffffe;
    }
    if (uVar4 < 0xfc0000) {
      return uVar2;
    }
    if ((int)(uVar4 - 0xfc0000) < 0) {
      uVar4 = uVar4 ^ 0xff;
      bVar6 = (int)(uVar4 + 0x10000) < 0;
      if (uVar4 != 0xffff0000 && bVar6 == SCARRY4(uVar4,0x10000)) {
        return uVar2;
      }
      uVar2 = uVar2 + 0x60000000;
      if (uVar4 == 0xffff0000 || bVar6 != SCARRY4(uVar4,0x10000)) {
        uVar2 = uVar2 & 0x80000000;
      }
      return uVar2;
    }
    if ((int)((uVar2 + 0x800000) * 2) < 0) {
      return uVar2;
    }
    uVar2 = uVar2 + 0xa0000000;
  }
  return (uVar2 >> 0x17 | 0xff) << 0x17;
}


/* 08001f4e | FUN_08001f4e | 136 bytes */

uint FUN_08001f4e(uint param_1,uint param_2,uint param_3)

{
  uint uVar1;
  uint uVar2;
  int in_lr;
  uint *puVar3;
  bool bVar4;
  
  puVar3 = (uint *)(in_lr + 2U & 0xfffffffc);
  uVar1 = *puVar3;
  if ((((int)uVar1 < 0) || (param_3 = param_2 * 2, param_3 < 0xff000001)) &&
     (param_1 * 2 < 0xff000001)) {
    if (param_1 * 2 == 0xff000000) {
      uVar2 = ((int)param_1 >> 0x1f) * -3 + 2;
      if (param_3 == 0xff000000) {
        uVar2 = uVar2 + ((uint)(0xfeffffff < param_3) - ((int)param_2 >> 0x1f));
      }
    }
    else {
      uVar2 = param_2 >> 0x1f;
    }
  }
  else {
    uVar2 = 8;
  }
  uVar1 = uVar1 >> (uVar2 * 3 & 0xff) & 7;
  switch(uVar1) {
  case 4:
    param_1 = param_2;
switchD_08001fb6_caseD_5:
    bVar4 = (param_1 & 0x80000000) != 0;
    uVar1 = param_1 * 2;
    if (uVar1 != 0) {
      bVar4 = uVar1 < 0x1000001;
    }
    if (bVar4 && (uVar1 != 0 && uVar1 != 0x1000000)) {
      param_1 = param_1 & 0x80000000;
    }
    return param_1;
  case 5:
    goto switchD_08001fb6_caseD_5;
  case 6:
  case 7:
    return 0x7fc00000;
  default:
                    /* WARNING: Could not recover jumptable at 0x08001fb4. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    uVar1 = (*(code *)(puVar3 + uVar1 + 1))();
    return uVar1;
  }
}

