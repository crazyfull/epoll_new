#ifndef CLSEPOLLREACTOR_H
#define CLSEPOLLREACTOR_H
#include "clsBufferPool.h"
#include "clsTCPSocket.h"
#include "clsGCList.h"
#include "clsSocketList.h"
#include "clsDNSLookup.h"
#include "constants.h"

// EpollReactor
//class DNSLookup;
class TimerManager;
class UDPSocket;
class SocketList;
class EpollReactor
{
public:
    EpollReactor(int id, int maxConnection, int max_events = MAX_EVENTS);
    ~EpollReactor();

    // factory from app to create high-level handler for accepted fd
    //using acceptCallback = std::function<TCPSocket*()>; // (extensibility)
    using acceptCallback = TCPSocket* (*)(void*);

    void setOnAccepted(acceptCallback fncallback, void *p);
    bool register_fd(int fd, epoll_event *pEvent,SockTypes sockType, void *ptr);
    //void mod_fd(int fd, epoll_event *pEvent, uint32_t flags);
    bool addFlags(SocketContext *pContext, uint32_t flags);
    void removeFlags(SocketContext *pContext, uint32_t flags);
    void del_fd(int fd, bool removeFromList = false);
    bool add_fd(int fd, epoll_event *pEvent, uint32_t events);
    bool add_listener(int port);
    void stop_listener();

    // accept handoff from accept-thread (18 hot-reload/handoff point)
    void run(std::atomic<bool> &stop);
    void wake();
    void adoptAccepted(int m_fd);
    void setUseGarbageCollector(bool newUseGarbageCollector);
    bool getIPbyName(const char *hostname, DNSLookup::callback_t callback, void *p, DNSLookup::QUERY_TYPE QuryType = DNSLookup::A);
    void deleteLater(TCPSocket* pSockBase);
    void updateCashedTime();

    BufferPool *bufferPool();

    uint64_t getCachedNow() const;

private:
    bool m_useGarbageCollector {true};
    int m_reactorID {0};
    int m_epollSocket {-1};
    int m_wakeupFd {-1};    //baraye exit safe epoll
    int m_maxEvent {100};
    int m_maxConnection {100};
    timespec m_cached_now;
    TimerManager *m_pTimers;

    std::vector<int> m_listenerList;
    GCList<TCPSocket> m_GCList;
    SocketList *m_pConnectionList;
    BufferPool m_bufferPool;
    DNSLookup *m_pDNSLookup;

    //std::unordered_map<int,SocketBaseHandle> m_ConnectionMap; // (user map as requested)
    acceptCallback m_onAcceptCallback {};
    void* m_onAcceptCallbackCtx {nullptr};

    //void handle_read(int fd, SocketBase *pSockBase);
    //void handle_write(int fd, SocketBase *pSockBase);

    void init();
    void maintenance();
    void shutdown_all();

    // (16) telemetry placeholder; could be atomic counters per shard
    uint64_t make_key(int fd, uint32_t genID);
    int extract_fd(uint64_t key);
    uint32_t extract_gen(uint64_t key);

    void onTCPEvent(int fd, uint32_t &ev, void *ptr);
    void onUDPEvent(int fd, uint32_t &ev, void *ptr);
    void onTimerEvent(int fd, uint32_t &ev, void *ptr);
    void onTimerManagerEvent(int fd, uint32_t &ev, void *ptr);
    void onDNSEvent(int fd, uint32_t &ev, void *ptr);

    void checkDnsTimeouts();
    void runGarbageCollector();
    void checkIdleConnections();
    void checkStalledConnections();
};


#endif // CLSEPOLLREACTOR_H
