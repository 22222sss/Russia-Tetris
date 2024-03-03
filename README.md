# Russia-Tetris（多人在线字符版俄罗斯方块）



## 项目简介（Project Description）

**Russia-Tetris**是一款多人在线俄罗斯方块游戏，提供登录、注册、查询成绩、查看排行榜和开始游戏等功能。

用户可通过用户名和密码登录，查看最近20次的游戏成绩，并查询全服前十名玩家的排行榜。游戏设有简单、普通和困难三种难度模式供选择。

游戏的基本规则是通过方向键和空格键移动、旋转和摆放游戏自动输出的各种方块，使之排列成完整的一行或多行并且消除得分，使玩家享受俄罗斯方块的乐趣，随时挑战自己，与全服玩家一决高下！



**项目主要功能：**

- **多人在线：** 支持多个用户同时在线游戏。
- **登录和注册：** 用户可以通过输入用户名和密码进行登录，也可以进行注册。
- **查询成绩：**用户登录成功后，可以查询自己最近20次的游戏成绩，包括游玩的难度模式和分数。
- **查询排行榜：** 用户可以查询三种难度模式下全服前十名玩家的用户名、分数和获取该分数的时间。
- **多种难度模式：** 游戏提供简单、普通和困难三种模式供用户选择开始游戏。



**项目主要特点:**

- 提供一个多人在线俄罗斯方块游戏平台，让用户可以随时随地进行游戏。

- 通过登录和注册功能，为用户提供个性化的游戏体验。

- 记录用户的游戏成绩，让用户可以随时查看自己的游戏进步。

- 提供排行榜功能，让用户可以看到自己在全服中的排名情况。

- 提供多种难度模式，让用户可以根据自己的游戏水平选择合适的难度进行游戏。

  

  

  

  ## 安装和使用说明 (Installation and Usage Instructions)

  **安装步骤：**

  1. **克隆仓库 (Clone the Repository)**

  用户需要从GitHub或其他代码托管平台克隆项目到本地。具体的命令行指令是：

  ```text
  git clone git@github.com:22222sss/Russia-Tetris.git
  ```

     

  

     2.**安装依赖 (Install Dependencies)**

  

  - **安装spdlog日志库以及libevent事件通知库。**

  

  

  **在Ubuntu 上安装spdlog可以按照以下步骤进行操作:**

  

  1.首先，打开终端并运行以下命令更新软件包列表
  
  ```text
  sudo apt update
  ```

  2.接下来，运行以下命令安装spdlog的依赖项:
  
  ```text
  sudo apt install cmake
  ```

  3.在终端中导航到您希望进行spdlog安装的目录，并克隆spdlog的GitHub存储库
  
  ```text
  git clone https://github.com/gabime/spdlog.git
  ```

  4.进入刚刚克隆下来的spdlog目录
  
  ```text
  cd spdlog
  ```

  5.创建一个新的目录来构建spdlog
  
  ```text
  mkdir build
  cd build
  ```

  6.运行cmake命令来配置构建:
  
  ```text
  cmake..
  ```

  7.运行make命令以构建spdlog
  
  ```text
  make
  ```

  8.最后，使用以下命令将spdlog安装到系统中:
  
  ```text
  sudo make install
  ```

  完成以上步骤后，您应该成功安装了spdlog.

  

  

  **在Ubuntu 上安装libevent可以按照以下步骤进行操作:**

  

  1.下载**libevent**开发库源代码

  在终端输入：
  
  ```text
  git clone https://github.com/yaoxiaokui/libevent-2.0.21-stable.git
  ```

  2.进入libevent-2.0.21-stable目录
  
  ```text
  cd libevent-2.0.21-stable
  ```

  3、配置安装路径
  
  ```text
  ./configure -prefix=/usr
  ```

  4、编译并安装
  
  ```text
  make
  make install
  ```

  5、测试安装是否成功
  
  ```text
  ls -al /usr/lib | grep libevent
  ```

  如果输出为下所示则表示安装成功

  ![image-20240301172203713](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240301172203713.png)

  

     3.**编译项目 (Compile the Project)**
  
  
  
  1. 切换到src目录
  
  ```text
  cd Russia-Tetris/src
  ```
  
  2. 编译项目 
  
  ```text
  make
  ```
  
    如果输出为下所示则表示编译成功
  
  
  
  ![image-20240302203929505](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302203929505.png)
  
    3.运行项目
  
  ```text
  ./test
  ```
  

如果输出为下所示则表示运行成功



![image-20240302204112267](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302204112267.png)



在cmd窗口下输入这串命令

```text
telnet Linux下的IP地址 端口号
```



查询**Linux下的IP地址**请输入命令

```text
ifconfig
```



![image-20240302205755241](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302205755241.png)



端口号位于**/src/Common/Common.h**里，可自行更改



![image-20240302210025525](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302210025525.png)



**最后进行telnet连接**

![image-20240302205246607](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302205246607.png)



如果输出为下所示则表示连接成功



![image-20240302210226493](C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20240302210226493.png)





## **项目结构和文件组织 (Project Structure and File Organization)**



| 文件/目录 | 描述         | 用途                         |
| --------- | ------------ | ---------------------------- |
| /src      | 源代码目录   | 存放项目的源代码             |
| /docs     | 文档目录     | 包含项目的文档和使用手册     |
| /tests    | 测试目录     | 存放测试脚本和测试数据       |
| README.md | 项目说明文件 | 提供项目的基本信息和使用指南 |



