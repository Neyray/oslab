#!/usr/bin/env python3
"""ID=1: bench, MLFQ, ABI/recovery, preemption, preset integrity.
--experiments verifies RR and reproduces lost wakeup in disposable copies.
"""
import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path
from harness import KERNEL_TREE, Console, Report, build, variant
REPO=KERNEL_TREE.parents[1]
PACK=REPO/'materials/lab4-v2-20261009'

def snapshot(c):
    c.send(b'\x10')
    m=c.expect(r'lab3 memory: free=(\d+) pid=\d+ brk=0x[0-9a-f]+')
    return int(m.group(1)) if m else -1

def run(r,c,cmd,pattern,timeout=40):
    start=len(c.output); c.send(cmd+'\n')
    result=c.expect(pattern,timeout)
    alive=c.expect(r'sh> ',5) if result else None
    data=c.output[start:]
    r.check(cmd,pattern,bool(result and alive) and b'FAIL' not in data,
            result.group(0).decode(errors='replace') if result else data[-500:].decode(errors='replace'))
    return data

def integrity(r):
    for path in (PACK/'gifts').rglob('*'):
        if path.is_file() and path.name!='Makefile.upgrade':
            rel=path.relative_to(PACK/'gifts')
            r.check('preset '+str(rel),'byte-identical to official v2',path.read_bytes()==(KERNEL_TREE/rel).read_bytes())
    for path in (PACK/'support/tests').iterdir():
        for dest in [KERNEL_TREE/'support/tests'/path.name]+([KERNEL_TREE/'user'/path.name] if path.suffix in ['.c','.h'] else []):
            r.check('official '+str(dest.relative_to(KERNEL_TREE)),'byte-identical',path.read_bytes()==dest.read_bytes())
    protected=['kernel/kernel.ld','kernel/riscv.h','kernel/types.h','kernel/memlayout.h',
               'kernel/course_sid.h','kernel/trampoline.S','user/user.ld','user/printf.c',
               'user/ulib.c','user/sh.c','user/hi.c','user/spin.c']
    for name in protected:
        expected=json.loads((KERNEL_TREE/'tests/lab4-presets.json').read_text())['sha256'][name]
        actual=hashlib.sha256((KERNEL_TREE/name).read_bytes()).hexdigest()
        r.check('historical preset '+name,'unchanged since lab3-submit',expected==actual)

def experiments(r):
    rr=variant(KERNEL_TREE,'lab4-rr',cflags='-DLAB4_RR_TEST')
    c=Console(rr)
    try:
        c.expect('sh> ')
        run(r,c,'bench',r'PASS: bench')
        data=run(r,c,'alt',r'alt done')
        chars=bytes(x for x in data.split(b'\n',1)[-1].split(b'alt done')[0] if x in b'AB')
        r.check('RR preemption','both workers interleave',chars.count(b'A')==16 and chars.count(b'B')==16 and len(re.findall(rb'A+|B+',chars))>=3)
        Path('/tmp/lab4-rr.log').write_bytes(c.output)
    finally:
        c.close();shutil.rmtree(rr.parent)
    temp=Path(tempfile.mkdtemp(prefix='oslab-lost-wakeup-'))
    faulty=temp/'tree'
    shutil.copytree(KERNEL_TREE,faulty,ignore=shutil.ignore_patterns('*.o','*.log','user-flat'))
    source=faulty/'kernel/proc.c';text=source.read_text()
    # Delay alone cannot schedule another process here: only U-mode traps
    # preempt. Yield forces child exit into the unprotected wait-condition gap.
    old='  acquire(&p->lock);\n  release(lk);\n  p->chan=chan;'
    new="""  if(lk==&wait_lock) {
    release(lk);
    for(volatile int i=0;i<1000000;i++);
    proc_yield();
    acquire(&p->lock);
  } else {
    acquire(&p->lock);
    release(lk);
  }
  p->chan=chan;"""
    assert text.count(old)==1
    source.write_text(text.replace(old,new));build(faulty)
    c=Console(faulty)
    try:
        boot=c.expect('sh> ');c.send('bench\n')
        stuck=c.absent(r'RESULT avg_fork_wait|PASS: bench',2)
        c.send(b'\x10');c.expect(r'PID PPID STATE LEVEL CPU SLICE TICKETS NAME')
        c.absent('NEVER-MATCH',0.2)
        r.check('lost wakeup','faulty wait sleeps with a zombie child',bool(boot) and stuck and b'SLEEPING' in c.output and b'ZOMBIE' in c.output)
        Path('/tmp/lab4-lost-wakeup.log').write_bytes(c.output)
    finally:
        c.close();shutil.rmtree(temp)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--experiments',action='store_true');args=ap.parse_args()
    r=Report('lab4: official bench + assigned MLFQ + boundary/recovery + regressions')
    build(KERNEL_TREE);integrity(r)
    c=Console(KERNEL_TREE)
    try:
        r.check('boot','shell starts',bool(c.expect(r'sh> ')))
        before=snapshot(c)
        for i in range(3):
            data=run(r,c,'bench',r'PASS: bench')
            r.check('bench concurrency '+str(i),'all eight exit statuses recovered',b'RESULT concurrent_children = 8' in data)
        run(r,c,'schedabi',r'SCHEDABI PASS')
        start=len(c.output);c.send('mlfqtest\n')
        tiers=c.expect(r'RESULT mlfq_slices = 7 workers = (\d+):(\d+):(\d+)\r?\n',30)
        ready=c.expect(r'MLFQ interactive-ready pid=(\d+)\r?\n',10)
        c.send(b'\x10')
        state=c.expect((ready.group(1).decode() if ready else 'MISSING')+r' \d+ (?:RUNNABLE|RUNNING|SLEEPING) 0 \d+ [01] \d+ mlfqtest',5)
        ok=c.expect('MLFQTEST PASS',15);alive=c.expect('sh> ',5)
        data=c.output[start:]
        r.check('MLFQ tiers','1/2/4 ticks and progress for every CPU worker',bool(tiers) and all(int(v)>4 for v in tiers.groups()),tiers.group(0).decode() if tiers else 'missing tiers')
        r.check('MLFQ interactive','interactive level 0; wake <=5 ticks; background advances',bool(ready and state and ok and alive) and b'FAIL' not in data)
        data=run(r,c,'alt',r'alt done')
        chars=bytes(x for x in data.split(b'\n',1)[-1].split(b'alt done')[0] if x in b'AB')
        r.check('preemption','A/B interleave, 16 chars each',chars.count(b'A')==16 and chars.count(b'B')==16 and len(re.findall(rb'A+|B+',chars))>=3,chars.decode())
        after=snapshot(c)
        r.check('recovery','same free-page count after all operations',before>0 and before==after,f'{before} -> {after}')
        r.check('invariants','no panic or kernel trap',b'panic:' not in c.output and b'kernel trap:' not in c.output)
        Path('/tmp/lab4-user.log').write_bytes(c.output)
    finally:c.close()
    if args.experiments:experiments(r)
    return r.finish()

if __name__=='__main__':raise SystemExit(main())
