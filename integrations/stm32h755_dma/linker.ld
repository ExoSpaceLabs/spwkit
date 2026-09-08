ENTRY(Reset_Handler)

MEMORY
{
    FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    AXIRAM  (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    D2SRAM1 (rwx) : ORIGIN = 0x30000000, LENGTH = 128K
}

__StackTop = ORIGIN(AXIRAM) + LENGTH(AXIRAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(256);
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    .data :
    {
        . = ALIGN(8);
        __data_start__ = .;
        *(.data*)
        __data_end__ = .;
    } > AXIRAM AT > FLASH
    __data_load__ = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > AXIRAM

    /* DMA1/DMA2 cannot access ITCM/DTCM. Keep the actual transfer storage in
     * D2 SRAM and align it to the Cortex-M7 L1 data-cache line size. */
    .dma_buffer (NOLOAD) :
    {
        . = ALIGN(32);
        __dma_buffer_start__ = .;
        KEEP(*(.dma_buffer*))
        . = ALIGN(32);
        __dma_buffer_end__ = .;
    } > D2SRAM1

    . = ALIGN(8);
    __end__ = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
