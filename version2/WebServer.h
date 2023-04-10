#ifndef WEBSERVER_H
#define WEBSERVER_H
#include "epoll.h"
#include "threadpool.h"
#include "timer.h"
#include <unistd.h>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <iostream>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

struct ServerOpt
{
    int port;
    int timeout;
    bool is_linger;
    int thread_num;
    Trigermode trig;
};

enum Trigermode{
    DEFAULT = 0,
    CONNECT_ET = 1,
    LISTEN_ET = 2,
    CONN_LIST_ET = 3
};

class WebServer{
public:
    WebServer(ServerOpt opt);
    ~WebServer();

    void work();
    void InitEventTrig(Trigermode trig);
    bool InitSocket();
    int setNonBlocking(int fd);

private:

    int listenfd_;
    bool is_closed_;
    char* src_path_;
    int listen_event_;
    int connect_event_;

    int port_;
    int timeout_;
    bool is_linger_;
    std::unique_ptr<Epoller> epoller_ptr_;
    std::unique_ptr<ThreadPool> threadpool_ptr_;
    std::unique_ptr<Timer> timer_ptr_;
};

#endif