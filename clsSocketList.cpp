
#include "clsSocketList.h"
#include "clsTCPSocket.h"
#include <cstdio>

uint32_t SocketList::genIDCounter() const
{
    return m_genIDCounter;
}

uint32_t SocketList::count() const
{
    return m_activeConnectionList.size();
}


uint32_t SocketList::maximumSize() const
{
    /*
    int ret = 0;
    for (int i = 0; i < (int)m_list.size(); i++) {
        if(m_list[i]->fd > 0){
            ret++;
        }
    }
*/

    return m_list.size();
}

std::vector<SockInfo *> *SocketList::list()
{
    return &m_list;
}

void SocketList::forEachActive(std::function<void (SockInfo *)> callback) {
    m_activeConnectionList.for_each(callback);
}

SocketList::SocketList(int MaxFD):m_list(MaxFD, nullptr) {

    //init preallocate
    for (int i = 0; i < MaxFD; i++) {
        m_list[i] = new SockInfo;
    }
}

SocketList::~SocketList()
{
    for (int i = 0; i < (int)m_list.size(); i++) {
        delete m_list[i];
    }
}

SockInfo *SocketList::add(int fd, SockTypes sockType, void* ptr) {
    if (fd <= 0 || fd >= (int)m_list.size()){
        return nullptr;
    }

    SockInfo *pSocketInfo = m_list[fd];
    pSocketInfo->fd = fd;
    pSocketInfo->type = sockType;
    if(ptr)
        pSocketInfo->socketBasePtr = ptr;


    //
    //m_count++;
    m_genIDCounter++;
    if(m_genIDCounter == 0)
        m_genIDCounter = 1;

    pSocketInfo->genID = m_genIDCounter;   //m_GenIDList[fd];

    //add client to TCP active list
    m_activeConnectionList.push_back(pSocketInfo);

    return pSocketInfo;
}

SockInfo *SocketList::get(int fd, uint32_t sockgenId) {
    if (fd < 0 || fd >= (int)m_list.size())
        return nullptr;

    //printf("SocketList::get genID: %d sockgenId: %d\n", m_list[fd]->genID, sockgenId);
    /*
    if(m_list[fd]->genID == 0){
        return nullptr;
    }*/

    if(m_list[fd]->genID != sockgenId){
        return nullptr;
    }

    return m_list[fd];
}

void SocketList::remove(int fd) {
    if (fd < 0 || fd >= (int)m_list.size())
        return;

    SockInfo* pSocketInfo = m_list[fd];
    if (pSocketInfo) {
        //remove from active list
        m_activeConnectionList.remove(pSocketInfo);

        pSocketInfo->fd = -1;
        //m_count--;

        if(pSocketInfo->socketBasePtr){
            //printf("SocketList::remove: [%d]\n\n", fd);

            /*
            auto* p = clientSocket->socketBasePtr;
            clientSocket->socketBasePtr = nullptr;
            delete p;
            */
        }

        pSocketInfo->genID = ++m_genIDCounter;
        pSocketInfo->type = IS_NOT_SET;
        m_genIDCounter++;
    }
}


