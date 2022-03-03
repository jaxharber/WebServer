#include "config.h"

Config::Config()
{
    port = 9006;
    log_write = 0;//默认同步
    trig_mode = 0;//listenfd LT + connfd LT
    listen_trig_mode = 0;
    conn_trig_mode = 0;
    linger_opt = 0;
    sql_num = 8;
    thread_num = 8;
    close_log = 0;//默认不关闭
    actor_mode = 0;//默认proactor
}

void Config::parse_arg(int argc, char* argv[])
{
    int ch;
    const char* str = "p:l:m:o:s:t:c:a:";
    //解析命令行参数
    while ((ch = getopt(argc, argv, str)) != -1)
    {
        switch(ch)
        {
            case 'p':
                port = atoi(optarg);
                break;
            case 'l':
                log_write = atoi(optarg);
                break;
            case 'm':
                trig_mode = atoi(optarg);
                break;
            case 'o':
                linger_opt = atoi(optarg);
                break;
            case 's':
                sql_num = atoi(optarg);
                break;
            case 't':
                thread_num = atoi(optarg);
                break;
            case 'c':
                close_log = atoi(optarg);
                break;
            case 'a':
                actor_mode = atoi(optarg);
                break;
            default:
                break;
        }
    }
}