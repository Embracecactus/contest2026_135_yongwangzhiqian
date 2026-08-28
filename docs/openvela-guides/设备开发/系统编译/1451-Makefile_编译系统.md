# Makefile 编译系统

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1451&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:29  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/build/Makefile_guide.md) | 简体中文 \]

# 一、概述

在当前版本中，openvela 使用 Makefile 文件组织编译流程。编译的总入口位于 <span class="reference">nuttx/Makefile</span> 文件，根据编译主机平台的不同，分支执行核心编译文件：

  - Windows 平台执行 <span class="reference">nuttx/tools/Win.mk</span>。
  - Unix 类平台执行 <span class="reference">nuttx/tools/Unix.mk</span>。

<span class="reference">nuttx/tools/</span> 目录包含编译过程中所需的必要脚本和 C 程序。

## 1、关键文件和配置

除了核心编译文件外，编译 openvela 时还需要以下关键文件和配置：

1.  板级构建宏定义与构建选项文件。
    
      - 文件位置：<span class="reference">nuttx/Make.defs</span>。
      - 来源：从模板文件 <span class="reference">nuttx/board/${arch}/${chip}/${board}/${config}/scripts/Make.defs</span> 拷贝而来。

2.  条件编译配置文件。
    
      - 文件位置：根目录的 <span class="reference">configs/defconfig</span>。
      - 功能：被拷贝为 <span class="reference">.config</span> 文件，作为 openvela 的基础配置文件，支持高度裁剪和模块化配置。
      - 实现方式：通过各模块目录中的 <span class="reference">kernel Kconfig</span> 实现模块化配置。

## 2、编译流程关键点

1.  配置编译主机。
    
    使用 <span class="reference">nuttx/tools/configure.sh</span> 脚本选择编译主机的配置。

2.  关键文件包含。
    
    在 <span class="reference">Make.defs</span> 文件中，包含以下两个关键文件，这些文件会被传递给各阶段的 Makefile：
    
      - <span class="reference">nuttx/.config</span>：构建配置文件。
      - <span class="reference">nuttx/tools/Config.mk</span>：通用宏定义文件。

3.  文件生成与调用。
    
      - 文件生成
        
          - 在 <span class="reference">nuttx/</span> 和 <span class="reference">apps/</span> 的各级子目录中，<span class="reference">Makefile</span>、<span class="reference">Make.defs</span> 和 <span class="reference">Make.dep</span> 文件会在 <span class="reference">Makefile</span> 执行的各阶段中被调用或生成。
          - <span class="reference">Make.dep</span>：由工具 <span class="reference">tools/mkdep</span> 在编译过程中生成，其内部使用 <span class="reference">gcc -M</span> 命令生成符合 <span class="reference">Makefile</span> 构建目标格式的依赖语句。
    
      - 文件调用
        
          - 各级子目录的 <span class="reference">Makefile</span> 文件会在文件头部 <span class="reference">include</span> 板级构建宏配置文件 <span class="reference">nuttx/Make.defs</span>。

通过这种组织方式，openvela 实现了灵活的编译流程，支持多平台构建和高度模块化配置。

# 二、构建依赖树

以下内容是基于 **FlatMode sim:nsh** 板子配置的编译过程，归纳出编译目标的依赖树。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005730966_001.svg)

## 1、依赖树概述

依赖树中的节点表示构建过程中的各个目标概览。大部分目标下执行的具体命令未在图中展示，详细的执行指令将在下一节的关键目标介绍中说明。

## 2、依赖树解析方式

  - 依赖关系解析：<span class="reference">Makefile</span> 根据依赖关系，从子节点的构建目标开始执行。
  - 叶子节点标注：依赖树中虚线框标注的部分表示 <span class="reference">Makefile</span> 叶子节点目标的执行动作概述。
  - 高效执行：通过这种依赖树结构，<span class="reference">Makefile</span> 能够高效地解析和执行构建目标，确保编译过程的有序性和模块化。

# 三、关键目标

## 1、<span class="reference">context</span> 目标

<span class="reference">context</span> 目标用于确定构建目标的上下文环境，主要完成以下任务：

  - 生成关键的配置头文件：<span class="reference">config.h</span> 和 <span class="reference">version.h</span>。
  - 在每个构建目标内执行 <span class="reference">make context</span>，以确认下级构建环境的正确性。
  - 建立部分配置目录的符号链接（<span class="reference">ln</span>），供后续 <span class="reference">Makefile</span> 执行时定位。

