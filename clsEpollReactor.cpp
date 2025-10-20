#include "clsEpollReactor.h"

#include "clsSocketList.h"
#include "clsUDPSocket.h"
#include "clsTimer.h"
#include "clsTimerManager.h"
#include <malloc.h>

EpollReactor::EpollReactor(int id, int maxConnection, int max_events): m_reactorID(id), m_maxEvent(max_events), m_maxConnection(maxConnection), m_bufferPool(BUFFER_POOL_SIZE)
{
    init();
}

EpollReactor::~EpollReactor() {
    if(m_epollSocket != -1)
        ::close(m_epollSocket);

    if(m_wakeupFd != -1)
        ::close(m_wakeupFd);

    if(m_pConnectionList){
        delete m_pConnectionList;
        m_pConnectionList = nullptr;
    }

    if(m_pDNSLookup){
        delete m_pDNSLookup;
    }


    if(m_pTimers){
        delete m_pTimers;
    }

}

void EpollReactor::init()
{
    //
    m_pConnectionList = new SocketList(m_maxConnection);

    //
    m_epollSocket = epoll_create1(EPOLL_CLOEXEC);
    if(m_epollSocket == -1)
        throw std::runtime_error("epoll_create1");

    //
    m_wakeupFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(m_wakeupFd == -1)
        throw std::runtime_error("eventfd");
    struct epoll_event ev;
    //add_fd(m_wakeupFd,&ev, EPOLLIN);

    //
    m_pDNSLookup = new DNSLookup(this);
    m_pDNSLookup->setTimeout(DNS_LOOKUP_TIMEOUT_SEC);
    m_pDNSLookup->setCache_ttl_sec(DNS_CACHE_TTL_SEC);
    m_pDNSLookup->setMaxRetries(DNS_MAX_RETRIES);

    //
    m_pTimers = new TimerManager;
    m_pTimers->setReactor(this);

    //Managing IDLE Connections
    m_pTimers->addTimer(IDLE_CONNECTION_INTERVAL_MS, [this] () {
        maintenance();
    });

    //Managing STALLED Connections
    m_pTimers->addTimer(STALLED_CONNECTION_INTERVAL_MS, [this] () {
        maintenance();
    });

    //DNS timeout timer
    m_pTimers->addTimer(DNS_TIMEOUT_INTERVAL_MS, [this] {
        this->checkDnsTimeouts();
    });

    //Garbage collector timer
    m_pTimers->addTimer(GARBAGE_COLLECTOR_INTERVAL_MS, [this] {
        this->runGarbageCollector();
    });
}

void EpollReactor::setOnAccepted(acceptCallback fncallback, void* p) {
    m_onAcceptCallback = std::move(fncallback);
    m_onAcceptCallbackCtx = p;
}

uint64_t EpollReactor::make_key(int fd, uint32_t genID) {
    return (static_cast<uint64_t>(genID) << 32) | static_cast<uint32_t>(fd);
}

int EpollReactor::extract_fd(uint64_t key) {
    return static_cast<int>(key & 0xFFFFFFFF);
}

uint32_t EpollReactor::extract_gen(uint64_t key) {
    return static_cast<uint32_t>(key >> 32);
}

bool EpollReactor::addFlags(SocketContext *pContext, uint32_t flags)
{
    if (!(pContext->ev.events & flags)){
        pContext->ev.events |= flags;

        //printf("mod_add() flags: %d\n", flags);

        if(epoll_ctl(m_epollSocket, EPOLL_CTL_MOD, pContext->fd, &pContext->ev) == -1){
            perror("EPOLL_CTL_MOD mod_add");
            return false;
        }
        return true;
    }
    return false;
}

void EpollReactor::removeFlags(SocketContext *pContext, uint32_t flags)
{
    if (pContext->ev.events & flags) {
        pContext->ev.events &= ~flags;

        //printf("mod_remove() flags: %d\n", flags);

        if(epoll_ctl(m_epollSocket, EPOLL_CTL_MOD, pContext->fd, &pContext->ev) == -1){
            perror("EPOLL_CTL_MOD mod_remove");
        }
    }
}

