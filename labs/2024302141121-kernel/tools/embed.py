#!/usr/bin/env python3
"""Keep the official linker script immutable; convey ELF layout to flat loader."""
import struct
import sys
from pathlib import Path

def elf_layout(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    entry, shoff, shsize, shnum, strindex = header[4], header[6], header[11], header[12], header[13]
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shsize)
                for i in range(shnum)]
    strings = sections[strindex]
    names = data[strings[4]:strings[4] + strings[5]]
    allocated = []
    writable = []
    for s in sections:
        name = names[s[0]:].split(b'\0', 1)[0].decode()
        if not s[2] & 2 or not s[5]:
            continue
        allocated.append((s[3], s[3] + s[5], name))
        if s[2] & 1:
            writable.append(s[3])
    end = max(b for a, b, name in allocated)
    roend = min(writable) if writable else (end + 4095) & ~4095
    if roend % 4096 or entry >= roend:
        raise ValueError('code/data must be on separate pages')
    return entry, roend, end

if sys.argv[1] == 'linker':
    source = Path('user/user.ld').read_text()
    source = source.replace('  .data : { *(.data .data.*) }',
                            '  . = ALIGN(4096);\n  .data : { *(.data .data.*) *(.sdata .sdata.*) }')
    source = source.replace('    *(.sbss*)', '    *(.sbss*)\n    *(COMMON)')
    Path('user-flat/user.ld').write_text(source)
elif sys.argv[1] == 'table':
    programs = sys.argv[2:]
    out = ['.section .rodata', '.balign 8', '.global _uprog_table', '_uprog_table:']
    for name in programs:
        entry, roend, end = elf_layout(f'user-flat/{name}.elf')
        if len(name.encode()) >= 32:
            raise ValueError('embedded name too long')
        out += [f'.quad _uprog_{name}_start, _uprog_{name}_end',
                f'.quad {entry}, {roend}, {end}', f'.asciz "{name}"',
                f'.zero {31 - len(name.encode())}']
    out += ['.zero 72']
    for name in programs:
        out += ['.balign 8', f'_uprog_{name}_start:',
                f'.incbin "user-flat/{name}.bin"', f'_uprog_{name}_end:']
    Path('kernel/userimg.S').write_text('\n'.join(out) + '\n')