Makefile 示例：  

    # context
    #
    # The context target is invoked on each target build to assure that NuttX is
    # properly configured.  The basic configuration steps include creation of the
    # the config.h and version.h header files in the include/nuttx directory and
    # the establishment of symbolic links to configured directories.

  

    ## tools/Unix.mk 核心Makefile文件
    
    %.context: include/nuttx/config.h .dirlinks
            $(Q) $(MAKE) -C $(patsubst %.context,%,$@) TOPDIR="$(TOPDIR)" context
            $(Q) touch $@

特殊情况：<span class="reference">apps</span> 目录中的 <span class="reference">context</span>。

  - 在大多数目录中，<span class="reference">context</span> 目标没有额外动作。
  - 在关键的 <span class="reference">apps</span> 目录中，<span class="reference">context</span> 目标会执行 <span class="reference">register-all</span>，该目标会在配置了 <span class="reference">buildin</span> 的 app 目录下执行 <span class="reference">register</span> 目标。
  - 该过程会生成 <span class="reference">.bdat</span> 和 <span class="reference">.pdat</span> 文件，这些文件用于最终生成 <span class="reference">buildin</span> 库的头文件。

## 2、<span class="reference">tools/mkdep</span> 目标

<span class="reference">tools/mkdep</span> 是构建系统中的一个关键目标，用于生成编译文件的依赖配置。它通过调用 <span class="reference">mkdeps</span> 工具，在 <span class="reference">depend</span> 阶段自动生成符合 <span class="reference">Makefile</span> 语法的依赖文件。

功能描述：

  - <span class="reference">mkdeps</span> 是由 <span class="reference">tools/mkdeps.c</span> 编译生成的工具。
  - 它使用 <span class="reference">gcc -M</span> 命令生成依赖信息，并输出为 <span class="reference">Makefile</span> 格式。
  - 各级子目录的 <span class="reference">Make.dep</span> 文件由 <span class="reference">mkdeps</span> 工具在 <span class="reference">depend</span> 目标下自动生成，并在后续构建过程中被引入。

Makefile 示例：

以下是 <span class="reference">tools/mkdep</span> 的核心 Makefile 配置。  

    ## tools/Unix.mk 核心Makefile文件
    
    tools/mkdeps$(HOSTEXEEXT):
            $(Q) $(MAKE) -C tools -f Makefile.host mkdeps$(HOSTEXEEXT)

## 3、<span class="reference">pass2dep</span> 目标

<span class="reference">pass2dep</span> 目标使用 <span class="reference">mkdep</span> 工具在所有编译目录中生成依赖文件。它通过遍历配置的目录，调用 <span class="reference">depend</span> 目标生成依赖文件，并确保所有依赖关系正确。

功能描述：

  - <span class="reference">pass2dep</span> 目标依赖于 <span class="reference">context</span> 和 <span class="reference">tools/mkdeps</span>。
  - 它会遍历所有内核依赖目录（<span class="reference">KERNDEPDIRS</span>），并在每个目录中执行 <span class="reference">depend</span> 目标。
  - 生成的依赖文件会被自动引入到后续的构建过程中。

Makefile 示例：

以下是 <span class="reference">pass2dep</span> 的核心 Makefile 配置：  

    ## tools/Unix.mk 核心Makefile文件
    pass2dep: context tools/mkdeps$(HOSTEXEEXT) tools/cnvwindeps$(HOSTEXEEXT)
            $(Q) for dir in $(KERNDEPDIRS) ; do \
                    $(MAKE) -C $$dir EXTRAFLAGS="$(KDEFINE) $(EXTRAFLAGS)" depend || exit; \
            done

### 以 <span class="reference">apps</span> 为例的依赖生成

在 <span class="reference">apps</span> 目录中，<span class="reference">pass2dep</span> 目标会生成依赖文件，并将其引入到构建过程中。

**<span class="reference">apps</span> 目录的 Makefile 配置**

以下是 <span class="reference">nuttx-apps/Makefile</span> 中的依赖生成逻辑：  

    ## nuttx-apps/Makefile文件 
    
    .depdirs: $(foreach SDIR, $(CONFIGURED_APPS), $(SDIR)_depend)
    
    .depend: Makefile .depdirs
            $(Q) touch $@
    
    depend: .depend

**在配置的 app 内执行**

