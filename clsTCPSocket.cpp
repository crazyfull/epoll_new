#include "clsTCPSocket.h"
#include "clsEpollReactor.h"
#include "clsSocketList.h"
#include "epoll.h"
#include "clsDNSLookup.h"
#include "clsServer.h"

TCPSocket::TCPSocket()
{

}

TCPSocket::socketStatus TCPSocket::getStatus() const
{
    return status;
}

void TCPSocket::setStatus(socketStatus newStatus)
{
    //faghat zamani status mitone avaz beshe ke close nabashe
    if (status != Closed)
        status = newStatus;
}

void TCPSocket::updateLastActive()
{
    if(m_pReactor){
        m_SocketContext.lastActive = m_pReactor->getCachedNow(); //std::chrono::steady_clock::now();

        printf("lastActive %lu \n", m_SocketContext.lastActive);
    }
}

EpollReactor *TCPSocket::getReactor() const
{
    return m_pReactor;
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
    if(m_onData){
        m_onData(m_callbacksArg, d, n);
    }else{
        onReceiveData(d, n);
    }
}

void TCPSocket::handleOnAccepted()
{
    if(m_onAccepted){
        m_onAccepted(m_callbacksArg);
    }else{
        onAccepted();
    }
}

void TCPSocket::handleOnClose()
{
    if(m_onClose){
        m_onClose(m_callbacksArg);
    }else{
        onClose();
    }
}

void TCPSocket::handleOnConnectFailed()
{
    if(m_onConnectFailed){
        m_onConnectFailed(m_callbacksArg);
    }else{
        onConnectFailed();
    }
}

void TCPSocket::handleOnConnecting()
{
    if(m_onConnecting){

        m_onConnecting(m_callbacksArg);

    }else{
        onConnecting();
    }
}

void TCPSocket::handleOnConnected()
{
    if(m_onConnected){
        m_onConnected(m_callbacksArg);
    }else{
        onConnected();
    }
}

void TCPSocket::handleOnPause() {
    if (m_onPause)
        m_onPause(m_callbacksArg);
}

void TCPSocket::handleOnResume() {
    if (m_onResume)
        m_onResume(m_callbacksArg);
}

void TCPSocket::pause_reading() {
    if (!m_pReactor || m_readPaused)
        return;  // جلوگیری از تکرار
    printf("pause_reading(%d)\n", m_readPaused);
    m_readPaused = true;
    m_pReactor->removeFlags(&m_SocketContext, EPOLLIN);
    handleOnPause();  // trigger callback
}

void TCPSocket::resume_reading() {
    if (!m_pReactor || !m_readPaused)
        return;

    printf("resume_reading()\n");
    m_readPaused = false;
    m_pReactor->addFlags(&m_SocketContext, EPOLLIN);
    handleOnResume();  // trigger callback
}

