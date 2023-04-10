compile : g++ main.cpp epoll.cpp httprequest.cpp -g -o server -lpthread
run : ./server <port>
