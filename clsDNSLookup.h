#ifndef CLSDNSLOOKUP_H
#define CLSDNSLOOKUP_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <random>

#include "clsEpollReactor.h"
//#define DEBUG 1
typedef void (*dns_callback_t)(const char *hostname, char **ips, size_t count, void *user_data);

struct DNSRequest {
    std::string hostname;
    dns_callback_t cb;
    void* user_data;
    uint16_t qid;
    uint16_t qtype;  // A=1, AAAA=28
    time_t sent_time;
};

class DNSLookup {
public:
    DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec = 300, size_t cache_max_size = 1000);
    ~DNSLookup();

    int resolve(const char *hostname, dns_callback_t cb, void *user_data);
    void on_dns_read();
    void maintenance();
    int fd() const;
    void close();

private:
    EpollReactor* m_pReactor;
    struct SocketContext m_SocketContext {};

    std::map<std::pair<uint16_t, uint16_t>, DNSRequest*> m_pending;  // key: {qid, qtype}
    struct CacheEntry {
        std::vector<std::string> ips;
        time_t timestamp;
    };
    std::unordered_map<std::string, CacheEntry> m_cache_map;
    std::list<std::string> m_cache_list;
    size_t m_cache_max_size;
    size_t m_cache_ttl_sec;
    std::vector<std::string> m_dns_servers;

    uint16_t generate_query_id();
    std::vector<uint8_t> build_dns_query(const std::string& hostname, uint16_t qtype = 1);
    bool parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips);
    bool send_query(const std::vector<uint8_t>& query, uint16_t qid);
    void load_dns_servers();
    void call_callback(DNSRequest* req, const std::vector<std::string>& ips);
    void free_ips(char** ips, size_t count);
    void update_lru_cache(const std::string& hostname, const std::vector<std::string>& ips);
};

#endif // CLSDNSLOOKUP_H
