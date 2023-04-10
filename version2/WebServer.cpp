#include "WebServer.h"

WebServer::WebServer(ServerOpt opt) : epoller_ptr_(new Epoller()), threadpool_ptr_(new ThreadPool(opt.thread_num)), timer_ptr_(new Timer()){
    port_ = opt.port;
    timeout_ = opt.timeout;
    is_linger_ = opt.is_linger;
    is_closed_ = false;

    src_path_ = getcwd(nullptr, 100);
    strncat(src_path_, "/html/", 10);

    InitEventTrig(opt.trig);
    if(!InitSocket()){
        std::cout << "server init error" << std::endl;
        is_closed_ = true;
    }
}

WebServer::~WebServer(){

}

void WebServer::work(){

}

// 设置监听fd和连接fd的触发模式
void WebServer::InitEventTrig(Trigermode trig){
    listen_event_ = EPOLLRDHUP;
    connect_event_ = EPOLLRDHUP | EPOLLONESHOT;
    switch (trig)
    {
    case CONNECT_ET:
        connect_event_ |= EPOLLET;
        break;
    case LISTEN_ET:
        listen_event_ |= EPOLLET;
    case CONN_LIST_ET:
        connect_event_ |= EPOLLET, listen_event_ |= EPOLLET;
    default:
        break;
    }
}

bool WebServer::InitSocket(){
    listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd_ == -1){
        std::cout << "get socket error" << std::endl;
        return false;
    }

    // 1 0 服务器主动关闭时，设置立即返回并放弃发送缓冲区（直接发送RST，不需要等待2MSL时间）
    // 1 1 优雅关闭，等待发送缓冲区数据发送完毕
    struct linger tmp = {1, 1};
    setsockopt(listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof tmp);
    
    sockaddr_in listen_addr;
    if(port_ > 65536 | port_ < 1024){
        std::cout << "port: " << port_ << " error" << std::endl;
        close(listenfd_);
        return false;
    }

    bzero(&listen_addr, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(port_);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int ret = bind(listenfd_, (sockaddr*)&listen_addr, sizeof(listen_addr));   
    if(ret == -1){
        std::cout << "bind error port : " << port_ << std::endl;
        close(listenfd_);
        return false;
    }

    ret = listen(listenfd_, 5);
    if(ret == -1){
        std::cout << "listen error" << std::endl;
        close(listenfd_);
        return false;
    }

    if(!epoller_ptr_){
        std::cout << "epoll init error" << std::endl;
        close(listenfd_);
        return false;
    }

    ret = epoller_ptr_->addfd(listenfd_, listen_event_ | EPOLLIN);
    if(ret == 0){
        std::cout << "epoll addfd error" << std::endl;
        close(listenfd_);
        return false;
    }

    setNonBlocking(listenfd_);
    std::cout << "server start in port " << port_ << std::endl;
    return true;
}

int WebServer::setNonBlocking(int fd){
    int old_option = fcntl(fd, F_GETFL, 0);
    int new_option =  old_option | O_NONBLOCK;
    return fcntl(fd, F_SETFL, new_option);
}