void EpollReactor::del_fd(int fd, bool removeFromList) {
    epoll_ctl(m_epollSocket, EPOLL_CTL_DEL, fd, nullptr);
    if(removeFromList)
        m_pConnectionList->remove(fd);
}

bool EpollReactor::add_fd(int fd, epoll_event *pEvent, uint32_t events)
{
    pEvent->events = events;
    if(epoll_ctl(m_epollSocket, EPOLL_CTL_ADD, fd, pEvent) == -1){
        printf("error: add_fd\n");
        return false;
    }

    return true;
}

bool EpollReactor::add_listener(int port)
{
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(listen_fd < 0) {
        perror("socket");
        return false;
    }

    //add to listener list
    m_listenerList.push_back(listen_fd);

    if(TCPSocket::setSocketNonblocking(listen_fd) == -1) {
        perror("fcntl");
        return false;
    }

    TCPSocket::setSocketShared(listen_fd, true);
    TCPSocket::setSocketResourceAddress(listen_fd, true);
    TCPSocket::setSocketNoDelay(listen_fd, true);


    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((uint16_t) port);

    if(bind(listen_fd, (sockaddr*) & a, sizeof a) < 0) {
        perror("bind");
        return false;
    }

    if(::listen(listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        return false;
    }

    //add listener to connection list
    /*
    SockInfo *sockinfo = m_pConnectionList->add(listen_fd, IS_TCP_LISTENER);
    if(!sockinfo){
        printf("errpr: can not listen\n");
        ::close(listen_fd);
        return false;
    }
    */

    //add listener fd to epoll
    struct epoll_event listen_ev;
    listen_ev.events = (EPOLLIN );
    bool ret = register_fd(listen_fd, &listen_ev, IS_TCP_LISTENER, nullptr);
    return ret;
}

void EpollReactor::stop_listener()
{
    for (int fd : m_listenerList) {
        del_fd(fd);
        m_pConnectionList->remove(fd);
        close(fd);
    }

    m_listenerList.clear();
}



void EpollReactor::adoptAccepted(int m_fd) {
    // printf("EpollReactor::adoptAccepted()\n");

    while (true) {
        sockaddr_in cli {};
        socklen_t cl = sizeof cli;
        int fd = ::accept4(m_fd, (sockaddr *)&cli, &cl, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // all request accepted
            }

            if (errno == EINTR) {
                continue; // try again
            }

            perror("accept4");
            break;
        }

        TCPSocket::setSocketShared(fd, true);
        TCPSocket::setSocketResourceAddress(fd, true);
        TCPSocket::setSocketNoDelay(fd, true);

        TCPSocket *pSocketbase = nullptr;
        if (m_onAcceptCallback) {
            pSocketbase = m_onAcceptCallback(m_onAcceptCallbackCtx);
        }

        if (!pSocketbase) {
            //onAccepted callback function not found
            ::close(fd);
            continue;
        }

        pSocketbase->setReactor(this);
        pSocketbase->_accepted(fd);

    }
}

bool EpollReactor::register_fd(int fd, epoll_event *pEvent,SockTypes sockType, void* ptr)
{
    if(!pEvent)
        return false;

    SockInfo* sockinfo = m_pConnectionList->add(fd, sockType);
    if (!sockinfo) {

        printf("can not add new connection to ConnectionList\n");
        return false;
    }

    /*
     *  in bakhsh tavasole khode socket bayad handle beshe
    //::close(fd);
    if (sockType == IS_TCP_SOCKET) {
        TCPSocket *pTCPSocket = static_cast<TCPSocket*>(ptr);
        if(pTCPSocket)
            delete pTCPSocket;
    }

    if (sockType == IS_UDP_SOCKET) {
        UDPSocket *pUDPSocket = static_cast<UDPSocket*>(ptr);
        if(pUDPSocket)
            delete pUDPSocket;
    }

    if (sockType == IS_TIMER_SOCKET) {

        /* lazem nist khode timer tavasote user delete bayad beshe
            Timer *pTimer = static_cast<Timer*>(ptr);
            if(pTimer)
                delete pTimer;
            * /
    }
    */



    if(ptr)
        sockinfo->socketBasePtr = ptr;

    if(pEvent->events == 0){
        pEvent->events = EPOLL_EVENTS_TCP_MULTITHREAD_NONBLOCKING;  //EPOLLOUT
    }

    //make key
    pEvent->data.u64 = make_key(fd, sockinfo->genID);
    if (epoll_ctl(m_epollSocket, EPOLL_CTL_ADD, fd, pEvent) == -1) {
        perror("epoll_ctl add");
        return false;
    }

    return true;
}

