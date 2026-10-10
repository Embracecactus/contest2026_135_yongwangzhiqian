# 按需电脑单帧取景合同

电脑明确发起一帧请求，经独立 `camera` PC 授权进入现有 V4L2 服务。相机不常开，
不调用视觉问答/云端，不写 SD、不自动保存电脑原图。原视觉问答继续使用同一
capture owner。此能力不是 UVC，也没有声称 USB 取景帧率等于传感器帧率。

单请求槽；等待/采集最多 8 s，成功帧最多留存 120 s。缓冲按需分配，上限
102400 B，另复用驱动的一帧 V4L2 MMAP；驱动/SDK峰值另测。无新增线程或计时器，
现有 vision worker 执行采集，产品循环负责期限/准入。查询不触发采集、重扫或
挂载；元数据锁外执行分配、采集和释放。状态读取 80 B，帧分块每次 16 B，
带请求身份和总长校验；传完或取消必须释放。电脑仅保留当前预览一帧。

`CCQ1` 32 B：magic4、action BE32（1 开始，2 取消/释放）、当前 id BE64、
非零 nonce16。开始仅接受当前 id，分配递增新 id，禁止覆盖尚未释放的帧。
不自动重试开始。`CCS1` 80 B：magic4、phase4、id8、nonce16、完成时间8、
bytes/width/height/format 各4、error4、flags4、fps/sequence/elapsed/remaining
各4。phase：0空闲、1待采、2采集中、3有效帧、4失败、5取消、6过期。
flags：admitted=1、valid=2。时间是设备 monotonic ms，无墙钟或识别语义。
相机控制/状态使用 CONFIG kind **23**，帧读取继续使用 **22**；kind 21 保留给
已经发布的回答长度偏好。712 的早期相机客户端使用 kind 21，不能与本候选混用；
须使用匹配工具，读取不支持时明确失败，不回退重放到回答偏好接口。
`camera-frame` 读取携带 id8、offset4、expected-size4；错身份、过期、取消、
越界一律拒绝。TLS 和最终元数据身份复核防止拼接不同请求。

实际格式、宽高、帧序号由成功 V4L2 回执取得；当前配置请求 JPEG 640×480、
传感器 30 fps，服务校验实际格式/帧间隔。USB 是有界单帧轮询，实际耗时单独
测量。捕获失败不提供旧图，不编造视觉识别结果。

真实监听/播报、内容播放、资源安装、OTA、关机及授权退出时不准入新采集；
撤销/断开取消活动请求并丢弃缓存。采集中的取消在帧等待的 5 ms 检查点生效，
驱动 I/O/关闭时延不冒充硬上界。关机沿用 vision quiesce，资源未释放不报告
完成。默认 HIL 只存非敏感格式、时间、长度、错误统计。

## 使用入口

沿用电脑配对流程，在 `workbench pair-start --allow camera ...` 中明确请求
相机权限，由原生 App 的“允许这台电脑连接？”对话框显示并授权。原有
resources/scenes/tasks/diagnostics 不自动包含 camera；旧 profile 需要合法重新
授权，不能读取手机 Keystore 或使用工程绕过。

运行既有本机工作台：

```sh
python3 tools/bk7258/bk7258.py workbench serve --port NATIVE_CDC_PORT \
  --profile pc.profile --workbench-dir new-camera-session
```

页面“按需单帧取景”提供采一帧、取消采集/传输、读取状态及释放缓存。
新请求先清除上次预览；有效图像解码显示后只留在本机页面内存。新一次相机
操作丢弃服务的上一帧，导出诊断不含图像。退出工作台会请求取消并关闭会话。

CLI `workbench camera-capture` 默认仅返回格式、长度、设备单调时间、传输耗时、
哈希和释放结果，不打印图片或保存 JPEG。`camera-status` 不会触发采样，
`camera-cancel` 明确释放当前缓存。一次采集/传输始终复用同一个授权连接；
缺权限、断线、超时或读取身份变化均不会自动补采。合法授权缺席仅暂停实板
USB 正向验收，主机生产协议/状态机、原生配置入口及 V4L2 后端可分层验证。
