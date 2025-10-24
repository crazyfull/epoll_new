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
    enum socketStatus{
        Ready = 1,
        Closed = 2,
        Closing = 3,
        Connecting = 4,
        Connected = 5
    };


    uint64_t recBytes = 0;
    uint64_t sndBytes = 0;

    void setReactor(EpollReactor* r);

    using OnDataFn = void(*)(void* , const uint8_t* data, size_t len);
    //using OnConnectingFn = void(*)(void* routin, void* p);
    using OnConnectingFn = void(*)(void* p);

    using OnAcceptedFn = void(*)(void*);
    using OnCloseFn = void(*)(void*);
    using OnConnectedFn = void(*)(void*);
    using OnConnectFailedFn = void(*)(void*);

    using OnPauseFn = void(*)(void* p);
    using OnResumeFn = void(*)(void* p);



    void setOnData(OnDataFn fn, void *Arg);
    void setOnConnecting(OnConnectingFn fn, void* Arg);
    void setOnConnected(OnConnectedFn fn, void *Arg);

    void setOnAccepted(OnAcceptedFn fn, void* Arg);
    void setOnClose(OnCloseFn fn, void* Arg);
    void setOnConnectFailed(OnConnectFailedFn fn, void* Arg);

    void setOnPause(OnPauseFn fn, void* Arg);
    void setOnResume(OnResumeFn fn, void* Arg);


    //using CloseCallback = std::function<void(int)>;                   // fd
    //using EpollModCallback = std::function<void(int, uint32_t)>;      // fd, newFlags

    virtual~TCPSocket() =  default; // (13) RAII
    virtual void onAccepted() {}                                // (non-hot)
    virtual void onClose() {}                                   // (non-hot)
    virtual void onConnectFailed(){}  // (non-hot)
    virtual void onConnecting(){}
    virtual void onConnected(){}
    virtual void onReceiveData(const uint8_t* Data, size_t len){}


    // setter hot path entry — called by shard on EPOLLIN
    void onReadable();
    void onWritable();
    void handleHalfClose();

    //void setCloseCallback(CloseCallback cb);
    //void setEpollModCallback(EpollModCallback cb);

    // app-side send helper (thread-affinity: shard thread)
    void send(const void * data, size_t len);
    void close(bool force = false);
    bool connectTo(const char *host, uint16_t port);


    // accessors
    int fd() const;
    TCPSocket *getPointer();
    //void setSocketContext(TCPConnectionHandle &c);

    bool adoptFd(int fd);

    static void setSocketOption(int fd, int name, bool isEnable);
    static void setSocketShared(int fd, bool isEnable);
    static int setSocketNonblocking(int fd);
    static void setSocketResourceAddress(int fd, bool isEnable);
    static void setSocketNoDelay(int fd, bool isEnable);
    static void setSocketCork(int fd, bool isEnable);
    static void setSocketKeepAlive(int fd, bool isEnable);
    static void setSocketLowDelay(int fd, bool isEnable);


    SocketContext *getSocketContext() ;

    void pause_reading();
    void resume_reading();
    int getErrorCode();

    socketStatus getStatus() const;

    EpollReactor *getReactor() const;
    void _accepted(int fd);

protected:
    OnDataFn m_onData { nullptr };
    OnAcceptedFn m_onAccepted { nullptr };
    OnCloseFn m_onClose { nullptr };
    OnConnectFailedFn m_onConnectFailed { nullptr };
    OnConnectingFn m_onConnecting { nullptr };
    OnConnectedFn m_onConnected { nullptr };

    OnPauseFn m_onPause { nullptr };
    OnResumeFn m_onResume { nullptr };

    //argumnets
    void* m_callbacksArg { nullptr };

    struct SocketContext m_SocketContext {}; // composition with low-level TCP
    void _connect(const char *hostname, char **ips, size_t count);
    void setStatus(socketStatus newStatus);
private:
    EpollReactor* m_pReactor = nullptr;
    bool m_readPaused { false };
    bool m_pendingClose { false };
    socketStatus status {Ready};



    void updateLastActive();

    //CloseCallback close_cb_{};
    //EpollModCallback epoll_mod_cb_{};
    void handleOnData(const uint8_t * d, size_t n);
    void handleOnAccepted();
    void handleOnClose();
    void handleOnConnectFailed();
    void handleOnConnecting();
    void handleOnConnected();
    void handleOnPause();
    void handleOnResume();

    static void connect_cb(const char *hostname, char **ips, size_t count, DNSLookup::QUERY_TYPE qtype, void *p);

};



#endif // CLSTCPSOCKET_H
