#include "http_conn.h"

/* 定义http响应的一些状态信息 */
const char* ok_200_title = "ok";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The request file was not found from this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving this requested line.\n";

// /* 网站根目录 */
// const char* doc_root = "root";

locker lock;
//用户名和密码
map<string, string> users;

/* 将数据库中的用户名和密码载入到服务器的map中来，map中的key为用户名，value为密码 */
void http_conn::initmysql_result(sql_connection_pool *connPool)
{
    /* 从连接池中取一个连接 */
    MYSQL* conn = NULL;
    connectionRAII cur_conn(&conn, connPool);
    //在user表中检索username，passwd数据
    if (mysql_query(conn, "SELECT username, passwd FROM user"))
    {
        LOG_ERROR("SELECT ERROR in user TABLE:%s\n", mysql_error(conn));
    }
    //从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(conn);
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    //typedef char ** MYSQL_ROW; /* 返回的每一行的值，全部用字符串来表示*/
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);//第一列
        string temp2(row[1]);//第二列
        users[temp1] = temp2;
    }
}

/* 设置fd为非阻塞 */
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 添加fd进入epoll红黑树中 */
void addfd(int epollfd, int fd, bool one_shot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == trig_mode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
         event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从红黑树中移除 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* 修改fd上的监听事件,将事件重置为EPOLLONESHOT */
void modfd(int epollfd, int fd, int ev, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == trig_mode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* 关闭连接，关闭一个连接，客户总量减一 */
void http_conn::close_conn(bool real_close)
{
    if ((real_close && (m_sockfd) != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}
/* 初始化连接,初始化套接字地址 */
void http_conn::init(int sockfd, const sockaddr_in& addr, char* root, int trig_mode, 
                int close_log, string user_name, string passwd, string data_base)   
{
    m_sockfd = sockfd;
    m_address = addr;
    /* 如下两行避免time_wait状态，仅用于调试，实际使用时去掉 */
    // int reuse = 1;
    // setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true, trig_mode);
    m_user_count++;

    doc_root = root;
    m_trig_mode = trig_mode;
    m_close_log = close_log;

    strcpy(m_sql_user, user_name.c_str());
    strcpy(m_sql_passwd, passwd.c_str());
    strcpy(m_sql_data_base, data_base.c_str());

    init();
}
/* 初始化新接受的连接 */
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    cgi = 0;
    m_sql_state = 0;//数据库转态默认为读
    m_timer_flag = 0;
    m_improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SZIE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* 从状态机 用于解析出一行内容 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_idx > 1) && m_read_buf[m_checked_idx - 1] == '\r')
            {
                 m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或者对方关闭连接 */
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //两种工作模式： LT、ET 针对connfd
    if (0 == m_trig_mode)
    {
        //LT
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if (bytes_read <= 0) return false;
        return true;
    } 
    else
    {
        //ET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                //非阻塞模式下如果没有数据会返回，不会阻塞着读，因此需要循环读取
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    }
    return true;
}

/* 解析http请求行，获得请求方法(支持GET和POST) m_method， URI(m_url), http版本号(m_version) */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';//字符串结束符
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    //只有"/”时，显示默认页面
    if (strlen(m_url) == 1) 
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* 解析http请求的头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    /* 遇到空行，头部字段解析完毕 */
    if (text[0] == '\0')
    {
        /* 若http请求有消息体，则还要继续读取m_content_length字节的消息体,状态机转移到CHECK_STATE_CONTENT状态 */
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }

    /* 处理Connection头部字段 */
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "Keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    /* 处理Content-Length头部字段 */
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    /* 处理Host头部字段 */
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else 
    {
        LOG_INFO("unknow headers %s", text);
    }
    return NO_REQUEST;
}

/* 判断http请求的消息体是否完整读入 */
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= (m_checked_idx + m_content_length))
    {
        text[m_content_length] = '\0';
        //post请求中的消息体的数据为用户名+密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 主状态机 对http各部分进行分析 */
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK))
        {
            //获取一行
            text = get_line();
            //下一行起始位置
            m_start_line = m_checked_idx;
            printf("got 1 http line: %s\n", text);
            LOG_INFO("got 1 http line %s", text);
            //记录主状态机当前状态
            switch (m_check_state)
            {
                case CHECK_STATE_REQUESTLINE:
                {
                    ret = parse_request_line(text);
                    if (ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER:
                {
                    ret = parse_headers(text);
                    if (ret == BAD_REQUEST)
                    {
                        return BAD_REQUEST;
                    }
                    else if (ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    break;
                }
                case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    if (ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    line_status = LINE_OPEN;
                    break;
                }
                default:
                    return INTERNAL_ERROR;
            }
        }
        return NO_REQUEST;
}

/* 当得到一个完整的，正确的http请求时，就分析目标文件的属性，若
    目标文件存在，对所有用户可读，且不是目录，则使用mmap将其映射到内存
    地址m_file_address处，并告诉调用者获取文件成功 */
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    printf("m_url:%s\n", m_url);
    const char* p = strrchr(m_url, '/');
    /* 由flag来判断是否进行数据库请求 POST */
    if (1 == cgi && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        /* 登录或者注册校验 */
        /* 提取用户名和密码 */
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        /* 提取用户名和密码 user=123&password=123 */
        char username[100], password[100];
        int i = 5;
        for ( ; m_string[i] != '&'; ++i)
            username[i - 5] = m_string[i];
        username[i - 5] = '\0';
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        /* 2 = 登录 3 = 注册 */
        if (*(p + 1) == '3')
        {
            //注册时查看该用户名是否已存在
           // char query_word[256] = {0};
            // snprintf(query_word, 255, "INSERT INTO user(username, passwd) VALUES('%s', '%s')", username, password);
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, username);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if (users.find(username) == users.end())
            {
                lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(username, password));
                lock.unlock();

                if (!res)
                    strcpy(m_url, "/login.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }
        }
        else if (*(p + 1) == '2')
        {
            //登录时验证是否是已注册用户
            if (users.find(username) != users.end() && users[username] == password)
                strcpy(m_url, "/welcome.html");
            else 
                strcpy(m_url, "/logError.html");
        }
    }
    // 0 = 请求注册页面
    if (*(p + 1) == '0')
    {
        char *m_url_real = "/register.html";
        strcpy(m_url_real, m_url_real);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }
    // 1 = 请求登录页面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = "/login.html";
        strcpy(m_url_real, m_url_real);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }
    // 5 = 请求图片资源
    else if (*(p + 1) == '5')
    {
        char *m_url_real = "/picture.html";
        strcpy(m_url_real, m_url_real);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }
    // 6 = 请求视频
    else if (*(p + 1) == '6')
    {
        char *m_url_real = "/video.html";
        strcpy(m_url_real, m_url_real);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }
    //显示关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = "/fans.html";
        strcpy(m_url_real, m_url_real);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }
    //获取实际的文件 GET
    else 
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }
    LOG_INFO("real file: %s", m_real_file);
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    // printf("%s", m_real_file);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作 */
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* 写http响应 对大文件进行特殊处理 */
bool http_conn::write()
{
    int temp = 0;
    // int bytes_have_send = 0;//已发送字节数
    // int bytes_to_send = m_write_idx;//剩余多少字节待发送
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            /* 若tcp写缓冲没有空间，则等待下一轮EPOLLOUT事件，在此期间，服务器无法立即接收到同一客户的下一个请求
                但可以保证连接的完整性 */
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        //分为两部分传输（1. 状态行 + 头部 + 空行 2. 消息体）
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;//第一部分已经传完，之后不再传
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            //只更新第一部分
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        if (bytes_to_send <= 0)
        {
            /* 发送http响应成功，根据http请求中的Connection字段决定是否立即关闭连接 */
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
            if (m_linger)
            {
                init(); 
                return true;
            }
            else            
                return false;         
        }
    }
}

/* 往写缓冲区写入待发送的数据 */
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SZIE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SZIE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SZIE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    printf("echo :%s\n", m_write_buf);
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

/* 添加状态行 */
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/* 添加响应头和空白行 */
bool http_conn::add_headers(int content_len)
{       
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

/* 添加消息体 */
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

/* 根据服务器处理http请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
                return false;
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                printf("FILE_REQUEST\n");
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                //为大文件传输做准备
                bytes_to_send = m_write_idx + m_file_stat.st_size;//待发送字节数
                return true;
            }
            else 
            {
                const char* ok_string = "<html> <body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/* 线程池中工作线程调用，这是处理http请求的入口函数 */
void http_conn::process()
{
    printf("process()\n");
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
}