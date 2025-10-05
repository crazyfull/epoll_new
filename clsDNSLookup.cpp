#include "clsDNSLookup.h"
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <sstream>

DNSLookup::DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec, size_t cache_max_size, uint16_t max_retries) :
    m_pReactor(reactor),
    m_cache_max_size(cache_max_size),
    m_cache_ttl_sec(cache_ttl_sec),
    m_max_retries(max_retries) {
    m_Timeout = 3;
    init_request_pool(1000);
    load_dns_servers();
    m_dns_servers = {"8.8.8.8", "8.8.4.4"};
    reset_socket();
}

DNSLookup::~DNSLookup() {
    close();
    for (auto& p : m_pending) {
        call_callback(p.second, {});
        release_request(p.second);
    }
    m_pending.clear();
}

int DNSLookup::fd() const {
    return m_SocketContext.fd;
}

void DNSLookup::close() {
    if (m_SocketContext.fd != -1) {
        m_pReactor->del_fd(m_SocketContext.fd, true);
        ::close(m_SocketContext.fd);
        m_SocketContext.fd = -1;
    }
}

void DNSLookup::reset_socket() {
#ifdef DEBUG
    int error = 0;
    socklen_t errlen = sizeof(error);
    getsockopt(m_SocketContext.fd, SOL_SOCKET, SO_ERROR, &error, &errlen);
    printf("Socket error detected: %s\n", strerror(error));
#endif

    close();

    m_SocketContext.fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_SocketContext.fd == -1) {
#ifdef DEBUG
        perror("DNS socket creation failed");
#endif
        return;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(m_SocketContext.fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
#ifdef DEBUG
        perror("DNS bind failed");
#endif
        close();
        return;
    }

    m_SocketContext.ev.events = EPOLLIN | EPOLLERR;
    bool ret = m_pReactor->register_fd(fd(), &m_SocketContext.ev, IS_DNS_LOOKUP_SOCKET, this);
    if (!ret) {
#ifdef DEBUG
        perror("Add DNS fd to epoll failed");
#endif
        close();
        return;
    }

#ifdef DEBUG
    printf("DNSLookup initialized with fd %d\n", fd());
#endif
}

void DNSLookup::setTimeout(uint16_t newTimeout) {
    m_Timeout = newTimeout;
}

void DNSLookup::setCache_ttl_sec(size_t newCache_ttl_sec) {
    m_cache_ttl_sec = newCache_ttl_sec;
}

void DNSLookup::setMaxRetries(uint16_t newMaxRetries) {
    m_max_retries = newMaxRetries;
}

void DNSLookup::init_request_pool(size_t pool_size) {
    m_request_pool.resize(pool_size);
    for (auto& req : m_request_pool) {
        m_free_requests.push_back(&req);
    }
}

DNSLookup::DNSRequest* DNSLookup::acquire_request() {
    if (m_free_requests.empty()) {
        return nullptr;
    }
    DNSRequest* req = m_free_requests.front();
    m_free_requests.pop_front();
    return req;
}

void DNSLookup::release_request(DNSRequest* req) {
    delete[] req->hostname;
    req->hostname = nullptr;
    m_free_requests.push_back(req);
}

