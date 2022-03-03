#include "webserver.h"

WebServer::WebServer()
{
    //http_conn
    users = new http_conn[MAX_FD];
    users_timer = new client_data[MAX_FD];

    //当前工作目录
    char cur_dir[200];
    getcwd(cur_dir, 200);
    char server_path[6] = "/root";
    m_root = (char*)malloc(strlen(cur_dir) + strlen(server_path) + 1);
    strcpy(m_root, cur_dir);
    strcat(m_root, server_path);
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listen_fd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete [] users;
    delete [] users_timer;
    delete m_thread_pool;
}

void WebServer::init(int port , string userName, string passWord, string databaseName,
                    int log_write , int opt_linger, int trig_mode, int sql_num,
                    int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_username = userName;
    m_password = passWord;
    m_databaseName = databaseName;
    m_log_write = log_write;
    m_linger = opt_linger;
    m_trig_mode = trig_mode;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_actor_model = actor_model;
} 
//listenfd和connfd的工作模式(LT或者ET)
void WebServer::trig_mode()
{
   switch (m_trig_mode)
   {
       case 0:
            m_listen_trig = 0;//LT+LT
            m_conn_trig = 0;
            break;
        case 1:
            m_listen_trig = 0;//LT+ET
            m_conn_trig = 1;
            break;
        case 2:
            m_listen_trig = 1;//ET+LT
            m_conn_trig = 0;
            break;
        case 3:
            m_listen_trig = 1;//ET+ET
            m_conn_trig = 1;
            break;
        default:
            break;
   }
}

//是否启用日志功能
void WebServer::log_write()
{
    //启用日志功能
    if (0 == m_close_log)
    {
        //开启异步写
        if (1 == m_log_write)
            log::getInstance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            log::getInstance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

//初始化数据库连接池
void WebServer::sql_pool()
{
    m_sql_pool =  sql_connection_pool::getInstance();
    m_sql_pool->init("localhost", 3306, m_username, m_password, m_databaseName, m_sql_num, m_close_log);

    //加载数据
    users->initmysql_result(m_sql_pool);
}

void WebServer::thread_pool()
{
    m_thread_pool = new threadPool<http_conn>(m_actor_model, m_sql_pool, m_thread_num);
}
//监听准备
void WebServer::eventListen()
{
    m_listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listen_fd >= 0);

    //优雅关闭连接
    if (1 == m_linger)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listen_fd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;//设置端口复用 避免time_wait状态
    setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listen_fd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listen_fd, 5);
    assert(ret >= 0);

    util.init(TIMESLOT);

    //epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    util.addfd(m_epollfd, m_listen_fd, false, m_listen_trig);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    util.setnonblocking(m_pipefd[1]);
    util.addfd(m_epollfd, m_pipefd[0], false, 0);

    util.addsig(SIGPIPE, SIG_IGN);
    util.addsig(SIGALRM, util.sig_handler, false);
    util.addsig(SIGTERM, util.sig_handler, false);

    Util::u_pipefd = m_pipefd;
    Util::u_epollfd = m_epollfd;
    alarm(TIMESLOT);
}
/* 初始化connfd并创建定时器 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_conn_trig, m_close_log, m_username, m_password, m_databaseName);
    
    //设置回调函数和超时时间，并将定时器加入链表
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    until_timer* timer = new until_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    util.m_timer_lst.add_timer(timer);
    printf("success accept\n");
}

/* 若连接上有数据传输则调整定时器 */
void  WebServer::adjust_timer(until_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    util.m_timer_lst.adjust_timer(timer);
    LOG_INFO("DATA TRANSMITING ONCE");
}

