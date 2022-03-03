#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

sql_connection_pool::sql_connection_pool()
{
    m_usedCount = 0;
    m_freeCount = 0;
}
//局部静态变量懒汉式
sql_connection_pool* sql_connection_pool::getInstance()
{
    static sql_connection_pool poolInstance;
    return &poolInstance;
}

void sql_connection_pool::init(string host, int port, string user, string passWord, string dataBase, int maxConn, int close_log)
{
    m_host = host;
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_dataBase = dataBase;
    m_close_log = close_log;

    for (int i = 0; i < maxConn; ++i)
    {
        MYSQL* conn = NULL;
        conn = mysql_init(conn);
        if (conn == NULL)
        {
            LOG_ERROR("MYSQL Init error");
            exit(1);
        }
        LOG_INFO("host:%s_port:%d_user:%s_passwd:%s_db:%s", host.c_str(), m_port, user.c_str(), passWord.c_str(), dataBase.c_str());
        conn =mysql_real_connect(conn, host.c_str(), user.c_str(), passWord.c_str(), dataBase.c_str(), m_port, NULL, 0);
        if (conn == NULL)
        {
            LOG_ERROR("MYSQL Connect error");
            exit(1);
        }
        LOG_INFO("establish %dth sql_connection", i);
        conn_pool_list.push_back(conn);
        ++m_freeCount;
    }
    reserve = sem(m_freeCount);
    m_maxCount = m_freeCount;
}

/* 当有请求时，从数据库连接池中返回一个可用连接，更新已使用和空闲连接数 */
MYSQL* sql_connection_pool::getConnection()
{
    MYSQL* conn = NULL;
    if (0 == conn_pool_list.size())
        return NULL;
    reserve.wait();
    m_mutex.lock();
    conn = conn_pool_list.front();
    conn_pool_list.pop_front();
    --m_freeCount;
    ++m_usedCount;
    m_mutex.unlock();
    return conn;
}

//归还池中
bool sql_connection_pool::realeaseConnection(MYSQL* conn)
{
    if (NULL == conn)
        return false;
    m_mutex.lock();
    conn_pool_list.push_back(conn);
    ++m_freeCount;
    --m_usedCount;
    m_mutex.unlock();
    reserve.post();
    return true;
}

//销毁连接池
void sql_connection_pool::destoryPool()
{
    m_mutex.lock();
    if (conn_pool_list.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it = conn_pool_list.begin(); it != conn_pool_list.end(); ++it)
        {
            MYSQL* cur = *it;
            mysql_close(cur);
        }
        m_usedCount = 0;
        m_freeCount = 0;
        conn_pool_list.clear();
    }
    m_mutex.unlock();
}

int sql_connection_pool::getFreeCount()
{
    return this->m_freeCount;
}

sql_connection_pool::~sql_connection_pool()
{
    destoryPool();
}

//不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放
connectionRAII::connectionRAII(MYSQL **conn, sql_connection_pool* connPool)
{
    *conn = connPool->getConnection();
    connRAII = *conn;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->realeaseConnection(connRAII);
}


