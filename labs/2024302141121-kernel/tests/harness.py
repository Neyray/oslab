"""Shared QEMU driver for the self-written lab tests.

Variant kernels (another banner protocol, another buffer semantics, extra
printf cases) are built in temporary copies of the tree, so the preset
kernel/course_sid.h in the real tree is never modified.
"""

import os
import re
import select
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

KERNEL_TREE = Path(__file__).resolve().parent.parent
QEMU = ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
        "-kernel", "kernel/kernel", "-nographic"]


class Report:
    def __init__(self, title):
        self.failed = 0
        self.passed = 0
        print(title)

    def check(self, test_id, what, ok, detail=""):
        print(f"[{'PASS' if ok else 'FAIL'}] {test_id} {what}"
              + (f"  ({detail})" if detail else ""))
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        return ok

    def skip(self, test_id, what, reason):
        print(f"[SKIP] {test_id} {what}  ({reason})")

    def finish(self):
        print(f"== {self.passed} passed, {self.failed} failed")
        return 1 if self.failed else 0


def params(tree):
    """Read the per-student macros from the tree's course_sid.h."""
    text = (Path(tree) / "kernel" / "course_sid.h").read_text()
    return {k: int(v) for k, v in
            re.findall(r"^#define (\w+) (\d+)", text, flags=re.M)}


def build(tree):
    subprocess.run(["make", "clean"], cwd=tree, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    result = subprocess.run(["make"], cwd=tree, capture_output=True,
                            text=True)
    if result.returncode != 0:
        raise SystemExit(f"build failed in {tree}\n"
                         f"{result.stdout[-2000:]}{result.stderr[-2000:]}")


def variant(tree, name, overrides=None, cflags=None):
    """Copy the tree, override course_sid.h macros and/or CFLAGS, build."""
    destination = Path(tempfile.mkdtemp(prefix=f"oslab-{name}-")) / "tree"
    shutil.copytree(tree, destination,
                    ignore=shutil.ignore_patterns("*.o", "user-flat", "*.log"))
    header = destination / "kernel" / "course_sid.h"
    text = header.read_text()
    for key, value in (overrides or {}).items():
        text, count = re.subn(rf"^#define {key} \S+", f"#define {key} {value}",
                              text, flags=re.M)
        if count != 1:
            raise SystemExit(f"{key} not found in course_sid.h")
    header.write_text(text)
    if cflags:
        makefile = destination / "Makefile"
        makefile.write_text(makefile.read_text().replace(
            "CFLAGS = ", f"CFLAGS = {cflags} ", 1))
    build(destination)
    return destination


def boot_capture(tree, seconds=3.0, extra_args=()):
    """Boot without input and return everything printed within `seconds`."""
    proc = subprocess.Popen(QEMU + list(extra_args), cwd=tree,
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL)
    output = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            output += os.read(proc.stdout.fileno(), 4096)
    proc.kill()
    proc.wait()
    return output


class Console:
    """Interactive session: type into the UART and wait for output."""

    def __init__(self, tree):
        self.proc = subprocess.Popen(QEMU, cwd=tree, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        self.output = b""
        self.mark = 0

    def _pump(self, timeout):
        ready, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if ready:
            self.output += os.read(self.proc.stdout.fileno(), 4096)

    def expect(self, pattern, timeout=10.0):
        """Wait for `pattern` after the previous match; advance past it."""
        regex = re.compile(pattern.encode())
        deadline = time.time() + timeout
        while True:
            match = regex.search(self.output, self.mark)
            if match:
                self.mark = match.end()
                return match
            if time.time() >= deadline:
                return None
            self._pump(0.05)

    def absent(self, pattern, seconds):
        """True if `pattern` does not appear during the next `seconds`."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            self._pump(0.05)
        return re.search(pattern.encode(), self.output[self.mark:]) is None

    def send(self, data):
        self.proc.stdin.write(data.encode() if isinstance(data, str) else data)
        self.proc.stdin.flush()
        time.sleep(0.05)

    def run(self, command):
        """Wait for the shell prompt, then type a command line."""
        if not self.expect(r"sh> "):
            return False
        self.send(command + "\n")
        return True

    def close(self):
        self.proc.kill()
        self.proc.wait()
