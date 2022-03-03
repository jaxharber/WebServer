#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./timer/lst_timer.h"

const int MAX_FD = 65535;//最大文件描述符数
const int MAX_EVENT_NUMBER = 10000;//最大事件数
const int TIMESLOT = 5; //超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string userName, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    //事件分发
    void eventListen();
    void eventLoop();
    //定时器
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(until_timer *timer);
    //同一事件源处理
    void remove_timer(until_timer *timer, int sockfd);//移除定时器
    bool listen_handler();//listenfd处理
    //信号事件
    bool signal_handler(bool& timeout, bool& stop_server);
    //I/O事件
    void read_handler(int sockfd);
    void write_handler(int sockfd);

public:
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;
    int m_actor_model;

    int m_pipefd[2];
    int m_epollfd;
    http_conn* users;

    //数据库
    sql_connection_pool* m_sql_pool;
    string m_username;
    string m_password;
    string m_databaseName;
    int m_sql_num;

    //线程池
    threadPool<http_conn>* m_thread_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    //触发模式
    int m_listen_fd;
    int m_trig_mode;
    int m_listen_trig;
    int m_conn_trig;
    int m_linger;

    //定时器
    client_data* users_timer;
    Util util;

};

#endif