每个 app 的依赖生成逻辑定义在 <span class="reference">Application.mk</span> 文件中。以下是具体的 Makefile 配置：  

    ## nuttx-apps/Application.mk文件，也就是每个app编译执行的Makefile
    
    .depend: Makefile $(wildcard $(foreach SRC, $(SRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH))))) $(DEPCONFIG)
            $(Q) $(MKDEP) $(DEPPATH) --obj-suffix .c$(SUFFIX)$(OBJEXT) "$(CC)" -- $(CFLAGS) -- $(filter %.c,$^) >Make.dep
            $(Q) $(MKDEP) $(DEPPATH) --obj-suffix .S$(SUFFIX)$(OBJEXT) "$(CC)" -- $(CFLAGS) -- $(filter %.S,$^) >>Make.dep
            $(Q) $(MKDEP) $(DEPPATH) --obj-suffix $(CXXEXT)$(SUFFIX)$(OBJEXT) "$(CXX)" -- $(CXXFLAGS) -- $(filter %$(CXXEXT),$^) >>Make.dep
            $(Q) touch $@
    
    depend:: .depend

**最终效果**

  - 生成的 <span class="reference">Make.dep</span> 文件会被 <span class="reference">Application.mk</span> 自动引入到编译过程中。
  - 该过程确保了每个 app 的依赖关系正确无误。

## 4、静态库（lib.a）的生成与作用

在构建系统中，静态库（<span class="reference">lib.a</span>）是模块化构建的核心部分。每个模块（如 <span class="reference">sched</span> 或 <span class="reference">apps</span>）都会生成一个对应的静态库文件，这些库文件在最终链接阶段被整合到可执行文件中。以下内容详细描述了静态库的生成过程及其在构建流程中的作用。

### 静态库的生成：以 <span class="reference">sched</span> 为例

<span class="reference">sched</span> 模块的静态库生成逻辑定义在 <span class="reference">sched/</span> 目录下的 <span class="reference">Makefile</span> 文件中。以下是其主要功能和配置。

**功能描述**

  - 将汇编源文件（<span class="reference">.S</span>）和 C 源文件（<span class="reference">.c</span>）分别编译为目标文件（<span class="reference">.o</span>）。
  - 将所有目标文件打包为静态库文件（<span class="reference">libsched.a</span>）。
  - 使用通用的构建规则（如 <span class="reference">ASSEMBLE</span> 和 <span class="reference">COMPILE</span>）简化编译过程。

**Makefile 示例**

在顶层 <span class="reference">tools/LibTargets.mk</span> 文件中，定义了 <span class="reference">sched</span> 模块的构建目标和安装规则：  

    # tools/LibTargets.mk文件
    
    sched$(DELIM)libsched$(LIBEXT): pass2dep
            $(Q) $(MAKE) -C sched libsched$(LIBEXT) EXTRAFLAGS="$(KDEFINE) $(EXTRAFLAGS)"
    
    staging$(DELIM)libsched$(LIBEXT): sched$(DELIM)libsched$(LIBEXT)
            $(Q) $(call INSTALL_LIB,$<,$@)

在 <span class="reference">sched/</span> 目录下的 <span class="reference">Makefile</span> 文件中，定义了静态库的生成逻辑：  

    ## sched/目录下 Makefile文件
    
    # 定义目标文件  
    AOBJS = $(ASRCS:.S=$(OBJEXT))
    COBJS = $(CSRCS:.c=$(OBJEXT))
    
    # 定义源文件和最终目标  
    SRCS = $(ASRCS) $(CSRCS)
    OBJS = $(AOBJS) $(COBJS)
    BIN = libsched$(LIBEXT)
    
    # 默认目标  
    all: $(BIN)
    
    # 汇编文件编译规则  
    $(AOBJS): %$(OBJEXT): %.S
            $(call ASSEMBLE, $<, $@)
    
    # C 文件编译规则  
    $(COBJS): %$(OBJEXT): %.c
            $(call COMPILE, $<, $@)
    
    # 静态库生成规则  
    $(BIN): $(OBJS)
            $(call ARCHIVE, $@, $(OBJS))

关键点：

1.  目标文件生成：
    
      - 汇编文件（<span class="reference">.S</span>）通过 <span class="reference">ASSEMBLE</span> 规则生成目标文件。
      - C 文件（<span class="reference">.c</span>）通过 <span class="reference">COMPILE</span> 规则生成目标文件。

2.  静态库打包：
    
      - 使用 <span class="reference">ARCHIVE</span> 规则将所有目标文件打包为静态库文件（<span class="reference">libsched.a</span>）。

