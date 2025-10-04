#include "clsDNSLookup.h"
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <sstream>

DNSLookup::DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec, size_t cache_max_size) :
    m_pReactor(reactor),
    m_cache_max_size(cache_max_size),
    m_cache_ttl_sec(cache_ttl_sec) {
    m_Timeout = 3;

    load_dns_servers();
    m_dns_servers = {"8.8.8.8", "8.8.4.4"};  // fallback
    reset_socket();
}

DNSLookup::~DNSLookup() {
    close();
    for (auto& p : m_pending) {
        call_callback(p.second, {});
        delete p.second;
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

    close(); // Close existing socket if any

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

    m_SocketContext.ev.events = EPOLLIN | EPOLLERR; // Register EPOLLIN and EPOLLERR
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

void DNSLookup::setTimeout(uint16_t newTimeout)
{
    m_Timeout = newTimeout;
}

void DNSLookup::setCache_ttl_sec(size_t newCache_ttl_sec)
{
    m_cache_ttl_sec = newCache_ttl_sec;
}

int DNSLookup::resolve(const char *hostname, dns_callback_t cb, void *user_data, QUERY_TYPE QuryType) {
    if (!hostname || !cb) return -1;

    std::string host(hostname);
    time_t now = time(nullptr);

    // Check cache
    auto it = m_cache_map.find(host);
    if (it != m_cache_map.end() && (now - it->second.timestamp) < m_cache_ttl_sec) {
        // Use A or AAAA based on what is available
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

    if(QuryType == DNSLookup::A){
        // Generate query for A
        uint16_t qid_a = generate_query_id();
        DNSRequest* req_a = new DNSRequest{host, cb, user_data, qid_a, DNSLookup::A, now};
        m_pending[{qid_a, DNSLookup::A}] = req_a;

        std::vector<uint8_t> query_a = build_dns_query(host, DNSLookup::A);
        query_a[0] = qid_a >> 8;
        query_a[1] = qid_a & 0xFF;

#ifdef DEBUG
        std::stringstream ss_a;
        ss_a << "DNS query sent for " << hostname << " (ID " << qid_a << ", QTYPE=A): ";
        for (size_t i = 0; i < query_a.size(); ++i) {
            ss_a << std::hex << std::setw(2) << std::setfill('0') << (int)query_a[i] << " ";
        }
        printf("%s\n", ss_a.str().c_str());
#endif

        if (!send_query(query_a, qid_a)) {
            m_pending.erase({qid_a, DNSLookup::A});
            delete req_a;
            cb(hostname, nullptr, 0, DNSLookup::A, user_data);
            return -1;
        }
    }



    if(QuryType == DNSLookup::AAAA){
        // Generate query for AAAA
        uint16_t qid_aaaa = generate_query_id();
        DNSRequest* req_aaaa = new DNSRequest{host, cb, user_data, qid_aaaa, DNSLookup::AAAA, now};
        m_pending[{qid_aaaa, DNSLookup::AAAA}] = req_aaaa;

        std::vector<uint8_t> query_aaaa = build_dns_query(host, DNSLookup::AAAA);
        query_aaaa[0] = qid_aaaa >> 8;
        query_aaaa[1] = qid_aaaa & 0xFF;

#ifdef DEBUG
        std::stringstream ss_aaaa;
        ss_aaaa << "DNS query sent for " << hostname << " (ID " << qid_aaaa << ", QTYPE=AAAA): ";
        for (size_t i = 0; i < query_aaaa.size(); ++i) {
            ss_aaaa << std::hex << std::setw(2) << std::setfill('0') << (int)query_aaaa[i] << " ";
        }
        printf("%s\n", ss_aaaa.str().c_str());
#endif

        if (!send_query(query_aaaa, qid_aaaa)) {
            m_pending.erase({qid_aaaa, DNSLookup::AAAA});
            delete req_aaaa;
        }
    }

    return 0;
}

void DNSLookup::on_dns_read() {
    uint8_t buffer[512];
    struct sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t len = recvfrom(m_SocketContext.fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
#ifdef DEBUG
        perror("DNS recvfrom failed");
#endif
        reset_socket();
        return;
    }

#ifdef DEBUG
    std::stringstream ss;
    ss << "DNS response received (len=" << len << "): ";
    for (ssize_t i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    printf("%s\n", ss.str().c_str());
#endif

    if (len < 12) {
#ifdef DEBUG
        printf("DNS response too short\n");
#endif
        return;
    }

    uint16_t id = (buffer[0] << 8) | buffer[1];
    uint16_t flags = (buffer[2] << 8) | buffer[3];
    uint16_t rcode = flags & 0x0F;
    if (!(flags & 0x8000)) {
#ifdef DEBUG
        printf("Not a DNS response (QR=0)\n");
#endif
        return;
    }

    DNSRequest* req = nullptr;
    auto it = m_pending.find({id, DNSLookup::A});
    if (it == m_pending.end()) {
        it = m_pending.find({id, DNSLookup::AAAA});
        if (it == m_pending.end()) {
#ifdef DEBUG
            printf("Unknown query ID: [%u] size:[%zu]\n", id, m_pending.size());
#endif
            //printf("Unknown query ID: [%u] qid:[%hu]\n", id, it->second->qid);
            return;
        }
    }

    req = it->second;

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
        printf("DNS error for %s (QTYPE=%u): RCODE=%u (%s)\n", req->hostname.c_str(), req->qtype, rcode, rcode_str);
        size_t pos = 12;
        while (pos < len && buffer[pos] != 0) {
            if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }
            pos += buffer[pos] + 1;
        }
        pos++;
        pos += 4;
        uint16_t nscount = (buffer[8] << 8) | buffer[9];
        std::stringstream auth_ss;
        auth_ss << "Authority section (" << nscount << "): ";
        for (uint16_t i = 0; i < nscount && pos < len; ++i) {
            while (pos < len && buffer[pos] != 0) {
                if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }
                auth_ss << (char)buffer[pos + 1];
                pos += buffer[pos] + 1;
            }
            pos++;
            if (pos + 10 > len) break;
            pos += 10;
        }
        printf("%s\n", auth_ss.str().c_str());
#endif
        call_callback(req, {});
        delete req;
        m_pending.erase(it);
        return;
    }

    uint16_t qdcount = (buffer[4] << 8) | buffer[5];
    uint16_t ancount = (buffer[6] << 8) | buffer[7];

#ifdef DEBUG
    printf("on_dns_read() len: %zd qdcount: %hu ancount: %hu\n", len, qdcount, ancount);
#endif

    std::vector<std::string> ips;
    if (parse_dns_response(buffer, len, id, ips)) {
        update_lru_cache(req->hostname, ips, req->qtype);
        call_callback(req, ips);
    } else {
        call_callback(req, {});
    }

    delete req;
    m_pending.erase(it);
}

void DNSLookup::maintenance() {
    time_t now = time(nullptr);
    std::vector<std::pair<uint16_t, uint16_t>> to_remove;
    for (auto& p : m_pending) {
        if (now - p.second->sent_time >= m_Timeout) {
#ifdef DEBUG
            printf("DNS timeout for %s (ID %u, QTYPE=%u)\n", p.second->hostname.c_str(), p.first.first, p.first.second);
#endif
            call_callback(p.second, {});
            to_remove.push_back(p.first);
        }
    }
    for (const auto& id : to_remove) {
        delete m_pending[id];
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
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 65535);
    return static_cast<uint16_t>(dis(gen));
}

std::vector<uint8_t> DNSLookup::build_dns_query(const std::string& hostname, DNSLookup::QUERY_TYPE qtype) {
    std::vector<uint8_t> packet;
    uint16_t id = 0;
    packet.push_back(id >> 8);
    packet.push_back(id & 0xFF);

    packet.push_back(0x01);
    packet.push_back(0x00);

    packet.push_back(0x00);
    packet.push_back(0x01);
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00);

    std::string::size_type last_pos = 0;
    std::string::size_type pos;
    while ((pos = hostname.find('.', last_pos)) != std::string::npos) {
        size_t len = pos - last_pos;
        if (len > 63) len = 63;
        if (len == 0) { last_pos = pos + 1;
            continue;
        }

        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), hostname.begin() + last_pos, hostname.begin() + pos);
        last_pos = pos + 1;
    }

    size_t len = hostname.size() - last_pos;
    if (len > 63)
        len = 63;
    if (len > 0) {
        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), hostname.begin() + last_pos, hostname.end());
    }
    packet.push_back(0);

    packet.push_back(qtype >> 8);
    packet.push_back(qtype & 0xFF);
    packet.push_back(0x00);
    packet.push_back(0x01);

    return packet;
}