int DNSLookup::resolve(const char *hostname, dns_callback_t cb, void *user_data, QUERY_TYPE QuryType) {
    if (!hostname || !cb) return -1;

    std::string host(hostname);
    time_t now = time(nullptr);

    // Check cache
    auto it = m_cache_map.find(host);
    if (it != m_cache_map.end() && (now - it->second.timestamp) < m_cache_ttl_sec) {
        const std::vector<std::string>& cached_ips = it->second.a_ips.empty() ? it->second.aaaa_ips : it->second.a_ips;
        DNSLookup::QUERY_TYPE qtype = it->second.a_ips.empty() ? DNSLookup::AAAA : DNSLookup::A;
        size_t count = cached_ips.size();
        char** ips = nullptr;
        if (count > 0) {
            ips = new char*[count];
            for (size_t i = 0; i < count; ++i) {
                ips[i] = new char[cached_ips[i].size() + 1];
                strcpy(ips[i], cached_ips[i].c_str());
            }
        }

        printf("use cache\n");
        cb(hostname, ips, count, qtype, user_data);
        m_cache_list.erase(std::find(m_cache_list.begin(), m_cache_list.end(), host));
        m_cache_list.push_front(host);
        return 0;
    }

    if (QuryType == DNSLookup::A) {
        uint16_t qid_a = generate_query_id();
        DNSRequest* req_a = acquire_request();
        if (!req_a) return -1;
        req_a->hostname = new char[strlen(hostname) + 1];
        strcpy(req_a->hostname, hostname);
        *req_a = {req_a->hostname, cb, user_data, qid_a, DNSLookup::A, now, 0};
        m_pending[qid_a] = req_a;

        std::vector<uint8_t> query_a = build_dns_query(hostname, DNSLookup::A);
        if (!send_query(query_a, qid_a)) {
            m_pending.erase(qid_a);
            release_request(req_a);
            cb(hostname, nullptr, 0, DNSLookup::A, user_data);
            return -1;
        }
    }

    if (QuryType == DNSLookup::AAAA) {
        uint16_t qid_aaaa = generate_query_id();
        DNSRequest* req_aaaa = acquire_request();
        if (!req_aaaa) return -1;
        req_aaaa->hostname = new char[strlen(hostname) + 1];
        strcpy(req_aaaa->hostname, hostname);
        *req_aaaa = {req_aaaa->hostname, cb, user_data, qid_aaaa, DNSLookup::AAAA, now, 0};
        m_pending[qid_aaaa] = req_aaaa;

        std::vector<uint8_t> query_aaaa = build_dns_query(hostname, DNSLookup::AAAA);
        if (!send_query(query_aaaa, qid_aaaa)) {
            m_pending.erase(qid_aaaa);
            release_request(req_aaaa);
            cb(hostname, nullptr, 0, DNSLookup::AAAA, user_data);
            return -1;
        }
    }

    return 0;
}

void DNSLookup::on_dns_read() {
    struct sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t len = recvfrom(m_SocketContext.fd, m_shared_buffer, sizeof(m_shared_buffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
#ifdef DEBUG
        perror("DNS recvfrom failed");
#endif
        reset_socket();
        // تلاش مجدد برای تمام درخواست‌های در حال انتظار
        for (auto& p : m_pending) {
            if (p.second->retry_count < m_max_retries) {
                p.second->retry_count++;
                p.second->sent_time = time(nullptr);
                std::vector<uint8_t> query = build_dns_query(p.second->hostname, p.second->qtype);
                send_query(query, p.second->qid);
#ifdef DEBUG
                printf("Retry %u for %s (ID %u, QTYPE=%u)\n", p.second->retry_count, p.second->hostname, p.second->qid, p.second->qtype);
#endif
            }
        }
        return;
    }

    if (len < 12) {
#ifdef DEBUG
        printf("DNS response too short\n");
#endif
        return;
    }

    uint16_t id = (m_shared_buffer[0] << 8) | m_shared_buffer[1];
    uint16_t flags = (m_shared_buffer[2] << 8) | m_shared_buffer[3];
    uint16_t rcode = flags & 0x0F;
    if (!(flags & 0x8000)) {
#ifdef DEBUG
        printf("Not a DNS response (QR=0)\n");
#endif
        return;
    }

    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
#ifdef DEBUG
        printf("Unknown query ID: [%u] size:[%zu]\n", id, m_pending.size());
#endif
        return;
    }

    DNSRequest* req = it->second;
    if (rcode != 0) {
#ifdef DEBUG
        const char* rcode_str = "Unknown";
        switch (rcode) {
        case 1: rcode_str = "FormErr"; break;
        case 2: rcode_str = "ServFail"; break;
        case 3: rcode_str = "NXDOMAIN"; break;
        case 4: rcode_str = "NotImp"; break;
        case 5: rcode_str = "Refused"; break;
        }
        printf("DNS error for %s (QTYPE=%u): RCODE=%u (%s)\n", req->hostname, req->qtype, rcode, rcode_str);
#endif
        if (req->retry_count < m_max_retries) {
            req->retry_count++;
            req->sent_time = time(nullptr);
            std::vector<uint8_t> query = build_dns_query(req->hostname, req->qtype);
            if (send_query(query, req->qid)) {
#ifdef DEBUG
                printf("Retry %u for %s (ID %u, QTYPE=%u)\n", req->retry_count, req->hostname, req->qid, req->qtype);
#endif
                return;
            }
        }
        call_callback(req, {});
        release_request(req);
        m_pending.erase(it);
        return;
    }

    uint16_t qdcount = (m_shared_buffer[4] << 8) | m_shared_buffer[5];
    uint16_t ancount = (m_shared_buffer[6] << 8) | m_shared_buffer[7];

#ifdef DEBUG
    printf("on_dns_read() len: %zd qdcount: %hu ancount: %hu\n", len, qdcount, ancount);
#endif

    std::vector<std::string> ips;
    if (parse_dns_response(m_shared_buffer, len, id, ips)) {
        update_lru_cache(req->hostname, ips, req->qtype);
        call_callback(req, ips);
    } else {
        if (req->retry_count < m_max_retries) {
            req->retry_count++;
            req->sent_time = time(nullptr);
            std::vector<uint8_t> query = build_dns_query(req->hostname, req->qtype);
            if (send_query(query, req->qid)) {
#ifdef DEBUG
                printf("Retry %u for %s (ID %u, QTYPE=%u)\n", req->retry_count, req->hostname, req->qid, req->qtype);
#endif
                return;
            }
        }
        call_callback(req, {});
    }

    release_request(req);
    m_pending.erase(it);
}

