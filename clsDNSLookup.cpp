#include "clsDNSLookup.h"
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <iomanip>
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
    m_SocketContext.ev.events = EPOLLIN | EPOLLET;

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

void DNSLookup::on_dns_read() {
    uint8_t buffer[512];
    struct sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t len = recvfrom(m_SocketContext.fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        perror("DNS recvfrom failed");
        return;
    }

    // Log response packet
    std::stringstream ss;
    ss << "DNS response received (len=" << len << "): ";
    for (ssize_t i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    printf("%s\n", ss.str().c_str());

    // Parse response
    if (len < 12) {
        printf("DNS response too short\n");
        return;
    }

    uint16_t id = (buffer[0] << 8) | buffer[1];
    uint16_t flags = (buffer[2] << 8) | buffer[3];
    uint16_t rcode = flags & 0x0F;
    if (!(flags & 0x8000)) {
        printf("Not a DNS response (QR=0)\n");
        return;
    }

    // Try both A and AAAA qtypes
    DNSRequest* req = nullptr;
    auto it = m_pending.find({id, 1});  // Check A
    if (it == m_pending.end()) {
        it = m_pending.find({id, 28});  // Check AAAA
        if (it == m_pending.end()) {
            printf("Unknown query ID: %u\n", id);
            return;
        }
    }
    req = it->second;

    if (rcode != 0) {
        const char* rcode_str = "Unknown";
        switch (rcode) {
        case 1: rcode_str = "FormErr"; break;
        case 2: rcode_str = "ServFail"; break;
        case 3: rcode_str = "NXDOMAIN"; break;
        case 4: rcode_str = "NotImp"; break;
        case 5: rcode_str = "Refused"; break;
        }
        printf("DNS error for %s (QTYPE=%u): RCODE=%u (%s)\n", req->hostname.c_str(), req->qtype, rcode, rcode_str);
        // Log authority section
        size_t pos = 12;
        while (pos < len && buffer[pos] != 0) {
            if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }
            pos += buffer[pos] + 1;
        }
        pos++;  // Null
        pos += 4;  // QTYPE + QCLASS
        uint16_t nscount = (buffer[8] << 8) | buffer[9];
        std::stringstream auth_ss;
        auth_ss << "Authority section (" << nscount << "): ";
        for (uint16_t i = 0; i < nscount && pos < len; ++i) {
            while (pos < len && buffer[pos] != 0) {
                if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }
                auth_ss << (char)buffer[pos + 1];
                pos += buffer[pos] + 1;
            }
            pos++;  // Null
            if (pos + 10 > len) break;
            pos += 10;  // TYPE, CLASS, TTL, RDLENGTH
        }
        printf("%s\n", auth_ss.str().c_str());
        call_callback(req, {});
        delete req;
        m_pending.erase(it);
        return;
    }

    uint16_t qdcount = (buffer[4] << 8) | buffer[5];
    uint16_t ancount = (buffer[6] << 8) | buffer[7];

    printf("on_dns_read() len: %zd qdcount: %hu ancount: %hu\n", len, qdcount, ancount);

    std::vector<std::string> ips;
    if (parse_dns_response(buffer, len, id, ips)) {
        m_cache[req->hostname] = {ips, time(nullptr)};
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
        if (now - p.second->sent_time > 5) {
            printf("DNS timeout for %s (ID %u, QTYPE=%u)\n", p.second->hostname.c_str(), p.first.first, p.first.second);
            call_callback(p.second, {});
            to_remove.push_back(p.first);
        }
    }
    for (const auto& id : to_remove) {
        delete m_pending[id];
        m_pending.erase(id);
    }

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
    uint16_t id = 0;  // Will be overridden in send
    packet.push_back(id >> 8);
    packet.push_back(id & 0xFF);

    // Flags: standard query, recursion desired
    packet.push_back(0x01);  // QR=0, Opcode=0, AA=0, TC=0, RD=1
    packet.push_back(0x00);  // RA=0, Z=0, RCODE=0

    // Counts
    packet.push_back(0x00); packet.push_back(0x01);  // QDCOUNT=1
    packet.push_back(0x00); packet.push_back(0x00);  // ANCOUNT=0
    packet.push_back(0x00); packet.push_back(0x00);  // NSCOUNT=0
    packet.push_back(0x00); packet.push_back(0x00);  // ARCOUNT=0

    // Question: hostname
    std::string::size_type last_pos = 0;
    std::string::size_type pos;
    while ((pos = hostname.find('.', last_pos)) != std::string::npos) {
        size_t len = pos - last_pos;
        if (len > 63) len = 63;
        if (len == 0) { last_pos = pos + 1; continue; }
        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), hostname.begin() + last_pos, hostname.begin() + pos);
        last_pos = pos + 1;
    }
    size_t len = hostname.size() - last_pos;
    if (len > 63) len = 63;
    if (len > 0) {
        packet.push_back(static_cast<uint8_t>(len));
        packet.insert(packet.end(), hostname.begin() + last_pos, hostname.end());
    }
    packet.push_back(0);  // End of name

    // QTYPE (A=1, AAAA=28)
    packet.push_back(qtype >> 8); packet.push_back(qtype & 0xFF);
    // QCLASS (IN=1)
    packet.push_back(0x00); packet.push_back(0x01);

    return packet;
}

