#ifndef EPOLL_H
#define EPOLL_H
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netinet/ip.h>  /* superset of previous */
#include <sys/timerfd.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>


// ============================== Tunables ===============================
                                                 // EPOLLERR
#define EPOLL_EVENTS_TCP_NONBLOCKING (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLET) //
#define EPOLL_LISTINER_EVENTS (EPOLLIN | EPOLLRDHUP | EPOLLET) // EPOLLRDHUP | EPOLLET bada ezafe shodan test nashode hanooz


#endif // EPOLL_H
