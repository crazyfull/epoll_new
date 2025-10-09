#ifndef SOCKETCONTEXT_H
#define SOCKETCONTEXT_H
// ============================== SocketContext (low-level) ====================
// Owns fd, rw buffers, and queue; used via composition by high-level handlers.
#include "clsSendQueue.h"
#include <cstdint>
#include <sys/epoll.h>
#include <chrono>

struct SocketContext
{
    int fd {-1 };
    uint16_t port { 0 };;
    char *rBuffer { nullptr };
    struct epoll_event ev {};
    size_t rBufferCapacity { 0 };
    size_t rBufferLength { 0 };
    SendQueue* writeQueue {nullptr};
    std::chrono::steady_clock::time_point lastActive {};
};
#endif // SOCKETCONTEXT_H
