#ifndef CLSMULTIPLEXEDTUNNEL_H
#define CLSMULTIPLEXEDTUNNEL_H

#include "clsTCPSocket.h"
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h> // for htonl, htons

constexpr uint32_t INITIAL_WINDOW_SIZE = 256 * 1024;
constexpr uint32_t HEADER_SIZE = 12;
//constexpr uint32_t LOW_WATERMARK = 64 * 1024;

enum class FrameType : uint8_t {
    Data = 0,
    WindowUpdate = 1,
    Ping = 2,
    GoAway = 3
};

enum FrameFlags : uint16_t {
    SYN = 0x01,
    ACK = 0x02,
    FIN = 0x04,
    RST = 0x08
};

enum class GoAwayCode : uint32_t {
    Normal = 0,
    ProtocolError = 1,
    InternalError = 2
};

class MultiplexedTunnel : public TCPSocket {
public:
    using OnStreamDataFn = void(*)(void*, uint32_t, const uint8_t*, size_t);
    using OnStreamCloseFn = void(*)(void*, uint32_t);

    struct Stream {
        uint32_t id{};
        uint32_t sendWindow = INITIAL_WINDOW_SIZE;
        uint32_t recvWindow = INITIAL_WINDOW_SIZE;
        bool localClosed = false;
        bool remoteClosed = false;
        std::deque<std::vector<uint8_t>> pendingData;
        OnStreamDataFn onData = nullptr;
        OnStreamCloseFn onClose = nullptr;
        void* arg = nullptr;
    };

    explicit MultiplexedTunnel(bool isClient = true);
    ~MultiplexedTunnel() override = default;

    uint32_t openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg);
    void sendToStream(uint32_t streamId, const uint8_t* data, size_t len);
    void closeStream(uint32_t streamId, bool rst = false);
    void sendWindowUpdate(uint32_t streamId, uint32_t delta);
    void shutdown(GoAwayCode code = GoAwayCode::Normal);

    void onReceiveData(const uint8_t* data, size_t len) override;

protected:
    void sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload = nullptr);
    void processPending(uint32_t streamId);
    void handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length);
    void handlePingFrame(const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleGoAwayFrame(const uint8_t* payload, uint32_t length);

private:
    bool m_isClient;
    uint32_t m_nextStreamId;
    std::unordered_map<uint32_t, Stream> m_streams;
    std::vector<uint8_t> m_parseBuffer;
};

#endif // CLSMULTIPLEXEDTUNNEL_H
