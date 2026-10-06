# Ghidra 11.0.3 headless pre-analysis script. Run with ARM:LE:32:Cortex.
from java.math import BigInteger
from ghidra.program.model.symbol import SourceType
from ghidra.program.model.data import ByteDataType, WordDataType, DWordDataType, ArrayDataType
import re

root = getScriptArgs()[0]
mem = currentProgram.getMemory()
flash = mem.getBlock(toAddr(0x08000000))
flash.setName("FLASH")
flash.setWrite(False)
flash.setExecute(True)
ram = mem.createInitializedBlock("SRAM", toAddr(0x20000000), 0x5000, 0, monitor, False)
ram.setRead(True)
ram.setWrite(True)
ram.setExecute(False)
mem.setBytes(toAddr(0x20000000), getBytes(toAddr(0x080020f8), 0x68))
for name, start, size in [("PERIPHERALS", 0x40000000, 0x30000),
                           ("RCC_BITBAND", 0x42420000, 0x100),
                           ("CORTEX_SYSTEM", 0xe000e000, 0x1000)]:
    block = mem.createUninitializedBlock(name, toAddr(start), size, False)
    block.setRead(True)
    block.setWrite(True)
    block.setExecute(False)
    block.setVolatile(True)

createData(toAddr(0x08000000), ArrayDataType(DWordDataType.dataType, 59, 4))
createLabel(toAddr(0x08000000), "interrupt_vectors", True)
createData(toAddr(0x080020ce), ArrayDataType(ByteDataType.dataType, 10, 1))
createData(toAddr(0x080020f8), ArrayDataType(ByteDataType.dataType, 0x68, 1))
createLabel(toAddr(0x080020ce), "nrf_tx_address", True)
createLabel(toAddr(0x080020d3), "nrf_rx_address", True)

globals_spec = [
    (0x20000000, "cycles_per_us", DWordDataType.dataType, 1),
    (0x20000004, "tick_ms", DWordDataType.dataType, 1),
    (0x20000008, "control_tick_divider", ByteDataType.dataType, 1),
    (0x20000009, "ahb_apb_prescaler_table", ByteDataType.dataType, 16),
    (0x20000019, "adc_prescaler_table", ByteDataType.dataType, 4),
    (0x2000001e, "adc_samples", WordDataType.dataType, 4),
    (0x20000028, "rc_channels", WordDataType.dataType, 8),
    (0x20000038, "tx_packet", ByteDataType.dataType, 32),
    (0x2000005a, "previous_ch0", WordDataType.dataType, 1),
    (0x2000005c, "previous_ch1", WordDataType.dataType, 1),
    (0x2000005e, "previous_ch3", WordDataType.dataType, 1),
    (0x20000060, "key_state", ByteDataType.dataType, 1),
    (0x20000064, "last_key_tick", DWordDataType.dataType, 1),
    (0x20000068, "trim_offsets", WordDataType.dataType, 5),
]
for start, name, dt, count in globals_spec:
    createData(toAddr(start), dt if count == 1 else ArrayDataType(dt, count, dt.getLength()))
    createLabel(toAddr(start), name, True)

reg_labels = {
    0x40010800:"GPIOA_CRL", 0x40010804:"GPIOA_CRH", 0x40010808:"GPIOA_IDR",
    0x4001080c:"GPIOA_ODR", 0x40010810:"GPIOA_BSRR", 0x40010814:"GPIOA_BRR",
    0x40010c00:"GPIOB_CRL", 0x40010c04:"GPIOB_CRH", 0x40010c08:"GPIOB_IDR",
    0x40010c0c:"GPIOB_ODR", 0x40010c10:"GPIOB_BSRR", 0x40010c14:"GPIOB_BRR",
    0x40011000:"GPIOC_CRL", 0x40011004:"GPIOC_CRH", 0x40011008:"GPIOC_IDR",
    0x40011010:"GPIOC_BSRR", 0x40011014:"GPIOC_BRR",
    0x40010004:"AFIO_MAPR", 0x40020000:"DMA1_ISR", 0x40020004:"DMA1_IFCR",
    0x40020008:"DMA1_CHANNEL1_CCR", 0x4002000c:"DMA1_CHANNEL1_CNDTR",
    0x40020010:"DMA1_CHANNEL1_CPAR", 0x40020014:"DMA1_CHANNEL1_CMAR",
    0x40021000:"RCC_CR", 0x40021004:"RCC_CFGR", 0x40021008:"RCC_CIR",
    0x40021014:"RCC_AHBENR", 0x40021018:"RCC_APB2ENR", 0x4002101c:"RCC_APB1ENR",
    0x40012400:"ADC1_SR", 0x40012404:"ADC1_CR1", 0x40012408:"ADC1_CR2",
    0x4001240c:"ADC1_SMPR1", 0x40012410:"ADC1_SMPR2", 0x4001242c:"ADC1_SQR1",
    0x40012430:"ADC1_SQR2", 0x40012434:"ADC1_SQR3", 0x4001244c:"ADC1_DR",
    0x40013800:"USART1_SR", 0x40022000:"FLASH_ACR",
    0x42420000:"RCC_HSEON_BITBAND", 0x42420060:"RCC_PLLON_BITBAND",
    0xe000e010:"SYSTICK_CTRL", 0xe000e014:"SYSTICK_LOAD", 0xe000e018:"SYSTICK_VAL",
    0xe000ed08:"SCB_VTOR", 0xe000ed0c:"SCB_AIRCR", 0xe000ed23:"SCB_SYSTICK_PRIORITY",
}
for start, name in reg_labels.items():
    createData(toAddr(start), ByteDataType.dataType if start == 0xe000ed23 else DWordDataType.dataType)
    createLabel(toAddr(start), name, True)

