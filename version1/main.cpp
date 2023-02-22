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
    //cout<<"heap size is : "<<timer_heap.size()<<endl;
}

int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        printf("usage : %s <port>\n", argv[0]);
        return 1;
    }

    //创建线程池
    threadpool<httpConect>* pool = nullptr;
    try
    {
        pool = new threadpool<httpConect>;
    }
    catch( ... )
    {
       return 1;
    }

    //创建用户http请求数组
    httpConect* users = new httpConect[MAX_FD];

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    sockaddr_in listen_addr;
    bzero(&listen_addr, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(atoi(argv[1]));
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int ret;
    ret = bind(listenfd, (sockaddr*)&listen_addr, sizeof(listen_addr));   
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    //epoll
    int epollfd = epoll_init();
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);

    //signal
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    set_nonblocking(sig_pipefd[1]);
    addfd(epollfd, sig_pipefd[0], true);

    bool stop_server = false;
    add_sig(SIGHUP);
    add_sig(SIGINT);
    add_sig(SIGTERM);
    add_sig(SIGPIPE);

    while(!stop_server)
    {   
        cout<<"*****************************************"<<endl;
        cout<<"epoll start"<<endl;
        int num = my_epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        cout<<"epoll end"<<endl;
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

                //accept错误
                if(clientfd < 0)
                {
                    printf("error is %d\n", errno);
                    continue;
                }

                //服务器繁忙
                if(httpConect::m_user_count >= MAX_FD)
                {
                    //当clientfd大于65536且用户数小于65536时，user数组会不会溢出？
                    send_error(clientfd, "web server busy\n");
                    continue;
                }
                char addr[20];
                inet_ntop(AF_INET, (void*)&client_addr.sin_addr, addr, 20);
                printf("client connected, client ip: %s \n", addr);
                users[clientfd].init(clientfd, client_addr);
                
                //建立新连接时开始定时
                addTimerToHeap(users + clientfd, TIME_LIMITED);
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
            else if(events[i].events & EPOLLIN)//用户请求事件
            {  
                //只要有用户请求事件就将定时器和用户请求分离
                users[sockfd].closeTimer();
                if(users[sockfd].read())
                {
                    pool->append(users + sockfd);
                }
                else
                {
                    users[sockfd].close_connect();  
                }
            }
            else if(events[i].events & EPOLLOUT)//回复客户请求，即把客户请求的内容发给客户
            {              
                if(!users[sockfd].write())//如果是keep-alive的话就不断开连接
                {
                    users[sockfd].close_connect();
                    users[sockfd].closeTimer();
                }
                addTimerToHeap(users + sockfd, TIME_LIMITED);
            }
            else if(events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                users[sockfd].closeTimer();
                users[sockfd].close_connect();
            }
            
        }

        //每一轮epoll结束后，处理超时连接
        //如果某个连接超时了，但是又触发了EPOLLIN事件，则会为将该连接的定时器分离，然后再请求处理完成后再建立新的定时器
        //这样做的好处是，即使某个连接超时了，如果该连接又发送请求，不会为该连接创建新链接，节省资源。
        handle_empired_connect();
        cout<<"*****************************************"<<endl;
    }

    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;
    return 0;
}   