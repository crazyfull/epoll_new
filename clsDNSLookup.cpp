#include "clsDNSLookup.h"
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <poll.h>  // for parsing resolv.conf if needed

DNSLookup::DNSLookup(EpollReactor* reactor, size_t cache_ttl_sec) :
    m_pReactor(reactor),
    m_cache_ttl_sec(cache_ttl_sec) {

    load_dns_servers();
    //if (m_dns_servers.empty()) {
    m_dns_servers = {"8.8.8.8", "8.8.4.4"};  // fallback
    //}

    // Create UDP socket
    m_SocketContext.fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_SocketContext.fd == -1) {
        perror("DNS socket creation failed");
        return;
    }

    // Bind to any port (ephemeral)
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(m_SocketContext.fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("DNS bind failed");
        close();
        return;
    }

    // set event
    m_SocketContext.ev.events = EPOLLIN;// | EPOLLET;

    // Add to epoll
    bool ret = m_pReactor->register_fd(fd(), &m_SocketContext.ev, IS_DNS_LOOKUP_SOCKET, this);
    if(!ret){
        perror("Add DNS fd to epoll failed");
        close();
        return;
    }


    printf("DNSLookup initialized with fd %d\n", fd());
}

DNSLookup::~DNSLookup() {
    close();

    // Flush pending (call callbacks with 0 ips if needed)
    for (auto& p : m_pending) {
        call_callback(p.second, {});
        delete p.second;
    }
    m_pending.clear();
}

int DNSLookup::fd() const {
    return m_SocketContext.fd;
}

void DNSLookup::close()
{
    m_pReactor->del_fd(m_SocketContext.fd, true);   // hamishe lazem nis
    ::close(m_SocketContext.fd);
    m_SocketContext.fd = -1;
}

int DNSLookup::resolve(const char *hostname, dns_callback_t cb, void *user_data) {
    if (!hostname || !cb)
        return -1;

    std::string host(hostname);
    time_t now = time(nullptr);

    // Check cache
    auto it = m_cache.find(host);
    if (it != m_cache.end() && (now - it->second.second) < m_cache_ttl_sec) {
        std::vector<std::string> cached_ips = it->second.first;
        size_t count = cached_ips.size();
        char** ips = new char*[count];
        for (size_t i = 0; i < count; ++i) {
            ips[i] = new char[cached_ips[i].size() + 1];
            strcpy(ips[i], cached_ips[i].c_str());
        }
        cb(hostname, ips, count, user_data);
        // Caller frees ips
        return 0;
    }

    // Generate query
    uint16_t qid = generate_query_id();
    DNSRequest* req = new DNSRequest{host, cb, user_data, qid, now};
    m_pending[qid] = req;

    std::vector<uint8_t> query = build_dns_query(host);
    if (!send_query(query, qid)) {
        m_pending.erase(qid);
        delete req;
        cb(hostname, nullptr, 0, user_data);
        return -1;
    }

    printf("DNS query sent for %s (ID %u)\n", hostname, qid);
    return 0;
}

void DNSLookup::on_dns_read() {

    uint8_t buffer[512];  // DNS max UDP size
    struct sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t len = recvfrom(m_SocketContext.fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        perror("DNS recvfrom failed");
        return;
    }


    // Parse response (header 12 bytes)
    if (len < 12)
        return;

    uint16_t id = (buffer[0] << 8) | buffer[1];
    uint16_t flags = (buffer[2] << 8) | buffer[3];
    if ((flags & 0x0F) != 0x0C){
        perror("Not standard query response");
        //return;  // Not standard query response
    }

    uint16_t qdcount = (buffer[4] << 8) | buffer[5];
    uint16_t ancount = (buffer[6] << 8) | buffer[7];

    printf("on_dns_read() len: %zd qdcount: [%hu]\n", len, ancount);

    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;  // Unknown query


    DNSRequest* req = it->second;
    std::vector<std::string> ips;
    if (parse_dns_response(buffer, len, id, ips)) {
        // Cache it
        m_cache[req->hostname] = {ips, time(nullptr)};
        call_callback(req, ips);
    } else {
        call_callback(req, {});
    }

    delete req;
    m_pending.erase(id);
}

void DNSLookup::maintenance() {
    time_t now = time(nullptr);
    // Flush timeout pending (5 sec timeout)
    std::vector<uint16_t> to_remove;
    for (auto& p : m_pending) {
        if (now - p.second->sent_time > 5) {
            call_callback(p.second, {});
            to_remove.push_back(p.first);
        }
    }
    for (uint16_t id : to_remove) {
        delete m_pending[id];
        m_pending.erase(id);
    }

    // Flush old cache
    std::vector<std::string> old_hosts;
    for (auto& c : m_cache) {
        if (now - c.second.second > m_cache_ttl_sec) {
            old_hosts.push_back(c.first);
        }
    }
    for (const auto& h : old_hosts) {
        m_cache.erase(h);
    }
}

