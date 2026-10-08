#!/usr/bin/env python3
"""Official lab3 tests, regression and failure rollback. Run from any cwd.
--skip-mm omits temporary MM self-test builds; --all-orders also tests 0 and 1.
"""
import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from harness import KERNEL_TREE, Console, Report, build, variant, params


def dump_and_free(console):
    console.send(b'\x10')
    match = console.expect(r'lab3 memory: free=(\d+) pid=\d+ brk=0x[0-9a-f]+')
    return int(match.group(1)) if match else -1


def run(report, console, name, verdict, timeout=20):
    console.send(name + '\n')
    match = console.expect(verdict, timeout=timeout)
    report.check(name, verdict, bool(match), match.group(0).decode() if match else 'no verdict')
    report.check(name + ' alive', 'shell returns', bool(console.expect(r'sh> ')))
    return match


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-mm', action='store_true')
    parser.add_argument('--all-orders', action='store_true')
    args = parser.parse_args()
    tree = KERNEL_TREE
    report = Report('lab3 self-tests: official + boundary + rollback + regression')
    build(tree)
    console = Console(tree)
    try:
        report.check('boot', 'Shell starts', bool(console.expect(r'sh> ')))
        first = console.output
        banner = b'\n'.join(first.split(b'\n')[:3]) + b'\n'
        report.check('lab1', 'banner is byte-identical', banner == (tree / 'expect_banner.txt').read_bytes())
        block = first.split(b'DUMP-PAGETABLE begin\n', 1)[1].split(b'DUMP-PAGETABLE end', 1)[0]
        lines = block.splitlines()
        valid = all(re.fullmatch(rb'(L[12]|LEAF) va=0x[0-9a-f]+ pa=0x[0-9a-f]+ perm=[r-][w-][x-][u-]', line) for line in lines)
        leaves = {int(m[0], 16): m[1].decode() for m in re.findall(rb'LEAF va=(0x[0-9a-f]+) pa=0x[0-9a-f]+ perm=(....)', block)}
        p = params(tree)
        data_top = 0x2000
        report.check('dump ABI', 'code/data/guard/stack/trapframe/trampoline permissions', valid and
                     leaves.get(0) == 'r-xu' and leaves.get(0x1000) == 'rw-u' and
                     all(data_top + i * 4096 not in leaves for i in range(p['LAB3_GUARD_PAGES'])) and
                     leaves.get(data_top + p['LAB3_GUARD_PAGES'] * 4096) == 'rw-u' and
                     leaves.get(0x3fffffe000) == 'rw--' and leaves.get(0x3ffffff000) == 'r-x-')
        before = dump_and_free(console)
        run(report, console, 'badaccess', r'TEST-3a PASS')
        # run() consumes through the shell prompt, so verify both verdicts in its transcript.
        report.check('badaccess 3b', 'beyond-sbrk process is killed', b'TEST-3b PASS' in console.output)
        run(report, console, 'guardtest', r'GUARDTEST PASS pages=\d+ failures=0')
        console.send('vmtest\n')
        ready = console.expect(r'VMTEST input-ready')
        console.send('cross-input\n')
        report.check('cross-page', 'write and read across nonadjacent physical pages', bool(ready and console.expect(r'VMTEST PASS')) and b'CROSS-PAGE-OK!\n' in console.output)
        report.check('cross-page alive', 'shell returns', bool(console.expect(r'sh> ')))
        start = len(console.output)
        run(report, console, 'schedtest', r'SCHEDTEST PASS')
        segment = console.output[start:]
        report.check('timer preemption', 'child runs before parent waits', segment.find(b'SCHED-CHILD\n') >= 0 and segment.find(b'SCHED-CHILD\n') < segment.find(b'SCHED-PARENT-WAIT\n'))
        run(report, console, 'memcap', r'TEST-4 PASS: OOM returned error after \d+ chunks;', 240)
        run(report, console, 'oomtest', r'OOMTEST PASS bytes=\d+\n', 240)
        after = dump_and_free(console)
        report.check('conservation', 'no user page/table/trapframe leak after all programs', before > 0 and before == after and len(re.findall(rb'^DUMP-PAGETABLE begin$', console.output, re.M)) == 3, f'{before} -> {after}; replay begins at line start')
        Path('/tmp/lab3-user.log').write_bytes(console.output)
    finally:
        console.close()
    concurrent = Console(tree)
    try:
        concurrent.expect(r'sh> ')
        concurrent.send('concurrent\n')
        new_shell = concurrent.expect(r'sh> ')
        spin_before = concurrent.expect(r'spin \d+')
        concurrent.send('hi\n')
        hi = concurrent.expect(r'hi: user program running, pid=\d+')
        prompt = concurrent.expect(r'sh> ')
        spin_after = concurrent.expect(r'spin \d+')
        report.check('spin + sh', 'supplied spin progresses while supplied sh accepts hi',
                     bool(new_shell and spin_before and hi and prompt and spin_after))
        Path('/tmp/lab3-concurrent.log').write_bytes(concurrent.output)
    finally:
        concurrent.close()
    for script in ['smoke', 'bufstorm']:
        result = subprocess.run(['python3', str(tree / 'support/inject_uart.py'), '--tree', str(tree),
                                 '--script', str(tree / f'support/tests/{script}.script'),
                                 '--log', f'/tmp/lab3-{script}.log'], capture_output=True, text=True, timeout=60)
        report.check('lab2 ' + script, 'official UART regression', result.returncode == 0, result.stdout.strip().replace('\n', '; '))
    # Verify normal hi/badecall UART sessions contain ecall only, no unexpected faults.
    with tempfile.TemporaryDirectory(prefix='oslab-lab3-traps-') as directory:
        log = Path(directory) / 'int.log'
        process = subprocess.Popen(['qemu-system-riscv64', '-machine', 'virt', '-bios', 'none',
                                    '-kernel', 'kernel/kernel', '-nographic', '-d', 'int', '-D', str(log)],
                                   cwd=tree, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        import time
        time.sleep(0.3)
        process.stdin.write(b'hi\nbadecall\n'); process.stdin.flush()
        time.sleep(0.3)
        process.kill(); process.communicate()
        faults = [line for line in log.read_text().splitlines() if 'async:0' in line and 'cause:0000000000000008' not in line]
        report.check('normal traps', 'only ecall synchronous exceptions in hi/badecall', not faults, f'{len(faults)} unexpected faults')
    if not args.skip_mm:
        orders = [params(tree)['LAB3_ALLOC_ORDER']]
        if args.all_orders:
            orders = [0, 1, 2]
        for order in orders:
            temporary = variant(tree, f'lab3-mm-order{order}', {'LAB3_ALLOC_ORDER': order}, cflags='-DLAB3_MM_SELFTEST')
            session = Console(temporary)
            try:
                success = session.expect(r'MMTEST RESULT failures=0', timeout=60)
                report.check('MM order=' + str(order), 'allocator exhaustion + mapping/walk/growth rollback + copy permissions', bool(success) and b'MMTEST FAIL' not in session.output)
                Path(f'/tmp/lab3-mm-order{order}.log').write_bytes(session.output)
            finally:
                session.close()
                shutil.rmtree(temporary.parent)
    return report.finish()


if __name__ == '__main__':
    raise SystemExit(main())
