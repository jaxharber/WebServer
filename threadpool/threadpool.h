#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/lock.h"
#include "../CgiMysql/sql_connection_pool.h"
/* 半同步半反应堆线程池实现 */

template <typename T>
class threadPool
{
public:
    /* thread_num是线程池中的线程数量，max_requests是请求队列中最多允许的，等待处理的请求数量 */
    threadPool(int actor_model, sql_connection_pool* connPool, int thread_num = 8, int max_requests = 10000);
    ~threadPool();
    /* 往请求队列中添加任务 两种模式 */
    bool append(T* request, int sql_state);
    bool append_proactor(T* request);
    /* 工作线程运行的函数 */
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number; /* 线程池的线程数 */
    int m_max_requests; /* 请求队列中允许的最大请求数 */
    pthread_t* m_threads; /* 线程池数组 */
    std::list<T*> m_workqueue; /* 请求队列 */
    locker m_queuelocker; /* 保护请求队列的互斥锁 */
    sem m_queuestat; /* 是否有任务需要处理 */
    bool m_stop; /* 是否结束线程 */
    sql_connection_pool* m_connPool; //数据库连接池
    int m_actor_model;//模型切换
};

template <typename T>
threadPool<T>::threadPool(int actor_model, sql_connection_pool* connPool, int thread_num, int max_requests)
         : m_thread_number(thread_num), m_max_requests(max_requests),
           m_stop(false), m_threads(NULL), m_actor_model(actor_model), m_connPool(connPool)
{
    if ((thread_num <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }

    m_threads = new pthread_t[thread_num];
    if (!m_threads)
    {
        throw std::exception();
    }
    /* 创建thread_numb个线程，并将其设置为脱离线程 */
    for (int i = 0;i < thread_num; ++i)
    {
        printf("create the %dth thread\n", i);
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            printf("destory the thread\n");
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadPool<T>::~threadPool()
{
    delete [] m_threads;
    m_stop = true;
}


// reactor模式使用
template <typename T>
bool threadPool<T>::append(T* request, int sql_state)
{
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_sql_state = sql_state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//信号量增加，大于0时唤醒等待线程
    return true;
}

//proactor
template <typename T>
bool threadPool<T>::append_proactor(T* request)
{
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//信号量增加，大于0时唤醒等待线程
    return true;
}


template <typename T>
void* threadPool<T>::worker(void* arg)
{
    threadPool* pool = (threadPool*)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadPool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
        {
            continue;
        }
        printf("run() get one job\n");
        //两种模式
        if (1 == m_actor_model)
        {
            //reactor模式
            //读事件
            if (0 == request->m_sql_state)
            {
                if (request->read())
                {
                    request->m_improv = 1;
                    connectionRAII mysqlconn(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    //断开连接或者无数据可读
                    request->m_improv = 1;
                    request->m_timer_flag = 1;
                }
            }
            else
            {
                //写事件
                if (request->write())
                {
                    request->m_improv = 1;
                }
                else
                {
                    //写失败标记timer标志以移除定时器
                    request->m_improv = 1;
                    request->m_timer_flag = 1;
                }
            }
        }
        else
        {
            //proactor
            connectionRAII mysqlconn(&request->mysql, m_connPool);
            request->process();
        }
       
    }
}
#endif