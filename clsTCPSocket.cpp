#include "clsTCPSocket.h"
#include "clsEpollReactor.h"
#include "clsSocketList.h"
#include "epoll.h"
#include "clsDNSLookup.h"
#include "clsServer.h"

TCPSocket::TCPSocket()
{

}

void TCPSocket::setReactor(EpollReactor *r) {
    m_pReactor = r;
    if (!m_SocketContext.writeQueue) {
        m_SocketContext.writeQueue = new SendQueue(*m_pReactor->bufferPool());
    }
}

void TCPSocket::setSocketOption(int fd, int name, bool isEnable)
{
    int ka = isEnable;
    setsockopt(fd, SOL_SOCKET, name, &ka, sizeof(ka));
}

void TCPSocket::setSocketShared(int fd, bool isEnable)
{
    //Allow local port reuse
    setSocketOption(fd, SO_REUSEPORT, isEnable);
}

int TCPSocket::setSocketNonblocking(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    if(f == -1)
        return -1;

    return fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

void TCPSocket::setSocketResourceAddress(int fd, bool isEnable)
{
    setSocketOption(fd, SO_REUSEADDR, isEnable);
}

void TCPSocket::setSocketNoDelay(int fd, bool isEnable)
{
    setSocketOption(fd, TCP_NODELAY, isEnable);
}

void TCPSocket::setSocketCork(int fd, bool isEnable)
{
    //TCP_CORK
    setSocketOption(fd, TCP_CORK, isEnable);
}

void TCPSocket::setSocketKeepAlive(int fd, bool isEnable)
{
    setSocketOption(fd, SO_KEEPALIVE, isEnable);
}

void TCPSocket::setSocketLowDelay(int fd, bool isEnable)
{
    int tos = IPTOS_LOWDELAY;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
}


SocketContext *TCPSocket::getSocketContext()
{
    return &m_SocketContext;
}

void TCPSocket::handleOnData(const uint8_t *d, size_t n) {
    if(onData_)
        onData_(this, d, n);
}



void TCPSocket::pause_reading() {
    if (!m_pReactor || m_readPaused)
        return;
    printf("pause_reading()\n");

    m_pReactor->mod_remove(&m_SocketContext, ~EPOLLIN); // Disable EPOLLIN
    m_readPaused = true;
}

void TCPSocket::resume_reading() {
    if (!m_pReactor || !m_readPaused)
        return;

    printf("resume_reading()\n");
    m_pReactor->mod_add(&m_SocketContext,  EPOLLIN);
    m_readPaused = false;
}


/**/
void TCPSocket::close()
{
    m_pReactor->del_fd(m_SocketContext.fd);

    if(m_SocketContext.rBuffer) {
        m_pReactor->bufferPool()->deallocate(m_SocketContext.rBuffer);
        //::free(m_SocketContext.rBuffer);
        m_SocketContext.rBuffer = nullptr;
    }

    m_SocketContext.writeQueue->clear();

    printf("close()\n");

    ::shutdown(m_SocketContext.fd, SHUT_RDWR);
    ::close(m_SocketContext.fd);
    this->onClose();
    printf("recBytes: [%d] sndBytes: [%d]\n", recBytes, sndBytes);
    // m_pReactor->onSocketClosed(m_SocketContext.fd);
}

void TCPSocket::_connect(const char *hostname, char **ips, size_t count)
{
    if (!ips || count == 0) {
        printf("No result for %s\n", hostname);
        onConnectFailed();
        return;
    }

    // استفاده از اولین IP (برای سادگی؛ می‌تونی چند IP رو امتحان کنی اگر لازم)
    const char* ip = ips[0];
    printf("connecting to [%s] (%s:%d)...\n", hostname, ip, m_SocketContext.port);

    // ایجاد سوکت TCP non-blocking
    m_SocketContext.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (m_SocketContext.fd == -1) {
        perror("socket creation failed");
        onConnectFailed();
        return;
    }

    // تنظیم گزینه‌های سوکت
    TCPSocket::setSocketNoDelay(m_SocketContext.fd, true);
    //SocketBase::setSocketLowDelay(m_SocketContext.fd, true);
    TCPSocket::setSocketKeepAlive(m_SocketContext.fd, true);
    TCPSocket::setSocketResourceAddress(m_SocketContext.fd, true);

    // آماده‌سازی آدرس
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_SocketContext.port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        // اگر IPv4 نبود، می‌تونی IPv6 رو چک کنی (برای حالا ساده نگه می‌داریم)
        perror("inet_pton failed (IPv4 assumed)");
        onConnectFailed();
        close();
        return;
    }

    // فراخوانی connect
    int ret = ::connect(m_SocketContext.fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        // اتصال فوری موفق (نادر اما ممکن)
        adoptFd(m_SocketContext.fd, m_pReactor); // اضافه کردن به shard/reactor
        onConnected();
        return;
    }

    if (errno == EINPROGRESS) {


        // اتصال در حال انجام (non-blocking)
        adoptFd(m_SocketContext.fd, m_pReactor); // اضافه کردن به shard/reactor

        // ثبت برای EPOLLOUT تا وقتی writable شد، چک کنیم (در onWritable مدیریت می‌شه)

        //set events
        m_SocketContext.ev.events = EPOLL_EVENTS_TCP_MULTITHREAD_NONBLOCKING | EPOLLOUT | EPOLLERR;

        bool ret = m_pReactor->register_fd(fd(), &m_SocketContext.ev, IS_TCP_SOCKET, this);
        if(ret){
            onConnecting();
            return;
        }

    }

    // خطای connect
    perror("connect failed");
    onConnectFailed();
    close();
}

