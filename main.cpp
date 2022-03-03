#include "./config/config.h"

int main(int argc, char* argv[])
{
    //数据库信息
    string user = "root";
    string passwd = "root";
    string databasename = "webdb";

    Config config;
    config.parse_arg(argc, argv);

    WebServer webserver;
    //初始化
    webserver.init(config.port, user, passwd, databasename, config.log_write, config.linger_opt, config.trig_mode, config.sql_num, config.thread_num, config.close_log, config.actor_mode);

    //日志
    webserver.log_write();
    //数据库
    webserver.sql_pool();
    //线程池
    webserver.thread_pool();
    //触发模式
    webserver.trig_mode();
    //事件分发
    webserver.eventListen();
    webserver.eventLoop();

    return 0;
}