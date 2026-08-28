# 快速入门（Ubuntu）

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1426&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:15  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/quickstart/openvela_ubuntu_quick_start.md) | 简体中文 \]

本指南将指导您在 **Ubuntu 22.04** 操作系统上完成 openvela 的开发环境准备、源代码下载、编译构建，并最终通过 Vela Emulator 运行编译产物。

> **环境要求**
> 
> 本文仅适配 **Ubuntu 22.04**。不支持在 Windows Subsystem for Linux (WSL) 或 Docker 容器环境中进行编译。
> 
> **AI 辅助搭建（可选）**
> 
> 如果您使用 AI 编程助手（如 [Claude Code](https://docs.anthropic.com/en/docs/claude-code)），可以通过 openvela AI Skills 自动完成以下全部搭建流程：
> 
>     > git clone https://github.com/open-vela/.claude.git .claude
>     >
> 
> 然后告诉 AI 助手："帮我搭建 openvela 开发环境"。
> 
> AI 将自动完成环境检测、依赖安装、代码源选择、源码下载、编译和模拟器启动，并在遇到问题时提供针对性的解决方案。
> 
> 如需手动搭建，请继续阅读以下步骤。

# 步骤一：准备工作

在开始之前，请确保您的开发环境满足以下要求。

## 1\. 硬件要求

  - **硬盘：** 至少 40 GB 可用空间，用于存放源代码和编译产物。
  - **内存：** 至少 16 GB RAM。

## 2\. 操作系统要求

  - **操作系统：** Ubuntu 22.04 (arm64/x86\_64)

## 3\. 安装开发工具

在开始之前，您需要安装编译 openvela 所需的软件包。

打开终端，执行以下命令，更新软件包列表并安装 Git、curl、CMake、Python 3、libc++abi-dev 和 build-essential 工具链。  

    sudo apt update
    sudo apt install git curl cmake python3 libc++abi-dev build-essential

## 4\. 安装 Git LFS 组件

> **说明**：本项目包含大体积的二进制文件（如模型权重、数据集）。请务必配置 **Git LFS**，**否则拉取的文件将损坏（仅显示为几 KB 的指针文本）而无法运行**。

请在 Ubuntu 终端中执行以下命令进行安装和初始化：  

    # 第一步：配置官方源并安装 (确保获取最新版)
    curl -s https://packagecloud.io/install/repositories/github/git-lfs/script.deb.sh | sudo bash
    sudo apt-get install git-lfs
    
    # 第二步：初始化配置 (重要：必须执行此步，否则 LFS 不会生效)
    git lfs install

# 步骤二：下载源代码

openvela 使用 <span class="reference">repo</span> 工具管理其分布在多个 Git 仓库中的源代码。

## 1\. 安装 Repo 工具

<span class="reference">repo</span> 是一个构建于 Git 之上的代码库管理工具。执行以下命令来安全地下载并安装它。  

    curl -sSL "https://storage.googleapis.com/git-repo-downloads/repo" > repo
    chmod +x repo
    sudo mv repo /usr/local/bin

安装完成后，可运行 <span class="reference">repo --version</span> 进行验证。

## 2\. 初始化并同步代码库

1.  创建一个工作目录，用于存放 openvela 的所有源代码。  
    
        mkdir openvela && cd openvela

2.  使用 <span class="reference">repo</span> 初始化项目清单，并指定 <span class="reference">dev</span> 分支。
    
    请根据您的网络环境和偏好，从以下任一平台选择一种方式（推荐使用 SSH）来初始化仓库。
    
    ### 选项 A：从 GitHub 下载
    
      - 方式一：SSH（推荐）
        
        此方式需要您先将 SSH 公钥添加至您的 GitHub 账户，请参考 [GitHub 官方文档](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/adding-a-new-ssh-key-to-your-github-account)。  
        
            repo init -u ssh://git@github.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs
    
      - 方式二：HTTPS  
        
            repo init -u https://github.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs
    
    ### 选项 B：从 Gitee 下载
    
      - 方式一：SSH (推荐)
        
        此方式需要您先将 SSH 公钥添加至您的 Gitee 账户，请参考 [Gitee 官方文档](https://gitee.com/help/articles/4191)。  
        
            repo init -u ssh://git@gitee.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs
    
      - 方式二：HTTPS  
        
            repo init -u https://gitee.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs
    
    ### 选项 C：从 GitCode 下载
    
      - 方式一：SSH (推荐)
        
        此方式需要您先将 SSH 公钥添加至您的 GitCode 账户，请参考 [GitCode 官方文档](https://docs.gitcode.com/docs/help/home/user_center/security_management/ssh)。  
        
            repo init -u ssh://git@gitcode.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs
    
      - 方式二：HTTPS  
        
            repo init -u https://gitcode.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ --git-lfs

3.  执行同步命令，repo 将根据清单文件 (<span class="reference">openvela.xml</span>) 下载所有相关的源代码仓库。  
    
        repo sync -c -j8
    
    ![alt text](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005715643_004.png)
    
    > **操作提示**
    > 
    >   - 首次同步耗时较长，具体时间取决于您的网络状况和磁盘性能。
    >   - 若因网络问题中断，可重复执行 <span class="reference">repo sync</span> 进行增量同步。

# 步骤三：编译源代码

完成源代码下载后，请在 openvela 根目录下执行以下编译步骤。

## 1\. （可选）自定义内核配置

您可以通过 <span class="reference">menuconfig</span> 命令打开图形化界面，以调整 NuttX 内核与组件的配置。  

    ./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap/ --cmake menuconfig

> **操作技巧**
> 
>   - 按 <span class="reference">/</span> 键可搜索配置项。
>   - 按 <span class="reference">空格键</span> 可切换选中状态（启用/禁用/模块化）。
>   - 配置完成后，选择 **Save** 保存并退出。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005715738_006.png)

## 2\. 执行编译

执行以下命令，构建整个项目。  

    ./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap/ --cmake -j$(nproc)

编译成功后，您将在 <span class="reference">cmake\_out/vela\_goldfish-arm64-v8a-ap</span> 目录下找到 <span class="reference">nuttx</span> 等编译产物。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005715836_007.png)

# 步骤四：运行模拟器

在 openvela 根目录下，执行以下脚本启动 <span class="reference">Vela Emulator</span> 并加载您的编译产物。  

    ./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap/

模拟器启动后，您将看到 <span class="reference">goldfish-armv8a-ap\></span> 提示符，表明 openvela 已成功运行。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005715940_008.png)

# 后续步骤

  - 常见问题
    
      - [快速入门常见问题](https://doc.openvela.com/document?id=1428&version=dev-ai-contest-2026&language=cn)
      - [开发者常见问题解答](https://doc.openvela.com/document?id=1739&version=dev-ai-contest-2026&language=cn)

  - 进一步阅读
    
      - [使用模拟器调试](https://doc.openvela.com/document?id=1431&version=dev-ai-contest-2026&language=cn)
      - [ADB 命令](https://doc.openvela.com/document?id=1430&version=dev-ai-contest-2026&language=cn)
      - [发送模拟器控制台命令](https://doc.openvela.com/document?id=1432&version=dev-ai-contest-2026&language=cn)