void TCPSocket::connect_cb(const char *hostname, char **ips, size_t count, void *p)
{
    TCPSocket *pSocketBase = static_cast<TCPSocket*>(p);
    if(!pSocketBase){
        printf("connect callback not found\n");
        return;
    }

    pSocketBase->_connect(hostname, ips, count);
}

bool TCPSocket::connectTo(const std::string &host, uint16_t port)
{
    if(!m_pReactor){
        printf("error: Reactor not found!\n");
        return false;
    }

    printf("resolve [%s]\n", host.c_str());
    m_SocketContext.port = port;
    return false;//DNSLookup::resolve(host.c_str(), connect_cb, this) == 0;
}



int TCPSocket::fd() const
{
    return m_SocketContext.fd;
}

void TCPSocket::setOnData(OnDataFn fn) {
    onData_ = fn;
}

void TCPSocket::adoptFd(int fd, EpollReactor *reactor) {

    int sndbuf = 1 * 1024; // 16KB
    //setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    setReactor(reactor);

    m_SocketContext.fd = fd;
    //m_SocketContext.writeBuffer = (char*)::malloc(SLAB_SIZE);
    m_SocketContext.rBuffer = (char*)m_pReactor->bufferPool()->allocate(SLAB_SIZE);  //(char*)::malloc(SLAB_SIZE);
    m_SocketContext.rBufferCapacity = SLAB_SIZE;
    m_SocketContext.rBufferLength = 0;
    m_SocketContext.lastActive = std::chrono::steady_clock::now();

    //faild allocate
    if (!m_SocketContext.rBuffer)
    {
        perror("allocate failed!");
        close();
    }


    // اینجا باید register_fd بزنی (بسته به پیاده‌سازی Reactor)
}




void TCPSocket::onReadable()
{

    while(true)
    {
        ssize_t bytesRec = ::recv(m_SocketContext.fd, m_SocketContext.rBuffer + m_SocketContext.rBufferLength, m_SocketContext.rBufferCapacity - m_SocketContext.rBufferLength -1, 0);

        if(bytesRec > 0)  {
            recBytes += bytesRec;

            m_SocketContext.rBufferLength += (size_t) bytesRec;
            //m_SocketContext.rBuffer[m_SocketContext.rBufferLength] = 0;
            m_SocketContext.lastActive = std::chrono::steady_clock::now();
            handleOnData(reinterpret_cast<uint8_t*>(m_SocketContext.rBuffer), m_SocketContext.rBufferLength);  // hot-path via fn pointer
            m_SocketContext.rBufferLength = 0; // app consumed in this skeleton

            if (m_readPaused) {
                //printf("backpressure pause onReadable\n");
                break;
            }

            continue;
        }

        if(bytesRec == 0) {

            if(!m_pendingClose){
                printf("bytesRec == 0 close() %zu----------------------\n", m_SocketContext.writeQueue->size());
                if (!m_SocketContext.writeQueue->empty()) {
                    m_pendingClose = true;
                    m_pReactor->mod_add(&m_SocketContext, EPOLLOUT);
                }else{
                    close();
                }
            }
            break;
        }

        if(bytesRec < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            printf("bytesRec = -1 close()\n");
            close();
            break;
        }
    }

}


