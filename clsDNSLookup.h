#ifndef CLSDNSLOOKUP_H
#define CLSDNSLOOKUP_H

//#include "clsTCPSocket.h"
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <list>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
//#define DEBUG 1

class Timer;
class EpollReactor;
class DNSLookup {
public:
    enum QUERY_TYPE {
        A = 1,
        AAAA = 28
    };

    typedef void (*callback_t)(const char *hostname, char **ips, size_t count, DNSLookup::QUERY_TYPE qtype, void *user_data);

    struct DNSRequest {
        char* hostname;
        callback_t cb;
        void* user_data;
        uint16_t qid;
        DNSLookup::QUERY_TYPE qtype;
        time_t sent_time;
        uint16_t retry_count;
    };

    DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec = 300, size_t cache_max_size = 2000);
    ~DNSLookup();

    int resolve(const char *hostname, callback_t cb, void *user_data, DNSLookup::QUERY_TYPE QuryType = DNSLookup::A);
    void on_dns_read();
    void maintenance();
    int fd() const;
    void close();
    void reset_socket();

    void setTimeout(uint16_t newTimeout);
    void setCache_ttl_sec(size_t newCache_ttl_sec);
    void setMaxRetries(uint16_t newMaxRetries);

private:
    EpollReactor* m_pReactor;
    Timer *m_pTimer;
    struct SocketContext m_SocketContext {};
    std::unordered_map<uint16_t, DNSRequest*> m_pending;
    uint8_t m_shared_buffer[512];
    uint8_t m_dns_header[12] = {
        0x00, 0x00,
        0x01, 0x00,
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00
    };
    uint16_t m_next_qid = 1;

    struct CacheEntry {
        std::vector<std::string> a_ips;
        std::vector<std::string> aaaa_ips;
        time_t timestamp;
    };

    std::unordered_map<std::string, CacheEntry> m_cache_map;
    std::list<std::string> m_cache_list;
    size_t m_cache_max_size;
    size_t m_cache_ttl_sec;
    std::vector<std::string> m_dns_servers;
    uint16_t m_Timeout;
    uint16_t m_max_retries;

    // Object Pool
    std::vector<DNSRequest> m_request_pool;
    std::list<DNSRequest*> m_free_requests;

    void init_request_pool(size_t pool_size);
    DNSRequest* acquire_request();
    void release_request(DNSRequest* req);

    uint16_t generate_query_id();
    std::vector<uint8_t> build_dns_query(const char* hostname, QUERY_TYPE qtype = DNSLookup::A);
    bool parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips);
    bool send_query(const std::vector<uint8_t>& query, uint16_t qid);
    void load_dns_servers();
    void call_callback(DNSRequest* req, const std::vector<std::string>& ips);
    void free_ips(char** ips, size_t count);
    void update_lru_cache(const std::string& hostname, const std::vector<std::string>& ips, DNSLookup::QUERY_TYPE qtype);
};

#endif // CLSDNSLOOKUP_H
