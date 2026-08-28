# Feature Context API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1720&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:50  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/feature/feature_framework_context.md) | 简体中文 \]

# Feature Context API

Feature 框架提供的统一数据类型与上下文操作接口。通过 <span class="reference">ft\_value\_t</span> 封装前端（QuickJS、WAMR 等）的原生对象，开发者无需感知具体前端差异即可完成类型转换、数组对象操作与内存管理。

头文件：<span class="reference">\#include \<feature\_context.h\></span>

# openvela 实现说明

  - **核心数据类型 <span class="reference">ft\_value\_t</span>**：统一封装前端的 JSValue/Wasm 对象，根据目标平台是否 64 位使用 16 字节或 8 字节存储
  - **<span class="reference">ft\_context\_ref</span>**：伴随 <span class="reference">ft\_value\_t</span> 的上下文对象，所有 API 都需要传入，用于定位具体的前端 Runtime
  - **值类型 vs 引用类型**：
      - <span class="reference">ft\_from\_int</span> / <span class="reference">ft\_from\_bool</span> 等返回值类型，无需显式释放
      - <span class="reference">ft\_from\_string</span> / <span class="reference">ft\_from\_buffer</span> / <span class="reference">ft\_new\_object</span> 等返回引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放
  - **释放规则**：
      - **无需释放**：Feature 实现函数入参、作为返回值传回前端的 <span class="reference">ft\_value\_t</span>
      - **必须释放**：<span class="reference">ft\_from\_xxx</span> 创建的对象、<span class="reference">ft\_new\_object</span> 新建对象、<span class="reference">ft\_array\_at</span> 取出的元素、<span class="reference">ft\_obj\_get\_property</span> 返回的属性、<span class="reference">ft\_parse\_json</span> 解析结果
      - 字符串：<span class="reference">ft\_to\_string</span> 返回的 <span class="reference">const char\*</span> 必须用 <span class="reference">ft\_free\_string</span> 释放

# Feature Context 与前端运行时的关系

下图展示了 Feature 框架如何通过 <span class="reference">ft\_value\_t</span> 与 <span class="reference">ft\_context\_ref</span> 统一封装前端运行时（以 QuickJS 为例）的原生对象：

![Feature Context 与前端运行时的关系](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005811490_ft_context.svg)

  - **Feature Framework Interface**：Feature 框架对外提供的统一 C 接口，由 <span class="reference">ft\_value\_t</span>（数据）与 <span class="reference">ft\_context\_ref</span>（上下文）两部分组成。
  - **JS Implementation**：具体前端运行时的实现。<span class="reference">ft\_value\_t</span> 背后对应 <span class="reference">JSValue</span>，<span class="reference">ft\_context\_ref</span> 背后对应 <span class="reference">JSContext</span>，两者之间是 N:1 的关系（多个值归属于同一上下文）。
  - 切换其他前端（如 WAMR）时，Feature 实现侧的代码不需要修改，只需替换底层映射关系。

# 类型与上下文访问

## ft\_context\_get\_data

    void* ft_context_get_data(ft_context_ref ft_ctx);

获取当前 Feature 上下文关联的用户数据指针。该用户数据由 Feature 管理器在初始化阶段绑定。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。

**返回值**：

返回关联的用户数据指针，若未绑定则返回 <span class="reference">NULL</span>。

## ft\_get\_type

    ft_type ft_get_type(ft_context_ref ft_ctx, ft_value_t ft_val);

获取给定 <span class="reference">ft\_value\_t</span> 的类型。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">ft\_val</span> 待查询类型的 <span class="reference">ft\_value\_t</span>。

**返回值**：

返回值类型枚举 <span class="reference">ft\_type</span>：

  - <span class="reference">FT\_TYPE\_NULL</span>：null
  - <span class="reference">FT\_TYPE\_UNDEF</span>：undefined
  - <span class="reference">FT\_TYPE\_NONE</span>：未定义值
  - <span class="reference">FT\_TYPE\_NUMBER</span>：数值
  - <span class="reference">FT\_TYPE\_BOOL</span>：布尔
  - <span class="reference">FT\_TYPE\_STRING</span>：字符串
  - <span class="reference">FT\_TYPE\_ARRAY</span>：数组
  - <span class="reference">FT\_TYPE\_BUFFER</span>：二进制缓冲区
  - <span class="reference">FT\_TYPE\_TYPED\_BUFFER</span>：类型化缓冲区
  - <span class="reference">FT\_TYPE\_OBJECT</span>：对象

## ft\_undefined

    ft_value_t ft_undefined(ft_context_ref ft_ctx);

