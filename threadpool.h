#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<thread>
#include<mutex>
#include<condition_variable>
#include<queue>
#include<functional>
#include<cassert>

/*
    线程池实现:
    1 任务队列，任务队列中包含线程需要执行的回调函数
    2 任务队列的访问需要互斥
    3 条件变量实现条件同步，没有任务时线程睡眠，有任务时唤醒线程
*/
class ThreadPool{
public:
    explicit ThreadPool(size_t Thread_num = 8) : tool_ptr_(std::make_shared<tools>()){
        assert(Thread_num > 0);
        for(size_t i = 0; i < Thread_num; ++i){
            std::thread([&](){   
                std::unique_lock<std::mutex> locker(tool_ptr_->mtx);
                while(true){
                    if(tool_ptr_->tasks.size()){
                        auto task = tool_ptr_->tasks.front();
                        tool_ptr_->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock();
                    }else if(tool_ptr_->is_closed){
                        break;
                    }else{
                        tool_ptr_->cond.wait(locker); // 任务队列为空时，锁被释放，线程进入睡眠
                    }
                }
            }).detach();
        }
    }

    ~ThreadPool(){
        if(tool_ptr_){
            std::lock_guard<std::mutex> locker(tool_ptr_->mtx);
            tool_ptr_->is_closed = true;
        }
        tool_ptr_->cond.notify_all();
    }

    template<class T>
    void AddTask(T&& task){
        {
            std::unique_lock<std::mutex> lock(tool_ptr_->mtx);
            tool_ptr_->tasks.emplace(std::forward<T>(task));
        }
        tool_ptr_->cond.notify_one();
    }

private:
    struct tools{
        std::mutex mtx;
        std::condition_variable cond;
        bool is_closed;
        std::queue<std::function<void()>> tasks;
    };
    std::shared_ptr<tools> tool_ptr_; 
};

#endif