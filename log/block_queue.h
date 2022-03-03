#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/lock.h"

using namespace std;

template <typename T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
        : m_size(0), m_max_size(max_size), m_front(-1), m_back(-1)
    {
        if (max_size <= 0)
            exit(-1);
        m_array = new T[max_size];  
    }
    void clear()
    {   
        m_lock.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_lock.unlock();
    }
    ~block_queue()
    {
        m_lock.lock();
        if (m_array != NULL)
        {
            delete [] m_array;
        }
        m_lock.unlock();
    }
    bool full()
    {
        m_lock.lock();
        if (m_size >= m_max_size)
        {
            m_lock.unlock();
            return false;
        }
        m_lock.unlock();
        return true;
    }
    bool empty()
    {
        m_lock.lock();
        if (0 != m_size)
        {
            m_lock.unlock();
            return false;
        }
        m_lock.unlock();
        return true;
    }
    bool front(T& value)
    {
        m_lock.lock();
        if (0 == m_size)
        {
            m_lock.unlock();
            return false;
        }
        value = m_array[m_front];
        m_lock.unlock();
        return true;
    }
    bool back(T& value)
    {
        m_lock.lock();
        if (0 == m_size)
        {
            m_lock.unlock();
            return false;
        }
        value = m_array[m_back];
        m_lock.unlock();
        return true;
    }
    int size() 
    {
        int tmp = 0;
        m_lock.lock();
        tmp = m_size;
        m_lock.unlock();
        return tmp;
    }
    int max_size()
    {
        int tmp = 0;
        m_lock.lock();
        tmp = m_max_size;
        m_lock.unlock();
        return tmp;
    }
    /* 生产者线程 */
    bool push(const T& value)
    {
        m_lock.lock();
        if (m_size >= m_max_size)
        {
            m_cond.broadcast();
            m_lock.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = value;
        m_size++;
        m_cond.broadcast();
        m_lock.unlock();
        return true;
    }
    /* 消费者线程 */
    bool pop(T& value)
    {
        m_lock.lock();
        while (m_size <= 0)
        {
            if (!m_cond.wait(m_lock.get()))
            {
                m_lock.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        value = m_array[m_front];
        m_size--;
        m_lock.unlock();
        return true;
    }
    
public:
    locker m_lock;
    cond m_cond;
    T* m_array;//保存元素的数组
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};
#endif