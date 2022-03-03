#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_list::sort_timer_list() : head(NULL), tail(NULL) {}

/* 链表销毁时，删除所有定时器 */
sort_timer_list::~sort_timer_list()
    {
        until_timer* tmp;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

/* 将目标定时器添加到链表中
     * 1. 小于所有
     * 2. 其他合适位置
     * */
void sort_timer_list::add_timer(until_timer* timer)
    {
        if (!timer) return;
        if (!head)
        {
            head = tail = timer;
            return;
        }
        //1
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->pre = timer;
            head = timer;
            return;
        }
        //2.
        add_timer(timer, head);
    }

/* 定时器任务发生变化，调整对应定时器在链表中的位置（只考虑时间延长的情况，往尾后移动
     * 1. 尾部
     * 2. 头部
     * 3. "中间位置"插入之后位置
     * */
void sort_timer_list::adjust_timer(until_timer* timer)
    {
        if (!timer) return;
        until_timer* tmp = timer->next;
        //1.
        if (!tmp || timer->expire < tmp->expire) return;
        //2.
        if (timer == head)
        {
            head = head->next;
            head->pre = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
       else {
           //3.
           timer->next->pre = timer->pre;
           timer->pre->next = timer->next;
           add_timer(timer, timer->next);
       }
    }

/* 将目标定时器从timer中删除
     * 1. 只有一个
     * 2. 头
     * 3. 尾
     * 4. “中间位置”
     * */
void sort_timer_list::delete_timer(until_timer* timer)
    {
        if (!timer) return;
        //1.
        if (timer == head && timer == tail)
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        //2.
        if (timer == head)
        {
            head = head->next;
            head->pre = NULL;
            delete timer;
            return;
        }

        //3.
        if (timer == tail)
        {
            tail = tail->pre;
            tail->next = NULL;
            delete timer;
            return;
        }
        //4.
        timer->next->pre = timer->pre;
        timer->pre->next = timer->next;
        delete timer;
        return;
    }

/* SIGALRM信号每次触发就在其信号处理函数中（同一时间源则是主函数）中执行tick函数，处理链表上到期的任务*/
void sort_timer_list::tick()
    {
        if (!head) return;
        printf("timer tick\n");
        time_t cur = time(NULL);
        until_timer* tmp = head;
        /*从头节点开始依次处理，直到遇到一个未到期的 */
        while (tmp)
        {
            if (cur < tmp->expire) break;
            tmp->cb_func(tmp->user_data);//利用回调函数处理
            /* 处理完该任务后就从链表中删除 */
            head = tmp->next;
            if (head) head->pre = NULL;
            delete tmp;
            tmp = head;
        }
    }

 /* 重载的辅助函数 将timer添加到以cur_head之后的链表中 */
void sort_timer_list::add_timer(until_timer* timer, until_timer* cur_head)
    {
        until_timer* prev = cur_head;
        until_timer* tmp = prev->next;
        //找到第一个时间大于目标timer的节点，然后在该节点之前插入
        while (tmp)
        {
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->pre = timer;
                timer->pre = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }

        /*遍历完后还未找到则插入尾部 */
        if (!tmp)
        {
            prev->next = timer;
            timer->pre = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

void Util::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Util::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Util::addfd(int epollfd, int fd, bool one_shot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == trig_mode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else 
    {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
/* 信号处理函数 */
void Util::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
/* 设置信号处理函数 */
void Util::addsig(int sig, void(*handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}
/* 定时处理任务 */
void Util::timer_handler()
{
    /* 一次alarm调用只会引起一次SIGALARM信号，需要重新定时 */
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}
void Util::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Util::u_pipefd = 0;
int Util::u_epollfd = -1;
class Util;

void cb_func(client_data* user_data)
{
    epoll_ctl(Util::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
