#ifndef CLSDNSLOOKUP_H
#define CLSDNSLOOKUP_H

#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <random>  // for random query ID

#include "clsEpollReactor.h"  // for integration

typedef void (*dns_callback_t)(const char *hostname, char **ips, size_t count, void *user_data);

struct DNSRequest {
    std::string hostname;
    dns_callback_t cb;
    void* user_data;
    uint16_t query_id;
    time_t sent_time;
};

class DNSLookup {
public:
    DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec = 300);  // TTL in seconds
    ~DNSLookup();

    // Resolve hostname (async via epoll)
    int resolve(const char *hostname, dns_callback_t cb, void *user_data);

    // Called by reactor in run() when EPOLLIN on DNS fd
    void on_dns_read();

    // Flush old cache and pending timeouts (call in reactor maintenance)
    void maintenance();

    int fd() const;
    void close();
private:
    EpollReactor* m_pReactor;
    struct SocketContext m_SocketContext {};

    std::unordered_map<uint16_t, DNSRequest*> m_pending;  // by query_id
    std::unordered_map<std::string, std::pair<std::vector<std::string>, time_t>> m_cache;  // hostname -> (IPs, timestamp)
    size_t m_cache_ttl_sec;
    std::vector<std::string> m_dns_servers;  // e.g., "8.8.8.8"

    // DNS packet helpers (inspired by dns.c)
    uint16_t generate_query_id();
    std::vector<uint8_t> build_dns_query(const std::string& hostname, uint16_t qtype = 1 /* A */);
    bool parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips);
    bool send_query(const std::vector<uint8_t>& query, uint16_t qid);
    void load_dns_servers();  // parse /etc/resolv.conf
    void call_callback(DNSRequest* req, const std::vector<std::string>& ips);
    void free_ips(char** ips, size_t count);  // helper for callback
};

#endif // CLSDNSLOOKUP_H
