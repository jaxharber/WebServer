#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include <map>

#include "../lock/lock.h"
#include "../CgiMysql/sql_connection_pool.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

/* 线程池模板参数类 */
class http_conn
{
public:
    /* 文件名的最大长度 */
    static const int FILENAME_LEN = 200;
    /* 读缓冲区的大小 */
    static const int READ_BUFFER_SIZE = 2048;
    /* 写缓冲区的大小 */
    static const int WRITE_BUFFER_SZIE = 1024;
    /* http请求方法 */
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT, 
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    /* 解析客户请求时，主状态机的状态 */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    /* 服务器处理http请求的可能结果 */
    enum HTTP_CODE
    {
        NO_REQUEST,//请求不完整，继续读取
        GET_REQUEST,//获得完整请求
        BAD_REQUEST,//请求语法错误  
        NO_RESOURCE,//该资源不存在
        FORBIDDEN_REQUEST,//客户无访问权限
        FILE_REQUEST,//客户端求取文件
        INTERNAL_ERROR,//服务器内部错误
        CLOSED_CONNECTION//客户端已关闭连接
    };
    /* 行的读取状态 */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    /* 初始化新接受的连接 */
    void init(int sockfd, const sockaddr_in& addr, char* root, int trig_mode, int close_log, string user_name, string passwd, string data_base);
    /* 关闭连接 */
    void close_conn(bool real_close = true);
    /* 处理客户请求 */
    void process();
    /* 非阻塞读操作 */
    bool read();
    /* 非阻塞写操作 */
    bool write();   
    /* user数据库数据载入内存 */
    void initmysql_result(sql_connection_pool *connPool);
    //获取客户端地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }

private:
    /*初始化连接 */
    void init();
    /* 解析http请求 */
    HTTP_CODE process_read();
    /* 填充http应答 */
    bool process_write(HTTP_CODE ret);

    /* process_read调用以分析http请求 */
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() 
    {
        return m_read_buf + m_start_line;
    }
    LINE_STATUS parse_line();

    /* process_write调用以填充http应答 */
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    /* 所有socket上的事件都被注册到同一个epoll内核事件表中，故将epoll文件描述符设置为静态 */
    static int m_epollfd;
    /* 统计用户数量 */
    static int m_user_count;

    /* 数据库连接 */
    MYSQL* mysql;
    /* 对数据库的操作状态 读: 0 写: 1 */
    int m_sql_state;

    int m_timer_flag;
    int m_improv;

private:
    /* 该http连接的socket和对方的socket地址 */
    int m_sockfd;
    sockaddr_in m_address;

    /* 读缓冲区 */
    char m_read_buf[READ_BUFFER_SIZE];
    /* 标识读缓冲区中已经读入的用户数据的最后一个字节的下一个位置 */
    int m_read_idx;
    /* 当前正在分析的字符在在读缓冲区中的位置 */
    int m_checked_idx;
    /* 当前正在解析的行的起始位置 */
    int m_start_line;
    /* 写缓冲区 */
    char m_write_buf[WRITE_BUFFER_SZIE];
    /* 写缓冲区中待发送的字节数 */
    int m_write_idx;

    /* 主状态机当前所处状态 */
    CHECK_STATE m_check_state;
    /* 请求方法 */
    METHOD m_method;

    /* 客户请求的目标文件的完整路径，其内容等于doc_root(网站根目录)+m_url */
    char m_real_file[FILENAME_LEN];
    /* 客户请求的目标文件的文件名 */
    char* m_url;
    /* http协议版本号 */
    char* m_version;
    /* 主机名 */
    char* m_host;
    /* http请求的消息体的长度 */
    int m_content_length;
    /* http请求是否要保持连接 */
    bool m_linger;

    /* 客户请求的目标文件被mmap到内存的位置 */
    char* m_file_address;
    /* 目标文件状态，判断文件是否存在，是否为目录，是否可读，并获取文件大小等 */
    struct stat m_file_stat;
    /* 使用writev来执行写操作 */
    struct iovec m_iv[2];
    int m_iv_count;
    /* 服务器根目录 */
    char* doc_root;

    /* 是否启用post */
    int cgi;
    /* 存储请求的消息体数据 */
    char* m_string;
    /* 剩余待发送的数据 */
    int bytes_to_send;
    /* 已发送的数据 */
    int bytes_have_send;
    /* 该连接下的用户，用于数据校验*/
    map<string, string> m_users;

    /* 触发模式: LT 或 ET */
    int m_trig_mode;
    /* 是否关闭日志 */
    int m_close_log;

    /* 用户名, 密码, 数据库名 */
    char m_sql_user[100];
    char m_sql_passwd[100];
    char m_sql_data_base[100];
};
#endif