# lab2 测试与回归记录

## 自动化入口

```bash
cd ~/projects/oslab/labs/2024302141121-kernel
make clean && make
python3 support/inject_uart.py --tree . \
  --script support/tests/smoke.script --log smoke-inject.log
python3 support/inject_uart.py --tree . \
  --script support/tests/bufstorm.script --log bufstorm-inject.log
python3 check_expect.py 2024302141121
```

## 用例一 系统调用与错误路径

输入 `hi` 后必须打印有效 PID 并回到 `sh>`；随后执行 `badecall`，90–99 号未知调用必须全部返回 `-1`，输出 `TEST-1 PASS`。此用例覆盖 fork、exec、getpid、write、exit、wait 以及未知编号的默认分支。

实测：自动注入驱动显示 `boot ok`，两个 EXPECT 均为 PASS。

## 用例二 缓冲边界与恢复

进入 `bufstorm` 后先注入 100 个 `x`，超过个人缓冲容量 64，再注入三行普通文本。预期允许溢出数据丢失，但必须保留换行边界、完成四次读取、打印 `BUFSTORM lines=4` 并回到 `sh>`。

实测：自动注入驱动显示 `boot ok`，`BUFSTORM lines=4` 为 PASS；本次统计 116 个有效读取字节。

## 中断活性

执行 `spin` 后注入 `uart-ok`。预期持续的用户输出中能看到 `uart-ok` 回显，证明忙循环期间 UART 外部中断仍可进入；`-d int` 同时持续记录 cause 1 的 S 态软件中断，证明 `LAB2_TICK=3` 的定时器桥接仍活跃。

## 异常日志

组合运行 `hi`、`badecall`、`bufstorm` 时，`regression-int.log` 中有 171 条 cause 8，同步异常均为预期 U 态 ecall；排除 cause 8 后同步异常计数为 0。没有 `user trap:` 或 `kernel trap:` 诊断输出。

## Lab1 回归

`python3 check_expect.py 2024302141121` 形状校验通过。QEMU 实际启动输出的前三行与 `expect_banner.txt` 逐字节一致，校验和仍为 `12536`。

