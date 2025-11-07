#ifndef CLSGCLIST_H
#define CLSGCLIST_H
#include <cstdio>
#include <deque>
#include <cstdint>
#include <malloc.h>
#include <time.h>

#ifndef LIKELY
#  define LIKELY(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif


template <typename T>
class GCList {
    struct GCNode {
        T*        ptr;
        uint32_t  createdTime;
    };

    // تنظیم‌ها (می‌تونی بسته به فشار ترافیک تغییر بدی)
    static constexpr size_t   THRESHOLD_SIZE = 1024;   // اگر صف به این حد رسید، شروع به آزادسازی کن
    static constexpr uint32_t MAX_AGE_MS     = 10; // آیتم‌های قدیمی‌تر از این مقدار حذف می‌شن

    std::deque<GCNode> m_qList;

public:
    GCList()                     = default;
    GCList(const GCList&)        = delete;
    GCList& operator=(GCList&&)  = delete;
    GCList(GCList&&)             = delete;
    GCList& operator=(const GCList&) = delete;

    ~GCList() noexcept {
        flush_all();
    }

    // add object
    void retire(T* p) noexcept {
        //is not thread-safe
        m_qList.push_back(GCNode{p, now_s_coarse()});

        //flush();
    }

    void flush() noexcept {
        if (UNLIKELY(m_qList.size() < THRESHOLD_SIZE + 1))
            return; // hanooz be adnaze kafi bozorg nashode

        uint32_t now_ms = now_s_coarse();
        auto it = std::next(m_qList.begin(), THRESHOLD_SIZE);// item shomare 1024
        if ((now_ms - it->createdTime) >= MAX_AGE_MS) {

            // tamame itemhaye ghable 1024 delete beshan chon bishtar az 10 sanie omr daran
            auto erase_end = m_qList.begin() + THRESHOLD_SIZE;
            for (auto itr = m_qList.begin(); itr != erase_end; ++itr) {
                T* p = itr->ptr;
                delete p;
                printf("deleted\n");
            }

            m_qList.erase(m_qList.begin(), erase_end);
        }
    }

    void flush_all() noexcept {
        if(!m_qList.empty())
            //printf("GC flush_all()\n");

        while (!m_qList.empty()) {
            T* p = m_qList.front().ptr;
            m_qList.pop_front();
            //printf("flush_all delete..................................................................................\n", m_qList.size());
            delete p;
        }


    }

private:
    static uint32_t now_s_coarse() noexcept {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return static_cast<uint32_t>(ts.tv_sec); // get secound
    }
};



#endif // CLSGCLIST_H

