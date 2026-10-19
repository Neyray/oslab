# lab1 骨架 Makefile(课程提供;新增源文件时把它加进 OBJS)
TOOLPREFIX ?= $(shell if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then echo riscv64-unknown-elf-; else echo riscv64-linux-gnu-; fi)
CC   = $(TOOLPREFIX)gcc
LD   = $(TOOLPREFIX)ld
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie
QEMU = qemu-system-riscv64

OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o \
       kernel/kalloc.o kernel/vm.o kernel/mmtest.o kernel/proc.o kernel/syscall.o kernel/trap.o kernel/kernelvec.o \
       kernel/timervec.o kernel/trampoline.o kernel/userimg.o kernel/main.o

UPROGS = sh hi spin badecall bufstorm readcap readempty syserr badaccess memcap vmtest guardtest schedtest oomtest concurrent
UPROGS += bench lottery_test stride_test

UCOMMON = $(if $(wildcard user/common.c),user-flat/common.o)

OBJCOPY = $(TOOLPREFIX)objcopy

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -I.

kernel/kernel: $(OBJS) kernel/kernel.ld fs.img
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# The v2 trampoline has alloc/execute flags and 4KiB alignment. Keep its
# section and R_RISCV_ALIGN relocations intact; the linker places the orphan.
kernel/trampoline.o: kernel/trampoline.S kernel/riscv.h kernel/memlayout.h Makefile
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

user-flat/user.ld: user/user.ld tools/embed.py
	mkdir -p user-flat
	python3 tools/embed.py linker

user-flat/ulib.o: user/ulib.c $(wildcard kernel/*.h) user/user.h
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/printf.o: user/printf.c user/user.h
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/usys.o: user/usys.S kernel/syscall.h
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/common.o: user/common.c user/common.h user/user.h
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/%.bin: user/%.c user/user.h $(wildcard kernel/*.h) user-flat/user.ld user-flat/ulib.o user-flat/printf.o user-flat/usys.o $(UCOMMON)
	$(CC) $(UFLAGS) -Iuser -c $< -o user-flat/$*.o
	$(LD) -T user-flat/user.ld -o user-flat/$*.elf user-flat/$*.o user-flat/ulib.o user-flat/printf.o user-flat/usys.o $(UCOMMON)
	$(OBJCOPY) -O binary user-flat/$*.elf $@

kernel/userimg.S: Makefile tools/embed.py $(addprefix user-flat/,$(UPROGS:=.bin))
	python3 tools/embed.py table $(UPROGS)

.SECONDARY: user-flat/ulib.o user-flat/printf.o user-flat/usys.o

fs.img:
	truncate -s 1M $@

clean-extra:
	rm -rf user-flat kernel/userimg.S user/usys.S fs.img

clean: clean-extra

# 验收环境固定(排雷环节禁止改动本行以下内容)
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -f kernel/*.o kernel/kernel
