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
#define EPOLL_EVENTS_TCP_MULTITHREAD_NONBLOCKING (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLET) //

static constexpr int MAX_EVENTS = 2048;             // (7) batch epoll_wait
static constexpr int LISTEN_BACKLOG = SOMAXCONN;
static constexpr size_t BACK_PRESSURE = 512*1024;   //1*(1024*1024); //1 MG
//static constexpr int IDLE_TIMEOUT_SEC = 30;       // (13) graceful idle GC

static constexpr size_t BUFFER_POOL_SIZE = 10 * (1024*1024); //10 MG
static constexpr size_t SLAB_SIZE = 8 * 1024;       // (4)(9) pooled buffers

//static constexpr size_t HIGH_WATERMARK = 64 * 1024;
static constexpr size_t LOW_WATERMARK = 128 * 1024;

//static constexpr size_t RING_HIGH = 4 * 1024;     // (12) backpressure high بین ۵۰٪ تا ۷۵٪ ظرفیت بافر هر سوکت (مثل SLAB_SIZE) تنظیم می‌کنند
//static constexpr size_t RING_LOW = 2 * 1024;      // (12) backpressure low
//اگر SLAB_SIZE=64KB، تنظیم RING_HIGH=32KB و RING_LOW=16KB منطقی است.

#endif // EPOLL_H
