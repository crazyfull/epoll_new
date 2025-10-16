#include "clsBufferPool.h"
#include "tlsf.h"

BufferPool::BufferPool(size_t pool_size) {
    if (pool_size < 4096)
        pool_size = 4096;

    pool_ = malloc(pool_size);
    m_tlsf = tlsf_create_with_pool(pool_, pool_size);

}

BufferPool::~BufferPool() {
    tlsf_destroy(m_tlsf);
    free(pool_);
}

void *BufferPool::allocate(size_t size) {
    return tlsf_malloc(m_tlsf, size);
}

void BufferPool::deallocate(void *ptr) {
    tlsf_free(m_tlsf, ptr);
}

size_t BufferPool::size()
{
    return tlsf_size();
}
