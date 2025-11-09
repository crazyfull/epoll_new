#ifndef SOCKS5STREAMHANDLER_H
#define SOCKS5STREAMHANDLER_H

#include "clsTCPSocket.h"
#include "clsMultiplexedTunnel.h"
#include <vector>

// این کلاس مشابه بخش 'connector' از clsSocks5Proxy عمل می‌کند،
// اما درخواست را از 'stream' می‌خواند نه از 'acceptor'.

class Socks5StreamHandler {
public:
    enum State {
        WaitingRequest,
        Connecting,
        Connected
    };

    MultiplexedTunnel* m_tunnel;
    MultiplexedTunnel::Stream* m_stream;
    TCPSocket m_connector; // سوکت برای اتصال به مقصد نهایی
    State m_state;
    std::vector<uint8_t> m_buffer; // بافر برای بسته‌های ناقص احتمالی

    Socks5StreamHandler(MultiplexedTunnel* tunnel, MultiplexedTunnel::Stream* stream)
        : m_tunnel(tunnel), m_stream(stream), m_state(State::WaitingRequest)
    {
        // تنظیم آرگومان‌ها و callbackها
        stream->arg = this;
        stream->onData = &Socks5StreamHandler::OnStreamData;
        stream->onClose = &Socks5StreamHandler::OnStreamClose;

        // تنظیم callbackهای سوکت مقصد
        m_connector.setReactor(tunnel->getReactor());
        m_connector.setOnConnected(&Socks5StreamHandler::OnConnectorConnected, this);
        m_connector.setOnConnectFailed(&Socks5StreamHandler::OnConnectorConnectFailed, this);
        m_connector.setOnData(&Socks5StreamHandler::OnConnectorData, this);
        m_connector.setOnClose(&Socks5StreamHandler::OnConnectorClose, this);
    }

    ~Socks5StreamHandler() {
        // اطمینان از بسته شدن سوکت در صورت حذف هندلر
        if (m_connector.getStatus() != TCPSocket::Closed) {
            m_connector.close(true);
        }
    }

    // --- Trampolines ---
    static void OnStreamData(void* p, uint32_t id, const uint8_t* data, size_t len) {
        static_cast<Socks5StreamHandler*>(p)->HandleStreamData(data, len);
    }
    static void OnStreamClose(void* p, uint32_t id) {
        static_cast<Socks5StreamHandler*>(p)->HandleStreamClose();
    }
    static void OnConnectorConnected(void* p) {
        static_cast<Socks5StreamHandler*>(p)->HandleConnectorConnected();
    }
    static void OnConnectorConnectFailed(void* p) {
        static_cast<Socks5StreamHandler*>(p)->HandleConnectorConnectFailed();
    }
    static void OnConnectorData(void* p, const uint8_t* data, size_t len) {
        static_cast<Socks5StreamHandler*>(p)->HandleConnectorData(data, len);
    }
    static void OnConnectorClose(void* p) {
        static_cast<Socks5StreamHandler*>(p)->HandleConnectorClose();
    }

    // --- Logic ---
    void HandleStreamData(const uint8_t* data, size_t len) {
        if (m_state == State::WaitingRequest) {
            // فرض می‌کنیم اولین بسته حاوی کل درخواست SOCKS5 است که توسط کلاینت فوروارد شده
            // (برای سادگی، پارس کردن تکه تکه را اینجا حذف می‌کنیم، اما clsSocks5Proxy آن را دارد)
            m_buffer.insert(m_buffer.end(), data, data + len);

            // شبیه ProcessRequest در clsSocks5Proxy
            if (m_buffer.size() < 4) return; // حداقل هدر

            uint8_t atyp = m_buffer[3];
            size_t addrLen = 0;
            if (atyp == 0x01) addrLen = 4;  // IPv4
            else if (atyp == 0x03) {  // Domain
                if (m_buffer.size() < 5) return;
                addrLen = m_buffer[4] + 1;
            } else if (atyp == 0x04) addrLen = 16;  // IPv6
            else {
                SendStreamErrorReply(0x08); // Address type not supported
                return;
            }

            size_t totalLen = 4 + addrLen + 2; // + port
            if (m_buffer.size() < totalLen) return; // بسته کامل نرسیده

            // استخراج آدرس و پورت
            std::string host;
            uint16_t port = 0;
            size_t offset = 4;

            if (atyp == 0x01) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, m_buffer.data() + offset, ipStr, INET_ADDRSTRLEN);
                host = ipStr;
                offset += 4;
            } else if (atyp == 0x03) {
                uint8_t domainLen = m_buffer[offset];
                offset++;
                host.assign(reinterpret_cast<const char*>(m_buffer.data() + offset), domainLen);
                offset += domainLen;
            } // IPv6 (برای سادگی حذف شد، اما منطق مشابه IPv4 است)

            port = (m_buffer[offset] << 8) | m_buffer[offset + 1];

            printf("[Server] Stream %u: Connecting to %s:%d\n", m_stream->id, host.c_str(), port);
            m_state = State::Connecting;
            if (!m_connector.connectTo(host.c_str(), port)) {
                HandleConnectorConnectFailed();
            }

            // پاک کردن بافر از درخواست (داده‌های بعدی، دیتای خام TCP هستند)
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + totalLen);

        } else if (m_state == State::Connected) {
            // داده‌ها را به مقصد نهایی (connector) ارسال کن
            m_connector.send(data, len);
        }
    }

    void HandleConnectorConnected() {
        printf("[Server] Stream %u: Connected to destination.\n", m_stream->id);
        m_state = State::Connected;

        // ارسال پاسخ موفقیت SOCKS5 به کلاینت (از طریق استریم)
        uint8_t reply[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        m_tunnel->sendToStream(m_stream->id, reply, 10);

        // اگر داده‌ای در بافر مانده بود (در حالت Connecting دریافت شده)، ارسال کن
        if (!m_buffer.empty()) {
            m_connector.send(m_buffer.data(), m_buffer.size());
            m_buffer.clear();
        }
    }

    void HandleConnectorConnectFailed() {
        printf("[Server] Stream %u: Failed to connect.\n", m_stream->id);
        SendStreamErrorReply(0x04); // Host unreachable
    }

    void HandleConnectorData(const uint8_t* data, size_t len) {
        // داده‌ها را از مقصد نهایی به کلاینت (از طریق استریم) ارسال کن
        m_tunnel->sendToStream(m_stream->id, data, len);
    }

    void HandleStreamClose() {
        printf("[Server] Stream %u: Closed by client.\n", m_stream->id);
        m_connector.close(true); // اتصال به مقصد را قطع کن
        //delete this; // segment fault
    }

    void HandleConnectorClose() {
        printf("[Server] Stream %u: Closed by destination.\n", m_stream->id);
        m_tunnel->closeStream(m_stream->id, false); // استریم را ببند
        // delete this; // توسط OnStreamClose پاک خواهیم شد
    }

    void SendStreamErrorReply(uint8_t errorCode) {
        uint8_t reply[10] = {0x05, errorCode, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        m_tunnel->sendToStream(m_stream->id, reply, 10);
        m_tunnel->closeStream(m_stream->id, false);
    }
};

#endif // SOCKS5STREAMHANDLER_H
