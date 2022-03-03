#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class log
{
public:
    //c++11以后,使用局部变量懒汉式不用加锁
    static log* getInstance()
    {
        static log log_instance;
        return &log_instance;
    }
    //异步写入时写线程工作方式
    static void* flush_log_thread(void* arg)
    {
         log::getInstance()->async_write_log();
    }
    //日志文件名、日志缓冲区大小、每个文件最大行数以及最长的日志阻塞队列
    bool init(const char* file_name, int close_flag, int log_buf_size = 8192, int split_lines = 500000, int max_queue_size = 0);
    //规范日志输出格式
    void write_log(int level, const char* format, ...);
    //将数据刷出缓冲区
    void flush(void);
    
private:
    log();
    virtual ~log();
    void* async_write_log()
    {
        string tmp;
        while (m_log_queue->pop(tmp))
        {
            m_mutex.lock();
            fputs(tmp.c_str(), log_file);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128];//日志路径名
    char log_name[128];//日志文件名
    long long m_line_count;//当天日志总行数记录 用来“分页”
    int m_split_lines;//日志最大行数
    int m_log_buf_size;//日志缓冲区大小
    int m_today;//按天分类，记录当前日志是哪一天
    FILE* log_file;//打开的log文件指针
    char* m_buf;//日志缓冲区（同步写入）
    block_queue<string>* m_log_queue;//阻塞队列（异步写入）
    bool m_is_async;//日志写入方式（同步或者异步)
    locker m_mutex;
    int m_close_log;//关闭日志
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::getInstance()->write_log(0, format, ##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_INFO(format, ...)  if(0 == m_close_log) {log::getInstance()->write_log(1, format,  ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_WARN(format, ...)  if(0 == m_close_log) {log::getInstance()->write_log(2, format,  ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {log::getInstance()->write_log(3, format,  ##__VA_ARGS__); log::getInstance()->flush();}

#endif