bool DNSLookup::send_query(const std::vector<uint8_t>& query, uint16_t qid) {
    std::vector<uint8_t> pkt = query;
    pkt[0] = qid >> 8;
    pkt[1] = qid & 0xFF;

    if (m_dns_servers.empty()) {
        printf("No DNS servers available\n");
        return false;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    if (inet_pton(AF_INET, m_dns_servers[0].c_str(), &server_addr.sin_addr) <= 0) {
        printf("Invalid DNS server address: %s\n", m_dns_servers[0].c_str());
        return false;
    }

    ssize_t sent = sendto(m_SocketContext.fd, pkt.data(), pkt.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("DNS sendto failed");
        return false;
    }
    return true;
}

int DNSLookup::resolve(const char *hostname, dns_callback_t cb, void *user_data) {
    if (!hostname || !cb) return -1;

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
        return 0;
    }

    // Generate query for A
    uint16_t qid_a = generate_query_id();
    DNSRequest* req_a = new DNSRequest{host, cb, user_data, qid_a, 1, now};  // QTYPE=A
    m_pending[{qid_a, 1}] = req_a;

    std::vector<uint8_t> query_a = build_dns_query(host, 1);  // A
    query_a[0] = qid_a >> 8;
    query_a[1] = qid_a & 0xFF;

    // Log query packet for A
    std::stringstream ss_a;
    ss_a << "DNS query sent for " << hostname << " (ID " << qid_a << ", QTYPE=A): ";
    for (size_t i = 0; i < query_a.size(); ++i) {
        ss_a << std::hex << std::setw(2) << std::setfill('0') << (int)query_a[i] << " ";
    }
    printf("%s\n", ss_a.str().c_str());

    if (!send_query(query_a, qid_a)) {
        m_pending.erase({qid_a, 1});
        delete req_a;
        cb(hostname, nullptr, 0, user_data);
        return -1;
    }

    /*
    // Generate query for AAAA
    uint16_t qid_aaaa = generate_query_id();
    DNSRequest* req_aaaa = new DNSRequest{host, cb, user_data, qid_aaaa, 28, now};  // QTYPE=AAAA
    m_pending[{qid_aaaa, 28}] = req_aaaa;

    std::vector<uint8_t> query_aaaa = build_dns_query(host, 28);  // AAAA
    query_aaaa[0] = qid_aaaa >> 8;
    query_aaaa[1] = qid_aaaa & 0xFF;

    // Log query packet for AAAA
    std::stringstream ss_aaaa;
    ss_aaaa << "DNS query sent for " << hostname << " (ID " << qid_aaaa << ", QTYPE=AAAA): ";
    for (size_t i = 0; i < query_aaaa.size(); ++i) {
        ss_aaaa << std::hex << std::setw(2) << std::setfill('0') << (int)query_aaaa[i] << " ";
    }

    printf("%s\n", ss_aaaa.str().c_str());

    if (!send_query(query_aaaa, qid_aaaa)) {
        m_pending.erase({qid_aaaa, 28});
        delete req_aaaa;
        // Still proceed with A query
    }*/

    return 0;
}

bool DNSLookup::parse_dns_response(const uint8_t* packet, size_t len, uint16_t query_id, std::vector<std::string>& ips) {
    if (len < 12) {
        printf("DNS response too short\n");
        return false;
    }

    size_t pos = 12;
    // Skip question section
    while (pos < len && packet[pos] != 0) {
        if ((packet[pos] & 0xC0) == 0xC0) {
            pos += 2;  // Compression pointer
            break;
        }
        pos += packet[pos] + 1;
    }
    pos++;  // Null terminator
    if (pos + 4 > len) {
        printf("Invalid question section\n");
        return false;
    }
    pos += 4;  // QTYPE + QCLASS

    uint16_t ancount = (packet[6] << 8) | packet[7];
    printf("Parsing %hu answers\n", ancount);
    for (uint16_t i = 0; i < ancount && pos < len; ++i) {
        // Skip name
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

        if (pos + 8 > len) {  // TYPE (2) + CLASS (2) + TTL (4)
            printf("Invalid answer section (not enough bytes for TYPE+CLASS+TTL at pos %zu)\n", pos);
            break;
        }

        uint16_t type = (packet[pos] << 8) | packet[pos + 1];
        pos += 4;  // TYPE + CLASS
        pos += 4;  // TTL
        if (pos + 2 > len) {
            printf("Invalid answer section (not enough bytes for RDLENGTH at pos %zu)\n", pos);
            break;
        }
        uint16_t rdlen = (packet[pos] << 8) | packet[pos + 1];
        pos += 2;

        if (pos + rdlen > len) {
            printf("Invalid RDLENGTH (%u at pos %zu, len %zu)\n", rdlen, pos, len);
            break;
        }

        if (type == 1 && rdlen == 4) {  // A record
            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr;
            memcpy(&addr.s_addr, packet + pos, 4);
            if (inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
                printf("Parsed A record: %s\n", ip_str);
            }
        } else if (type == 28 && rdlen == 16) {  // AAAA record
            char ip_str[INET6_ADDRSTRLEN];
            struct in6_addr addr;
            memcpy(&addr, packet + pos, 16);
            if (inet_ntop(AF_INET6, &addr, ip_str, sizeof(ip_str))) {
                ips.push_back(ip_str);
                printf("Parsed AAAA record: %s\n", ip_str);
            }
        } else {
            printf("Skipping record type %u with rdlen %u\n", type, rdlen);
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
