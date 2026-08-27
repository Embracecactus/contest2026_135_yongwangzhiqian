# BK7258 主机回归测试

本目录直接编译仓库中的现役 `chips/bk7258` 实现，用主机 mock 隔离 MMIO、SDK 和
NuttX 内核接口。它用于快速发现源码迁移、生成 ABI 和纯逻辑回归，不代替固件构建或
T5-Board 实板 xTS。

## 运行

依赖 GCC/Clang、Python 3、`pkg-config` 和 cmocka。唯一完整入口是：

```bash
./tests/bk7258/run_tests.sh
```

脚本从干净的 `tests/bk7258/build/` 开始，记录提交、编译器、Python、cmocka、
sanitizer 和权威分区 CSV 哈希，然后依次执行公共模块、BL1、BL2 和 AP/CP 外设测试。
成功结束必须出现 `BK7258_HOST_TEST_PASS`。构建产物只写入该 `build/` 目录。

也可在本目录执行分层入口：

```bash
make run-core
make run-bl1
make run-bl2
make run-ap
```

## 当前覆盖

- 公共层：RPTUN mailbox、CP/AP RPTUN core、PM activity、BL1 policy；
- BL1：libc、SHA-256、flash、clock、runtime 和现行 Beken manifest；
- BL2：security counter、flash-map/CRC trailer 写入和 CP/AP pair policy；
- AP/CP 外设：JPEG、YUV/H.264、scale/rotate、CAN 和 IrDA。

分区头不使用历史副本，而是由
`boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv`
在 `build/layout/` 临时生成。BL1 公钥 fixture 是确定性的公钥字节，仅用于模拟验签
ABI；它不是私钥、下载密钥或可部署信任根。BL2 不固定断言任何签名公钥，因为正式
构建必须按每一代的新密钥生成对应源码。

## 边界

主机 PASS 只能证明被编译模块的逻辑和 ABI。它不证明串口、时钟、电源、真实 flash、
CAN 收发器、RTC、存储介质或 12 小时稳定性。涉及硬件的状态必须另附当前代构建身份、
下载边界、原始串口日志和恢复结果。
