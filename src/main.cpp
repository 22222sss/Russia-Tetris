#include"Server/Server.h"
#include"Common/Common.h"
#include"Utility/Utility.h"
#include"PlayerInfo/PlayerInfo.h"
#include"TetrisGame/TetrisGame.h"
#include"EventLoop/EventLoop.h"
#include"User/User.h"
#include"UImanage/UImanage.h"
#include"Filedata_manage/Filedata.h"

Block blockDefines[7][4] = { 0 };

// 创建 spdlog::logger 对象
std::shared_ptr<spdlog::logger> logger = spdlog::basic_logger_mt("logger", "log.txt");

int main()
{
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        //printf("create socket Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("create socket Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }
    // 对应伪代码中的bind(sockfd, ip::port和一些配置);
    struct sockaddr_in addr;    // 用于存放ip和端口的结构
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEFAULT_PORT);
    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        close(serverSocket);
        //printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("bind Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }
    // 对应伪代码中的listen(sockfd);    
    if (listen(serverSocket, MAXLINK) == -1)
    {
        close(serverSocket);
        //printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("listen Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }


    // 初始化 libevent
   
    EventLoop eventloop;  

    //创建并添加服务器套接字事件
    eventloop.registerFdEvent(serverSocket, EV_READ | EV_PERSIST, Server::handleNewClientConnection, (void*)&eventloop);


    // 创建定时器事件
    
    // 设置定时器的超时时间为 0.1 秒
    struct timeval delay = { 0, 100000 }; // 100000 微秒 = 0.1 秒

    
    eventloop.registerFdEvent(-1, EV_PERSIST, TetrisGame::processTimerEvent, NULL, &delay);

    //创建注销事件和下线用户事件
    eventloop.registerFdEvent(-1, EV_PERSIST, EventLoop::unregister_Event_User, NULL, &delay);


    printf("======waiting for client's request======\n");

    eventloop.run();

    return 0;
}

