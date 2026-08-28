# 执行 CoreMark 基准测试

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1637&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:02  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/debugging_tools/performance/Benchmark/coremark.md) | 简体中文 \]

# 一、概述

CoreMark 是一款专为测量嵌入式系统中央处理器 (CPU) 性能而设计的行业标准基准测试。它由 EEMBC (Embedded Microprocessor Benchmark Consortium) 的 Shay Gal-on 于 2009 年开发，旨在提供一个比 Dhrystone 更真实、更全面的性能评估标准。

该基准测试完全由 C 语言编写，其工作负载主要模拟了 CPU 在实际应用中常见的操作，包含以下几种核心算法：

  - **列表处理 (List Processing):** 对链表进行查找、排序、插入和删除操作。
  - **矩阵操作 (Matrix Manipulation):** 执行常见的矩阵乘法等运算。
  - **状态机 (State Machine):** 通过状态转换来处理输入数据流。
  - **循环冗余校验 (CRC):** 计算数据的校验和，以验证前述算法结果的正确性。

# 二、启用功能

您可以通过以下 Kconfig 配置项来启用 CoreMark 功能：  

    CONFIG_BENCHMARK_COREMARK=y

# 三、执行测试

CoreMark 可用于评估单核及多核处理器的核心性能，启用该功能并编译固件后，可直接在 openvela 的 shell 中运行测试。  

    ap> coremark
    2K performance run parameters for coremark.
    CoreMark Size    : 666
    Total ticks      : 207740
    Total time (secs): 20.774000
    Iterations/Sec   : 529.508039
    Iterations       : 11000
    Compiler version : GCC11.3.1 20220712
    Compiler flags   : -Wstrict-prototypes -nostdlib -pipe -O3 -fno-strict-aliasing -fomit-frame-pointer -mthumb -Wa,-mthumb -Wa,-mimplicit-it=always -fno-common -Wall -Wshadow -x
    Memory location  : Please put data memory location here
                            (e.g. code in flash, data on heap, etc)
    seedcrc          : 0xe9f5
    [0]crclist       : 0xe714
    [0]crcmatrix     : 0x1fd7
    [0]crcstate      : 0x8e3a
    [0]crcfinal      : 0x33ff
    Correct operation validated. See README.md for run and reporting rules.
    CoreMark 1.0 : 529.508039 / GCC11.3.1 20220712 -Wstrict-prototypes -nostdlib -pipe -O3 -fno-strict-aliasing -fomit-frame-pointer -mthumb -Wa,-mthumb -Wa,-mimplicit-it=always p

# 四、解读测试结果

命令执行后会输出详细的性能数据和校验信息。下表对关键输出参数进行了解释：

| **参数 (Parameter)**                                | **说明 (Description)**                    | **示例值 (Example Value)**                                        |
| :------------------------------------------------ | :-------------------------------------- | :------------------------------------------------------------- |
| <span class="reference">Run Type</span>           | 测试的运行类型和参数。                             | <span class="reference">2K performance run...</span>           |
| <span class="reference">CoreMark Size</span>      | 测试使用的数据缓冲区大小。                           | <span class="reference">666</span>                             |
| <span class="reference">Total ticks</span>        | 完成所有迭代所消耗的系统时钟节拍（ticks）总数。              | <span class="reference">207740</span>                          |
| <span class="reference">Total time (secs)</span>  | 完成测试的实际总耗时，单位为秒。                        | <span class="reference">20.774000</span>                       |
| **<span class="reference">Iterations/Sec</span>** | **核心性能得分。该数值是衡量 CPU 性能的关键指标，越高表示性能越强。** | **<span class="reference">529.508039</span>**                  |
| <span class="reference">Iterations</span>         | 测试执行的总迭代次数。                             | <span class="reference">11000</span>                           |
| <span class="reference">Compiler version</span>   | 用于编译测试代码的编译器版本。                         | <span class="reference">GCC11.3.1 20220712</span>              |
| <span class="reference">Compiler flags</span>     | 编译和链接时使用的标志，这些标志会显著影响最终性能得分。            | <span class="reference">-O3 -fno-strict-aliasing...</span>     |
| <span class="reference">seedcrc</span>            | 用于三组 CRC 计算的初始种子值。                      | <span class="reference">0xe9f5</span>                          |
| <span class="reference">\[0\]crclist</span>       | 列表处理算法的 CRC 校验和，用于验证结果正确性。              | <span class="reference">0xe714</span>                          |
| <span class="reference">\[0\]crcmatrix</span>     | 矩阵操作算法的 CRC 校验和。                        | <span class="reference">0x1fd7</span>                          |
| <span class="reference">\[0\]crcstate</span>      | 状态机算法的 CRC 校验和。                         | <span class="reference">0x8e3a</span>                          |
| <span class="reference">\[0\]crcfinal</span>      | 综合三次迭代后的最终 CRC 校验和，用于确保测试的有效性。          | <span class="reference">0x33ff</span>                          |
| <span class="reference">Final Score</span>        | 最终得分的紧凑格式总结，附加了编译器版本和标志等环境信息。           | <span class="reference">CoreMark 1.0 : 529.508039 / ...</span> |

# 五、参考资料

  - [CoreMark 官方网站 (EEMBC)](https://www.eembc.org/coremark/)
  - [CoreMark GitHub 仓库](https://github.com/eembc/coremark)