构造 undefined 类型的 <span class="reference">ft\_value\_t</span>。用于向前端返回"无值"结果。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。

**返回值**：

返回一个类型为 <span class="reference">FT\_TYPE\_UNDEF</span> 的 <span class="reference">ft\_value\_t</span>。该值为值类型，无需显式释放。

# 基本类型转换（Native → ft\_value\_t）

以下接口将 C 原生类型转换为 <span class="reference">ft\_value\_t</span>，便于传递给前端。

## ft\_from\_int

    ft_value_t ft_from_int(ft_context_ref ft_ctx, int32_t val);

将 32 位有符号整数转换为 <span class="reference">ft\_value\_t</span>。返回值为值类型，无需释放。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 32 位有符号整数值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_NUMBER</span>）。

## ft\_from\_uint

    ft_value_t ft_from_uint(ft_context_ref ft_ctx, uint32_t val);

将 32 位无符号整数转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 32 位无符号整数值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_int64

    ft_value_t ft_from_int64(ft_context_ref ft_ctx, int64_t val);

将 64 位有符号整数转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 64 位有符号整数值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_uint64

    ft_value_t ft_from_uint64(ft_context_ref ft_ctx, uint64_t val);

将 64 位无符号整数转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 64 位无符号整数值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_double

    ft_value_t ft_from_double(ft_context_ref ft_ctx, double val);

将双精度浮点数转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 双精度浮点值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_bool

    ft_value_t ft_from_bool(ft_context_ref ft_ctx, bool val);

将布尔值转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 布尔值。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_BOOL</span>）。

## ft\_from\_string

    ft_value_t ft_from_string(ft_context_ref ft_ctx, const char* val);

将 C 字符串转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 以 <span class="reference">\\0</span> 结尾的 C 字符串。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_STRING</span>）。

**注意**：

  - 返回的 <span class="reference">ft\_value\_t</span> 为引用类型，**必须**通过 <span class="reference">ft\_free\_value</span> 释放。
  - 实现会拷贝 <span class="reference">val</span> 的内容，调用后原指针可以立即释放。

# 二进制缓冲区转换

## ft\_from\_buffer

    ft_value_t ft_from_buffer(ft_context_ref ft_ctx, uint8_t* buff, uint32_t size);

将 Native 字节缓冲区封装为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">buff</span> 字节缓冲区起始指针。
  - <span class="reference">size</span> 缓冲区字节数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_BUFFER</span>）。

**注意**：

  - 返回的 <span class="reference">ft\_value\_t</span> 为引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放。

## ft\_from\_typed\_buffer

    ft_value_t ft_from_typed_buffer(ft_context_ref ft_ctx, uint8_t* buff,
                                    uint32_t size, FtTypedArrayType type);

将 Native 缓冲区封装为前端的类型化数组（Typed Array）。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">buff</span> 字节缓冲区起始指针。
  - <span class="reference">size</span> 缓冲区字节数。
  - <span class="reference">type</span> 类型化数组的元素类型，详见 <span class="reference">FtTypedArrayType</span>：
      - <span class="reference">FT\_Int8Array</span> / <span class="reference">FT\_Uint8Array</span>
      - <span class="reference">FT\_Int16Array</span> / <span class="reference">FT\_Uint16Array</span>
      - <span class="reference">FT\_Int32Array</span> / <span class="reference">FT\_Uint32Array</span>
      - <span class="reference">FT\_Float32Array</span> / <span class="reference">FT\_Float64Array</span>

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_TYPED\_BUFFER</span>）。必须通过 <span class="reference">ft\_free\_value</span> 释放。

**示例**：  

    uint8_t* buff = ft_to_buffer(ft_ctx, &size, data);
    // 对 buff 做处理
    ft_value_t ret = ft_from_typed_buffer(ft_ctx, buff, size, FT_Uint8Array);

## ft\_parse\_json

    ft_value_t ft_parse_json(ft_context_ref ft_ctx, const char* buf,
                             size_t buf_len, const char* filename);

解析 JSON 字符串为 <span class="reference">ft\_value\_t</span> 对象。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">buf</span> JSON 字符串指针。
  - <span class="reference">buf\_len</span> JSON 字符串长度（字节）。
  - <span class="reference">filename</span> 用于错误信息定位的文件名，可传 <span class="reference">NULL</span>。

**返回值**：

成功时返回解析得到的 <span class="reference">ft\_value\_t</span>；失败时返回类型为 <span class="reference">FT\_TYPE\_UNDEF</span> 的值。

**注意**：

  - 返回的 <span class="reference">ft\_value\_t</span> 为引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放。

# 数组类型转换（Native 数组 → ft\_value\_t）

