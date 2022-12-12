#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include"locker.h"
#include<exception>
#include<iostream>

#define THREAD_NUM 4

template<typename T>
class threadpool{
public:
    threadpool(int thread_num, int request_num);
    ~threadpool();
    bool append(T* request);

private:
    static void* worker(void* arg);
    void run(int id);

private:
    int m_thread_num;//线程个数
    int m_request_num;//最大请求个数
    pthread_t* m_pthread;//线程池数组
    std::list<T*> m_request_queue;//请求队列
    locker m_queue_locker; //请求队列的锁
    //sem m_queue_stat;
    bool m_stop;
    cond m_queue_cond;
};

int round_num = 0;
int thread_id = 0;

template<typename T>
threadpool<T>::threadpool(int thread_num = THREAD_NUM, int request_num = 10000) : m_thread_num(thread_num), m_request_num(request_num), m_pthread(nullptr), m_stop(false) {
    if(thread_num <= 0 || request_num <= 0){
        throw std::exception();
    }

    //创建线程池
    m_pthread = new pthread_t[m_thread_num];

    if(!m_pthread){
        throw std::exception();
    }

    //创建线程，每个线程都调用worker静态函数
    for(int i = 0; i < m_thread_num; ++i){
        std::cout<<"Create the "<<i+1<<"th thread"<<std::endl;
        if(pthread_create(m_pthread + i, nullptr, worker, this) != 0){
            delete []m_pthread;
            throw std::exception();
        }
    
        //设置为脱离线程
        if(pthread_detach(m_pthread[i])){
            delete []m_pthread;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete []m_pthread;
    m_stop = true;
}

//往请求队列中添加请求
template<typename T>
bool threadpool<T>::append(T* request){
    m_queue_locker.lock();
    if(m_request_queue.size() >= m_request_num){
        m_queue_locker.unlock();
        return false;
    }
    m_request_queue.push_back(request);
    m_queue_locker.unlock();
    //m_queue_stat.post(); // 信号量+1 唤醒睡眠线程
    m_queue_cond.broadcast();//唤醒所有线程
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool*)arg;
    int id = thread_id;
    ++thread_id;
    pool->run(id);
}

template<typename T>
void threadpool<T>::run(int id){

    while(!m_stop){
        m_queue_cond.wait();//线程设置为休眠状态
        if(round_num != id) continue;
        //std::cout<<id<<"th thread working"<<std::endl;
        ++round_num;
        round_num %= THREAD_NUM;

        m_queue_locker.lock();
        if(m_request_queue.empty()){ //对于请求队列的操作都要放进临界区中
            m_queue_locker.unlock();
            continue;
        }
        T* request = m_request_queue.front();
        m_request_queue.pop_front();
        m_queue_locker.unlock();

        if(!request) continue;
        request->process(); //T任务类型必须提供process函数
    }
}

#endif