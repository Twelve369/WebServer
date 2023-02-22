#ifndef LOCKER_H
#define LOCKER_H
#include<semaphore.h>
#include<pthread.h>
#include<exception>

//封装POSIX信号量
class sem{
public:
    sem(){
        if(sem_init(&m_sem, 0, 0) != 0){
            throw std::exception();
        }
    }

    ~sem(){
        sem_destroy(&m_sem);
    }

    //信号量-1
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }

    //信号量+1
    bool post(){
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem; //信号量
};


//封装互斥锁
class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex, 0) != 0){
            throw std::exception();
        }
    }

    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

//封装条件变量
class cond
{
public:
    cond()
    {
        if(pthread_mutex_init(&m_mutex, nullptr) != 0)
        {
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond, nullptr) != 0)
        {
            throw std::exception();
        }
    }

    ~cond()
    {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    bool wait()
    {
        int ret;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};


#endif