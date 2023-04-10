#ifndef TIMER_H
#define TIMER_H

#include <memory>
#include <queue>
#include <sys/time.h>
#include <functional> 
#include <assert.h>

using TimeCallBack = std::function<void()>;

struct TimeNode{
    size_t empired_time;
    TimeCallBack cb;
    bool operator<(const TimeNode& r) const{
        return this->empired_time > r.empired_time;
    }
};

class Timer
{
public:
    Timer() {}
    ~Timer() {}

    void addTimer(int timeoutms, const TimeCallBack& cb){
        assert(timeoutms > 0);
        TimeNode tn;
        tn.cb = cb;
        timeval tv;
        gettimeofday(&tv, nullptr);
        tn.empired_time = tv.tv_sec*1000 + tv.tv_usec/1000 + timeoutms;
        timeheap_.push(tn);
    }

    void tick(){
        if(timeheap_.empty()) return;

        while(!timeheap_.empty()){
            auto node = timeheap_.top();
            size_t nowtime;
            timeval tv;
            gettimeofday(&tv, nullptr);
            nowtime = tv.tv_sec*1000 + tv.tv_usec/1000;
            if(nowtime < node.empired_time) break;
            node.cb();
            timeheap_.pop();
        }
    }

private:
    std::priority_queue<struct TimeNode> timeheap_;
};

#endif