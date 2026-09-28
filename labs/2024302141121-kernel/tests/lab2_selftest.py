#!/usr/bin/env python3
"""lab2 self-written tests, one per example in the task book (section 4),
driven through the UART like a person typing:

  T2-1  read with length exactly LAB2_BUF_SIZE        (user/readcap.c)
        + an overlong line and recovery afterwards
  T2-2  read on an empty buffer                       (user/readempty.c)
        blocks, then returns per LAB2_BUF_SEMANTICS
  T2-3  return value of illegal syscall numbers       (user/syserr.c)
        + other rejected requests: fds, lengths, pointers, exec, wait
  A1    every embedded program has main at address 0 (exec's entry point)
  A2    T2-1/T2-2 again on a copy built with the other buffer semantics

Usage (from the kernel tree):
  python3 tests/lab2_selftest.py
"""

import re
import subprocess
import sys

from harness import KERNEL_TREE, Console, Report, build, params, variant


def check_entry_points(report, tree):
    """exec jumps to address 0 of a flat binary; ENTRY(main) is lost there."""
    makefile = (tree / "Makefile").read_text()
    programs = re.search(r"^UPROGS = (.*)$", makefile, flags=re.M).group(1).split()
    misplaced = []
    for program in programs:
        symbols = subprocess.run(
            ["riscv64-unknown-elf-nm", str(tree / "user-flat" / f"{program}.elf")],
            capture_output=True, text=True).stdout
        entry = re.search(r"^([0-9a-f]+) T main$", symbols, flags=re.M)
        if not entry or int(entry.group(1), 16) != 0:
            misplaced.append(program)
    report.check("A1", f"main is at address 0 in all {len(programs)} programs",
                 not misplaced, ", ".join(misplaced) or " ".join(programs))


def run_readcap(report, tree, size, label):
    console = Console(tree)
    try:
        ok = console.run("readcap")
        ok = ok and console.expect(rf"send {size}-byte line") is not None
        console.send("a" * (size - 1) + "\n")
        ok = ok and console.expect(r"send overlong line") is not None
        console.send("b" * (size + 36) + "\n")
        ok = ok and console.expect(r"send short line") is not None
        console.send("ok\n")
        result = console.expect(
            r"READCAP (\w+) overlong_total=(\d+) overlong_calls=(\d+)")
        report.check(label, f"read(0, buf, {size}) of a {size}-byte line returns "
                     f"{size}; overlong line keeps its newline; buffer recovers",
                     bool(ok and result and result.group(1) == b"PASS"),
                     result.group(0).decode() if result else "no verdict")
        report.check(f"{label} alive", "shell prompt returns after overflow",
                     console.expect(r"sh> ") is not None)
    finally:
        console.close()


def run_readempty(report, tree, semantics, label):
    verdict = r"READEMPTY n=1 byte=113"          # 113 == 'q'
    console = Console(tree)
    try:
        ok = bool(console.run("readempty") and console.expect(r"readempty: ready"))
        blocked = console.absent(r"READEMPTY", 1.0)
        report.check(f"{label}a", "empty buffer: read blocks instead of returning 0/-1",
                     ok and blocked)
        console.send("q")                        # one key, no Enter
        if semantics == 1:
            early = console.expect(verdict, timeout=3) is not None
            report.check(f"{label}b", "stream mode (1): one key wakes the reader "
                         "before Enter", early)
            console.send("\n")
        else:
            waited = console.absent(r"READEMPTY", 1.5)
            console.send("\n")
            late = console.expect(verdict, timeout=3) is not None
            report.check(f"{label}b", "line mode (0): the reader wakes only after "
                         "Enter", waited and late)
        report.check(f"{label} alive", "shell prompt returns",
                     console.expect(r"sh> ") is not None)
    finally:
        console.close()


def run_syserr(report, tree, label):
    console = Console(tree)
    try:
        console.run("syserr")
        result = console.expect(r"SYSERR (\w+) failures=(\d+)")
        report.check(label, "syscall 0 / 23 / -1 / unimplemented return -1; bad fd, "
                     "length, pointer, exec, wait rejected",
                     bool(result and result.group(1) == b"PASS"),
                     result.group(0).decode() if result else "no verdict")
        console.run("hi")
        report.check(f"{label} alive", "kernel still runs programs afterwards",
                     console.expect(r"hi: user program running, pid=\d+") is not None)
    finally:
        console.close()


def main():
    tree = KERNEL_TREE
    p = params(tree)
    size = p["LAB2_BUF_SIZE"]
    semantics = p["LAB2_BUF_SEMANTICS"]
    report = Report(f"lab2 self-tests (buffer={size}, semantics={semantics})")
    build(tree)

    run_readcap(report, tree, size, "T2-1")
    run_readempty(report, tree, semantics, "T2-2")
    run_syserr(report, tree, "T2-3")

    check_entry_points(report, tree)
    other = 1 - semantics
    other_tree = variant(tree, f"sem{other}", {"LAB2_BUF_SEMANTICS": other})
    run_readcap(report, other_tree, size, f"A2 (semantics={other}) T2-1")
    run_readempty(report, other_tree, other, f"A2 (semantics={other}) T2-2")

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