3.  模块化构建：
    
      - 每个模块独立生成静态库，便于后续的链接和管理。

### 静态库的生成：以 <span class="reference">apps</span> 为例

<span class="reference">apps</span> 模块的静态库生成逻辑定义在 <span class="reference">nuttx-apps/Makefile</span> 文件中。与 <span class="reference">sched</span> 不同，<span class="reference">apps</span> 模块需要分别编译每个配置的应用程序，并最终将它们链接为一个统一的静态库（<span class="reference">libapps.a</span>）。

**功能描述**

  - 遍历所有配置的应用程序目录（<span class="reference">CONFIGURED\_APPS</span>）。
  - 在每个应用程序目录中执行 <span class="reference">archive</span> 目标，生成对应的目标文件。
  - 将所有目标文件链接为一个静态库文件（<span class="reference">libapps.a</span>）。

**Makefile 示例**

以下是 <span class="reference">nuttx-apps/Makefile</span> 文件中定义的静态库生成规则：  

    ## nuttx-apps/Makefile
    
    ## BIN为libapps.a 目标
    $(BIN): $(foreach SDIR, $(CONFIGURED_APPS), $(SDIR)_all)
            $(Q) for app in ${CONFIGURED_APPS}; do \
                    $(MAKE) -C "$${app}" archive ; \
            done

关键点：

1.  遍历应用程序目录：
    
      - 使用 <span class="reference">foreach</span> 遍历所有配置的应用程序目录（<span class="reference">CONFIGURED\_APPS</span>）。
      - 每个应用程序目录会被单独处理，确保模块化构建。

2.  独立编译：
    
      - 在每个应用程序目录中执行 <span class="reference">archive</span> 目标，生成对应的目标文件。
      - 每个应用程序的目标文件会被单独编译，便于后续的统一打包。

3.  统一打包：
    
      - 将所有应用程序的目标文件链接为一个静态库文件（<span class="reference">libapps.a</span>）。
      - 统一的静态库文件便于后续的链接和管理。

### 应用程序目录的编译规则

在每个应用程序目录中，<span class="reference">archive</span> 目标的实现逻辑定义在 <span class="reference">nuttx-apps/Application.mk</span> 文件中。以下是其主要功能和配置。

**Makefile 示例**

    ## nuttx-apps/Application.mk文件，也就是每个app编译执行的Makefile
    
    ## 各OBJ的生成又调用了ELFCOMPILE
    OBJS = $(RAOBJS) $(CAOBJS) $(COBJS) $(CXXOBJS) $(RUSTOBJS) $(ZIGOBJS)
    all:: $(OBJS)
    
    archive:
            $(call ARCHIVE_ADD, $(call CONVERT_
            PATH,$(BIN)), $(OBJS))

关键点解析

1.  目标文件生成：
    
      - 各种语言的源文件（如 C、C++、Rust、Zig）会被分别编译为目标文件（<span class="reference">OBJS</span>）。
      - 目标文件的生成规则由构建系统自动处理。

2.  静态库打包：
    
      - <span class="reference">archive</span> 目标调用了 <span class="reference">ARCHIVE\_ADD</span> 宏，将目标文件添加到静态库中。

3.  宏定义：
    
      - <span class="reference">ARCHIVE\_ADD</span> 宏定义在 <span class="reference">Config.mk</span> 文件中，具体实现如下：  
        
            ## Config.mk 文件  
            
            define ARCHIVE_ADD  
                    $(ECHO_BEGIN)"AR (add): ${shell basename $(1)} "  
                    $(Q) $(AR) $1 $2  
                    $(ECHO_END)  
            endef
    
    该宏使用 <span class="reference">ar</span> 工具将目标文件添加到静态库中，并输出构建日志。

## 5、<span class="reference">nuttx</span> 二进制文件的生成

在构建系统中，<span class="reference">${BIN}</span> 是最终生成的可执行文件 <span class="reference">nuttx</span> 的占位符。<span class="reference">nuttx</span> 是构建流程的核心输出，包含了所有模块的链接结果。以下内容详细描述了 <span class="reference">nuttx</span> 的生成过程，包括不同架构下的链接规则和后续的二进制文件生成。

### 构建流程概述

