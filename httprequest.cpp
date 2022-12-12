#include "httprequest.h"

const char* ok_title_200 = "OK";
const char* error_title_400 = "Bad Request";
const char* error_form_400 = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_title_403 = "Forbidden";
const char* error_form_403 = "You do not have permission to get file from this server.\n";
const char* error_title_404 = "Not Found";
const char* error_form_404 = "The requested file was not found on this server.\n";
const char* error_title_500 = "Internal Error";
const char* error_form_500 = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/root/myWebServer/html";

int httpConect::m_epollfd = -1;
int httpConect::m_user_count = 0;
//使用优先队列构建时间堆定时器
priority_queue<myTimer*, vector<myTimer*>, timerComr> timer_heap;
locker time_queue_lock;

void addTimerToHeap(httpConect* request, int timeout)
{
    myTimer *mt = new myTimer(request, timeout);
    request->addTimer(mt);
    time_queue_lock.lock();
    timer_heap.push(mt);
    time_queue_lock.unlock();
}

int set_nonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL, 0);
    int new_option =  old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void httpConect::addTimer(myTimer* mt)
{
    if(m_timer == nullptr)
    {
        m_timer = mt;
    }
}

void httpConect::closeTimer()
{
    //cout<<"extract timer from connect"<<endl;
    if(m_timer != nullptr)
    {
        m_timer->clearCon();
        m_timer = nullptr;
    }
}

void httpConect::init(int fd, const sockaddr_in& addr)
{
    m_sockfd = fd;
    m_address = addr;
    addfd(m_epollfd, fd, true);
    ++m_user_count;

    //设置地址可重复使用，调试用
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    init();
}

void httpConect::init()
{
    m_method = GET; 
    m_check_state = CHECK_STATE_REQUESTLINE;

    m_read_idx = 0;
    m_check_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_content_len = 0;
    m_content = 0;
    m_linger = false;
    
    memset(m_read_buf, '\0', sizeof(m_read_buf));
    memset(m_write_buf, '\0', sizeof(m_write_buf));
    memset(m_real_file, '\0', sizeof(m_real_file));
}

void httpConect::close_connect()
{
    if(m_epollfd != -1)
    {
        printf("close connect...\n");
        --m_user_count;
        removefd(m_epollfd, m_sockfd);
    }
}

bool httpConect::read()
{
    if(m_read_idx >= READ_BUF_SIZE)
    {
        return false;
    }

    while(true) //ET模式 遇到EAGAIN和EWOULDBLOCK说明客户端还有信息没发过来
    {   
        int read_num = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUF_SIZE-m_read_idx, 0);
        if(read_num == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return true;
            }
            return false;
        }
        else if(read_num == 0) //FIN
        {   
            return false;
        }
        m_read_idx += read_num;
    }
}

