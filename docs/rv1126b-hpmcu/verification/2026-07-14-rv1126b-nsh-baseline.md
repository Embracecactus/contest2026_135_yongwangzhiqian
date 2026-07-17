# 2026-07-14 RV1126B NSH 已板测基线证据记录

> 不可变性声明：本记录只描述当日已刷写、已板测的基线；后续工作树或构建产物不得追溯性替代它。

## 基线身份与范围

- 仓库 HEAD：`8987bbc`。
- 关键保留条件：当时 BSP 源码是**未提交且未跟踪（uncommitted/untracked）的 overlay**；因此该板测结果绑定该实际工作树，而非仅绑定 HEAD。
- 构建后端：classic Make。
- 入口地址：`0x48c02000`。
- RAM：`0x48c02000 + 0x3a000`。
- 相关源码/配置路径（均相对 contest 仓库）：
  - `board/contest_board/chip/rv1126b_start.c`
  - `board/contest_board/chip/rv1126b_lowputc.c`
  - `board/contest_board/configs/nsh/defconfig`

## 受保护的 UART/IPIC 事实

- UART5 使用 M0 `PA6`/`PA7`、`FUNC5`，时钟为 24MHz，串口参数为 1.5M 8N1。
- 原始 IRQ 为 `61`；INTMUX 为 group 1、bit 29。
- IPIC 已执行 init/SOI/EOI；RX/TX 均为中断驱动。

## 已捕获工件

| 工件 | 大小 | sha256 |
| --- | ---: | --- |
| `nuttx` | 157084 bytes | `d424f288a4ab5904efafe32bc383fcdf14652907a26f4956135c05d14a3ff54f` |
| `nuttx.bin` and `rtt.bin` | 80320 bytes | `9fafb3ab062242b6d644b2a51cd63bb5897d8640e343a8d1a9839d272439302e` |
| `nuttx_amp.img` and `amp.img` | 84992 bytes | `43a3692c66422ca3a66b80255923cd47ee0e878b9fda0e71d33f2e025f9edd4c` |
| `update.img` | 1451395658 bytes | `851ca7e74ed7d31df900dcc0bd8444357784e6e2b780fa597d7844523c71a5cc` |

复制对逐字节等价性已确认：`nuttx.bin == rtt.bin`，且 `nuttx_amp.img == amp.img`。

## 实际用户终端记录

```text
nsh> help
help usage:  help [-v] [<cmd>]

    .           cd          exit        mkrd        sleep       unset
    [           cp          expr        mount       source      uptime
    ?           cmp         false       mv          test        usleep
    alias       dirname     help        printf      time        watch
    unalias     dd          hexdump     pwd         true        xd
    basename    dmesg       kill        rm          truncate
    break       echo        ls          rmdir       uname
    cat         exec        mkdir       set         umount
nsh>
```

## 验收矩阵

| 项目 | 状态 |
| --- | --- |
| boot | 已验证 |
| banner | 已验证 |
| prompt | 已验证 |
| RX | 已验证 |
| help | 已验证 |
| prompt return | 已验证 |
| `uname -a` | 尚未取得证据 |
| board revision | 尚未取得证据 |
| exact flash command | 尚未取得证据 |
| timestamped capture | 尚未取得证据 |

## 替代边界

后续 cleanup builds 是独立候选；在重新刷写并完成板测前，不能替代本已板测基线。