在所有叶子节点依赖目标（如静态库 <span class="reference">lib</span>）生成完成后，构建系统会执行最终的链接操作，生成 <span class="reference">nuttx</span> 可执行文件。以 <span class="reference">flatbuild</span> 为例，核心命令如下：  

    ## tools/Unix.mk 核心Makefile文件
    $(Q) $(MAKE) -C $(ARCH_SRC) EXTRA_OBJS="$(EXTRA_OBJS)" LINKLIBS="$(LINKLIBS)" APPDIR="$(APPDIR)" EXTRAFLAGS="$(KDEFINE) $(EXTRAFLAGS)" $(BIN)

  - <span class="reference">${ARCH\_SRC}</span>：根据 CPU 架构，指向 <span class="reference">nuttx/arch/\*\*/src</span> 目录。
  - <span class="reference">${BIN}</span>：最终生成的可执行文件 <span class="reference">nuttx</span>。

### 不同架构下的链接规则

**<span class="reference">sim</span> 架构**

在 <span class="reference">sim</span> 架构下，链接规则定义在 <span class="reference">arch/sim/src/Makefile</span> 文件中。以下是主要的链接逻辑：  

    ## arch/sim/src 下Makfefile文件
    
    nuttx$(EXEEXT): libarch$(LIBEXT) board/libboard$(LIBEXT) $(HEADOBJ) $(LINKOBJS) $(HOSTOBJS) nuttx-names.dat
            $(Q) echo "LD:  nuttx$(EXEEXT)"
            $(Q) $(LD) -r $(LDLINKFLAGS) $(RELPATHS) $(EXTRA_LIBPATHS) $(EXTRALINKCMDS) \
                 -o nuttx.rel $(REQUIREDOBJS) $(LDSTARTGROUP) $(RELLIBS) $(EXTRA_LIBS) $(LDENDGROUP)
    ifneq ($(CONFIG_HOST_MACOS),y)
            $(Q) $(OBJCOPY) --redefine-syms=nuttx-names.dat nuttx.rel
            $(Q) $(CC) $(CFLAGS) -Wl,-verbose 2>&1 | \
                 sed -e '/====/,/====/!d;//d' -e 's/__executable_start/_stext/g' \
                     -e 's/^\(\s\+\)\(\.init_array\)/\1\2 : { }\n\1.sinit/g' \
                     -e 's/^\(\s\+\)\(\.fini_array\)/\1\2 : { }\n\1.einit/g' \
                     -e 's/__init_array_start/_sinit/g' -e 's/__init_array_end/_einit/g' \
                     -e 's/__fini_array_start/_sfini/g' -e 's/__fini_array_end/_efini/g' >nuttx.ld
            $(Q) echo "__init_array_start = .; __init_array_end = .; __fini_array_start = .; __fini_array_end = .;" >>nuttx.ld
    endif
    ifneq ($(CONFIG_ALLSYMS),y)
            $(if $(CONFIG_HAVE_CXX),\
            $(Q) "$(CXX)" $(CFLAGS) $(LDFLAGS) -o $(TOPDIR)/$@ $(HEADOBJ) nuttx.rel $(HOSTOBJS) $(STDLIBS),\
            $(Q) "$(CC)" $(CFLAGS) $(LDFLAGS) -o $(TOPDIR)/$@ $(HEADOBJ) nuttx.rel $(HOSTOBJS) $(STDLIBS))
    else
            $(Q) # Link and generate default table
            $(Q) $(if $(wildcard $(shell echo $(NUTTX))),,$(call LINK_ALLSYMS, $@))
            $(Q) # Extract all symbols
            $(Q) $(call LINK_ALLSYMS, $^)
            $(Q) # Extract again since the table offset may changed
            $(Q) $(call LINK_ALLSYMS, $^)
    endif
            $(Q) $(NM) $(TOPDIR)/$@ | \
                    grep -v '\(compiled\)|\(\.o$$\)|\( [aUw] \)|\(\.\.ng$$\)|\(LASH[RL]DI\)' | \
                    sort > $(TOPDIR)/System.map

关键点：

  - 中间文件生成： 生成 <span class="reference">nuttx.rel</span> 文件作为中间链接结果。
  - 符号表处理： 使用 <span class="reference">OBJCOPY</span> 和 <span class="reference">sed</span> 修改符号表，确保符号名称符合要求。
  - 最终链接： 根据是否启用 <span class="reference">CONFIG\_ALLSYMS</span>，选择不同的链接流程。
  - 生成符号表文件： 使用 <span class="reference">nm</span> 工具生成 <span class="reference">System.map</span> 文件。

**<span class="reference">arm</span> 架构**

