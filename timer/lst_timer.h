#ifndef LST_TIMER
#define LST_TIMER
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

class until_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    until_timer* timer;
};
/* 定时器类 */
class until_timer
{
public:
    until_timer() : pre(NULL), next(NULL) {}
public:
    time_t expire;//任务超时时间，使用绝对时间
    void (*cb_func)(client_data* data);//任务回调函数
    /* 回调函数处理的用户数据，由定时器执行者传给回调函数 */
    client_data* user_data;
    until_timer* pre;
    until_timer* next;
};

/* 定时器链表 升序、双向链表、带有头结点和尾节点 */
class sort_timer_list
{
public:
    sort_timer_list();
    ~sort_timer_list(); 
    void add_timer(until_timer* timer);
    void adjust_timer(until_timer* timer);
    void delete_timer(until_timer* timer);
    void tick();
private:
    void add_timer(until_timer* timer, until_timer* cur_head);
    until_timer* head;
    until_timer* tail;
};

class Util 
{
public:
    Util() {}
    ~Util() {}
    void init(int timeslot);
    int setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int trig_mode);
    static void sig_handler(int sig);
    void addsig(int sig, void(*handler)(int), bool restart = true);
    void timer_handler();
    void show_error(int connfd, const char *info);
public:
    static int* u_pipefd;
    sort_timer_list m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

/* 定时器回调函数 */
void cb_func(client_data* user_data);
#endif 