void TCPSocket::send(const void* data, size_t len) {
    if (!data || len == 0)
        return;

    while (len > 0 && m_SocketContext.writeQueue->empty()) {
        ssize_t n = ::send(m_SocketContext.fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            sndBytes += n;
            data = (const char*)data + n;
            len -= (size_t)n;
            if (len == 0)
                return; // همه ارسال شد
            break;
        }
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // kernel buffer پر است
            } else {
                close();
                return;
            }
        }
    }

    if (len > 0) {

        m_SocketContext.writeQueue->push(data, len); // add to Queue list

        if (m_SocketContext.writeQueue->size() > BACK_PRESSURE) {
            pause_reading();
        }else{
            m_pReactor->mod_add(&m_SocketContext, EPOLLOUT);
        }

    }
}
void TCPSocket::onWritable() {
    if (!m_SocketContext.writeQueue)
        return;

    // tashkhise inke darim connect mishim ya na
    if (m_SocketContext.writeQueue->empty() && !m_pendingClose) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(m_SocketContext.fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
            perror("getsockopt SO_ERROR");
            onConnectFailed();
            close();
            return;
        }
        if (err != 0) {
            printf("connect failed: %s\n", strerror(err));
            onConnectFailed();
            close();
            return;
        }

        // اتصال موفق
        onConnected();
        // حالا EPOLLOUT رو اگر لازم نیست خاموش کن (بسته به نیاز به send اولیه)
        m_pReactor->mod_remove(&m_SocketContext, ~EPOLLOUT);
        return;
    }

    // ادامه کد اصلی برای ارسال داده‌ها (با بهبود: استفاده از sendmsg و iovec برای batch)
    const size_t MAX_BATCH_BYTES = 256 * 1024; // tunable: max bytes per sendmsg
#ifdef IOV_MAX
    const size_t MAX_IOV = IOV_MAX;
#else
    const size_t MAX_IOV = 1024;
#endif

    while (!m_SocketContext.writeQueue->empty()) {
        // ساخت iovec از queue
        std::vector<struct iovec> iov;
        iov.reserve(std::min<size_t>(MAX_IOV, m_SocketContext.writeQueue->count()));

        size_t batch_bytes = 0;
        for (auto it = m_SocketContext.writeQueue->begin(); it != m_SocketContext.writeQueue->end() && iov.size() < MAX_IOV; ++it) {
            size_t blen = it->len;
            if (blen == 0) continue;
            if (!iov.empty() && batch_bytes + blen > MAX_BATCH_BYTES) break;
            iov.push_back({it->data, blen});
            batch_bytes += blen;
            if (batch_bytes >= MAX_BATCH_BYTES) break;
        }

        if (iov.empty()) break;

        // sendmsg
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov.data();
        msg.msg_iovlen = iov.size();

        ssize_t bytesSent = ::sendmsg(m_SocketContext.fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (bytesSent > 0) {
            sndBytes += bytesSent;
            size_t remaining = static_cast<size_t>(bytesSent);

            // مصرف از queue
            while (remaining > 0 && !m_SocketContext.writeQueue->empty()) {
                auto &buf = m_SocketContext.writeQueue->front();
                if (remaining >= buf.len) {
                    remaining -= buf.len;
                    m_SocketContext.writeQueue->pop_front();
                } else {
                    memmove(buf.data, static_cast<char*>(buf.data) + remaining, buf.len - remaining);
                    buf.len -= remaining;
                    remaining = 0;
                }
            }
            continue;
        }

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                perror("sendmsg");
                close();
                return;
            }
        }
    }

    if (m_SocketContext.writeQueue->empty() && m_pendingClose) {
        printf("End\n");
        close();
        return;
    }

    if (m_SocketContext.writeQueue->empty() ) {
        m_pReactor->mod_remove(&m_SocketContext, ~EPOLLOUT);
        resume_reading();
    }
}

/* old just use send()
void SocketBase::onWritable() {
    if (!m_SocketContext.writeQueue)
        return;

    while (!m_SocketContext.writeQueue->empty()) {
        auto& buffer = m_SocketContext.writeQueue->front();
        ssize_t bytesSent = ::send(m_SocketContext.fd, buffer.data, buffer.len, MSG_NOSIGNAL | MSG_DONTWAIT);

        if (bytesSent > 0) {
            sndBytes += bytesSent;
            if ((size_t)bytesSent < buffer.len) {
                // بخشی از داده ارسال شد، باقیمانده رو نگه می‌داریم
                memmove(buffer.data, (char*)buffer.data + bytesSent, buffer.len - bytesSent);
                buffer.len -= bytesSent;
                break;
            }

            m_SocketContext.writeQueue->pop_front(); // ارسال کامل، آزادسازی
            continue;
        }

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            return;
        }
    }

    if (m_SocketContext.writeQueue->empty() && m_pendingClose) {
        printf("End\n");
        close(); // حالا که ارسال تمام شد، ببندیم
        return;
    }

    if (m_SocketContext.writeQueue->empty() ) {
        m_pReactor->mod_remove(&m_SocketContext, ~EPOLLOUT);
        resume_reading();
    }
}
//*/


