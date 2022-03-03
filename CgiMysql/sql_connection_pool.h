#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/lock.h"
#include "../log/log.h"

using namespace std;

class sql_connection_pool
{
public:
    MYSQL* getConnection();//获取一个数据库连接
    bool realeaseConnection(MYSQL* conn);//释放连接
    int getFreeCount();//获取空闲连接数目
    void destoryPool();//销毁连接池

    static sql_connection_pool* getInstance();//单例模式创建连接池
    //初始化连接池
    void init(string host, int port, string user, string passWord, string dataBase, int maxConn, int close_log);
private:
    sql_connection_pool();
    ~sql_connection_pool();

    int m_maxCount;//最大连接数
    int m_usedCount;//已使用连接数
    int m_freeCount;//空闲连接数
    locker m_mutex;
    list<MYSQL *> conn_pool_list;//连接池
    sem reserve;//可使用的资源数目，实现线程间的同步
public:
    string m_host;//主机ip地址
    int m_port;//主机端口
    string m_user;//用户名
    string m_passWord;//密码
    string m_dataBase;//数据库名
    int m_close_log;//日志功能启用与否
};

//RAII机制释放数据库连接
class connectionRAII
{
public:
    connectionRAII(MYSQL **conn, sql_connection_pool* connPool);
    ~connectionRAII();
private:
    MYSQL* connRAII;
    sql_connection_pool* poolRAII;
};
#endif