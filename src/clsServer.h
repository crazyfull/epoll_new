#ifndef SERVER_H
#define SERVER_H
#include "clsEpollReactor.h"

// ============================== Server / Server (10)(17)(18) =========
#include <thread>
#include <vector>

class Server
{
public:
    explicit Server(int maxConnection, int shards = std::max(1u, std::thread::hardware_concurrency()));
    ~Server();

    void setOnAccepted(EpollReactor::acceptCallback f, void *p);
    void AddNewListener(int Port, const char *bindIP = nullptr);
    bool start();
    void stop();


    void setUseGarbageCollector(bool value);
    EpollReactor *getRoundRobinShard();

private:
    int m_shardCount;

    std::vector <std::unique_ptr <EpollReactor>> m_workerList {};
    std::vector <std::thread> m_threads {};
    std::thread m_thread {};
    std::atomic <bool> m_needToStop {};
    std::atomic <uint32_t> m_roundRobin { 0 };
    void setup_signals();

};

#endif // SERVER_H
