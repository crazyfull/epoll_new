#ifndef SOCKETCONTEXT_H
#define SOCKETCONTEXT_H
// ============================== SocketContext (low-level) ====================
// Owns fd, rw buffers, and queue; used via composition by high-level handlers.
#include "clsIntrusiveList.h"
#include "clsSendQueue.h"
#include <cstdint>
#include <cstdio>
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
    uint64_t lastActive {};

    ~SocketContext(){
        //printf("~SocketContext(---------------------------------------------------------------)\n");
        if(writeQueue){
            delete writeQueue;
            writeQueue = nullptr;
        }
    }
};
#endif // SOCKETCONTEXT_H