/* gpt
void SocketBase::onWritable() {
    if (!m_SocketContext.writeQueue)
        return;

    // safety caps
    const size_t MAX_BATCH_BYTES = 256 * 1024; // send at most 256KB per syscall (tunable)
#ifdef IOV_MAX
    const size_t MAX_IOV = IOV_MAX;
#else
    const size_t MAX_IOV = 1024; // fallback conservative
#endif

    while (!m_SocketContext.writeQueue->empty()) {
        // build iovec array from queue head
        std::vector<struct iovec> iov;
        iov.reserve(std::min<size_t>(MAX_IOV, m_SocketContext.writeQueue->count()));

        size_t batch_bytes = 0;
        // iterate over queue but don't pop yet — just construct iov until caps
        for (auto it = m_SocketContext.writeQueue->begin(); it != m_SocketContext.writeQueue->end() && iov.size() < MAX_IOV; ++it) {
            size_t blen = it->len;
            if (blen == 0)
                continue;
            if (!iov.empty() && batch_bytes + blen > MAX_BATCH_BYTES)
                break;
            iov.push_back({ it->data, blen });
            batch_bytes += blen;
            if (batch_bytes >= MAX_BATCH_BYTES)
                break;
        }

        if (iov.empty()) {
            // nothing to send (shouldn't happen), break to avoid spinning
            break;
        }

        // prepare msghdr for sendmsg so we can pass flags (MSG_NOSIGNAL)
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov.data();
        msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(iov.size());

        ssize_t bytesSent = ::sendmsg(m_SocketContext.fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (bytesSent > 0) {
            sndBytes += bytesSent;
            size_t remaining = static_cast<size_t>(bytesSent);

            // consume from queue according to remaining
            while (remaining > 0 && !m_SocketContext.writeQueue->empty()) {
                auto &buf = m_SocketContext.writeQueue->front();
                if (remaining >= buf.len) {
                    // fully consumed this buffer
                    remaining -= buf.len;
                    m_SocketContext.writeQueue->pop_front();
                } else {
                    memmove(buf.data, (char*)buf.data + remaining, buf.len - remaining);
                    buf.len -= remaining;
                    remaining = 0;
                }
            }

            // continue to try send more if queue still has data (loop)
            continue;
        }

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // kernel send buffer full -> wait for next EPOLLOUT
                break;
            } else if (errno == EINTR) {
                // interrupted, try again immediately
                continue;
            } else {
                // fatal error
                perror("onWritable: sendmsg error");
                close();
                return;
            }
        }
    } // end while queue not empty

    // if pending close and queue drained -> close
    if (m_SocketContext.writeQueue->empty() && m_pendingClose) {
        close();
        return;
    }

    if (m_SocketContext.writeQueue->empty()) {
        // disable EPOLLOUT and resume reading
        m_pReactor->mod_remove(&m_SocketContext, ~EPOLLOUT);
        resume_reading();
    } else {
        // still have data: ensure EPOLLOUT is enabled (reactor may already have it),
        // and possibly enforce backpressure (already applied in send())
        m_pReactor->mod_add(&m_SocketContext, EPOLLOUT);
    }
}
//*/


//*/
void TCPSocket::handleHalfClose() {
    char buf[1];
    ssize_t ret = ::recv(m_SocketContext.fd, buf, 1, MSG_PEEK | MSG_DONTWAIT);
    printf("handleHalfClose: %zd\n", ret);
    if (ret == 0) {
        m_pendingClose = true;
        if (!m_SocketContext.writeQueue->empty()) {
            //printf("فعال کردن اگر لازم()\n");
            m_pReactor->mod_add(&m_SocketContext, EPOLLOUT); // فعال کردن اگر لازم
        } else {
            printf("handleHalfClose bytesRec == 0 close()\n");
            close();
        }
    } else if (ret < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        printf("handleHalfClose errno\n");

        close();
    }

    // چک SO_ERROR
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(m_SocketContext.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        printf("Socket error: %d\n", err);
        close();
    }
}

