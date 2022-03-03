#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver.h"
using namespace std;
//配置信息
class Config
{
public:
    Config() ;
    ~Config() {}

    void parse_arg(int argc, char* argv[]);

public:
    int port;
    int log_write;//日志写入方式

    int trig_mode;//LT+ET触发的组合模式（4种），控制下面两个参数
    int listen_trig_mode;
    int conn_trig_mode;

    int linger_opt;
    int sql_num;
    int thread_num;
    int close_log;//是否使用日志
    int actor_mode;//actor模型选择
};
#endif