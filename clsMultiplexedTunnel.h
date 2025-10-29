#ifndef CLSMULTIPLEXEDTUNNEL_H
#define CLSMULTIPLEXEDTUNNEL_H

#include "clsTCPSocket.h" // Assumes this includes <cstdint>, <deque>, <unordered_map>, <vector>, etc.
#include <arpa/inet.h>  // برای توابع Endianness (htons, htonl, ...)
#include <cstring>      // برای memcpy

constexpr uint32_t INITIAL_WINDOW_SIZE = 256 * 1024;
constexpr uint32_t HEADER_SIZE = 12;
//constexpr uint32_t BACK_PRESSURE = 1024 * 1024; // حد آستانه برای backpressure (مثال)

// Frame types
enum class FrameType : uint8_t {
    Data = 0,
    WindowUpdate = 1,
    Ping = 2,
    GoAway = 3
};

// Flags
enum FrameFlags {
    SYN = 0x01,
    ACK = 0x02,
    FIN = 0x04,
    RST = 0x08
};

// GoAway codes
enum class GoAwayCode : uint32_t {
    Normal = 0,
    ProtocolError = 1,
    InternalError = 2
};

class MultiplexedTunnel : public TCPSocket {
public:
    using OnStreamDataFn = void(*)(void*, uint32_t streamId, const uint8_t* data, size_t len);
    using OnStreamCloseFn = void(*)(void*, uint32_t streamId);

    struct Stream {
        uint32_t id;
        uint32_t sendWindow = INITIAL_WINDOW_SIZE;
        uint32_t recvWindow = INITIAL_WINDOW_SIZE;
        bool localClosed = false;
        bool remoteClosed = false;
        std::deque<std::vector<uint8_t>> pendingData; // Pending buffers if sendWindow low
        OnStreamDataFn onData = nullptr;
        OnStreamCloseFn onClose = nullptr;
        void* arg = nullptr;
    };

    // New: Callback for accepting a new remote stream
    // The handler should set the onData/onClose callbacks for the newStream pointer.
    using OnNewStreamFn = void(*)(void*, uint32_t streamId, Stream* newStream);

    MultiplexedTunnel(bool isClient = true); // true for client (odd IDs), false for server (even)
    virtual ~MultiplexedTunnel() = default;

    // API for streams
    uint32_t openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg);
    void sendToStream(uint32_t streamId, const uint8_t* data, size_t len);
    void closeStream(uint32_t streamId, bool rst = false); // rst for abrupt close
    void sendWindowUpdate(uint32_t streamId, uint32_t delta);

    // API for connection
    void setOnNewStream(OnNewStreamFn fn, void* arg);

    // Override to handle multiplexing
    void onReceiveData(const uint8_t* data, size_t len) override;

    // Optional: send GoAway for clean shutdown
    void shutdown(GoAwayCode code = GoAwayCode::Normal);

protected:
    void sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload = nullptr);
    void processPending(uint32_t streamId);
    void handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length);
    void handlePingFrame(const uint8_t* payload, uint32_t length, FrameFlags flags);
    void handleGoAwayFrame(const uint8_t* payload, uint32_t length);

private:
    bool m_isClient;
    uint32_t m_nextStreamId; // Starts at 1 for client (odd), 2 for server (even)
    std::unordered_map<uint32_t, Stream> m_streams;
    std::vector<uint8_t> m_parseBuffer; // For partial frames
    uint64_t m_lastPingTime = 0; // Optional keepalive, not implemented fully

    OnNewStreamFn m_onNewStream = nullptr;
    void* m_newStreamArg = nullptr;
};

#endif // CLSMULTIPLEXEDTUNNEL_H
