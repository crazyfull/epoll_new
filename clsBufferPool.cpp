#include "clsBufferPool.h"
#include "tlsf.h"

BufferPool::BufferPool(size_t pool_size) { // 1MB pool پیش‌فرض
    pool_ = malloc(pool_size);
    tlsf_ = tlsf_create_with_pool(pool_, pool_size);
}

BufferPool::~BufferPool() {
    tlsf_destroy(tlsf_);
    free(pool_);
}

void *BufferPool::allocate(size_t size) {
    return tlsf_malloc(tlsf_, size);
}

void BufferPool::deallocate(void *ptr) {
    tlsf_free(tlsf_, ptr);
}
