#ifndef EPOLL_H
#define EPOLL_H

#include<sys/epoll.h>
#include<assert.h>
#include"httprequest.h"

//test
#include<iostream>
using namespace std;

#define MAX_EVENT_NUM 5000
#define Listen 1024

int epoll_init();
int addfd(int epollfd, int fd, bool oneShot);
int removefd(int epollfd, int fd);
int modfd(int epollfd, int fd, int ev);
int my_epoll_wait(int epoll_fd, struct epoll_event *events, int max_events, int timeout);

#endif