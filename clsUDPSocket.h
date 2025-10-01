#ifndef CLSUDPSOCKET_H
#define CLSUDPSOCKET_H
#include "epoll.h"
#include <sys/socket.h>
#include <netinet/in.h>

struct UDPContext {
    int fd = -1;
    char* rBuffer = nullptr;
    size_t rBufferCapacity = 0;
    size_t rBufferLength = 0;
    struct epoll_event ev{};
    std::chrono::steady_clock::time_point lastActive{};
};

class EpollReactor;
class UDPSocket {
public:
    UDPSocket();
    virtual ~UDPSocket();

    // ارسال بسته UDP با تایم‌اوت اختیاری
    // اگر timeout_ms > 0، یک id برای ردیابی تولید می‌شه
    bool send(const void* data, size_t len, const struct sockaddr_in& to, int timeout_ms, uint64_t* out_id = nullptr);

    // حذف تایم‌اوت برای یک درخواست خاص
    void ack(uint64_t id);

    // کال‌بک مجازی برای دریافت پاسخ یا تایم‌اوت
    virtual void onResponse(uint64_t id, const uint8_t* data, size_t len, const struct sockaddr_in& from, bool timed_out) = 0;

    // بایند به پورت محلی (اختیاری، قبل از setReactor)
    void bind(uint16_t port = 0);

    // دسترسی‌ها
    int fd() const { return m_context.fd; }
    void setReactor(EpollReactor* r);
    UDPContext* getContext() { return &m_context; }

protected:
    virtual void onReadableInternal();

private:
    EpollReactor* m_pReactor = nullptr;
    UDPContext m_context{};
    uint64_t m_seq = 0;
    int create_udp_socket();
    void closeInternal();
};

#endif // CLSUDPSOCKET_H
