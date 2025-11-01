#ifndef CLSMULTIPLEXEDTUNNEL_H
#define CLSMULTIPLEXEDTUNNEL_H

#include "clsTCPSocket.h"
#include <arpa/inet.h>  // for htons/htonl/ntohs/ntohl
#include <cstring>      // for memcpy
#include <deque>
#include <unordered_map>
#include <vector>
#include <algorithm>    // for std::min

// Yamux Constants (Based on spec.md and const.go)
constexpr uint8_t YAMUX_VERSION = 0; // The official Yamux version
constexpr uint32_t HEADER_SIZE = 12;
constexpr uint32_t INITIAL_WINDOW_SIZE = 256 * 1024;
constexpr uint32_t BACK_PRESSURE_LIMIT = 4 * 1024 * 1024; // Example limit on write queue

// Frame types
enum class FrameType : uint8_t {
    Data = 0,
    WindowUpdate = 1,
    Ping = 2,
    GoAway = 3
};

// Flags (16 bits)
enum FrameFlags : uint16_t {
    SYN = 0x01,
    ACK = 0x02,
    FIN = 0x04,
    RST = 0x08
};

// GoAway codes (used as 4-byte payload or Length)
enum class GoAwayCode : uint32_t {
    Normal = 0,
    ProtocolError = 1,
    InternalError = 2
};

class MultiplexedTunnel : public TCPSocket {
public:
    // Callbacks definitions
    using OnStreamDataFn = void(*)(void*, uint32_t streamId, const uint8_t* data, size_t len);
    using OnStreamCloseFn = void(*)(void*, uint32_t streamId);

    struct Stream {
        uint32_t id;
        uint32_t sendWindow = INITIAL_WINDOW_SIZE; // Available space to SEND data
        uint32_t recvWindow = INITIAL_WINDOW_SIZE; // Available space to RECEIVE data
        bool localClosed = false;
        bool remoteClosed = false;
        std::deque<std::vector<uint8_t>> pendingData; // Data waiting for sendWindow to increase
        OnStreamDataFn onData = nullptr;
        OnStreamCloseFn onClose = nullptr;
        void* arg = nullptr;
    };

    using OnNewStreamFn = void(*)(void*, uint32_t streamId, Stream* newStream);



    MultiplexedTunnel(bool isClient = true);
    virtual ~MultiplexedTunnel() = default;

    // API for streams
    uint32_t openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg);
    void sendToStream(uint32_t streamId, const uint8_t* data, size_t len);
    void closeStream(uint32_t streamId, bool rst = false);

    // YAMUX: Delta is sent in the Length field of the header, no payload.
    void sendWindowUpdate(uint32_t streamId, uint32_t delta);

    // YAMUX: Ping/Pong
    void sendPing(bool isACK, uint32_t value);

    // API for connection
    void setOnNewStream(OnNewStreamFn fn, void* arg);

    // Override to handle multiplexing
    void onReceiveData(const uint8_t* data, size_t len) override;

    void shutdown(GoAwayCode code = GoAwayCode::Normal);

protected:
    // Helper to encapsulate frame creation and sending
    void sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload = nullptr);
    void processPending(uint32_t streamId);

    // Handlers for received frames
    void handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length);
    void handlePingFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleGoAwayFrame(const uint8_t* payload, uint32_t length);

private:
    bool m_isClient;
    uint32_t m_nextStreamId; // Starts at 1 for client (odd), 2 for server (even)
    std::unordered_map<uint32_t, Stream> m_streams;
    std::vector<uint8_t> m_parseBuffer;

    // New Stream Callback
    OnNewStreamFn m_onNewStream = nullptr;
    void* m_newStreamArg = nullptr;
};

#endif // CLSMULTIPLEXEDTUNNEL_H