void EpollReactor::setUseGarbageCollector(bool newUseGarbageCollector)
{
    m_useGarbageCollector = newUseGarbageCollector;
}

bool EpollReactor::getIPbyName(const char *hostname, DNSLookup::callback_t callback, void *p, DNSLookup::QUERY_TYPE QuryType)
{
    return m_pDNSLookup->resolve(hostname, callback, p, QuryType);
}

void EpollReactor::deleteLater(TCPSocket *pSockBase)
{
    if(pSockBase)
        m_GCList.retire(pSockBase);
}


BufferPool *EpollReactor::bufferPool()
{
    return &m_bufferPool;
}



void EpollReactor::onTCPEvent(int fd, uint32_t &ev, void *ptr){
    TCPSocket* pSockBase = static_cast<TCPSocket*>(ptr);
    if(!pSockBase){
        printf("pSockBase null\n");
        ::close(fd);
        return;
    }

    if (ev & EPOLLERR) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        printf("EPOLLERR: fd=%d, error=%d\n", fd, err);
        pSockBase->close();

        //#mohem in ghesmat bayad bere to close() chon zamani ke shutdown anjam mishe in event call nemishe
        //m_GCList.retire(pSockBase);     //mohem
        return;
    }

    if (ev & EPOLLHUP) {
        //#mohem inja raftare gheyre hamahang ba close hast
        printf("EPOLLHUP: fd=%d\n", fd);
        if (pSockBase->getSocketContext()->writeQueue->empty()) {
            pSockBase->close();
            //m_GCList.retire(pSockBase);
        } else {
            pSockBase->handleHalfClose(); // khali kardane send queue
        }
        return;
    }

    //get shutdown
    if(ev & (EPOLLRDHUP)) {
        //printf("EPOLLRDHUP\n");
        pSockBase->handleHalfClose();   //#mohem inja mtmaen nistam dorost bashe
    }

    if(ev & (EPOLLPRI)) {
        printf("EPOLLPRI\n");
    }

    if(ev & EPOLLIN){
        //printf("EPOLLIN\n");
        pSockBase->onReadable();
    }

    if(ev & EPOLLOUT){
        //printf("EPOLLOUT\n");
        pSockBase->onWritable();
    }
}

void EpollReactor::onUDPEvent(int fd, uint32_t &ev, void *ptr)
{
    //tarif nashode
}

void EpollReactor::onTimerEvent(int fd, uint32_t &ev, void *ptr)
{
    //
    Timer* pTimer = static_cast<Timer*>(ptr);
    if(!pTimer){
        printf("pTimer null\n");
        ::close(fd);
        return;
    }

    if (ev & EPOLLIN) {
        pTimer->onTick();
    }
}

void EpollReactor::onTimerManagerEvent(int fd, uint32_t &ev, void *ptr)
{
    TimerManager* pTimerm = static_cast<TimerManager*>(ptr);
    if(!pTimerm){
        printf("pTimer manager null\n");
        ::close(fd);
        return;
    }

    if (ev & EPOLLIN) {
        pTimerm->onTick();
    }
}

void EpollReactor::onDNSEvent(int fd, uint32_t &ev, void *ptr)
{
    DNSLookup *pDNSLookup = static_cast<DNSLookup*>(ptr);
    if(!pDNSLookup){
        printf("pDNSLookup null\n");
        ::close(fd);
        return;
    }

    if (ev & EPOLLIN) {
        pDNSLookup->on_dns_read();
    }

    if (ev & EPOLLERR) {
        pDNSLookup->reset_socket();
    }

    //printf("pDNSLookup new ev: %d\n", ev);
}

void EpollReactor::checkDnsTimeouts()
{
    m_pDNSLookup->maintenance();
}

