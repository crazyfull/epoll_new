#include "clsListener.h"
#include "clsSocketBase.h"
#include "clsEpollReactor.h"
#include "epoll.h"

Listener::Listener() {

}

Listener::~Listener()
{

}

bool Listener::Addlisten(int port)
{
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(listen_fd < 0) {
        perror("socket");
        return false;
    }

    if(SocketBase::setSocketNonblocking(listen_fd) == -1) {
        perror("fcntl");
        return false;
    }

    SocketBase::setSocketShared(listen_fd, true);
    SocketBase::setSocketResourceAddress(listen_fd, true);
    SocketBase::setSocketNoDelay(listen_fd, true);


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


    //add listener socket to epoll shard
    return m_pReactor->add_listener(listen_fd);
}
