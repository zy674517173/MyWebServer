#ifndef CONFIG_H
#define CONFIG_H
// 这些宏用于防止头文件的多次包含。它们确保 CONFIG_H 宏未定义时才定义它，以此避免重复包含头文件引起的编译错误。

#include "webserver.h"

using namespace std;

class Config
{
public:
    Config();       // Config();：声明 Config 类的构造函数，用于初始化配置对象。
    ~Config(){};    // ~Config(){}：声明 Config 类的析构函数，使用空的花括号表示析构函数没有任何操作。

    // 声明一个名为 parse_arg 的成员函数，该函数用于解析命令行参数。它接收两个参数：
    // int argc：命令行参数的数量。
    // char *argv[]：命令行参数的数组。
    void parse_arg(int argc, char*argv[]); 

    //端口号
    int PORT;

    //日志写入方式
    int LOGWrite;

    //触发组合模式
    int TRIGMode;

    //listenfd触发模式
    int LISTENTrigmode;

    //connfd触发模式
    int CONNTrigmode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型选择
    int actor_model;
};

#endif