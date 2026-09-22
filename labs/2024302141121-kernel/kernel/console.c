#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

#define UART_THR 0
#define UART_IER 1
#define UART_LSR 5
#define UART_LSR_DR (1U << 0)
#define UART_LSR_THRE (1U << 5)

#define THROTTLE_PERIOD (16UL + (COURSE_SID % 16UL))
#define THROTTLE_NOP_COUNT (32UL + (COURSE_SID % 32UL))

#if LAB1_BANNER_PROTOCOL < 0 || LAB1_BANNER_PROTOCOL > 2
#error "LAB1_BANNER_PROTOCOL must be 0, 1, or 2"
#endif

static uint64 physical_bytes;
static uint64 checksum;
static int checksum_enabled;

void console_putc(int c);

static struct {
  uint8 data[LAB2_BUF_SIZE];
  uint64 read_index;
  uint64 write_index;
  uint64 dropped;
} input;

#if LAB2_BUF_SIZE <= 0
#error "LAB2_BUF_SIZE must be positive"
#endif

#if LAB2_BUF_SEMANTICS < 0 || LAB2_BUF_SEMANTICS > 1
#error "LAB2_BUF_SEMANTICS must be 0 or 1"
#endif

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
  input.read_index = 0;
  input.write_index = 0;
  input.dropped = 0;

  /* lab1 is polling-only; keep the 16550 interrupt sources disabled. */
  *uart_reg(UART_IER) = 0;
  io_fence();
}

void
console_enable_interrupts(void)
{
  *uart_reg(UART_IER) = 1;
  io_fence();
}

void
console_intr(void)
{
  for (;;) {
    uint8 byte;

    io_fence();
    if ((*uart_reg(UART_LSR) & UART_LSR_DR) == 0)
      break;
    byte = *uart_reg(UART_THR);
    if (byte == '\r')
      byte = '\n';

    if (input.write_index - input.read_index < LAB2_BUF_SIZE) {
      input.data[input.write_index++ % LAB2_BUF_SIZE] = byte;
    } else {
      input.dropped++;
      /* Preserve a record boundary so an overlong line cannot deadlock read. */
      if (byte == '\n')
        input.data[(input.write_index - 1) % LAB2_BUF_SIZE] = byte;
    }

    /* Echo is part of the console contract even if the ring is full. */
    console_putc(byte);
  }
}

int
console_read(void *destination, int length)
{
  uint8 *output = destination;
  int count = 0;
  int interrupts_were_enabled;

  if (length <= 0)
    return 0;

  interrupts_were_enabled = intr_get();
  intr_off();
  while (count < length) {
    uint8 byte;

    while (input.read_index == input.write_index) {
      intr_on();
      asm volatile("wfi");
      intr_off();
    }

    byte = input.data[input.read_index++ % LAB2_BUF_SIZE];
    output[count++] = byte;

    if (byte == '\n' || byte == '\r')
      break;
#if LAB2_BUF_SEMANTICS == 1
    if (length == 1)
      break;
#endif
  }

  if (interrupts_were_enabled)
    intr_on();
  return count;
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