1. 源代码目录 (/src)
   源代码目录通常包含项目的所有源代码文件。在这个目录中，代码根据其功能和用途被组织成不同的子目录和文件。

   

   | 目录/文件       | 包含文件           | 描述                                                         | 用途                                                         |
   | --------------- | ------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
   | Common          | Common.h           | 包含项目通用的头文件以及通用的结构体以及通用的宏定义等       |                                                              |
   | EventLoop       | EventLoop.h/.cpp   | 事件循环处理单元（**核心网络框架**）                         | 用于处理异步操作或者监听事件。这个单元的主要功能包括新用户的连接管理、已连接用户游戏时方块的自由下落，以及处理已连接用户的键盘输入(根据用户当前所处的状态进行相应的程序处理)。 |
   | Filedata_mamage | Filedata.h/.cpp    | 用来处理userdata.csv的单元                                   | 读取、写入、解析和处理userdata.csv文件中的数据。             |
   | PlayerInfo      | PlayerInfo.h/.cpp  | 包含所有已注册的用户的个人信息（用户名和密码等）             | 用来管理所有已注册的用户的个人信息（用户名和密码等）         |
   | Server          | Server.h/.cpp      | 处理已连接用户的登录与注册逻辑                               | 用来管理用户登录的规则、逻辑和状态管理等                     |
   | TetrisGame      | TetrisGame.h./.cpp | 处理用户玩游戏时的游戏逻辑                                   | 包括俄罗斯方块游戏的规则、逻辑、用户的状态管理等             |
   | UImanage        | UImanage.h/.cpp    | 管理整个项目的界面调整                                       | 用于管理整个项目的用户界面调整,包括图形用户界面 (GUI) 和文字界面（TUI）的设计和实现。 |
   | User            | User.h/.cpp        | 包含当前用户的个人信息（如用户名，密码，当前状态等）         | 用来管理当前用户的个人信息（用户名和密码等）                 |
   | Utility         | Utility.h/.cpp     | 包含常用的工具类函数                                         | 这个单元包含了常用的工具类函数，用于提供项目中各个模块可能共享的辅助功能，如字符输出，获取当前时间等函数。 |
   | main            | main.cpp           |                                                              |                                                              |
   | makefile        |                    | 用于自动化构建过程，使得编译、链接和其他任务变得简单和可重复。 | 简化构建过程，使得项目的构建和维护变得更加方便和高效。       |
   | log.txt         |                    | 日志文件                                                     | 用于记录程序运行时产生的日志信息，如错误、警告、调试信息等，有助于开发人员进行故障排除和性能优化。 |
   | userdata.csv    |                    | 记录所有已注册的用户的相关信息（包括用户名和密码等）         | 这个文件是程序的重要数据源之一，用于用户信息的持久化存储和管理。 |
   
   


2. 文档目录 (/docs)
   这个项目采用了 `epoll` 和 `libevent` 作为事件处理机制，并参考了 `spdlog` 的来进行日志记录。
   
   
   
   项目的参考文档为`epoll` 和 `libevent`和`spdlog` 的官方文档
   
   

正如《程序员的自我修养》中所说：“良好的文档是软件质量的保证，也是开发者与用户沟通的桥梁。”



3. 测试目录 (/tests)
   测试是确保项目质量的关键。该目录下的userdata.csv包含一些用户信息（如用户名，密码等）的测试数据，以便开发者可以轻松地测试和验证代码的功能和性能。





## 软件架构



#### 核心技术栈

> | 技术     | 说明              | 备注                                                     |
> | -------- | ----------------- | -------------------------------------------------------- |
> | spdlog   | C++日志库         | https://github.com/gabime/spdlog.git                     |
> | libevent | 事件通知库        | https://github.com/yaoxiaokui/libevent-2.0.21-stable.git |
> | 网络编程 |                   | https://zhuanlan.zhihu.com/p/119085959                   |
> | epoll    | 事件驱动 I/O 模型 | https://zhuanlan.zhihu.com/p/93609693                    |
>



### 开发工具

> | 工具     | 说明             | 版本   | 备注                                                         |
> | -------- | ---------------- | ------ | ------------------------------------------------------------ |
> | `VS`     | `C++`开发`IDE`   | 2022   | https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-notes |
> | `Git`    | 项目版本管控工具 | latest | https://git-scm.com/                                         |
> | `Ubuntu` | Linux操作系统    | latest | https://ubuntu.com/                                          |
> | `make`   | 构建工具         | latest | https://www.gnu.org/software/make/                           |
>









## **项目展示 (Project Presentation)**



#### **登录注册界面**



![](D:\登录注册.gif)



#### **登录成功后查看用户最近的20次成绩（包括难度和分数）以及各难度全服top10成绩的用户信息（用户名，分数，以及获取该分数的时间）**， 同时提供返回上级菜单的功能



![](D:\登录成功查看.gif)



#### **开始游戏**



**简单模式（方块下降时间间隔为1秒）**

![](D:\简单模式.gif)



**普通模式（方块下降时间间隔为0.5秒）**

![](D:\普通模式.gif)



**困难模式（方块下降时间间隔为0.2秒）**

![](D:\困难模式.gif)



#### **游戏结束后返回初始菜单**

![](D:\游戏结束返回初始菜单.gif)







## 特别鸣谢

`Russia-Tetris`的诞生离不开开源软件和社区的支持，感谢以下开源项目及项目维护者：

- `spdlog`：https://github.com/gabime/spdlog.git
- `libevent`：https://github.com/yaoxiaokui/libevent-2.0.21-stable.git

同时也感谢其他没有明确写出来的开源组件提供给与维护者。

## 支持一下

如果觉得框架和项目还不错，点个⭐Star，这将是对**Russia-Tetris**极大的鼓励与支持。













