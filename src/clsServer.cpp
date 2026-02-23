#include "clsServer.h"

Server::Server(int maxConnection, int shards): m_shardCount(shards), m_needToStop(false)
{
    for(int i = 0; i < m_shardCount; ++i)
        m_workerList.emplace_back(std::make_unique<EpollReactor> (i, maxConnection));
}

Server::~Server() {
    stop();
}

bool Server::AddNewListener(int Port, const char *bindIP)
{
    /**/
    if(!bindIP)
        bindIP = "0.0.0.0";

    for (auto& worker : m_workerList) {

        if(!worker->add_listener(Port)){
            return false;
        }
    }

    printf("StartListen: (%s:%d)\n", bindIP, Port);
    return true;

}

void Server::setOnAccepted(EpollReactor::acceptCallback f,void* p) {
    for(auto &worker: m_workerList)
        worker->setOnAccepted(f, p);
}

bool Server::start()
{
    // assign shard (6) round-robin

    //disable chon to main signal ro control mikonim
    // setup_signals();

    for(int i = 0; i < m_shardCount; ++i) {
        m_threads.emplace_back([this, i]() {
            m_workerList[i]->run(m_needToStop);
        });
    }

    return true;
}

void Server::stop() {

    for (auto& worker : m_workerList) {
        worker->stop_listener();
    }

    bool exp = false;
    if (!m_needToStop.compare_exchange_strong(exp, true))
        return;

    for (auto& reactor: m_workerList)
        reactor->wake();

    if (m_thread.joinable()) m_thread.join();

    for (auto& t : m_threads) {
        if (t.joinable() && t.get_id() != std::this_thread::get_id()) {
            t.join();
        }
    }
}

void Server::setUseGarbageCollector(bool value)
{
    for(auto &worker: m_workerList)
        worker->setUseGarbageCollector(value);
}

EpollReactor* Server::getRoundRobinShard()
{
    if (m_shardCount == 0) {
        throw std::runtime_error("No shards available");
    }
    //round-robin
    auto idx = m_roundRobin.fetch_add(1) % m_shardCount;
    return m_workerList[idx].get();
}


void Server::setup_signals() {
    sigset_t mask;
    sigemptyset( &mask);
    sigaddset( &mask, SIGINT);
    sigaddset( &mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    if(sfd != -1)
        ::close(sfd);
}

