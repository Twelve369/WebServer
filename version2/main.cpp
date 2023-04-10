#include <iostream>
#include <functional>
#include "threadpool.h"
#include "timer.h"
#include "WebServer.h"
#include <memory>

int main(){

    ServerOpt so;
    so.is_linger = true;
    so.port = 9010;
    so.thread_num = 4;
    so.timeout = 10;
    so.trig = CONN_LIST_ET;
    std::unique_ptr<WebServer> server(new WebServer(so));
    server->work();

    return 0;
}