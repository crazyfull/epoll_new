#include "clsTimer.h"
#include "clsSocketList.h"
#include "clsEpollReactor.h"

Timer::Timer() {
    m_singleShotMode = false;
    m_pReactor = nullptr;
    m_SocketContext.fd = -1;
}

Timer::~Timer() {
    stop();
}

void Timer::setReactor(EpollReactor *r) {
    m_pReactor = r;
}

bool Timer::start(int interval_ms, Callback cb) {
    return startInternal(interval_ms, std::move(cb), false);
}

bool Timer::singleShot(int interval_ms, Callback cb) {
    return startInternal(interval_ms, std::move(cb), true);
}

void Timer::stop() {
    if (m_SocketContext.fd != -1) {
        printf("Timer::stop()\n");
        m_pReactor->del_fd(m_SocketContext.fd, true);
        ::close(m_SocketContext.fd);
        m_SocketContext.fd = -1;
    }
}

void Timer::setInterval(int interval_ms) {
    if (m_SocketContext.fd == -1)
        return;

    itimerspec new_value{};
    new_value.it_value.tv_sec = interval_ms / 1000;
    new_value.it_value.tv_nsec = (interval_ms % 1000) * 1000000;
    if (!m_singleShotMode) {
        new_value.it_interval = new_value.it_value;
    }

    timerfd_settime(m_SocketContext.fd, 0, &new_value, nullptr);
}

void Timer::onTick() {

    uint64_t expirations;
    read(m_SocketContext.fd, &expirations, sizeof(expirations)); // مصرف کنیم

    if (m_callback)
        m_callback();

    if (m_singleShotMode) {
        stop();
    }

}

int Timer::fd() const {
    return m_SocketContext.fd;
}

bool Timer::startInternal(int interval_ms, Callback cb, bool singleShot) {
    if(!m_pReactor)
        return false;

    stop(); // اگر قبلاً فعال بوده

    m_callback = std::move(cb);
    m_singleShotMode = singleShot;

    m_SocketContext.fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_SocketContext.fd == -1) {
        perror("timerfd_create");
        return false;
    }

    itimerspec new_value{};
    new_value.it_value.tv_sec = interval_ms / 1000;
    new_value.it_value.tv_nsec = (interval_ms % 1000) * 1000000;

    if (!singleShot) {
        new_value.it_interval = new_value.it_value; // periodic
    }

    if (timerfd_settime(m_SocketContext.fd, 0, &new_value, nullptr) == -1) {
        perror("timerfd_settime");
        close(m_SocketContext.fd);
        m_SocketContext.fd = -1;
        return false;
    }

    //epoll_event ev{};
    m_SocketContext.ev.events = EPOLLIN;

    bool ret = m_pReactor->register_fd(fd(), &m_SocketContext.ev, IS_TIMER_SOCKET, this);
    return ret;
}
