<!-- SPDX-License-Identifier: Apache-2.0 -->
# 2026-11-04 DVP/GC2145 寄存器错误传播

本次范围为摄像头传感器寄存器读写的错误语义。团队 HEAD 为 `541c6434`，
固定 SDK 为 `Embracecactus/bk_avdk_smp` `eb0d4252a83f0ba88f0eb947fcfa2e36cbcb9bb4`。
没有修改 SDK checkout、团队固件源码、manifest 或 SDK profile；没有执行
`sdk rebuild`、固件构建、签名、烧录或实板测试。源码、已安装库、主机测试与
设备状态分别记录如下。

## 结论

| 层级 | 结果 |
|---|---|
| 固定 SDK 源码 | `gc2145_read/write_register()` 丢弃 `dvp_camera_i2c_*_uint8()` 结果并固定返回 0，读失败仍把 0 写入输出；`bk_dvp_sensor_read/write_register()` 丢弃回调结果并返回 `BK_OK`，读调用直接把调用者缓冲交给回调 |
| 已安装库 | `ap`、`ap-aidk`、`ap-sdio4` 三个 bundle 的 `libbk_peripheral.a` 与 `libbk_dvp.a` 反汇编与源码一致：两层均在回调后返回常量 0 |
| 团队 chip 层 | `bk7258_dvp_sensor_read/write()` 已正确映射错误，读失败不发布值；无需修改 |
| 产品调用链 | `129d4f9a`（2026-09-16）已删除 AIDK GC2145 V4L2 控制转发，当前 AIDK/T5-Board imgsensor 无 `get_value/set_value`，团队代码无调用者；缺陷当前为潜在缺陷 |
| 设备 | 工作区无已构建 AP 镜像；运行中固件是否链接这些函数未核实，未做实板验证 |

SDK 头文件 `dvp_camera_types.h` 规定两个回调“0 成功，失败返回错误码”，
`dvp_sensor_devices.h` 规定 I2C helper “0 成功，其他为错误”。修正只恢复
这一既有合同，不新增错误框架。

## 候选 SDK 修正

修正必须同时作用于两层：只改 `bk_dvp.c` 时五项检查仍失败，只改
`dvp_gc2145.c` 时四项仍失败。补丁基于固定提交，`git apply --check` 通过，
以下 diff 代码块正文按 UTF-8、LF 换行并保留末尾一个换行保存时，
SHA-256 为 `25bd23eea1e8a56912549347648d5b4c827dd7569e6618c981d88f76aee077d5`。
内容与 `7079493e` 退役的 `ap-dvp-register-errors.patch` 等价，许可为
Beken Apache-2.0。

```diff
--- a/ap/components/bk_dvp/src/bk_dvp.c
+++ b/ap/components/bk_dvp/src/bk_dvp.c
@@ -1805,10 +1805,10 @@
 {
     bk_err_t ret = BK_FAIL;
     dvp_driver_handle_t *dvp_handle = (dvp_driver_handle_t *)handle;
-    if (dvp_handle && dvp_handle->sensor)
+    if (dvp_handle && dvp_handle->sensor && reg_val &&
+        dvp_handle->sensor->write_register)
     {
-        dvp_handle->sensor->write_register(reg_val->reg, reg_val->val);
-        ret = BK_OK;
+        ret = dvp_handle->sensor->write_register(reg_val->reg, reg_val->val);
     }

     return ret;
@@ -1818,10 +1818,13 @@
 {
     bk_err_t ret = BK_FAIL;
     dvp_driver_handle_t *dvp_handle = (dvp_driver_handle_t *)handle;
-    if (dvp_handle && dvp_handle->sensor)
+    if (dvp_handle && dvp_handle->sensor && reg_val &&
+        dvp_handle->sensor->read_register)
     {
-        dvp_handle->sensor->read_register(reg_val->reg, &reg_val->val);
-        ret = BK_OK;
+        uint32_t value = 0;
+        ret = dvp_handle->sensor->read_register(reg_val->reg, &value);
+        if (ret == BK_OK)
+            reg_val->val = value;
     }

     return ret;
--- a/ap/components/bk_peripheral/src/dvp/dvp_gc2145.c
+++ b/ap/components/bk_peripheral/src/dvp/dvp_gc2145.c
@@ -1750,15 +1750,15 @@
 int gc2145_read_register(uint32_t reg, uint32_t *data)
 {
     uint8_t val = 0;
-    SENSOR_I2C_READ(reg, &val);
-    *data = val;
-    return 0;
+    int ret = dvp_camera_i2c_read_uint8((GC2145_WRITE_ADDRESS >> 1), reg, &val);
+    if (ret == 0)
+        *data = val;
+    return ret;
 }

 int gc2145_write_register(uint32_t reg, uint32_t data)
 {
-    SENSOR_I2C_WRITE(reg, data);
-    return 0;
+    return dvp_camera_i2c_write_uint8((GC2145_WRITE_ADDRESS >> 1), reg, data);
 }

 const dvp_sensor_config_t dvp_sensor_gc2145 =
```