将 Native 数组转换为 <span class="reference">ft\_value\_t</span> 数组。所有返回值均为引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放。

## ft\_from\_int\_array

    ft_value_t ft_from_int_array(ft_context_ref ft_ctx, int32_t* val, uint32_t size);

将 int32 数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_ARRAY</span>）。

## ft\_from\_uint\_array

    ft_value_t ft_from_uint_array(ft_context_ref ft_ctx, uint32_t* val, uint32_t size);

将 uint32 数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_int64\_array

    ft_value_t ft_from_int64_array(ft_context_ref ft_ctx, int64_t* val, uint32_t size);

将 int64 数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_uint64\_array

    ft_value_t ft_from_uint64_array(ft_context_ref ft_ctx, uint64_t* val, uint32_t size);

将 uint64 数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_bool\_array

    ft_value_t ft_from_bool_array(ft_context_ref ft_ctx, bool* val, uint32_t size);

将布尔数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_double\_array

    ft_value_t ft_from_double_array(ft_context_ref ft_ctx, double* val, uint32_t size);

将 double 数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 源数组指针。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

## ft\_from\_string\_array

    ft_value_t ft_from_string_array(ft_context_ref ft_ctx, const char** val, uint32_t size);

将 C 字符串数组转换为 <span class="reference">ft\_value\_t</span>。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">val</span> 字符串指针数组。
  - <span class="reference">size</span> 数组元素个数。

**返回值**：

返回对应的 <span class="reference">ft\_value\_t</span>。

# 基本类型转换（ft\_value\_t → Native）

以下接口将前端传入的 <span class="reference">ft\_value\_t</span> 转换为 C 原生类型，便于 Feature 实现使用。

## ft\_to\_int

    bool ft_to_int(ft_context_ref ft_ctx, ft_value_t f_val, int32_t* val);

将 <span class="reference">ft\_value\_t</span> 转换为 32 位有符号整数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>，应为数值类型。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">int32\_t\*</span>。

**返回值**：

转换成功时返回 <span class="reference">true</span>，否则返回 <span class="reference">false</span>（通常因为 <span class="reference">f\_val</span> 不是数值类型）。

## ft\_to\_uint

    bool ft_to_uint(ft_context_ref ft_ctx, ft_value_t f_val, uint32_t* val);

将 <span class="reference">ft\_value\_t</span> 转换为 32 位无符号整数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">uint32\_t\*</span>。

**返回值**：

成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

## ft\_to\_int64

    bool ft_to_int64(ft_context_ref ft_ctx, ft_value_t f_val, int64_t* val);

将 <span class="reference">ft\_value\_t</span> 转换为 64 位有符号整数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">int64\_t\*</span>。

**返回值**：

成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

## ft\_to\_uint64

    bool ft_to_uint64(ft_context_ref ft_ctx, ft_value_t f_val, uint64_t* val);

将 <span class="reference">ft\_value\_t</span> 转换为 64 位无符号整数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">uint64\_t\*</span>。

**返回值**：

成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

## ft\_to\_double

    bool ft_to_double(ft_context_ref ft_ctx, ft_value_t f_val, double* val);

将 <span class="reference">ft\_value\_t</span> 转换为双精度浮点数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">double\*</span>。

**返回值**：

成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

## ft\_to\_bool

    bool ft_to_bool(ft_context_ref ft_ctx, ft_value_t ft_val, bool* val);

将 <span class="reference">ft\_value\_t</span> 转换为布尔值。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">ft\_val</span> 源 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">val</span> 用于接收结果的 <span class="reference">bool\*</span>。

**返回值**：

成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

## ft\_to\_string

    const char* ft_to_string(ft_context_ref ft_ctx, ft_value_t f_val);

将 <span class="reference">ft\_value\_t</span> 转换为 C 字符串。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>，应为字符串类型。

**返回值**：

成功时返回字符串指针；失败时返回 <span class="reference">NULL</span>。

**注意**：

  - 返回的字符串由框架管理，**必须**通过 <span class="reference">ft\_free\_string</span> 释放。
  - Feature 框架不保证该字符串长期有效，如需保留应及时拷贝。

## ft\_to\_buffer

    uint8_t* ft_to_buffer(ft_context_ref ft_ctx, size_t* p_size, ft_value_t f_val);

将 <span class="reference">ft\_value\_t</span> 转换为二进制缓冲区。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">p\_size</span> 输出参数，返回缓冲区字节数。
  - <span class="reference">f\_val</span> 源 <span class="reference">ft\_value\_t</span>，应为 <span class="reference">FT\_TYPE\_BUFFER</span> 或 <span class="reference">FT\_TYPE\_TYPED\_BUFFER</span>。

