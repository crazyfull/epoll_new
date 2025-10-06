#ifndef CLSTCPSOCKET_H
#define CLSTCPSOCKET_H
//
#include "clsSendQueue.h"
#include "epoll.h"
#include <functional>
#include <memory>
#include <sys/epoll.h>
#include <string>
#include "SocketContext.h"
#include "clsDNSLookup.h"

// ============================== SocketBase (OOP API) ======================
// Virtual only on non-hot paths; hot-path onData via function-pointer.
class Server;
class EpollReactor;
class TCPSocket
{
public:
    TCPSocket();

    int recBytes = 0;
    int sndBytes = 0;

    void setReactor(EpollReactor* r);
    using OnDataFn = void(*)(TCPSocket* , const uint8_t* , size_t);     // (hot path)
    //using CloseCallback = std::function<void(int)>;                   // fd
    //using EpollModCallback = std::function<void(int, uint32_t)>;      // fd, newFlags

    virtual~TCPSocket() =  default; // (13) RAII
    virtual void onAccepted() {}                                // (non-hot)
    virtual void onClose() {}                                   // (non-hot)
    virtual void onConnectFailed(){}  // (non-hot)
    virtual void onConnecting(){}
    virtual void onConnected(){}


    // setter hot path entry — called by shard on EPOLLIN
    void onReadable();
    void onWritable();
    void handleHalfClose();

    //void setCloseCallback(CloseCallback cb);
    //void setEpollModCallback(EpollModCallback cb);

    // app-side send helper (thread-affinity: shard thread)
    void send(const void * data, size_t len);
    void close();
    bool connectTo(const std::string &host, uint16_t port);


    // accessors
    int fd() const;
    //void setSocketContext(TCPConnectionHandle &c);
    void setOnData(OnDataFn fn);
    void adoptFd(int fd, EpollReactor *reactor);

    static void setSocketOption(int fd, int name, bool isEnable);
    static void setSocketShared(int fd, bool isEnable);
    static int setSocketNonblocking(int fd);
    static void setSocketResourceAddress(int fd, bool isEnable);
    static void setSocketNoDelay(int fd, bool isEnable);
    static void setSocketCork(int fd, bool isEnable);
    static void setSocketKeepAlive(int fd, bool isEnable);
    static void setSocketLowDelay(int fd, bool isEnable);


    SocketContext *getSocketContext() ;


protected:
    OnDataFn onData_ { nullptr };
    struct SocketContext m_SocketContext {}; // composition with low-level TCP
    void _connect(const char *hostname, char **ips, size_t count);


private:
    EpollReactor* m_pReactor = nullptr;
    bool m_readPaused { false };
    bool m_pendingClose { false };
    void pause_reading();
    void resume_reading();

    //CloseCallback close_cb_{};
    //EpollModCallback epoll_mod_cb_{};
    void handleOnData(const uint8_t * d, size_t n);
    static void connect_cb(const char *hostname, char **ips, size_t count, DNSLookup::QUERY_TYPE qtype, void *p);

};



#endif // CLSTCPSOCKET_H
