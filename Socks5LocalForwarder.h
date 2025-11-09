#ifndef SOCKS5LOCALFORWARDER_H
#define SOCKS5LOCALFORWARDER_H

#include "clsTCPSocket.h"
#include "clsMultiplexedTunnel.h"
#include <vector>
#include <string>

// ما state machine را از clsSocks5Proxy کپی می‌کنیم
enum class LocalSocksState {
    Greeting,
    Request,
    Connecting, // منتظر پاسخ استریم
    Connected,  // در حال فوروارد
    Error
};

// این کلاس خودش سوکت Acceptor است
class Socks5LocalForwarder : public TCPSocket {
public:
    MultiplexedTunnel* m_tunnel;
    uint32_t m_streamId;

    LocalSocksState m_state;
    std::vector<uint8_t> m_clientBuffer; // بافر برای بسته‌های ناقص SOCKS

    Socks5LocalForwarder(MultiplexedTunnel* tunnel)
        : m_tunnel(tunnel), m_streamId(0), m_state(LocalSocksState::Greeting)
    {
        // ما callbackهای TCPSocket (کلاس پدر) را در اینجا ست نمی‌کنیم،
        // چون به جای آن، توابع مجازی onAccepted, onReceiveData, onClose را بازنویسی می‌کنیم.
    }

    // --- بازنویسی توابع مجازی TCPSocket ---

    void onAccepted() override {
        printf("[Client] Local SOCKS connection accepted. fd=%d\n", fd());
        m_state = LocalSocksState::Greeting;
    }

    void onReceiveData(const uint8_t* data, size_t length) override {
        m_clientBuffer.insert(m_clientBuffer.end(), data, data + length);

        while (!m_clientBuffer.empty()) {
            if (m_state == LocalSocksState::Greeting) {
                if (!ProcessGreeting())
                    break; // منتظر داده بیشتر
            } else if (m_state == LocalSocksState::Request) {
                if (!ProcessRequest())
                    break; // منتظر داده بیشتر
            } else if (m_state == LocalSocksState::Connected) {
                // داده‌های عادی از مرورگر به سمت تونل
                ForwardToStream(m_clientBuffer.data(), m_clientBuffer.size());
                m_clientBuffer.clear();
                break;
            } else if (m_state == LocalSocksState::Connecting) {
                // مرورگر قبل از آماده شدن استریم در حال ارسال داده است
                // TODO: این داده‌ها باید بافر شوند
                printf("[Client] Warning: Buffering data received before stream connected.\n");
                break; // فعلا بافر می‌کنیم
            } else {
                break; // Error state
            }
        }
    }

    void onClose() override {
        printf("[Client] Local SOCKS connection closed. fd=%d\n", fd());
        if (m_streamId != 0) {
            m_tunnel->closeStream(m_streamId, true); // RST
        }
        // EpollReactor مسئول حذف این آبجکت خواهد بود (اگر GC فعال باشد)
        // اگر GC فعال نیست، باید در Socks5TunnelClient.cpp فکری برای delete this کنیم
    }

private:
    // --- منطق SOCKS5 (کپی شده از clsSocks5Proxy) ---

    bool ProcessGreeting() {
        if (m_clientBuffer.size() < 3) return false;
        if (m_clientBuffer[0] != 0x05) { /* ... error ... */ return false; }
        uint8_t nmethods = m_clientBuffer[1];
        if (m_clientBuffer.size() < 2 + nmethods) return false;

        bool supportsNoAuth = false;
        for (size_t i = 0; i < nmethods; ++i) {
            if (m_clientBuffer[2 + i] == 0x00) { supportsNoAuth = true; break; }
        }

        m_clientBuffer.erase(m_clientBuffer.begin(), m_clientBuffer.begin() + 2 + nmethods);
        uint8_t response[2] = {0x05, static_cast<uint8_t>(supportsNoAuth ? 0x00 : 0xFF)};
        send(response, 2); // 'send' تابع TCPSocket (کلاس پدر) است

        if (!supportsNoAuth) {
            m_state = LocalSocksState::Error;
            close(true);
            return false;
        }
        m_state = LocalSocksState::Request;
        return true;
    }