names = {
    0x08000188:"Reset_Handler", 0x080001a2:"Default_Handler", 0x08000128:"scatter_copy",
    0x08000144:"scatter_zero", 0x080018fc:"SystemInit",
    0x0800023c:"adc_gpio_init", 0x0800026c:"adc_dma_init", 0x080004fc:"status_gpio_init",
    0x08000710:"GPIO_Init", 0x08000826:"GPIO_SetBits", 0x0800082c:"micros",
    0x08000874:"soft_i2c_ack", 0x08000940:"soft_i2c_nack", 0x08000a0c:"soft_i2c_read_byte",
    0x08000acc:"soft_i2c_write_byte", 0x08000b8c:"soft_i2c_start", 0x08000c20:"soft_i2c_stop",
    0x08000cbc:"soft_i2c_wait_ack", 0x08000d70:"soft_i2c_gpio_init",
    0x08000dac:"i2c_read_register", 0x08000e10:"i2c_write_buffer",
    0x08000e70:"i2c_write_register", 0x08000eb4:"i2c_read_buffer",
    0x08000f3c:"nrf_check", 0x08000f88:"nrf_gpio_init", 0x08001028:"nrf_read_buffer",
    0x0800106c:"nrf_read_register", 0x08001098:"nrf_transmit_32",
    0x080010fc:"nrf_write_buffer", 0x08001140:"nrf_write_register",
    0x08001170:"nrf_init", 0x08001188:"build_and_send_packet", 0x080011fc:"interrupts_init",
    0x08001244:"NVIC_Init", 0x080012b4:"NVIC_PriorityGroupConfig", 0x080012cc:"RCC_ADCCLKConfig",
    0x080012e4:"RCC_AHBPeriphClockCmd", 0x08001304:"RCC_APB1PeriphClockCmd",
    0x08001324:"RCC_APB2PeriphClockCmd", 0x08001344:"clock_init",
    0x080013c0:"RCC_DeInit", 0x0800140c:"RCC_GetClocksFreq", 0x080015c0:"update_controls",
    0x080017d4:"load_trim_and_init_keys", 0x080017f8:"soft_spi_gpio_init",
    0x08001864:"soft_spi_transfer", 0x080018cc:"SysTick_Handler",
    0x08001910:"nrf_tx_mode", 0x08001978:"USART1_IRQHandler",
    0x0800198c:"board_init", 0x080019b0:"timebase_init", 0x080019d4:"delay_ms",
    0x080019f8:"scan_keys", 0x08001be0:"keys_gpio_init", 0x08001c40:"main",
    0x08001cc4:"eeprom_read_u16", 0x08001cda:"eeprom_check", 0x08001d06:"eeprom_write_u16",
    0x08001d20:"soft_float_add", 0x08001de4:"soft_float_to_uint", 0x08001e24:"soft_uint_to_float",
    0x08001e4c:"soft_float_multiply",
    0x08000f38:"NMI_Handler", 0x08000870:"HardFault_Handler", 0x08000f34:"MemManage_Handler",
    0x08000534:"BusFault_Handler", 0x08001988:"UsageFault_Handler", 0x080018c8:"SVC_Handler",
    0x080006d8:"DebugMon_Handler", 0x080012c8:"PendSV_Handler",
}
text = open(root + "/firmware_disassembly.txt").read()
seeds = set(int(x, 16) for x in re.findall(r'\tbl\s+0x([0-9a-f]+)', text))
seeds.update(names.keys())
seeds = sorted(x for x in seeds if 0x080000ec <= x < 0x080020ce)
currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"),
    toAddr(0x080000ec), toAddr(0x080020cd), BigInteger.ONE)
for start in seeds:
    a = toAddr(start)
    try:
        addEntryPoint(a)
        disassemble(a)
        f = getFunctionAt(a)
        if f is None:
            f = createFunction(a, names.get(start, "FUN_%08x" % start))
        if f is not None and start in names:
            f.setName(names[start], SourceType.USER_DEFINED)
    except Exception as ex:
        println("Seed %08x: %s" % (start, ex))
println("Prepared Cortex-M3 memory map, vectors and %d function seeds" % len(seeds))
