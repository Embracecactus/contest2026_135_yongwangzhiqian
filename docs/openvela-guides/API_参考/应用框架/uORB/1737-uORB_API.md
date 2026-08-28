# uORB API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1737&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:58  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/uorb.md) | 简体中文 \]

# uORB API

uORB 是 openvela 的发布/订阅消息总线，用于进程或线程间的异步数据通信。

头文件：<span class="reference">\#include \<uORB/uORB.h\></span>

# openvela 实现说明

  - **配置依赖**：需要启用 <span class="reference">CONFIG\_USENSOR</span> 和 <span class="reference">CONFIG\_UORB</span>
  - **跨核通信**：启用 <span class="reference">CONFIG\_SENSORS\_RPMSG</span> 后支持 topic 跨核传输
  - **内置传感器**：提供 34 种预定义传感器 topic（加速度计、陀螺仪、GPS 等）
  - **自定义 topic**：通过 <span class="reference">ORB\_DECLARE</span>、<span class="reference">ORB\_DEFINE</span>、<span class="reference">ORB\_ID</span> 宏定义
  - **调试工具**：<span class="reference">uorb listener</span> 工具可监听 topic 数据（需 <span class="reference">CONFIG\_DEBUG\_UORB</span>）

# 设备管理

## orb\_open

    int orb_open(const char *name, int instance, int flags);

打开指定名称和实例的 topic 设备节点.

## orb\_close

    int orb_close(int fd);

关闭文件描述符。

## orb\_unlink\_multi

    int orb_unlink_multi(const struct orb_metadata *meta, int instance);

移除 topic 节点。

# 发布接口

## orb\_advertise\_multi\_queue\_info

    int orb_advertise_multi_queue_info(const struct orb_metadata *meta, const void *data, int *instance, unsigned int queue_size, orb_info_t *info);

执行 topic 的初始广播（发布者注册），在 <span class="reference">/dev/uorb</span> 下创建对应的 topic 节点并发布初始数据。

## orb\_advertise\_multi\_queue\_persist

    int orb_advertise_multi_queue_persist_info(const struct orb_metadata *meta, const void *data, int *instance, unsigned int queue_size, orb_info_t *info);

<span class="reference">orb\_advertise\_multi\_queue\_persist</span> 类似于 <span class="reference">orb\_advertise\_multi\_queue</span>，但会保证所有订阅者（包括未来新增的）都能访问到当前及后续发布的数据。