## 主机证据

`tests/host/bk7258/test_bk7258_dvp_register_errors.py` 原样编译团队
`bk7258_dvp_error()`、`bk7258_dvp_sensor_read/write()`，固定 SDK 的
`bk_dvp_sensor_*_register()` 与 `gc2145_*_register()`，只在
`dvp_camera_i2c_{read,write}_uint8()` 注入 `BK_ERR_I2C_ACK_TIMEOUT` 与
`BK_FAIL`。`--sdk-patch` 只在临时副本应用候选补丁。

| 输入 | 结果 |
|---|---|
| 固定 SDK `eb0d425` | 退出 1：写错误未上报、读错误未上报、读失败发布无效值（团队 API 与 SDK API 各一项），共 5 项失败；正常读写与失败后恢复通过 |
| 固定 SDK + 候选补丁 | 退出 0：读写错误均传到调用者，读失败不改输出，失败后读写恢复，每次操作恰好访问总线一次 |

相关既有回归 `test_bk7258_dvp_stop_buffers.py`、`test_bk7258_i2c_lengths.py`、
`test_sdio_dma_completion.py` 均退出 0。

## 构建证据

锁定的 Arm GNU 10.3-2021.10 以 `ap-aidk` 的 `sdkconfig.h` 对打补丁的
`dvp_gc2145.c` 做 `-fsyntax-only`：0 错误，6 个警告与原文件完全相同
（SDK 日志宏既有警告）。`bk_dvp.c` 依赖 SDK 构建期生成的 `ram_regions.h`，
不能脱离 `sdk rebuild` 单独编译；其修改函数已在主机测试中以 `-Werror` 编译执行。
未生成新的 SDK bundle 或固件。

## 落地步骤

以下步骤需要所有者授权（fork 提交、推送与 manifest 固定值变更）：

1. 在 `Embracecactus/bk_avdk_smp` 以 `eb0d425` 为父提交应用上述补丁并发布，
   如 `fix/dvp-register-errors`。
2. 更新团队 manifest 中 `vendor/beken/bk_avdk_smp` 的 `revision` 与 `upstream`，
   按同步 SOP 只同步该项目。
3. 对每个 profile 运行 `tools/bk7258/bk7258.py sdk rebuild --profile <profile>
   --source ../vendor/beken/bk_avdk_smp --jobs 12 --replace`，再 `sdk verify`；
   预期 CP 摘要不变，三个 AP profile 摘要更新。
4. 在 `tests/host/bk7258/Makefile` 的 `check-review-regressions` 加入
   `python3 ./test_bk7258_dvp_register_errors.py --sdk $(OPENVELA_ROOT)/vendor/beken/bk_avdk_smp`。
   固定值更新前加入会使该目标按预期失败。
5. 在 `SOURCE_PROVENANCE.md` 记录 fork 提交、文件与 Apache-2.0。
6. 反汇编新 bundle，确认 `gc2145_read_register` 返回 I2C 结果；构建 AIDK
   镜像。当前产品没有寄存器控制入口，实板只能回归 `bkvision snapshot`/`record`；
   重新接入 V4L2 控制时须另做读写故障实板验收。

## 未覆盖

其他 SDK 传感器回调（如 GC0308、OV2640）及 GC2145 初始化表中的
`SENSOR_I2C_WRITE` 同样忽略 I2C 结果；它们不经过上述寄存器 API，未纳入本修正。
