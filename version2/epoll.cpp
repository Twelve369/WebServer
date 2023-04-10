#include "epoll.h"

Epoller::Epoller(int max_event_num) : epollfd_(epoll_create(1)), events(max_event_num){
    assert(max_event_num >= 1 && epollfd_ >= 0);
}

Epoller::~Epoller(){
    close(epollfd_);
}

int Epoller::wait(const int timeout){
    return epoll_wait(epollfd_, &events[0], events.size(), timeout);
}

bool Epoller::addfd(int fd, uint32_t event){
    epoll_event ee;
    memset(&ee, 0, sizeof ee);
    ee.data.fd = fd;
    ee.events = event;
    return epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ee) == 0;
}

bool Epoller::removefd(int fd){
    return epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

bool Epoller::modfd(int fd, uint32_t event){
    epoll_event ee;
    memset(&ee, 0, sizeof ee);
    ee.data.fd = fd;
    ee.events = event;
    return epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd, &ee) == 0;
}
