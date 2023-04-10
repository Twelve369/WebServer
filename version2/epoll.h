#ifndef EPOLL_H
#define EPOLL_H

#include <sys/epoll.h>
#include <assert.h>
#include <memory>
#include <cstring>
#include <vector>
#include <unistd.h>

class Epoller{
public:
    Epoller(int max_event_num = 1024);
    ~Epoller();

    int wait(int timeout);
    bool addfd(int fd, uint32_t event);
    bool removefd(int fd);
    bool modfd(int fd, uint32_t event);

private:
    int epollfd_;
    std::vector<struct epoll_event> events;
};

#endif