#include<sys/socket.h>
#include<sys/epoll.h>
#include<assert.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<stdio.h>
#include<signal.h>
#include<unistd.h>

#include"threadpool.h"
#include"httprequest.h"
#include"locker.h"
#include"epoll.h"

#define MAX_FD  65536//最大用户请求个数 即可用的最大文件描述符个数
#define TIME_LIMITED 5000

extern struct epoll_event* events;
extern priority_queue<myTimer*, vector<myTimer*>, timerComr> timer_heap;
extern locker time_queue_lock;


//统一事件源
static int sig_pipefd[2]; 

//信号处理函数
void sig_handler(int sig)
{
    int save_err = errno;
    char msg = sig;
    send(sig_pipefd[1], &msg, 1, 0); //sig_pipefd[1]为非阻塞，若发送缓冲区不足，会返回EWOULDBLOCK错误
    errno = save_err;
}

//添加信号
void add_sig(int sig)
{
    struct sigaction sa; //要加struct，因为有同名sigaction函数
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1); 
}


void send_error(int fd, const char* err_info)
{
    printf("%s", err_info);
    send(fd, err_info, sizeof(err_info), 0);
    close(fd);
}

//处理超时连接
void handle_empired_connect()
{
    time_queue_lock.lock();
    while(!timer_heap.empty())
    {
        myTimer* mt = timer_heap.top();
        if(mt->isDeleted())
        {
            timer_heap.pop();
            delete mt;
        }
        else if(!mt->isValid())
        {
            timer_heap.pop();
            delete mt;
        }
        else
        {
            break;
        }
    }
    time_queue_lock.unlock();
    // cout<<"heap size is : "<<timer_heap.size()<<endl;
}

int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        printf("usage : %s <port>\n", argv[0]);
        return 1;
    }

    //创建线程池
    ThreadPool* pool = new ThreadPool;

    //创建用户http请求数组
    httpConect* users = new httpConect[MAX_FD];

    int ret;

    // 创建监听socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0){
        close(listenfd);
        std::cout << "create socket error" << std::endl;
        return 0;
    }

    // 设置linger
    struct linger opt_linger = {0};
    opt_linger.l_linger = 1;
    opt_linger.l_onoff = 1;
    ret = setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
    if(ret == -1){
        close(listenfd);
        std::cout << "set socket linger error" << std::endl;
        return 0;
    }

    // 设置端口复用
    int reuse = 1;
    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(ret == -1){
        close(listenfd);
        std::cout << "set socket reuseaddr error" << std::endl;
        return 0;
    }

    // 监听socket地址绑定
    sockaddr_in listen_addr;
    bzero(&listen_addr, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(atoi(argv[1]));
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ret = bind(listenfd, (sockaddr*)&listen_addr, sizeof(listen_addr));   
    if(ret < 0){
        close(listenfd);
        std::cout << "socket bind error" << std::endl;
        return 0;
    }

    // 监听socket开始监听
    ret = listen(listenfd, 5);
    if(ret < 0){
        close(listenfd);
        std::cout << "socket listen error" << std::endl;
        return 0;
    }

    // epoll设置
    int epollfd = epoll_init();
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false, false);

    // signal设置
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    set_nonblocking(sig_pipefd[1]);
    addfd(epollfd, sig_pipefd[0], true, true);
    add_sig(SIGHUP);
    add_sig(SIGINT);
    add_sig(SIGTERM);
    add_sig(SIGPIPE);

    bool stop_server = false;
    while(!stop_server)
    {   
        int num = my_epoll_wait(epollfd, events, -1);
        if((num < 0) && (errno != EINTR))
        {
            printf("epoll_wait error\n");
            break;
        }

        for(int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;

            if(sockfd == listenfd)//监听socket监听到了新连接
            {   

                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);

                int clientfd = accept(listenfd, (sockaddr*)&client_addr, &client_addr_len);
                if(clientfd > 0) {
                    //服务器繁忙
                    if(httpConect::m_user_count >= MAX_FD)
                    {
                        send_error(clientfd, "Server busy\n");
                        break;
                    }
                    users[clientfd].init(clientfd, client_addr);
                    //std::cout << "client connect, fd = " << clientfd << std::endl;
                }

                //建立新连接时开始定时
                //addTimerToHeap(users + clientfd, TIME_LIMITED);
            }  
            else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) //处理信号事件
            {
                char sig[1024];
                int ret = recv(sig_pipefd[0], sig, sizeof(sig), 0);
                if(ret == -1 || ret == 0)
                {   
                    continue;
                }
                else
                {
                    for(int i = 0; i < ret; ++i)
                    {
                        printf("I caught signal %d\n", sig[i]);
                        switch (sig[i])
                        {
                            case SIGHUP:
                            case SIGPIPE:
                                continue;

                            case SIGINT:
                            case SIGTERM:
                                stop_server = true;
                        }
                    }
                }
            } 
            else if(events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                //users[sockfd].closeTimer();
                users[sockfd].close_connect();
            }
            else if(events[i].events & EPOLLIN)//用户请求事件
            {  
                //只要有用户请求事件就将定时器和用户请求分离
                //users[sockfd].closeTimer();
                if(users[sockfd].read())
                {   
                    pool->AddTask(std::bind(&httpConect::process, &users[sockfd]));     
                }
                else
                {
                    users[sockfd].close_connect();
                }
            }
            else if(events[i].events & EPOLLOUT)//回复客户请求，即把客户请求的内容发给客户
            {   
                if(users[sockfd].write())//如果是keep-alive的话就不断开连接
                {   
                    //addTimerToHeap(users + sockfd, TIME_LIMITED);
                }else{
                    users[sockfd].close_connect();
                    //users[sockfd].closeTimer();
                }    
            }else{
                std::cout << "unknown event" << std::endl;
            }
        }

        //handle_empired_connect();
    }

    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;
    return 0;
}   