ENTRY(Reset_Handler)
MEMORY {
 VECTOR (rx) : ORIGIN = 0x08000000, LENGTH = 1K
 FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 96K
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
_estack = ORIGIN(RAM)+LENGTH(RAM);
SECTIONS {
 .isr_vector : { KEEP(*(.isr_vector)) } > VECTOR
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
 .ARM.extab : { *(.ARM.extab*) } > FLASH
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
 .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
 _sidata = LOADADDR(.data);
 .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
 /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(_ebss < _estack-4096,"Insufficient stack")
/* Official calibration page 0x08007C00 is deliberately outside all load sections. */