int TCPSocket::getErrorCode()
{
    // check SO_ERROR
    int err = 0;
    if(m_SocketContext.fd != -1){
        socklen_t len = sizeof(err);
        getsockopt(m_SocketContext.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    }
    return err;
}

/*
void TCPSocket::close()
{
    if (!m_pReactor || m_SocketContext.fd == -1 || getStatus() == Closed)
        return;


    setStatus(Closed);

    //delere from epoll and ConnectionList
    m_pReactor->del_fd(m_SocketContext.fd, true);

    if(m_SocketContext.rBuffer) {
        m_pReactor->bufferPool()->deallocate(m_SocketContext.rBuffer);
        m_SocketContext.rBuffer = nullptr;
    }

    //check graceful shutdown
    if(!m_SocketContext.writeQueue->empty()){
        printf("graceful shutdown!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }

    m_SocketContext.writeQueue->clear();
    ::shutdown(m_SocketContext.fd, SHUT_RDWR);
    ::close(m_SocketContext.fd);


    //writeQueue dar SocketContext free mishe

    printf("close()\n");

    //m_SocketContext.fd = -1;
    printf("recBytes: [%lu] sndBytes: [%lu]\n", recBytes, sndBytes);
    this->handleOnClose();




}
*/


void TCPSocket::close(bool force) {
    if (!m_pReactor || m_SocketContext.fd == -1 || getStatus() == Closed)
        return;

    if (m_SocketContext.writeQueue->empty() || force == true) {
        //printf("TCPSocket::close !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! %d \n", fd());
        //

        setStatus(Closed);
        //delere from epoll and ConnectionList
        m_pReactor->del_fd(m_SocketContext.fd, true);

        if (m_SocketContext.rBuffer) {
            m_pReactor->bufferPool()->deallocate(m_SocketContext.rBuffer);
            m_SocketContext.rBuffer = nullptr;
        }

        if (force) {
            m_SocketContext.writeQueue->clear();
        }


        ::shutdown(m_SocketContext.fd, SHUT_WR);
        ::close(m_SocketContext.fd);


        printf("recBytes: [%lu] sndBytes: [%lu]\n", recBytes, sndBytes);
        handleOnClose();

        //disable garbage collector
        //m_pReactor->deleteLater(this);

    } else {
        printf("graceful shutdown>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %d \n", fd());
        //handleHalfClose();
        /**/
        // pending برای graceful
        m_pendingClose = true;
        setStatus(Closing);
        ::shutdown(m_SocketContext.fd, SHUT_WR);  // بستن write، اما fd باز بمونه
        m_pReactor->addFlags(&m_SocketContext, EPOLLOUT);  // برای خالی کردن queue
        printf("pending close: waiting for queue to drain\n");

    }
    // حذف clear queue!
}




void TCPSocket::_connect(const char *hostname, char **ips, size_t count)
{
    if (!ips || count == 0) {
        printf("No result for %s\n", hostname);
        setStatus(Closed);
        handleOnConnectFailed();
        return;
    }

    // استفاده از اولین IP (برای سادگی؛ می‌تونی چند IP رو امتحان کنی اگر لازم)
    const char* ip = ips[0];
    printf("connecting to [%s] (%s:%d)...\n", hostname, ip, m_SocketContext.port);

    // ایجاد سوکت TCP non-blocking
    m_SocketContext.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (m_SocketContext.fd == -1) {
        perror("socket creation failed");
        setStatus(Closed);
        handleOnConnectFailed();
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
        setStatus(Closing);
        handleOnConnectFailed();
        close();
        return;
    }

    // فراخوانی connect
    int ret = ::connect(m_SocketContext.fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        // اتصال فوری موفق (نادر اما ممکن)
        if(adoptFd(m_SocketContext.fd)){
            setStatus(Connected);
            handleOnConnected();
        }else{
            setStatus(Closing);
            handleOnConnectFailed();
            close();
        }

        return;
    }

    if (errno == EINPROGRESS) {

        // اتصال در حال انجام (non-blocking)
        if(adoptFd(m_SocketContext.fd)){
            //set events
            m_SocketContext.ev.events = EPOLL_EVENTS_TCP_MULTITHREAD_NONBLOCKING | EPOLLOUT | EPOLLERR;

            bool ret = m_pReactor->register_fd(fd(), &m_SocketContext.ev, IS_TCP_SOCKET, this);
            if(ret){
                handleOnConnecting();
                return;
            }
        }
    }

    //
    perror("connect failed");
    setStatus(Closing);
    handleOnConnectFailed();
    close();
}

void TCPSocket::_accepted(int fd)
{
    if(getStatus() != Ready){
        //befor called
        return;
    }

    bool ret = m_pReactor->register_fd(fd, &getSocketContext()->ev, IS_TCP_SOCKET, this);
    if(ret){
        if(adoptFd(fd)){

            setStatus(TCPSocket::Connected);
            handleOnAccepted();  // callback
            //continue;
            return;
        }
    }

    //be har dalili accept nashod close call beshe ke GC emal beshe
    close();
}

void TCPSocket::connect_cb(const char *hostname, char **ips, size_t count, DNSLookup::QUERY_TYPE qtype, void *p)
{
    TCPSocket *pSocketBase = static_cast<TCPSocket*>(p);
    if(!pSocketBase){
        printf("connect callback not found\n");
        return;
    }

    pSocketBase->_connect(hostname, ips, count);
}

bool TCPSocket::connectTo(const char* host, uint16_t port)
{
    if(!m_pReactor){
        printf("error: Reactor not found!\n");
        return false;
    }

    if(getStatus() != Ready){
        printf("allready connect\n");
        return false;
    }

    setStatus(Connecting);
    m_SocketContext.port = port;
    return m_pReactor->getIPbyName(host, connect_cb, this);
}

int TCPSocket::fd() const
{
    return m_SocketContext.fd;
}

TCPSocket *TCPSocket::getPointer()
{
    return this;
}

void TCPSocket::setOnData(OnDataFn fn, void* Arg) {
    m_onData = fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnConnecting(OnConnectingFn fn, void* Arg)
{
    m_onConnecting = fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnConnected(OnConnectedFn fn, void* Arg)
{
    m_onConnected= fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnAccepted(OnAcceptedFn fn, void* Arg)
{
    m_onAccepted = fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnClose(OnCloseFn fn, void *Arg)
{
    m_onClose= fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnConnectFailed(OnConnectFailedFn fn, void *Arg)
{
    m_onConnectFailed= fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnPause(OnPauseFn fn, void* Arg) {
    m_onPause = fn;
    m_callbacksArg = Arg;
}

void TCPSocket::setOnResume(OnResumeFn fn, void* Arg) {
    m_onResume = fn;
    m_callbacksArg = Arg;
}


bool TCPSocket::adoptFd(int fd) {

    int sndbuf = 1 * 1024; // 16KB
    //setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    //setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sndbuf, sizeof(sndbuf));


    //printf("allocate size: %zu\n", m_pReactor->bufferPool()->size());

    m_SocketContext.fd = fd;
    //m_SocketContext.writeBuffer = (char*)::malloc(SLAB_SIZE);
    m_SocketContext.rBuffer = (char*)m_pReactor->bufferPool()->allocate(SLAB_SIZE);  //(char*)::malloc(SLAB_SIZE);
    m_SocketContext.rBufferCapacity = SLAB_SIZE;
    m_SocketContext.rBufferLength = 0;
    updateLastActive();

    //faild allocate
    if (!m_SocketContext.rBuffer)
    {
        perror("allocate failed!");
        return false;
    }

    return true;
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
            updateLastActive();
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
                //printf("bytesRec == 0 close() %zu----------------------\n", m_SocketContext.writeQueue->size());
                if (!m_SocketContext.writeQueue->empty()) {
                    m_pendingClose = true;
                    //change status for shurdown
                    setStatus(Closing);

                    //faal shodane EPOLLOUT baraye khali kardane safe ersal
                    m_pReactor->addFlags(&m_SocketContext, EPOLLOUT);
                }else{
                    close();
                }
            }
            break;
        }

        if(bytesRec < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            //printf("bytesRec = -1 close()\n");
            close();
            break;
        }
    }

}


void TCPSocket::send(const void* data, size_t len) {
    //printf("TCPSocket::send: fd%d\n", m_SocketContext.fd);
    if (!data || len == 0)
        return;

    while (len > 0 && m_SocketContext.writeQueue->empty()) {
        ssize_t n = ::send(m_SocketContext.fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {

            sndBytes += n;
            data = (const char*)data + n;
            len -= (size_t)n;
            updateLastActive();
            if (len == 0)
                return; // hame ersal shod
            break;
        }

        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // kernel buffer por shod
            } else {
                close(true);
                return;
            }
        }
    }

    if (len > 0) {

        m_SocketContext.writeQueue->push(data, len); // add to Queue list

        //printf("m_SocketContext.writeQueue->size()  size=%zu\n", m_SocketContext.writeQueue->size());

        if (m_SocketContext.writeQueue->size() > BACK_PRESSURE) {
            //printf("pause_reading()  size=%zu\n", m_SocketContext.writeQueue->size());
            pause_reading();
        }else{
            m_pReactor->addFlags(&m_SocketContext, EPOLLOUT);
        }

    }
}


void TCPSocket::onWritable() {


    if (getStatus() == Closing){
        printf("onWritable shutdown<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<,%d \n", fd());

    }

    if (!m_SocketContext.writeQueue)
        return;

    int err = getErrorCode();

    // tashkhise inke darim connect mishim ya na #mohem
    if (getStatus() == Connecting && !m_pendingClose) {

        if (err != 0) {
            printf("connect failed: %s\n", strerror(err));
            handleOnConnectFailed();
            close(true);  // Force close برای خطاها
            return;
        }

        // remove EPOLLOUT
        m_pReactor->removeFlags(&m_SocketContext, EPOLLOUT);

        //connected sucessfully
        setStatus(Connected);
        handleOnConnected();
        return;
    }

    if (err != 0) {
        printf("EPOLLERR in onWritable: fd=%d, error=%d\n", fd(), err);
        close(true);
        return;
    }



    // ادامه کد اصلی برای ارسال داده‌ها (با بهبود: استفاده از sendmsg و iovec برای batch)
    const size_t MAX_BATCH_BYTES = 256 * 1024; //256KB tunable: max bytes per sendmsg
#ifdef IOV_MAX
    const size_t MAX_IOV = IOV_MAX;
#else
    const size_t MAX_IOV = 1024;
#endif

    while (!m_SocketContext.writeQueue->empty()) {
        printf("begin writing...\n");

        // ساخت iovec از queue
        std::vector<struct iovec> iov;
        iov.reserve(std::min<size_t>(MAX_IOV, m_SocketContext.writeQueue->count()));

        size_t batch_bytes = 0;
        for (auto it = m_SocketContext.writeQueue->begin(); it != m_SocketContext.writeQueue->end() && iov.size() < MAX_IOV; ++it) {
            size_t blen = it->len;
            if (blen == 0)
                continue;

            if (!iov.empty() && batch_bytes + blen > MAX_BATCH_BYTES){
                printf("cod 01, iov.empty:[%d], batch_bytes:[%zu]\n", iov.empty(), batch_bytes + blen);
                break;
            }

            iov.push_back({it->data, blen});
            batch_bytes += blen;

            if (batch_bytes >= MAX_BATCH_BYTES){
                printf("cod 02\n");
                break;
            }
        }

        if (iov.empty()){
            printf("cod 03\n");
            break;
        }

        // sendmsg
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov.data();
        msg.msg_iovlen = iov.size();

        ssize_t bytesSent = ::sendmsg(m_SocketContext.fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
        printf("bytesSent: %zd\n", bytesSent);
        if (bytesSent > 0) {
            sndBytes += bytesSent;
            updateLastActive();
            size_t remaining = static_cast<size_t>(bytesSent);

            // مصرف از queue
            while (remaining > 0 && !m_SocketContext.writeQueue->empty()) {
                auto &buf = m_SocketContext.writeQueue->front();
                if (remaining >= buf.len) {
                    remaining -= buf.len;
                    m_SocketContext.writeQueue->pop_front();

                    printf("writeQueue->pop_front(): [%zu] ev[%u]\n", m_SocketContext.writeQueue->size(), m_SocketContext.ev.events);
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
                printf("write get EAGAIN\n");
                break;

            } else if (errno == EINTR) {
                continue;

            } else {
                perror("error sendmsg");
                close(true);
                return;
            }
        }
    }

    printf("iov End: %zd , empty: %d\n", m_SocketContext.writeQueue->size(), m_SocketContext.writeQueue->empty());

    if (!m_SocketContext.writeQueue->empty()) {
        if (!(m_SocketContext.ev.events & EPOLLOUT)){
            //m_pReactor->mod_add(&m_SocketContext, EPOLLOUT);
            printf("catch error====================================================================================================(\n");
        }

    }

    /*
    if (m_SocketContext.writeQueue->empty() && m_pendingClose) {
        printf("End\n");
        close(true);
        return;
    }
    */

    //hazf beshe
    if (m_SocketContext.writeQueue->empty() && m_pendingClose) {
        setStatus(Closed);
        m_pReactor->del_fd(m_SocketContext.fd, true);
        if (m_SocketContext.rBuffer) {
            m_pReactor->bufferPool()->deallocate(m_SocketContext.rBuffer);
            m_SocketContext.rBuffer = nullptr;
        }
        ::close(m_SocketContext.fd);  // حذف SHUT_RDWR
        printf("graceful close: queue drained-------------------------------------------------------------------------------\n");
        handleOnClose();
        return;
    }


    //resume
    if (m_SocketContext.writeQueue->size() <= LOW_WATERMARK) {
        printf("kissed LOW_WATERMARK: [%zu]\n", m_SocketContext.writeQueue->size());
        m_pReactor->removeFlags(&m_SocketContext, EPOLLOUT);
        resume_reading();
        return;
    }

    // age safe ersal khali shod flag send hazf beshe
    // age read az ghabl pause bood resume beshe
    if (m_SocketContext.writeQueue->empty()) {
        m_pReactor->removeFlags(&m_SocketContext, EPOLLOUT);
        if (m_readPaused) {
            printf("Write queue drained, resuming reading.\n");
            resume_reading();
        }
        return;
    }

}

//*/
void TCPSocket::handleHalfClose() {
    char buf[1];
    ssize_t ret = ::recv(m_SocketContext.fd, buf, 1, MSG_PEEK | MSG_DONTWAIT);
    //printf("handleHalfClose: %zd\n", ret);
    if (ret == 0) {

        if (!m_SocketContext.writeQueue->empty()) {
            m_pendingClose = true;
            setStatus(Closing);
            m_pReactor->addFlags(&m_SocketContext, EPOLLOUT); // فعال کردن اگر لازم
            printf("add EPOLLOUT TCPSocket::handleHalfClose() %zu\n", m_SocketContext.writeQueue->size());
        } else {
            //printf("handleHalfClose bytesRec == 0 close()\n");
            close();
            return;
        }


    } else if (ret < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        printf("handleHalfClose errno\n");

        close();
        return;
    }

    // check socket error
    int err = getErrorCode();
    if (err != 0) {
        printf("Socket error: [%d] msg:[%s]\n", err, strerror(err));
        close();
        return;
    }
}

