#include "clsSendQueue.h"
#include <cstdio>


SendQueue::SendQueue(BufferPool &pool) : m_pool(pool) {
    m_len = 0;
}

SendQueue::~SendQueue() {
    clear();
}

void SendQueue::push(const void *data, size_t len) {
    void* buf = m_pool.allocate(len);
    if(buf){
        memcpy(buf, data, len);
        m_queue.push_back({buf, len});
        m_len += len;
        //printf("SendQueue::push: [%zu] size[%zuKB]\n", len, m_len / 1024);//m_queue.size()
    }else{
        printf("SendQueue::push: can not allocate\n");
    }
}

/*
void SendQueue::push(const void *data, size_t len) {
    void* buf = m_pool.allocate(len);
    if(buf){
        memcpy(buf, data, len);
        Buffer b;
        b.data = (char*)buf;
        b.len = len;
        b.offset = 0;
        m_queue.push_back(b);
        m_len += len;
    } else {
        printf("SendQueue::push: can not allocate\n");
    }
}
*/

bool SendQueue::empty() const {
    return m_queue.empty();

}

void SendQueue::pop_front() {
    if (!m_queue.empty()) {
        printf("SendQueue::pop_front(): [%zu]\n", m_len);
        m_len -= m_queue.front().len;
        m_pool.deallocate(m_queue.front().data);
        m_queue.pop_front();

        //printf("pop_front\n");

    }
}

void SendQueue::clear() {
    while (!m_queue.empty()) {
        pop_front();
    }
}

size_t SendQueue::size() const {
    //printf("SendQueue::size(): [%zu]\n", m_queue.size());
    return m_len;
}

size_t SendQueue::count() const {
    return m_queue.size();
}

SendQueue::iterator SendQueue::begin()
{
    return m_queue.begin();
}

SendQueue::iterator SendQueue::end()
{
    return m_queue.end();
}

SendQueue::Buffer &SendQueue::front() {
    //printf("SendQueue::front(): [%zu]\n", m_queue.size());
    return m_queue.front();
}
