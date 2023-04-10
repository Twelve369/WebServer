#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include<sys/socket.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/stat.h>
#include<sys/mman.h>    
#include<stdarg.h>
#include<sys/uio.h>
#include<sys/time.h>
#include<queue>

//test
#include<iostream>
using namespace std;

#include"epoll.h"
#include"locker.h"

#define ALIVE_TIME 100

class myTimer;

class httpConect
{
public:
    static const int READ_BUF_SIZE = 2048;
    static const int WRITE_BUF_SIZE = 1024;
    static const int FILE_NAME_LEN = 200;

    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE};
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    enum HTTP_CODE{NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FOBBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, 
                CLOSED_CONNECTION };
    enum LINE_STATE{LINE_OPEN = 0, LINE_OK, LINE_BAD};

    httpConect(){ }
    ~httpConect(){ }

    void init(int fd, const sockaddr_in& addr); //初始化
    void process();
    bool read();
    bool write();
    void close_connect(); //关闭连接
    void addTimer(myTimer* mt);//增加定时器
    void closeTimer();
    
    static int m_epollfd;
    static int m_user_count;

private:    
    void init(); //初始化其他成员
    HTTP_CODE process_read(); //解析http请求
    LINE_STATE parse_line(); //解析一行
    HTTP_CODE parse_requestline(char* text); //解析请求行
    HTTP_CODE parse_header(char* text);  //解析头部
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request(); //根据url确认文件状态，并将文件映射到内存空间中
    void unmap();

    bool process_write(HTTP_CODE result);//把处理结果写入m_iv中
    bool add_response(const char* format, ... );
    bool add_status_line(int statu, const char* title);
    bool add_header(int content_length);
    bool add_content(const char* content);

    char m_read_buf[READ_BUF_SIZE];
    int m_read_idx;
    int m_check_idx;
    int m_start_line;//当前解析行的起始位置
    char m_write_buf[WRITE_BUF_SIZE];
    int m_write_idx;

    int m_sockfd;
    sockaddr_in m_address;

    METHOD m_method;
    CHECK_STATE m_check_state;

    char m_real_file[FILE_NAME_LEN];//文件名
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_len;
    bool m_linger;
    char* m_content;
    char doc_root[200];

    char* m_file_address;//将真实文件映射到该地址上
    struct stat m_file_stat;
    iovec m_iv[2];
    int m_iv_count;

    myTimer* m_timer;//定时器
};


//定时器
class myTimer
{
public:
    myTimer(httpConect* request, int timeout);
    ~myTimer();
    bool isDeleted();
    void setDeleted();
    void update(int timeout);
    size_t getExpiredtime() const;
    bool isValid();
    void clearCon();
    
private:
    size_t empired_time;
    httpConect* http_connect;
    bool deleted;
};

int set_nonblocking(int fd);
void addTimerToHeap(httpConect* request, int timeout);

//定时器比较仿函数
struct timerComr{
    bool operator()(const myTimer* mt1, const myTimer* mt2) const
    {
        //const对象只能调用const成员函数  原因：调用成员函数时会隐式传入当前对象的this指针，因为当前对象是const this指针，导致了类型不匹配
        return mt1->getExpiredtime() > mt2->getExpiredtime(); 
    }
};
#endif