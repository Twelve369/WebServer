#ifndef EPOLL_H
#define EPOLL_H

#include<sys/epoll.h>
#include<assert.h>
#include<memory>
#include<cstring>

#include"httprequest.h"

class Epoller{

    Epoller(int max_event_num = 1024);
    ~Epoller();

    int wait(int timeout);
    bool addfd(int fd, uint32_t events);
    bool removefd(int fd);
    bool modfd(int fd, uint32_t events);

private:
    int epollfd_;
    std::shared_ptr<vector<struct epoll_event>> events_ptr_;
};

#endif