void DNSLookup::maintenance() {
    time_t now = time(nullptr);
    std::vector<uint16_t> to_remove;
    for (auto& p : m_pending) {
        if (now - p.second->sent_time >= m_Timeout) {
            if (p.second->retry_count < m_max_retries) {
                p.second->retry_count++;
                p.second->sent_time = now;
                std::vector<uint8_t> query = build_dns_query(p.second->hostname, p.second->qtype);
                if (send_query(query, p.second->qid)) {
#ifdef DEBUG
                    printf("Retry %u for %s (ID %u, QTYPE=%u) due to timeout\n", p.second->retry_count, p.second->hostname, p.first, p.second->qtype);
#endif
                    continue;
                }
            }
#ifdef DEBUG
            printf("DNS timeout for %s (ID %u, QTYPE=%u, retries=%u)\n", p.second->hostname, p.first, p.second->qtype, p.second->retry_count);
#endif
            call_callback(p.second, {});
            to_remove.push_back(p.first);
        }
    }
    for (const auto& id : to_remove) {
        release_request(m_pending[id]);
        m_pending.erase(id);
    }

    std::vector<std::string> old_hosts;
    for (auto& c : m_cache_map) {
        if (now - c.second.timestamp > m_cache_ttl_sec) {
            old_hosts.push_back(c.first);
        }
    }
    for (const auto& h : old_hosts) {
        m_cache_list.erase(std::find(m_cache_list.begin(), m_cache_list.end(), h));
        m_cache_map.erase(h);
    }
}

uint16_t DNSLookup::generate_query_id() {
    uint16_t qid = m_next_qid++;
    if (m_next_qid == 65535) m_next_qid = 1;
    return qid;
}

std::vector<uint8_t> DNSLookup::build_dns_query(const char* hostname, QUERY_TYPE qtype) {
    std::vector<uint8_t> packet(m_dns_header, m_dns_header + 12);

    size_t last_pos = 0;
    size_t len = strlen(hostname);
    size_t pos;
    const char* ptr = hostname;
    while ((pos = strcspn(ptr, ".")) != len) {
        size_t segment_len = pos;
        if (segment_len > 63) segment_len = 63;
        if (segment_len == 0) {
            ptr += 1;
            len -= 1;
            continue;
        }
        packet.push_back(static_cast<uint8_t>(segment_len));
        packet.insert(packet.end(), ptr, ptr + segment_len);
        ptr += pos + 1;
        len -= pos + 1;
    }
    if (len > 0) {
        if (len > 63) len = 63;
        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), ptr, ptr + len);
    }
    packet.push_back(0);
    packet.push_back(qtype >> 8);
    packet.push_back(qtype & 0xFF);
    packet.push_back(0x00);
    packet.push_back(0x01);

    return packet;
}

