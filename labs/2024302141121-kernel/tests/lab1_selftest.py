#!/usr/bin/env python3
"""lab1 self-written tests, organised as the task book asks (section 4):

  T1-1  printf boundary cases: 0, negative, empty string, INT_MAX/INT_MIN,
        plus %x/%p/%c/%%, long extremes, NULL and unknown conversions
  T1-2  banner protocol format boundaries: sid/mod97 format, checksum
        recomputed on the host, checksum as the last line, byte equality
        with expect_banner.txt, and the same body under protocols 0 and 1
  A1/A2 extra checks: cold boot idempotence, no fault in -d int

Usage (from the kernel tree):
  python3 tests/lab1_selftest.py              # test this tree
  python3 tests/lab1_selftest.py --tree DIR   # e.g. an older worktree
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

from harness import KERNEL_TREE, Report, boot_capture, build, params, variant

LONG_TEXT = "0123456789" * 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", type=Path, default=KERNEL_TREE)
    args = parser.parse_args()
    tree = args.tree.resolve()

    p = params(tree)
    sid = p["COURSE_SID"]
    protocol = p["LAB1_BANNER_PROTOCOL"]
    report = Report(f"lab1 self-tests on {tree} (sid={sid}, protocol={protocol})")

    build(tree)
    output = boot_capture(tree)
    lines = output.split(b"\n")
    body = lines[0] + b"\n" + lines[1] + b"\n"

    # T1-1 printf boundaries, first those printed in the banner itself.
    fields = dict(re.findall(rb"(\w+)=('[^']*'|\S+)", lines[1]))
    banner_cases = [
        ("a", "zero", b"0", "%d of 0 prints a single digit"),
        ("b", "neg", b"-2147483648", "%d of INT_MIN (negative, no overflow)"),
        ("c", "max", b"2147483647", "%d of INT_MAX"),
        ("d", "empty", b"''", "%s of an empty string prints nothing"),
        ("e", "hex", b"0xffffffff", "%x of UINT_MAX: lowercase, 0x, no leading zero"),
        ("f", "long", LONG_TEXT.encode(), "%s of an 80-byte string is complete"),
    ]
    for sub, key, want, what in banner_cases:
        report.check(f"T1-1{sub}", what, fields.get(key.encode()) == want,
                     f"got {fields.get(key.encode())!r}")

    # T1-1 continued: conversions the banner never uses, via a test build.
    if "LAB1_PRINTF_EXTRA_TEST" in (tree / "kernel" / "main.c").read_text():
        extra = boot_capture(variant(tree, "printf",
                                     cflags="-DLAB1_PRINTF_EXTRA_TEST"))
        line = next((l for l in extra.split(b"\n") if l.startswith(b"extra ")), b"")
        got = dict(re.findall(rb"(\w+)=(\S+)", line))
        extra_cases = [
            ("g", "neg1", b"-1", "%d of -1"),
            ("h", "hexzero", b"0x0", "%x of 0 still prints one digit"),
            ("i", "lmin", b"-9223372036854775808", "%ld of LONG_MIN"),
            ("j", "lmax", b"18446744073709551615", "%lu of ULONG_MAX"),
            ("k", "null", b"(null)", "%s of a NULL pointer does not fault"),
            ("l", "ptr", b"0x80000000", "%p prints 0x plus lowercase hex"),
            ("m", "chr", b"Z", "%c"),
            ("n", "pct", b"%", "%% prints one percent sign"),
            ("o", "unknown", b"%q", "an unknown conversion is echoed, not dropped"),
        ]
        for sub, key, want, what in extra_cases:
            report.check(f"T1-1{sub}", what, got.get(key.encode()) == want,
                         f"got {got.get(key.encode())!r}")
    else:
        report.skip("T1-1g..o", "printf cases outside the banner",
                    "this tree predates the LAB1_PRINTF_EXTRA_TEST hook")

    # T1-2 banner protocol format boundaries.
    report.check("T1-2a", "line 1 has decimal sid and lowercase 0x mod97",
                 lines[0] == f"OSLAB1 sid={sid} mod97=0x{sid % 97:x}".encode(),
                 lines[0].decode(errors="replace"))
    if protocol == 2:
        checksum = sum(body)
        report.check("T1-2b", "[chk=N] equals the host-side byte sum of the body",
                     lines[2] == f"[chk={checksum}]".encode(),
                     f"host={checksum} kernel={lines[2].decode(errors='replace')}")
        report.check("T1-2c", "the checksum line is the last banner line and ends in \\n",
                     output.startswith(body + lines[2] + b"\n"))
    expect = (tree / "expect_banner.txt").read_bytes()
    report.check("T1-2d", "banner equals expect_banner.txt byte for byte",
                 output[:len(expect)] == expect, f"{len(expect)} bytes")
    plain = boot_capture(variant(tree, "proto0", {"LAB1_BANNER_PROTOCOL": 0}))
    report.check("T1-2e", "protocol 0 build: same body, no checksum line",
                 plain.startswith(body)
                 and not plain[len(body):].startswith(b"[chk="))
    dotted = boot_capture(variant(tree, "proto1", {"LAB1_BANNER_PROTOCOL": 1}))
    interleaved = b"".join(bytes([c]) + b"." for c in body)
    report.check("T1-2f", "protocol 1 build: '.' after every byte, newline included",
                 dotted.startswith(interleaved), f"{len(interleaved)} bytes")

    # Extra checks beyond the task book.
    report.check("A1", "two cold boots print identical bytes",
                 boot_capture(tree) == output)
    log = Path(tempfile.mkdtemp(prefix="oslab-int-")) / "int.log"
    boot_capture(tree, extra_args=["-d", "int", "-D", str(log)])
    faults = [l for l in log.read_text(errors="replace").splitlines()
              if "async:0" in l and "cause:0000000000000008" not in l]
    report.check("A2", "-d int shows no synchronous fault during boot",
                 not faults, faults[0] if faults else "0 faults")

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
