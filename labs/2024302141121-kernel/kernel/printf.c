#include <stdarg.h>

#include "types.h"

extern void console_putc(int c);

static int
emit_char(int c)
{
  console_putc(c);
  return 1;
}

static int
print_unsigned(uint64 value, uint base)
{
  static const char digits[] = "0123456789abcdef";
  char buffer[32];
  int count = 0;
  int used = 0;

  do {
    buffer[used++] = digits[value % base];
    value /= base;
  } while (value != 0);

  while (used > 0)
    count += emit_char(buffer[--used]);
  return count;
}

static int
print_signed(long value)
{
  uint64 magnitude;
  int count = 0;

  if (value < 0) {
    count += emit_char('-');
    /* Unsigned subtraction also handles LONG_MIN without signed overflow. */
    magnitude = 0UL - (uint64)value;
  } else {
    magnitude = (uint64)value;
  }

  return count + print_unsigned(magnitude, 10);
}

int
printf(const char *format, ...)
{
  va_list arguments;
  int count = 0;

  if (format == 0)
    return 0;

  va_start(arguments, format);
  while (*format != '\0') {
    int long_value = 0;
    char specifier;

    if (*format != '%') {
      count += emit_char(*format++);
      continue;
    }

    format++;
    if (*format == 'l') {
      long_value = 1;
      format++;
    }

    specifier = *format;
    if (specifier == '\0') {
      count += emit_char('%');
      break;
    }
    format++;

    switch (specifier) {
    case 'd':
      count += long_value ? print_signed(va_arg(arguments, long))
                          : print_signed((long)va_arg(arguments, int));
      break;
    case 'u':
      count += long_value
                 ? print_unsigned((uint64)va_arg(arguments, unsigned long), 10)
                 : print_unsigned((uint64)va_arg(arguments, unsigned int), 10);
      break;
    case 'x':
      count += emit_char('0');
      count += emit_char('x');
      count += long_value
                 ? print_unsigned((uint64)va_arg(arguments, unsigned long), 16)
                 : print_unsigned((uint64)va_arg(arguments, unsigned int), 16);
      break;
    case 'p':
      count += emit_char('0');
      count += emit_char('x');
      count += print_unsigned((uint64)va_arg(arguments, void *), 16);
      break;
    case 's': {
      const char *text = va_arg(arguments, const char *);
      if (text == 0)
        text = "(null)";
      while (*text != '\0')
        count += emit_char(*text++);
      break;
    }
    case 'c':
      count += emit_char(va_arg(arguments, int));
      break;
    case '%':
      count += emit_char('%');
      break;
    default:
      count += emit_char('%');
      if (long_value)
        count += emit_char('l');
      count += emit_char(specifier);
      break;
    }
  }
  va_end(arguments);
  return count;
}