bool DNSLookup::send_query(const std::vector<uint8_t>& query, uint16_t qid) {
    std::vector<uint8_t> pkt = query;
    pkt[0] = qid >> 8;
    pkt[1] = qid & 0xFF;

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
    //return true;///////////////////////////
    ssize_t sent = sendto(m_SocketContext.fd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
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

        if (type == DNSLookup::A && rdlen == 4) {  // A record
            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr;
            memcpy(&addr.s_addr, packet + pos, 4);
            if (inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
#ifdef DEBUG
                printf("Parsed A record: %s\n", ip_str);
#endif
            }
        } else if (type == DNSLookup::AAAA && rdlen == 16) {  // AAAA record
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
    req->cb(req->hostname.c_str(), ips, count, req->qtype, req->user_data);
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
                if (m_dns_servers.size() >= 2)
                    break;
            }
        }
    }
}

void DNSLookup::update_lru_cache(const std::string& hostname, const std::vector<std::string>& ips, DNSLookup::QUERY_TYPE qtype) {
    auto it = m_cache_map.find(hostname);
    if (it != m_cache_map.end()) {
        m_cache_list.erase(std::find(m_cache_list.begin(), m_cache_list.end(), hostname));
    } else {
        // Evict if over max size
        while (m_cache_list.size() >= m_cache_max_size) {
            std::string oldest = m_cache_list.back();
            m_cache_list.pop_back();
            m_cache_map.erase(oldest);
        }
    }

    // Update or insert cache entry
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