**返回值**：

成功时返回缓冲区起始指针；失败时返回 <span class="reference">NULL</span>。

**注意**：

  - 返回的指针由前端管理，不要手动 <span class="reference">free</span>。

# 数组操作

## ft\_array\_size

    uint32_t ft_array_size(ft_context_ref ft_ctx, const ft_value_t array);

获取 <span class="reference">ft\_value\_t</span> 数组的元素数量。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">array</span> 数组类型的 <span class="reference">ft\_value\_t</span>。

**返回值**：

返回数组元素个数。若 <span class="reference">array</span> 不是数组类型，返回 0。

## ft\_array\_at

    ft_value_t ft_array_at(ft_context_ref ft_ctx, const ft_value_t array, uint32_t idx);

按索引访问 <span class="reference">ft\_value\_t</span> 数组中的元素。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">array</span> 数组类型的 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">idx</span> 元素索引，从 0 开始。

**返回值**：

成功时返回索引位置的元素 <span class="reference">ft\_value\_t</span>；越界或 <span class="reference">array</span> 非数组时返回 undefined。

**注意**：

  - 返回的 <span class="reference">ft\_value\_t</span> 为引用类型，**必须**通过 <span class="reference">ft\_free\_value</span> 释放。

# 对象操作

## ft\_new\_object

    ft_value_t ft_new_object(ft_context_ref ft_ctx);

创建一个空的 <span class="reference">ft\_value\_t</span> 对象。Feature 可以向该对象挂载自定义属性。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。

**返回值**：

返回新建的对象 <span class="reference">ft\_value\_t</span>（类型为 <span class="reference">FT\_TYPE\_OBJECT</span>）。

**注意**：

  - 返回值为引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放。
  - 对象支持挂载子属性，释放根对象时子属性会被自动释放。

## ft\_obj\_get\_property

    ft_value_t ft_obj_get_property(ft_context_ref ft_ctx, ft_value_t ft_val, const char* prop);

按属性名读取 <span class="reference">ft\_value\_t</span> 对象的属性值。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">ft\_val</span> 对象类型的 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">prop</span> 属性名。

**返回值**：

成功时返回属性值的 <span class="reference">ft\_value\_t</span>；属性不存在时返回 undefined。

**注意**：

  - 返回的 <span class="reference">ft\_value\_t</span> 为引用类型，必须通过 <span class="reference">ft\_free\_value</span> 释放。

## ft\_obj\_set\_property

    bool ft_obj_set_property(ft_context_ref ft_ctx, ft_value_t obj,
                             const char* prop, ft_value_t val);

为 <span class="reference">ft\_value\_t</span> 对象设置属性值。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">obj</span> 目标对象 <span class="reference">ft\_value\_t</span>。
  - <span class="reference">prop</span> 属性名。
  - <span class="reference">val</span> 要设置的属性值。

**返回值**：

设置成功返回 <span class="reference">true</span>，失败返回 <span class="reference">false</span>。

# 内存管理

## ft\_free\_value

    void ft_free_value(ft_context_ref ft_ctx, ft_value_t ft_val);

释放 <span class="reference">ft\_value\_t</span> 引用计数。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">ft\_val</span> 待释放的 <span class="reference">ft\_value\_t</span>。

**注意**：

**需要释放**的 <span class="reference">ft\_value\_t</span> 来源：

  - <span class="reference">ft\_from\_xxx</span> 创建的对象
  - <span class="reference">ft\_new\_object</span> 新建的对象
  - <span class="reference">ft\_array\_at</span> 取出的元素
  - <span class="reference">ft\_obj\_get\_property</span> 返回的属性
  - <span class="reference">ft\_parse\_json</span> 解析得到的对象

**不需要释放**的 <span class="reference">ft\_value\_t</span>：

  - Feature 实现函数收到的入参
  - 作为返回值传回前端的 <span class="reference">ft\_value\_t</span>

未正确释放引用类型的 <span class="reference">ft\_value\_t</span> 会导致内存泄漏。

## ft\_free\_string

    void ft_free_string(ft_context_ref ft_ctx, const char* str);

释放由 <span class="reference">ft\_to\_string</span> 返回的字符串。

**参数**：

  - <span class="reference">ft\_ctx</span> 当前 Feature 上下文引用。
  - <span class="reference">str</span> 待释放的字符串指针。

**注意**：

  - Feature 框架不保证 <span class="reference">ft\_to\_string</span> 返回的字符串长期有效，使用完毕后必须及时调用本接口释放。
  - 不要使用 <span class="reference">free()</span> 或 <span class="reference">delete</span> 释放，必须使用本接口。