在 <span class="reference">arm</span> 架构下，链接规则定义在 <span class="reference">arch/arm/src/Makefile</span> 文件中。以下是主要的链接逻辑：  

    ## arch/arm/src 下Makfefile文件
    
    nuttx$(EXEEXT): $(HEAD_OBJ) board$(DELIM)libboard$(LIBEXT) $(addsuffix .tmp,$(ARCHSCRIPT))
            $(Q) echo "LD: nuttx"
    ifneq ($(CONFIG_ALLSYMS),y)
            $(Q) $(LD) --entry=__start $(LDFLAGS) $(LIBPATHS) $(EXTRA_LIBPATHS) \
                    -o $(NUTTX) $(HEAD_OBJ) $(EXTRA_OBJS) \
                    $(LDSTARTGROUP) $(LDLIBS) $(EXTRA_LIBS) $(LDENDGROUP)
    else
            $(Q) # Link and generate default table
            $(Q) $(if $(wildcard $(shell echo $(NUTTX))),,$(call LINK_ALLSYMS,$^))
            $(Q) # Extract all symbols
            $(Q) $(call LINK_ALLSYMS, $^)
            $(Q) # Extract again since the table offset may changed
            $(Q) $(call LINK_ALLSYMS, $^)
    endif
    ifneq ($(CONFIG_WINDOWS_NATIVE),y)
            $(Q) $(NM) $(NUTTX) | \
            grep -v '\(compiled\)|\(\$(OBJEXT)$$\)|\( [aUw] \)|\(\.\.ng$$\)|\(LASH[RL]DI\)' | \
            sort > $(TOPDIR)$(DELIM)System.map
    endif
            $(Q) $(call DELFILE, $(addsuffix .tmp,$(ARCHSCRIPT)))

关键点：

  - 入口点设置： 使用 <span class="reference">--entry=\_\_start</span> 指定程序入口点。
  - 符号表生成： 在非 Windows 平台上，生成 <span class="reference">System.map</span> 文件。
  - 清理临时文件： 删除中间生成的 <span class="reference">.tmp</span> 文件。

### 二进制文件的生成

在生成 <span class="reference">nuttx</span> 可执行文件后，根据需求生成不同格式的二进制文件（如 <span class="reference">.bin</span>、<span class="reference">.hex</span>、<span class="reference">.srec</span> 等）。这些文件的生成逻辑定义在 <span class="reference">tools/Unix.mk</span> 文件中。  

    ## tools/Unix.mk 核心Makefile文件
    
    ifeq ($(CONFIG_INTELHEX_BINARY),y)
            @echo "CP: nuttx.hex"
            $(Q) $(OBJCOPY) $(OBJCOPYARGS) -O ihex $(BIN) nuttx.hex
            $(Q) echo nuttx.hex >> nuttx.manifest
    endif
    ifeq ($(CONFIG_MOTOROLA_SREC),y)
            @echo "CP: nuttx.srec"
            $(Q) $(OBJCOPY) $(OBJCOPYARGS) -O srec $(BIN) nuttx.srec
            $(Q) echo nuttx.srec >> nuttx.manifest
    endif
    ifeq ($(CONFIG_RAW_BINARY),y)
            @echo "CP: nuttx.bin"
            $(Q) $(OBJCOPY) $(OBJCOPYARGS) -O binary $(BIN) nuttx.bin
            $(Q) echo nuttx.bin >> nuttx.manifest
    endif
    ifeq ($(CONFIG_UBOOT_UIMAGE),y)
            @echo "MKIMAGE: uImage"
            $(Q) mkimage -A $(CONFIG_ARCH) -O linux -C none -T kernel -a $(CONFIG_UIMAGE_LOAD_ADDRESS) \
                    -e $(CONFIG_UIMAGE_ENTRY_POINT) -n $(BIN) -d nuttx.bin uImage
            $(Q) if [ -w /tftpboot ] ; then \
                    cp -f uImage /tftpboot/uImage; \
            fi
            $(Q) echo "uImage" >> nuttx.manifest
    endif
            $(call POSTBUILD, $(TOPDIR))

关键点：

  - 多格式支持： 根据配置选项生成 <span class="reference">.hex</span>、<span class="reference">.srec</span>、<span class="reference">.bin</span> 和 <span class="reference">uImage</span> 文件。
  - U-Boot 镜像生成： 使用 <span class="reference">mkimage</span> 工具生成 <span class="reference">uImage</span> 文件，并支持自动复制到 TFTP 目录。
  - 构建后处理： 调用 <span class="reference">POSTBUILD</span> 钩子执行额外的构建后处理操作。

至此构建完成。
