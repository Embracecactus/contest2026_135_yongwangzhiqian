# 电脑工作台相机预览 CSP 修复验证

日期：2026-10-18。

## 问题与改动

工作台用 JPEG data URL 显示明确请求的单帧，但 HTTP CSP 未设置 `img-src`，
图片继承 `default-src 'none'` 而被浏览器拒绝。正式页面加载受控 JPEG 夹具时，
状态显示“有效单帧”，实际图片自然宽高为 0，并产生 `img-src / data` 违规。
原有图片内联缩放样式也被 `style-src 'self'` 拒绝。

本次仅增加 `img-src data:`，并将图片的 `max-width:100%;height:auto` 移到
既有样式表。补充 HTTP 响应头断言，约束图片来源和默认、脚本、连接策略。
没有开放远程图片或内联脚本/样式，没有修改固件、设备协议或授权流程。

## 发布候选的验证

发布分支从上游 `dev-ai-contest-2026` 的
`e59ac2bb1c33ad5dd501515428278c97995fea57` 建立，再快进到已发布的相机依赖
`260fb85a6f6a2bb2a5d18a70a6953da80a2254bd`（PR #133，包含 PR #132）。
本次新增提交只包含上述修复、测试与本记录；没有复制集成工作区的其他修改。

- 构建既有 `build/test_display_job_control` 及其夹具后，执行
  `python3 tests/host/bk7258/test_workbench_web.py`：11 项通过，覆盖 HTTP 到
  TLS 与原生安装器的既有测试。初次缺少测试程序的失败输出保留在本地。
- Black 24.10.0 检查两个 Python 修改文件通过；`git diff --check` 通过。
- Chrome for Testing 151.0.7922.34 加载发布候选的正式 HTTP 服务、页面、
  CSS 和 JS，仅设备结果使用受控 640×480 JPEG。图片实际解码、显示；在
  1280/375 px 视口分别显示宽 462/278 px，文档宽 1265/360 px，无横向溢出，
  CSP 违规数均为 0。浏览器和服务已关闭。

## 已有集成版本实板证据与边界

在提取本次补丁前，相同修复已在包含其他未发布修改的集成工作区完成实板验证：
Windows Chrome 155.0.8059.80 通过正式页面按钮、HTTP 服务、现有 USB TLS
设备会话完成首次取景、主动取消和再次取景。

- 两次新 JPEG 分别为 15247/15055 B，浏览器均解码并显示为 640×480；
  设备缓存释放确认成功。读取已释放状态后，页面清除旧图。
- 中间取消任务返回 canceled，释放确认成功，没有残留预览。
- 1280/375 px 视口显示宽度与发布候选结果一致，最终 CSP 违规数为 0。
- 该集成版本的工作台 HTTP 测试为 12 项，全部通过；比发布依赖基线多出的
  无关测试没有带入本次提交。

这些结果证明本次浏览器修复在集成版本中走通实板链路；不代表本发布分支的
完整固件已经重新烧录验证。本次发布准备未再操作板子。未保存或上传相机帧，
未宣称画面内容或画质已获人工验收；实板会话、服务和浏览器均已关闭。

## 本地证据

证据目录：工作区 `out/shaniu-workbench-preview-20261018/`。记录包含
`browser-red.json`、`browser-green.json`、`browser-hil-r2-results.json`、
`publish-browser.json`、`publish-native-build.log`、`publish-http-tests-r3.log`
及对应失败日志。原始日志和设备身份材料不随源码发布。

实板浏览器结果文件 SHA256：
`f69108e0e265be8fc8e682e4bf883fe8afd7b31cccc99fad8cd1922d46434d06`。