/* 对方关闭连接，我们也关闭并移除定时器 */
void WebServer::remove_timer(until_timer *timer, int sockfd)
{
    
    if (timer)
    {
        timer->cb_func(&users_timer[sockfd]);
        util.m_timer_lst.delete_timer(timer);
        LOG_INFO("connection fd: %d closed success", sockfd);
    }
    else
        LOG_ERROR("carefully the timer is null");
}
/* 处理listenfd LT 或者 ET(此处存疑) */
bool WebServer::listen_handler()
{
    struct sockaddr_in client_address;
    socklen_t client_address_length = sizeof(client_address);
    if (0 == m_listen_trig)
    {
        //LT
    int connfd = accept(m_listen_fd, (struct sockaddr*)&client_address, &client_address_length);
    if (connfd < 0)
    {
        LOG_ERROR("accept error! errno is %d", errno);
        return false;
    }
    if (http_conn::m_user_count >= MAX_FD)
    {
        util.show_error(connfd, "INTERNAL SERVER BUSY");
        LOG_ERROR("INTERNAL SERVER BUSY");
        return false;
    }
    timer(connfd, client_address);
    }
    else
    {
        //ET模式下无论成功与否都只能返回false??? 将全连接队列中所有都取出来
        while (true)
        {
        int connfd = accept(m_listen_fd, (struct sockaddr*)&client_address, &client_address_length);
        if (connfd < 0)
        {   
            LOG_ERROR("accept error! errno is %d", errno);
            break;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            util.show_error(connfd, "INTERNAL SERVER BUSY");
            LOG_ERROR("INTERNAL SERVER BUSY");
            break;
        }
        timer(connfd, client_address);
        }
        return false;
    }
    return true;
}
/* 信号事件处理 */
bool WebServer::signal_handler(bool& timeout, bool& stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0)
    {
        LOG_ERROR("m_pipefd[0] error");
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
                case SIGALRM:
                    timeout = true;
                    break;
                case SIGTERM:
                    stop_server = true;
                    break;
            }
        }
    }
    return true;
}
/* 事件处理模式 reactor or proactor*/
void WebServer::read_handler(int sockfd)
{
    until_timer* timer = users_timer[sockfd].timer;
    //reactor模式
    if (1 == m_actor_model)
    {
        if (timer)
        {
            adjust_timer(timer);
        }
        //有读请求，加入请求队列 读状态
        m_thread_pool->append(users + sockfd, 0);
        //此处有疑问？
        while (true)
        {
            if (1 == users[sockfd].m_improv)
            {
                if (1 == users[sockfd].m_timer_flag)
                {
                    remove_timer(timer, sockfd);
                    users[sockfd].m_timer_flag = 0;
                }
                users[sockfd].m_improv = 0;
                break;
            }
        }
    }
    else 
    {
        //同步模拟proactor  
        if (users[sockfd].read())
        {
            LOG_INFO("read data from client %s.", inet_ntoa(users[sockfd].get_address()->sin_addr));
            m_thread_pool->append_proactor(users + sockfd);
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            remove_timer(timer, sockfd);
        }
    }
}

void WebServer::write_handler(int sockfd)
{
    printf("write_handler\n");
    until_timer* timer = users_timer[sockfd].timer;
    //reactor模式
    if (1 == m_actor_model)
    {
        if (timer)
        {
            adjust_timer(timer);
        }
        //有写请求，加入请求队列 写状态
        m_thread_pool->append(users + sockfd, 1);
        //此处有疑问？为何要用while
        while (true)
        {
            if (1 == users[sockfd].m_improv)
            {
                if (1 == users[sockfd].m_timer_flag)
                {
                    remove_timer(timer, sockfd);
                    users[sockfd].m_timer_flag = 0;
                }
                users[sockfd].m_improv = 0;
                break;
            }
        }
    }
    else 
    {
        //同步模拟proactor  
        if (users[sockfd].write())
        {
            LOG_INFO("send data to client %s.", inet_ntoa(users[sockfd].get_address()->sin_addr));
            m_thread_pool->append_proactor(users + sockfd);
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            remove_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll_wait error");
            break;
        }
        for (int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;
            //统一事件源处理
            if (sockfd == m_listen_fd)
            {
                //listenfd
                LOG_INFO("m_listen_fd event");
                bool flag = listen_handler();
                if (!flag) continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //异常事件
                until_timer* timer = users_timer[sockfd].timer;
                remove_timer(timer, sockfd);
            }
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                //信号事件
                bool flag = signal_handler(timeout, stop_server);
                if (false == flag)
                {
                    LOG_ERROR("%s", "signal_handler error");
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                //客户连接读事件
                read_handler(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                //客户连接写事件
                printf("处理写事件\n");
                write_handler(sockfd);
            }
        }
        //延时处理定时事件
        if (timeout)
        {
            util.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}