    bool ProcessRequest() {
        if (m_clientBuffer.size() < 4) return false;
        if (m_clientBuffer[0] != 0x05) { SendErrorReply(0x01); return false; }
        if (m_clientBuffer[1] != 0x01) { SendErrorReply(0x07); return false; }

        uint8_t atyp = m_clientBuffer[3];
        size_t addrLen = 0;
        if (atyp == 0x01) addrLen = 4;
        else if (atyp == 0x03) {
            if (m_clientBuffer.size() < 5) return false;
            addrLen = m_clientBuffer[4] + 1;
        } else if (atyp == 0x04) addrLen = 16;
        else { SendErrorReply(0x08); return false; }

        size_t totalLen = 4 + addrLen + 2;
        if (m_clientBuffer.size() < totalLen) return false;

        // --- این بخش تفاوت اصلی است ---
        // ما درخواست را مصرف نمی‌کنیم، بلکه آن را برای ارسال به تونل آماده می‌کنیم
        std::vector<uint8_t> requestPacket(m_clientBuffer.begin(), m_clientBuffer.begin() + totalLen);

        // مصرف کردن درخواست از بافر ورودی
        m_clientBuffer.erase(m_clientBuffer.begin(), m_clientBuffer.begin() + totalLen);

        // --- به جای CONNECT، ما STREAM باز می‌کنیم ---
        if (!m_tunnel || m_tunnel->getStatus() != TCPSocket::Connected) {
            printf("[Client] Tunnel not connected. Aborting.\n");
            SendErrorReply(0x01);
            return false;
        }

        m_state = LocalSocksState::Connecting;
        m_streamId = m_tunnel->openStream(
            &Socks5LocalForwarder::OnStreamData,
            &Socks5LocalForwarder::OnStreamClose,
            this
            );

        if (m_streamId == 0) {
            SendErrorReply(0x01);
            return false;
        }

        // ارسال درخواست SOCKS5 به داخل استریم برای پردازش توسط سرور
        m_tunnel->sendToStream(m_streamId, requestPacket.data(), requestPacket.size());

        return true;
    }

    void ForwardToStream(const uint8_t* data, size_t len) {
        if (m_streamId != 0) {
            m_tunnel->sendToStream(m_streamId, data, len);
        }
    }

    void SendErrorReply(uint8_t errorCode) {
        uint8_t reply[10] = {0x05, errorCode, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        send(reply, 10);
        m_state = LocalSocksState::Error;
        close(true);
    }

    // --- Callbacks برای Stream ---
    static void OnStreamData(void* p, uint32_t id, const uint8_t* data, size_t len) {
        static_cast<Socks5LocalForwarder*>(p)->HandleStreamData(data, len);
    }
    static void OnStreamClose(void* p, uint32_t id) {
        static_cast<Socks5LocalForwarder*>(p)->HandleStreamClose();
    }

    void HandleStreamData(const uint8_t* data, size_t len) {
        if (m_state == LocalSocksState::Connecting) {
            // این اولین پاسخ از سرور است (پاسخ SOCKS5)
            printf("[Client] Stream %u: Got reply from server, forwarding to browser.\n", m_streamId);
            send(data, len); // ارسال پاسخ به مرورگر
            m_state = LocalSocksState::Connected;

            // اگر داده‌ای در بافر مانده بود (در حالت Connecting)، ارسال کن
            if (!m_clientBuffer.empty()) {
                ForwardToStream(m_clientBuffer.data(), m_clientBuffer.size());
                m_clientBuffer.clear();
            }

        } else if (m_state == LocalSocksState::Connected) {
            // داده‌های عادی از مقصد
            send(data, len);
        }
    }

    void HandleStreamClose() {
        printf("[Client] Stream %u: Closed by server.\n", m_streamId);
        m_streamId = 0;
        close(true); // اتصال محلی (مرورگر) را ببند
    }
};

#endif // SOCKS5LOCALFORWARDER_H