bool DNSLookup::send_query(const std::vector<uint8_t>& query, uint16_t qid) {
    if (query.size() > sizeof(m_shared_buffer))
        return false;
    memcpy(m_shared_buffer, query.data(), query.size());
    m_shared_buffer[0] = qid >> 8;
    m_shared_buffer[1] = qid & 0xFF;

    if (m_dns_servers.empty()) {
#ifdef DEBUG
        printf("No DNS servers available\n");
#endif
        return false;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    if (inet_pton(AF_INET, m_dns_servers[0].c_str(), &server_addr.sin_addr) <= 0) {
#ifdef DEBUG
        printf("Invalid DNS server address: %s\n", m_dns_servers[0].c_str());
#endif
        return false;
    }
    printf("sendto:\n");
    ssize_t sent = sendto(m_SocketContext.fd, m_shared_buffer, query.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent < 0) {
#ifdef DEBUG
        perror("DNS sendto failed");
#endif
        return false;
    }
    return true;
}

bool DNSLookup::parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips) {
    if (len < 12) {
#ifdef DEBUG
        printf("DNS response too short\n");
#endif
        return false;
    }

    size_t pos = 12;
    while (pos < len && packet[pos] != 0) {
        if ((packet[pos] & 0xC0) == 0xC0) {
            pos += 2;
            break;
        }
        pos += packet[pos] + 1;
    }
    pos++;
    if (pos + 4 > len) {
#ifdef DEBUG
        printf("Invalid question section\n");
#endif
        return false;
    }
    pos += 4;

    uint16_t ancount = (packet[6] << 8) | packet[7];
#ifdef DEBUG
    printf("Parsing %hu answers\n", ancount);
#endif
    for (uint16_t i = 0; i < ancount && pos < len; ++i) {
        bool name_ended = false;
        while (pos < len && !name_ended) {
            if ((packet[pos] & 0xC0) == 0xC0) {
                pos += 2;
                name_ended = true;
            } else if (packet[pos] == 0) {
                pos++;
                name_ended = true;
            } else {
                pos += packet[pos] + 1;
            }
        }

        if (pos + 8 > len) {
#ifdef DEBUG
            printf("Invalid answer section (not enough bytes for TYPE+CLASS+TTL at pos %zu)\n", pos);
#endif
            break;
        }

        uint16_t type = (packet[pos] << 8) | packet[pos + 1];
        pos += 4;
        pos += 4;
        if (pos + 2 > len) {
#ifdef DEBUG
            printf("Invalid answer section (not enough bytes for RDLENGTH at pos %zu)\n", pos);
#endif
            break;
        }
        uint16_t rdlen = (packet[pos] << 8) | packet[pos + 1];
        pos += 2;

        if (pos + rdlen > len) {
#ifdef DEBUG
            printf("Invalid RDLENGTH (%u at pos %zu, len %zu)\n", rdlen, pos, len);
#endif
            break;
        }

        if (type == DNSLookup::A && rdlen == 4) {
            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr;
            memcpy(&addr.s_addr, packet + pos, 4);
            if (inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
#ifdef DEBUG
                printf("Parsed A record: %s\n", ip_str);
#endif
            }
        } else if (type == DNSLookup::AAAA && rdlen == 16) {
            char ip_str[INET6_ADDRSTRLEN];
            struct in6_addr addr;
            memcpy(&addr, packet + pos, 16);
            if (inet_ntop(AF_INET6, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
#ifdef DEBUG
                printf("Parsed AAAA record: %s\n", ip_str);
#endif
            }
        }
        pos += rdlen;
    }

    return !ips.empty();
}

void DNSLookup::call_callback(DNSRequest* req, const std::vector<std::string>& ips_vec) {
    size_t count = ips_vec.size();
    char** ips = nullptr;
    if (count > 0) {
        ips = new char*[count];
        for (size_t i = 0; i < count; ++i) {
            ips[i] = new char[ips_vec[i].size() + 1];
            strcpy(ips[i], ips_vec[i].c_str());
        }
    }
    req->cb(req->hostname, ips, count, req->qtype, req->user_data);
}

void DNSLookup::free_ips(char** ips, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        delete[] ips[i];
    }
    delete[] ips;
}

void DNSLookup::load_dns_servers() {
    std::ifstream file("/etc/resolv.conf");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("nameserver ") == 0) {
            std::string server = line.substr(11);
            if (!server.empty() && inet_addr(server.c_str()) != INADDR_NONE) {
                m_dns_servers.push_back(server);
                if (m_dns_servers.size() >= 2) break;
            }
        }
    }
}

void DNSLookup::update_lru_cache(const std::string& hostname, const std::vector<std::string>& ips, DNSLookup::QUERY_TYPE qtype) {
    auto it = m_cache_map.find(hostname);
    if (it != m_cache_map.end()) {
        m_cache_list.erase(std::find(m_cache_list.begin(), m_cache_list.end(), hostname));
    } else {
        while (m_cache_list.size() >= m_cache_max_size) {
            std::string oldest = m_cache_list.back();
            m_cache_list.pop_back();
            m_cache_map.erase(oldest);
        }
    }

    if (it == m_cache_map.end()) {
        CacheEntry entry{{}, {}, time(nullptr)};
        if (qtype == DNSLookup::A) {
            entry.a_ips = ips;
        } else if (qtype == DNSLookup::AAAA) {
            entry.aaaa_ips = ips;
        }
        m_cache_map[hostname] = entry;
    } else {
        if (qtype == DNSLookup::A) {
            it->second.a_ips = ips;
        } else if (qtype == DNSLookup::AAAA) {
            it->second.aaaa_ips = ips;
        }
        it->second.timestamp = time(nullptr);
    }

    m_cache_list.push_front(hostname);
}
