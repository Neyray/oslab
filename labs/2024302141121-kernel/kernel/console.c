#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

#define UART_THR 0
#define UART_IER 1
#define UART_LSR 5
#define UART_LSR_THRE (1U << 5)

#define THROTTLE_PERIOD (16UL + (COURSE_SID % 16UL))
#define THROTTLE_NOP_COUNT (32UL + (COURSE_SID % 32UL))

#if LAB1_BANNER_PROTOCOL < 0 || LAB1_BANNER_PROTOCOL > 2
#error "LAB1_BANNER_PROTOCOL must be 0, 1, or 2"
#endif

static uint64 physical_bytes;
static uint64 checksum;
static int checksum_enabled;

static volatile uint8 *
uart_reg(uint64 offset)
{
  return (volatile uint8 *)(UART0 + offset);
}

static void
throttle_after_byte(void)
{
  uint64 i;

  physical_bytes++;
  if (physical_bytes != THROTTLE_PERIOD)
    return;

  for (i = 0; i < THROTTLE_NOP_COUNT; i++)
    asm volatile("nop");
  physical_bytes = 0;
}

/* Poll 16550 LSR.THRE, then place exactly one physical byte in THR. */
static void
uartputc_sync(uint8 byte)
{
  for (;;) {
    io_fence();
    if ((*uart_reg(UART_LSR) & UART_LSR_THRE) != 0)
      break;
  }

  *uart_reg(UART_THR) = byte;
  io_fence();
  throttle_after_byte();
}

void
console_init(void)
{
  physical_bytes = 0;
  checksum = 0;
  checksum_enabled = 0;

  /* lab1 is polling-only; keep the 16550 interrupt sources disabled. */
  *uart_reg(UART_IER) = 0;
  io_fence();
}

/* Emit one logical byte under the per-student line protocol. */
void
console_putc(int c)
{
  uint8 byte = (uint8)c;

  if (checksum_enabled)
    checksum += byte;

  uartputc_sync(byte);
#if LAB1_BANNER_PROTOCOL == 1
  uartputc_sync('.');
#endif
}

void
console_checksum_reset(void)
{
  checksum = 0;
  checksum_enabled = 1;
}

uint64
console_checksum_value(void)
{
  return checksum;
}

void
console_checksum_pause(void)
{
  checksum_enabled = 0;
}
