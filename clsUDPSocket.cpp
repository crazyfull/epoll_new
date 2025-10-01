#include "clsUDPSocket.h"
#include "clsEpollReactor.h"
#include "clsSocketList.h"
#include "clsTCPSocket.h"

UDPSocket::UDPSocket() {}

UDPSocket::~UDPSocket() {
    closeInternal();
}

void UDPSocket::setReactor(EpollReactor* r) {
    m_pReactor = r;
    if (!m_context.rBuffer) {
        m_context.rBuffer = (char*)m_pReactor->bufferPool()->allocate(SLAB_SIZE);
        m_context.rBufferCapacity = SLAB_SIZE;
        m_context.rBufferLength = 0;
    }

    if (m_context.fd == -1) {
        m_context.fd = create_udp_socket();
    }

    //Register with epoll
    m_context.ev.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    bool ret = m_pReactor->register_fd(fd(), &m_context.ev, IS_UDP_SOCKET, this);
    if (!ret) {
        perror("Failed to register UDP fd");
    }
}

void UDPSocket::bind(uint16_t port) {
    if (m_context.fd == -1) {
        m_context.fd = create_udp_socket();
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(m_context.fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("UDP bind failed");
        closeInternal();
    }
}

int UDPSocket::create_udp_socket() {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (fd < 0) {
        perror("UDP socket creation failed");
        return -1;
    }

    TCPSocket::setSocketNonblocking(fd);
    TCPSocket::setSocketResourceAddress(fd, true);

    int rcvbuf = 1024 * 1024; // 1MB
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    return fd;
}

bool UDPSocket::send(const void* data, size_t len, const struct sockaddr_in& to, int timeout_ms, uint64_t* out_id) {
    if (!data || len == 0 || m_context.fd == -1) {
        return false;
    }

    uint64_t id = 0;
    bool has_timeout = (timeout_ms > 0);
    if (has_timeout) {
        id = ++m_seq;
        if (out_id) {
            *out_id = id;
        }

        m_pReactor->addTimeout(id, this, timeout_ms);
    }

    socklen_t tolen = sizeof(to);
    ssize_t sent = ::sendto(m_context.fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT, (const struct sockaddr*)&to, tolen);

    if (sent != static_cast<ssize_t>(len)) {
        if (has_timeout) {
            m_pReactor->cancelTimeout(id);
        }
        if (sent >= 0) {
            // ارسال ناقص
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("UDP sendto failed");
        }
        return false;
    }

    return true;
}

void UDPSocket::ack(uint64_t id) {
    if (id != 0) {
        m_pReactor->cancelTimeout(id);
    }
}

void UDPSocket::onReadableInternal() {
    while (true) {
        struct sockaddr_in peer{};
        socklen_t addrlen = sizeof(peer);
        ssize_t n = ::recvfrom(m_context.fd, m_context.rBuffer, m_context.rBufferCapacity, 0,
                               (struct sockaddr*)&peer, &addrlen);
        if (n > 0) {
            m_context.lastActive = std::chrono::steady_clock::now();
            onResponse(0, reinterpret_cast<const uint8_t*>(m_context.rBuffer), static_cast<size_t>(n), peer, false);
            continue;
        }
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("UDP recvfrom error");
            closeInternal();
            break;
        }
    }
}

void UDPSocket::closeInternal() {
    if (m_context.fd != -1) {
        m_pReactor->del_fd(m_context.fd);
        if (m_context.rBuffer) {
            m_pReactor->bufferPool()->deallocate(m_context.rBuffer);
            m_context.rBuffer = nullptr;
        }
        ::close(m_context.fd);
        m_context.fd = -1;
        m_pReactor->m_pConnectionList->remove(m_context.fd);
    }
}
