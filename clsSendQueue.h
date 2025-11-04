#ifndef CLSSENDQUEUE_H
#define CLSSENDQUEUE_H

#include "clsBufferPool.h"
#include <cstring>
#include <deque>

class SendQueue {
public:

    struct Buffer {
        void* data;
        size_t len;
        //size_t offset = 0;
    };

    using iterator = std::deque<Buffer>::iterator;

    SendQueue(BufferPool& pool);
    ~SendQueue();
    void push(const void* data, size_t len);
    bool empty() const;
    Buffer& front();
    void pop_front();
    void clear();
    size_t size() const;
    size_t count() const;
    iterator begin();
    iterator end();


private:
    BufferPool& m_pool;
    std::deque<Buffer> m_queue;
    size_t m_len;
};

#endif // CLSSENDQUEUE_H