## orb\_unadvertise

    static inline int orb_unadvertise(int fd) { return orb_close(fd);

取消 topic 广播。

## orb\_publish\_multi

    ssize_t orb_publish_multi(int fd, const void *data, size_t len);

向 topic 发布指定长度的新数据。数据发布后，所有正在等待的订阅者会被唤醒；未等待的订阅者可通过 <span class="reference">orb\_check</span> 检查 topic 是否有更新。

## orb\_advertise

    static inline int orb_advertise(const struct orb_metadata *meta, const void *data);

发布一个 topic 的 advertiser。等价于 <span class="reference">orb\_advertise\_multi(meta, data, NULL)</span>，创建默认实例 0。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">data</span> 初始发布的数据（可为 <span class="reference">NULL</span>）。

**返回值**：

成功时返回 advertiser 的文件描述符，失败时返回负值并设置 <span class="reference">errno</span>。

## orb\_advertise\_multi

    static inline int orb_advertise_multi(const struct orb_metadata *meta,
                                          const void *data, int *instance);

发布带实例 ID 的 topic advertiser，用于多实例 topic 的场景。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">data</span> 初始发布数据。
  - <span class="reference">instance</span> 输入输出参数，返回新创建的实例 ID。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_advertise\_queue

    static inline int orb_advertise_queue(const struct orb_metadata *meta,
                                          const void *data, unsigned int queue_size);

发布带队列的 topic advertiser。队列长度大于 1 时支持缓存多条历史数据。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">data</span> 初始数据。
  - <span class="reference">queue\_size</span> 队列深度。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_advertise\_multi\_queue

    static inline int orb_advertise_multi_queue(const struct orb_metadata *meta,
                                                const void *data, int *instance,
                                                unsigned int queue_size);

同时指定实例 ID 和队列深度的 advertiser。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">data</span> 初始数据。
  - <span class="reference">instance</span> 输入输出参数，返回实例 ID。
  - <span class="reference">queue\_size</span> 队列深度。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_advertise\_multi\_queue\_persist\_info

    int orb_advertise_multi_queue_persist_info(const struct orb_metadata *meta,
                                               const void *data, int *instance,
                                               unsigned int queue_size,
                                               orb_info_t *info);

带持久化信息字段的 advertiser。<span class="reference">info</span> 参数用于传递额外的 topic 元信息。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">data</span> 初始数据。
  - <span class="reference">instance</span> 输入输出参数，返回实例 ID。
  - <span class="reference">queue\_size</span> 队列深度。
  - <span class="reference">info</span> topic 附加信息指针。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_publish

    static inline int orb_publish(const struct orb_metadata *meta, int fd, const void *data);

发布一个 topic 数据（默认长度，从 meta 获取）。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">fd</span> advertiser 文件描述符。
  - <span class="reference">data</span> 要发布的数据。

**返回值**：

成功时返回发布的字节数，失败时返回负值。

## orb\_publish\_auto

    static inline int orb_publish_auto(const struct orb_metadata *meta, int *fd,
                                       const void *data, int *instance);

自动 advertise + publish。若 <span class="reference">\*fd</span> 小于 0，会自动创建 advertiser。便于简单场景的快速发布。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">fd</span> 输入输出参数，advertiser 文件描述符。
  - <span class="reference">data</span> 要发布的数据。
  - <span class="reference">instance</span> 输入输出参数，返回实例 ID。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

# 订阅接口

## orb\_subscribe\_multi

    int orb_subscribe_multi(const struct orb_metadata *meta, unsigned instance);

以非唤醒方式订阅 topic。数据发布后，等待中的订阅者会被唤醒；未等待的订阅者可通过 <span class="reference">orb\_check</span> 检查更新。

如果订阅发生在 publish 之后，订阅成功后立刻调用 <span class="reference">orb\_check</span> 将返回 <span class="reference">true</span>。即使 topic 尚未 advertise，订阅也会成功——此时 topic 的时间戳为 0、不会触发 poll 事件、<span class="reference">orb\_check</span> 始终返回 <span class="reference">false</span>、也无法被 copy，直到后续 topic 被 advertise 为止。

## orb\_unsubscribe

    static inline int orb_unsubscribe(int fd) { return orb_close(fd);

取消订阅 topic。

## orb\_copy\_multi

    ssize_t orb_copy_multi(int fd, void *buffer, size_t len);

从 topic 获取指定长度的数据。这是**唯一**会重置"订阅者收到新数据"标记位的接口——一旦 <span class="reference">poll</span> 或 <span class="reference">orb\_check</span> 表明有更新，必须调用本接口完成消费。

## orb\_check

    int orb_check(int fd, bool *updated);

检查 topic 自上次 <span class="reference">orb\_copy</span> 后是否有新数据发布。用于不使用 <span class="reference">poll()</span> 的场景下判断是否需要调用 <span class="reference">orb\_copy</span>；也可避免在 topic 很可能已更新时仍要调用 <span class="reference">poll()</span> 的开销。更新状态按 fd 维度跟踪：返回 <span class="reference">true</span> 后，在同一 fd 上必须调用 <span class="reference">orb\_copy</span> 才会重置更新标记，否则本接口会持续返回 <span class="reference">true</span>。

## orb\_subscribe

    static inline int orb_subscribe(const struct orb_metadata *meta);

订阅 topic 的默认实例。等价于 <span class="reference">orb\_subscribe\_multi(meta, 0)</span>。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。

**返回值**：

成功时返回订阅者文件描述符，失败时返回负值。

## orb\_subscribe\_wakeup

    static inline int orb_subscribe_wakeup(const struct orb_metadata *meta);

订阅默认实例并启用 wakeup 模式（有新数据时异步唤醒调用者）。等价于 <span class="reference">orb\_subscribe\_multi\_wakeup(meta, 0)</span>。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_subscribe\_multi\_wakeup

    int orb_subscribe_multi_wakeup(const struct orb_metadata *meta, unsigned instance);

订阅指定实例 ID 的 topic 并启用 wakeup 模式。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">instance</span> 实例 ID。

**返回值**：

成功时返回文件描述符，失败时返回负值。

## orb\_copy

    static inline int orb_copy(const struct orb_metadata *meta, int fd, void *buffer);

从订阅者 fd 拷贝最新 topic 数据到 buffer，使用默认长度（从 meta 获取）。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。
  - <span class="reference">fd</span> 订阅者文件描述符。
  - <span class="reference">buffer</span> 接收数据的缓冲区。

**返回值**：

成功时返回拷贝的字节数，失败时返回负值。

## orb\_unlink

    static inline int orb_unlink(const struct orb_metadata *meta);

删除 topic 的默认实例。等价于 <span class="reference">orb\_unlink\_multi(meta, 0)</span>。

**参数**：

  - <span class="reference">meta</span> topic 元数据指针。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

# 控制接口

## orb\_get\_state

    int orb_get_state(int fd, struct orb_state *state);

获取 topic 所有订阅者的状态信息。该状态包含所有订阅者中的最大频率和最小批处理间隔，以及 <span class="reference">enable</span> 字段（指示当前节点是否已订阅或激活）。若当前无任何订阅者，状态字段将被置为：<span class="reference">max\_frequency=0</span>、<span class="reference">min\_batch\_interval=0</span>、<span class="reference">enable=false</span>。

## orb\_get\_events

    int orb_get_events(int fd, unsigned int *events);

获取指定订阅者的事件信息.

## orb\_ioctl

    int orb_ioctl(int fd, int cmd, unsigned long arg);

订阅者的 ioctl 控制, 与 ioctl 相同().

## orb\_flush

    int orb_flush(int fd);

刷新硬件缓冲区中累积的 topic 数据。当硬件 FIFO 未达 watermark 但希望立即读取时，调用本接口可强制把 FIFO 中的数据吐出；调用时机无限制。调用后可通过监听 fd 的 <span class="reference">POLLPRI</span> 事件，并用 <span class="reference">orb\_get\_events</span> 获取事件以判断 flush 是否完成。

## orb\_set\_batch\_interval

    int orb_set_batch_interval(int fd, unsigned batch_interval);

设置用户期望的批处理间隔，最终生效值取决于硬件 FIFO 能力。本接口会触发 <span class="reference">POLLPRI</span> 事件通知发布者，由发布者决定最终的批处理间隔。仅适用于具备硬件 FIFO 的 topic（如带硬件 FIFO 的传感器），否则调用无意义。

## orb\_get\_batch\_interval

    int orb_get_batch_interval(int fd, unsigned *batch_interval);

获取批处理模式下当前生效的批处理间隔。仅适用于具备硬件 FIFO 的 topic（如带硬件 FIFO 的传感器），否则调用无意义。参见 <span class="reference">orb\_set\_batch\_interval</span>。

## orb\_set\_interval

    int orb_set_interval(int fd, unsigned interval);

设置订阅者的最小上报间隔（单位：微秒）。

**参数**：

  - <span class="reference">fd</span> 订阅者文件描述符。
  - <span class="reference">interval</span> 上报间隔，单位微秒，<span class="reference">0</span> 表示无限制。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

## orb\_get\_interval

    int orb_get_interval(int fd, unsigned *interval);

获取订阅者当前的上报间隔（微秒）。

**参数**：

  - <span class="reference">fd</span> 订阅者文件描述符。
  - <span class="reference">interval</span> 输出参数，返回当前间隔值。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

## orb\_set\_frequency

    static inline int orb_set_frequency(int fd, unsigned frequency);

按频率（Hz）设置订阅者的上报间隔。等价于 <span class="reference">orb\_set\_interval(fd, frequency ? 1000000/frequency : 0)</span>。

**参数**：

  - <span class="reference">fd</span> 订阅者文件描述符。
  - <span class="reference">frequency</span> 目标频率（Hz），<span class="reference">0</span> 表示无限制。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

## orb\_get\_frequency

    static inline int orb_get_frequency(int fd, unsigned *frequency);

按频率（Hz）获取订阅者的当前上报速率。

**参数**：

  - <span class="reference">fd</span> 订阅者文件描述符。
  - <span class="reference">frequency</span> 输出参数，返回当前频率。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负值。

## orb\_get\_info

    int orb_get_info(int fd, orb_info_t *info);

获取 topic 信息。

## orb\_info

    void orb_info(const char *format, const char *name, const void *data);

打印传感器数据。

# 查询接口

## orb\_elapsed\_time

    orb_abstime orb_absolute_time(void);

获取当前系统时间（微秒）。

## orb\_exists

    int orb_exists(const struct orb_metadata *meta, int instance);

检查 topic 实例是否已被广播.

## orb\_group\_count

    int orb_group_count(const struct orb_metadata *meta);

获取已广播的 topic 实例数量。

## orb\_absolute\_time

    orb_abstime orb_absolute_time(void);

获取单调递增的绝对时间戳（单位：微秒），用于 topic 数据的时间戳标记。

**返回值**：

返回当前时间戳。

## orb\_get\_meta

    const struct orb_metadata *orb_get_meta(const char *name);

通过名称字符串获取 topic 元数据。

# 格式化 I/O

## orb\_sscanf

    int orb_sscanf(const char *buf, const char *format, void *data);

将字符串值转换为结构体缓冲区。

## orb\_fprintf

    int orb_fprintf(FILE *stream, const char *format, const void *data);

将传感器数据打印到文件。

# 事件循环

## orb\_loop\_init

    int orb_loop_init(struct orb_loop_s *loop, enum orb_loop_type_e type);

初始化 orb 事件循环, 使用 orb\_loop\_deinit 释放 function.

## orb\_loop\_run

    int orb_loop_run(struct orb_loop_s *loop);

启动事件循环。循环启动后，用户可以通过 <span class="reference">orb\_handle\_start</span> 动态添加新的文件描述符，也可通过 <span class="reference">orb\_handle\_stop</span> 关闭已添加的 fd。循环启动后会进入阻塞状态。

## orb\_loop\_deinit

    int orb_loop_deinit(struct orb_loop_s *loop);

注销当前事件循环。注销后如需再次使用必须重新初始化。通过 <span class="reference">orb\_handle\_init</span> 添加到循环中的 handle 由用户负责关闭。

## orb\_loop\_exit\_async

    int orb_loop_exit_async(struct orb_loop_s *loop);

向当前事件循环发送退出事件(不等待).

## orb\_handle\_init

    int orb_handle_init(struct orb_handle_s *handle, int fd, int events, void *arg, orb_datain_cb_t datain_cb, orb_dataout_cb_t dataout_cb, orb_eventpri_cb_t pri_cb, orb_eventerr_cb_t err_cb);

初始化 orb 句柄。

## orb\_handle\_start

    int orb_handle_start(struct orb_loop_s *loop, struct orb_handle_s *handle);

从事件循环中启动句柄。

## orb\_handle\_stop

    int orb_handle_stop(struct orb_loop_s *loop, struct orb_handle_s *handle);

从事件循环中停止句柄。
