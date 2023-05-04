#include "epoll.h"
 
struct epoll_event* events;

int epoll_init()
{
    int epollfd = epoll_create(512);
    assert(epollfd != -1);
    events = new epoll_event[1024];
    httpConect::m_epollfd = epollfd;
    return epollfd;
}

int addfd(int epollfd, int fd, bool oneShot, bool ET)
{
    
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneShot)
    {
        event.events |= EPOLLONESHOT;
    }

    if(ET){
        event.events |= EPOLLET;
    }
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        perror("epoll addfd error\n");
        return -1;
    } 
    set_nonblocking(fd);
    return 0;
}

int removefd(int epollfd, int fd)
{
    if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) < 0)
    {
        perror("epoll removefd error\n");
        return -1;
    }
    return 0;
}

int modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    if(epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0)
    {
        return -1;
    }
    return 0;
}

int my_epoll_wait(int epollfd, struct epoll_event *events, int timeout)
{
    int num = epoll_wait(epollfd, events, 1024, timeout);
    return num;
}