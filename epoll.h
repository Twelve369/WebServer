#ifndef EPOLL_H
#define EPOLL_H

#include<sys/epoll.h>
#include<assert.h>
#include"httprequest.h"

//test
#include<iostream>
using namespace std;

int epoll_init();
int addfd(int epollfd, int fd, bool oneShot, bool ET);
int removefd(int epollfd, int fd);
int modfd(int epollfd, int fd, int ev);
int my_epoll_wait(int epoll_fd, struct epoll_event *events, int timeout);

#endif