/*静态（Static）
在编程中，“静态”这个词的核心含义与“动态”相对。通常表示：
1.生命周期长：在程序启动时创建，在程序结束时销毁。
2.绑定到类而非实例：不依赖于具体的对象实例，而是属于整个类本身。
3.内存位置固定：通常存储在程序的静态存储区（或全局存储区），而不是堆或栈上。

内存区域包括
代码段：用于存放程序的机器指令（即函数代码）。只读，共享
全局/静态数据区：存放全局变量和静态变量。程序启动时分配，结束时释放。
堆：动态分配的内存（new/malloc）。手动管理，生命周期由程序员控制。
栈：存放局部变量，函数参数，返回地址。自动管理，函数调用时创建，返回时销毁。

-----------------------------------------------------------------------------

静态成员变量
1.定义与特点
静态成员变量是属于整个类的，而不是属于某个特定对象的，所以该类的对象共享同一个静态成员变量。

特点：
唯一性：无论创建多少个对象，静态成员变量在内存中只有一份拷贝。
类级访问：可以通过类名::变量名直接访问，而不需要创建对象。
必须在类外定义:在类内只是声明，必须在类外进行定义和初始化(除非是const整型可以在类内部声明的同时进行初始化)。
class MyClass 
{ 
public: static const int staticConstInt = 10; // 类内初始化 // ... 
};

class MyClass 
{ 
public: static const double staticConstDouble = 3.14; // 错误，非整型不能在类内初始化 // ... 
};

生命周期：从程序开始到结束，即使没有创建任何对象，它也存在。

#include <iostream>
using namespace std;

class Student {
public:
    static int count; // 静态成员变量声明

    Student() {
        count++; // 每创建一个对象，计数加1
    }
};

// 必须在类外定义并初始化
int Student::count = 0;

int main() {
    cout << "初始人数: " << Student::count << endl; // 0

    Student s1;
    Student s2;
    Student s3;

    cout << "当前学生人数: " << Student::count << endl; // 3
    cout << "通过对象访问: " << s1.count << endl; // 3（不推荐，容易误解）

    return 0;
}

静态成员函数
静态成员函数是属于 类本身 的函数，不依赖于任何对象实例。

特点：
无this指针：静态成员函数没有隐含的this指针，因此该函数不能访问非静态成员变量或调用非静态成员函数。
只能访问静态成员变量：它只能访问类的静态成员变量和其他静态成员函数。
类级调用：可以直接通过类名::函数名（）调用，无需创建对象。

class MathUtils {
private:
    static double pi; // 静态成员变量

public:
    static double getPi() {
        return pi;
    }

    static double circleArea(double r) {
        return pi * r * r; // 只能访问静态成员
    }

    // ❌ 错误示例：静态函数不能访问非静态成员
    // void nonStaticFunc(); // 假设这是一个非静态函数
    // static void badFunc() {
    //     nonStaticFunc(); // 编译错误！没有 this 指针
    // }
};

double MathUtils::pi = 3.14159;

int main() {
    cout << "Pi = " << MathUtils::getPi() << endl;
    cout << "半径为5的圆面积: " << MathUtils::circleArea(5) << endl;

    return 0;
}

特性	静态成员变量/函数	     非静态成员变量/函数
所属	    整个类	               每个对象实例
内存	   静态存储区（一份）	   每个对象都有自己的拷贝
访问方式   类名::成员 或 对象.成员	必须通过对象访问
this 指针	   无	                      有
初始化	   类外定义（变量）	     构造函数中初始化
生命周期	程序运行期间	     对象创建到销毁

静态 = 属于类，不属于对象。
静态成员变量：所有对象共享一份数据，用于保存类级别的状态。
静态成员函数：不依赖对象，只能操作静态成员，用于提供类级别的功能。

为什么“静态存储区”是“固定的”？
在程序编译和链接阶段，编译器就已经知道所有全局变量和静态变量的大小和类型。
操作系统在加载程序时，会为这些变量分配一块固定的内存区域，并确定它们的内存地址。
这个地址在整个程序运行期间不会改变。

int globalVar = 100;        // 全局变量 → 静态存储区

class MyClass {
    static int staticCount; // 静态成员变量 → 静态存储区
};

假设 globalVar 的地址是 0x1000，那么在整个程序运行期间，它永远在 0x1000 这个位置。不会像栈上的变量那样“忽上忽下”。

为什么这样设计？
效率高：
静态变量的地址在编译时就已知，访问速度快（直接寻址）。
不需要像堆那样动态分配/释放，避免了内存管理开销。





为什么 const 整型静态成员变量 可以在类内部直接初始化，而其他类型的静态成员变量必须在类外定义？

const 整型静态成员的核心特点是：它是一个编译期就知道值的常量。
编译器可以在编译时就确定它的值，而不需要等到程序运行。


静态成员变量本质上是一个全局变量，它必须在程序中有且仅有一个定义（定义 = 分配内存）。

类内声明：只是告诉编译器“有这么个东西”。
类外定义：才是真正为它分配内存。

class MyClass {
    static int count; // 声明（不分配内存）
};

int MyClass::count = 0; // 定义（分配内存）

但 const 整型是个例外：

编译器发现 static const int SIZE = 100; 后，通常不会为它分配实际的内存地址，而是像宏一样，在用到的地方直接替换为 100。


C++11 及以后的改进
从 C++11 开始，规则放宽了：

const 不再是必须的。
可以使用 constexpr 来定义编译期常量。

class Math {
public:
    static constexpr double PI = 3.1415926;     // ✅ 合法
    static constexpr int MAX_USERS = 1000;      // ✅ 合法
    static inline int count = 0;                // C++17 起支持 inline 静态变量
};

constexpr 表示“常量表达式”，比 const 更严格，确保值在编译期就能计算出来。

*/