#include "epoll.h"

Epoller::Epoller(int max_event_num) : epollfd_(epoll_create(1)){
    assert(max_event_num >= 1);
    events_ptr_ = make_shared<vector<struct epoll_event>> (new vector<epoll_event>(max_event_num));
}

Epoller::~Epoller(){
    close(epollfd_);
}

int Epoller::wait(const int timeout){
    return epoll_wait(epollfd_, &(*events_ptr_)[0], (*events_ptr_).size(), timeout);
}

bool Epoller::addfd(int fd, uint32_t events){
    epoll_event ee;
    memset(&ee, 0, sizeof ee);
    ee.data.fd = fd;
    ee.events = events;
    return epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ee) == 0;
}

bool Epoller::removefd(int fd){
    return epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

bool Epoller::modfd(int fd, uint32_t events){
    epoll_event ee;
    memset(&ee, 0, sizeof ee);
    ee.data.fd = fd;
    ee.events = events;
    return epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd, &ee) == 0;
}