uint16_t DNSLookup::generate_query_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 65535);
    return static_cast<uint16_t>(dis(gen));
}

std::vector<uint8_t> DNSLookup::build_dns_query(const std::string& hostname, uint16_t qtype) {
    std::vector<uint8_t> packet;
    uint16_t id = generate_query_id();  // Will be overridden in send
    packet.push_back(id >> 8);
    packet.push_back(id & 0xFF);

    // Flags: standard query, recursion desired
    packet.push_back(0x01);  // QR=0, Opcode=0, AA=0, TC=0, RD=1
    packet.push_back(0x00);  // RA=0, Z=0, RCODE=0

    // Counts
    uint16_t zero = 0;
    packet.push_back(zero >> 8); packet.push_back(zero & 0xFF);  // QDCOUNT=1 later
    packet.push_back(zero >> 8); packet.push_back(zero & 0xFF);  // ANCOUNT=0
    packet.push_back(zero >> 8); packet.push_back(zero & 0xFF);  // NSCOUNT=0
    packet.push_back(zero >> 8); packet.push_back(zero & 0xFF);  // ARCOUNT=0

    // Question: hostname
    std::string labels;
    size_t pos = 0;
    while ((pos = hostname.find('.', pos)) != std::string::npos) {
        size_t len = pos - (labels.empty() ? 0 : labels.size() + 1);
        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), hostname.begin() + (labels.size() ? labels.size() + 1 : 0), hostname.begin() + pos);
        pos++;
    }
    // Last label
    size_t last_len = hostname.size() - (labels.size() ? labels.size() + 1 : 0);
    packet.push_back(static_cast<uint8_t>(last_len));
    packet.insert(packet.end(), hostname.end() - last_len, hostname.end());
    packet.push_back(0);  // End

    // QTYPE A=1
    packet.push_back(qtype >> 8); packet.push_back(qtype & 0xFF);
    // QCLASS IN=1
    packet.push_back(0x00); packet.push_back(0x01);

    // Update QDCOUNT=1
    packet[4] = 0x00; packet[5] = 0x01;

    return packet;
}

bool DNSLookup::send_query(const std::vector<uint8_t>& query, uint16_t qid) {
    // Update ID in packet
    std::vector<uint8_t> pkt = query;
    pkt[0] = qid >> 8;
    pkt[1] = qid & 0xFF;

    // Send to first DNS server
    if (m_dns_servers.empty()) return false;

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    if (inet_pton(AF_INET, m_dns_servers[0].c_str(), &server_addr.sin_addr) <= 0)
        return false;

    ssize_t sent = sendto(m_SocketContext.fd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("DNS sendto failed");
        return false;
    }
    return true;
}

bool DNSLookup::parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips) {
    if (len < 12) return false;

    // Skip header and question (simplified: assume 1 question, variable length)
    size_t pos = 12;
    // Parse question to skip (labels)
    while (pos < len) {
        uint8_t label_len = packet[pos];
        if (label_len == 0) break;
        pos += label_len + 1;
    }
    pos += 4;  // QTYPE + QCLASS

    if (pos >= len) return false;

    uint16_t ancount = (packet[6] << 8) | packet[7];
    for (uint16_t i = 0; i < ancount && pos < len; ++i) {
        // Skip name (compressed, simplified)
        while (pos < len && packet[pos] != 0) {
            if ((packet[pos] & 0xC0) == 0xC0) { pos += 2; break; }  // Pointer
            pos += packet[pos] + 1;
        }
        if (pos >= len) break;
        pos++;  // Null

        // TYPE, CLASS (skip 4 bytes)
        pos += 4;

        // TTL (skip 4)
        pos += 4;

        // RDLENGTH
        uint16_t rdlen = (packet[pos] << 8) | packet[pos+1];
        pos += 2;

        if (pos + rdlen > len) break;

        // For A record (TYPE=1)
        if (rdlen == 4) {
            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr;
            memcpy(&addr.s_addr, packet + pos, 4);
            if (inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
            }
        }
        // Add AAAA if needed (rdlen==16, IPv6)

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
    req->cb(req->hostname.c_str(), ips, count, req->user_data);
    if (ips) {
        // Caller should free, but if not, free here? No, as per original.
        // Assume caller frees as in original code.
    }
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
            // Trim and add if IPv4
            if (!server.empty() && inet_addr(server.c_str()) != INADDR_NONE) {
                m_dns_servers.push_back(server);
                if (m_dns_servers.size() >= 2) break;  // Enough
            }
        }
    }
}