void EpollReactor::runGarbageCollector()
{

    if(m_useGarbageCollector){
        m_GCList.flush();
    }else{
        m_GCList.flush_all();
        //printf("m_pConnectionList count: %d\n", m_pConnectionList->count());

        //mallopt(M_MMAP_THRESHOLD, 128 * 1024);
        //malloc_trim(0);
    }
}

void EpollReactor::run(std::atomic<bool> &stop)
{
    printf("EpollReactor::run()\n");

    std::vector <epoll_event> evs(m_maxEvent);
    while(!stop.load(std::memory_order_relaxed))
    {
        int n = epoll_wait(m_epollSocket, evs.data(), (int)evs.size(), 1000);
        //printf("n %d\n", n);

        if(n < 0) {
            if(errno == EINTR)
                continue;

            perror("epoll_wait");
            break;
        }

        for(int i = 0; i < n; ++i)
        {
            uint64_t key = evs[i].data.u64;
            uint32_t ev = evs[i].events;
            int fd = extract_fd(key);
            uint32_t socketgenID = extract_gen(key);


            //printf("epoll_wait::run() fd: %d socketgenID: %d\n", fd, socketgenID);

            //
            SockInfo* socketInfo = m_pConnectionList->get(fd, socketgenID);
            if (!socketInfo) {
                printf("socketInfo null\n");
                continue;
            }

            //printf("socketInfo type[%d] ev:[%d]\n", socketInfo->type, ev);

            if (socketInfo->type == IS_TCP_LISTENER) {
                adoptAccepted(fd);
                continue;
            }

            if (socketInfo->type == IS_TCP_SOCKET) {

                onTCPEvent(fd, ev, socketInfo->socketBasePtr);
                continue;
            }

            if (socketInfo->type == IS_UDP_SOCKET) {
                onUDPEvent(fd, ev, socketInfo->socketBasePtr);
                continue;
            }

            if (socketInfo->type == IS_TIMER_SOCKET) {
                onTimerEvent(fd, ev, socketInfo->socketBasePtr);
                continue;
            }

            if (socketInfo->type == IS_TIMER_MANAGER_SOCKET) {
                onTimerManagerEvent(fd, ev, socketInfo->socketBasePtr);
                continue;
            }


            if (socketInfo->type == IS_DNS_LOOKUP_SOCKET) {
                onDNSEvent(fd, ev, socketInfo->socketBasePtr);
                continue;
            }
            //int fd = evs[i].data.fd;
            //printf("ev: %d\n", ev);

            /*
            if(fd == m_wakeupFd) {
                uint64_t x;
                read(m_wakeupFd, &x, sizeof x);
                continue;
            }*/


        }

    }

    shutdown_all();
}

void EpollReactor::wake() {
    uint64_t one = 1;
    write(m_wakeupFd, &one, sizeof one);
}


void EpollReactor::maintenance()
{

    // (13) idle cull
    /*
    std::vector<int> to_close;
    auto now = std::chrono::steady_clock::now();

    for(auto &kv: m_ConnectionMap) {
        auto &c = kv.second->m_SocketContext;
        auto idle = std::chrono::duration_cast<std::chrono::seconds> (now - c->lastActive).count();
        if(idle > IDLE_TIMEOUT_SEC)
            to_close.push_back(kv.first);
    }

    for(int fd: to_close){
        close_fd(fd);
    }

*/
    // printf("maintenance(%zd)\n", (long)time);

    // TODO: اگر idle GC می‌خواهی، از SocketList iterate کن و با lastActive ببند.
}

void EpollReactor::shutdown_all() {
    /*
    std::vector <int> fds;
    fds.reserve(m_ConnectionMap.size());

    for(auto & kv: m_ConnectionMap)
        fds.push_back(kv.first);

    for(int fd: fds)
        close_fd(fd);
    */

    // بسته‌کردن همه‌ی fdها (listener و client)
    // اگر SocketList اینترفیس برای enumerate ندارد، نگه‌داشتن لیست fdها لازم است.
    // فعلاً ساده: rely on process exit یا اضافه‌کردن متد iterate به SocketList.
}


