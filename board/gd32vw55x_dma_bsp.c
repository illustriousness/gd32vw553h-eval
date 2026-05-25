/*
 * GD MSDK UART/log code references the chip DMA driver, but the external
 * gd32-riscv-series package does not compile it by default. Build it from the
 * BSP so the package SConscript stays unmodified.
 */
#include "../packages/gd32-riscv-series-latest/GD32VW55x/GD32VW55x_standard_peripheral/Source/gd32vw55x_dma.c"