bool httpConect::write()//to_send大小不太确定
{
    int to_send = m_iv[0].iov_len;
    if(m_iv_count == 2) to_send += m_iv[1].iov_len;
    int have_send = 0;
    if(to_send == 0)//继续接收用户请求
    {
        init();
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return true;
    }

    while(true)
    {   
        int num = writev(m_sockfd, m_iv, m_iv_count);
        if(num == -1)
        {
            if(errno == EAGAIN)//TCP写缓存不足
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap(); //取消内存映射
            return false;
        }

        have_send += num;

        if(have_send >= to_send)//发送完成
        {   
            unmap();
            if(m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);

                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

//可变参数 valist va_start va_end vsnprintf
bool httpConect::add_response(const char* format, ... )
{
    if(m_write_idx >= WRITE_BUF_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUF_SIZE - m_write_idx, format, arg_list);
    if(len >= WRITE_BUF_SIZE - m_write_idx)
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool httpConect::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool httpConect::add_header(int content_length)
{
    add_response("Content-Length: %d\r\n", content_length);
    add_response("Connection: %s\r\n", (m_linger==true) ? "keep-alive" : "close");
    add_response("\r\n");
}

bool httpConect::add_content(const char* content)
{
    return add_response("%s", content);   
}

bool httpConect::process_write(HTTP_CODE result)
{
    switch(result)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_title_500);
            add_header(strlen(error_form_500));
            if(!add_content(error_form_500))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_title_400);
            add_header(strlen(error_form_400));
            if(!add_content(error_form_400))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_title_404);
            add_header(strlen(error_form_404));
            if(!add_content(error_form_404))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_title_200);

            if(m_method == POST)
            {
                cout<<m_content<<endl;
                add_response("Content-type:text/html\r\n");
                add_response("\r\n");
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_content;
                m_iv[1].iov_len = m_content_len;
                m_iv_count = 2;
                return true;
            }

            if(m_file_stat.st_size == 0)
            {
                const char* ok_form = "this is a empty file\n";
                add_header(strlen(ok_form));
                if(!add_content(ok_form))
                {
                    return false;
                }
            }
            else
            {
                add_header(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            break;
        }
        case FOBBIDDEN_REQUEST:
        {   
            add_status_line(403, error_title_403);
            add_header(strlen(error_form_403));
            if(!add_content(error_form_403))
            {
                return false;
            }
            break;
        }
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = WRITE_BUF_SIZE;
    m_iv_count = 1;
    return true;
}

// struct stat {
//         mode_t     st_mode;       //文件对应的模式，文件，目录等
//         ino_t      st_ino;       //inode节点号   
//         dev_t      st_dev;        //设备号码
//         dev_t      st_rdev;       //特殊设备号码
//         nlink_t    st_nlink;      //文件的连接数
//         uid_t      st_uid;        //文件所有者
//         gid_t      st_gid;        //文件所有者对应的组
//         off_t      st_size;       //普通文件，对应的文件字节数
//         time_t     st_atime;      //文件最后被访问的时间
//         time_t     st_mtime;      //文件内容最后被修改的时间
//         time_t     st_ctime;      //文件状态改变时间
//         blksize_t st_blksize;    //文件内容对应的块大小
//         blkcnt_t   st_blocks;     //文件内容对应的块数量
// };
httpConect::HTTP_CODE httpConect::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILE_NAME_LEN - len); //溢出安全

    if(strcmp(m_url, "/") == 0)//访问主页
    {   
        strncpy(m_real_file + len, "/main.html", FILE_NAME_LEN - len);
        stat(m_real_file, &m_file_stat);
        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        return FILE_REQUEST;
    }

    if(strcmp(m_url, "/post") == 0)//POST请求
    {   
        cout<<"post request"<<endl;
        return FILE_REQUEST;
    }

    if(stat(m_real_file, &m_file_stat) < 0) //stat()用来将参数file_name所指的文件状态, 复制到参数buf所指的结构中 成功返回0 失败返回-1
    {
        return NO_RESOURCE;
    }
    
    if(!(m_file_stat.st_mode & S_IROTH)) //S_IROTH 其他读（所有用户都能读）
    {
        return FOBBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)) //S_ISDIR() 判断一个路径是不是目录
    {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void httpConect::unmap()
{
    if(!m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

httpConect::LINE_STATE httpConect::parse_line()
{   
    for(; m_check_idx < m_read_idx; ++m_check_idx)
    {
        if(m_read_buf[m_check_idx] == '\r')
        {   
            if(m_check_idx + 1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_check_idx + 1] == '\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            else
            {
                return LINE_BAD;
            }
        }
        else if(m_read_buf[m_check_idx] == '\n')
        {
            if(m_check_idx > 0 && m_read_buf[m_check_idx - 1] == '\r')
            {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx] = '\0';
                ++m_check_idx;
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// GET  http://baidu.com    HTTP/1.1
httpConect::HTTP_CODE httpConect::parse_requestline(char* text)
{
    m_url = strpbrk(text, " \t"); // strpbrk() 找出第二参数中任一个字符出现在第一参数中的指针  没有的话返回null
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    cout<<method<<endl;
    if(strcasecmp(method, "GET") == 0)  //strcasecmp() 两个字符串比大小 相同返回0
    {
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0) //如果http请求是post请求
    {
        m_method = POST;
    }
    else
    {
        BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");  //strspn() 返回str起始部分group中任意字符的字符数
    m_version = strpbrk(m_url, " \t");

    if(!m_version)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        //m_url = strchr(m_url, '/'); //返回字符第一次出现在字符串的位置（指针）
        m_url = strpbrk(m_url, " \0");
    }
    
    if(!m_url)
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

httpConect::HTTP_CODE httpConect::parse_header(char* text)
{
    if(text[0] == '\0') //空白行
    {
        if(m_content_len == 0)
        {
            return GET_REQUEST;
        }
        m_check_state = CHECK_STATE_CONTENT;
        return NO_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_len = atol(text);
    }
    else if(strncasecmp(text, "Host", 4) == 0)
    {
        text += 4;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("unknown header : %s\n", text);
    }

    return NO_REQUEST;
}


httpConect::HTTP_CODE httpConect::parse_content(char* text)
{
    //解析content部分的第一行后，记录content的起点
    if(m_content == 0)
    {
        m_content = text;
    }

    if(m_read_idx >= m_check_idx + m_content_len)
    {
        m_content[m_content_len] = '\0';
        cout<<m_content<<endl;
        //text[m_content_len] = '\0';
        //m_content = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;//说明content部分还未完全接收
}

httpConect::HTTP_CODE httpConect::process_read()
{
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    //如果处于CHECK_STATE_CONTENT状态就不解析了，直接按content-length判断。
    while( ((m_check_state == CHECK_STATE_CONTENT) && (line_state == LINE_OK)) || ((line_state = parse_line()) == LINE_OK))
    {   
        char* text = m_read_buf + m_start_line;
        m_start_line =  m_check_idx;
     
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_requestline(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_header(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) 
                {
                    return do_request();
                }
                line_state = LINE_OPEN; 
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

void httpConect::process()
{
    HTTP_CODE result = process_read();
    if(result == NO_REQUEST)
    {
        addTimerToHeap(this, 10000);//超时时间10秒
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    if(!process_write(result))
    {
        closeTimer();
        close_connect();
        return;
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


myTimer::myTimer(httpConect* request, int timeout) : deleted(false), http_connect(request)
{
    //cout<<"myTimer()"<<endl;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    empired_time = tv.tv_sec*1000 + tv.tv_usec/1000 + timeout;//以毫秒来计时 tv_sec为秒 tv_usec为微秒
}

myTimer::~myTimer()
{
    //cout<<"~myTimer()"<<endl;
    if(http_connect != nullptr)
    {       
        http_connect->close_connect();
        http_connect = nullptr; 
    }
}

void myTimer::clearCon()
{
    http_connect = nullptr;
    deleted = true;
}

size_t myTimer::getExpiredtime() const
{
    return empired_time;
}

bool myTimer::isDeleted()
{
    return deleted;
}

bool myTimer::isValid()
{
    struct timeval now;
    gettimeofday(&now, nullptr);
    size_t now_time = now.tv_sec*1000 + now.tv_usec/1000;

    if(now_time < empired_time) //还没超时
    {
        return true;
    }
    else
    {
        return false;
    }
}

void myTimer::setDeleted()
{
    deleted = true;
}

void myTimer::update(int timeout)
{
    struct timeval tv;
    empired_time = tv.tv_sec*1000 + tv.tv_usec/1000 + timeout;
}




