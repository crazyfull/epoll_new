#ifndef CLSMULTIPLEXEDTUNNEL_H
#define CLSMULTIPLEXEDTUNNEL_H

#include "clsTCPSocket.h"
//#include "clsSendQueue.h"
//#include <arpa/inet.h>  // for htons/htonl/ntohs/ntohl
#include <cstring>      // for memcpy
#include <unordered_map>
#include <algorithm>    // for std::min

// Yamux Constants
constexpr uint8_t YAMUX_VERSION = 0;
constexpr uint32_t HEADER_SIZE = 12;
constexpr uint32_t INITIAL_WINDOW_SIZE = 256 * 1024;
constexpr uint32_t WINDOW_UPDATE_THRESHOLD = 8 * 1024;   //INITIAL_WINDOW_SIZE / 2;
constexpr uint32_t BACK_PRESSURE_LIMIT = 4 * 1024 * 1024;
constexpr size_t PARSE_BUFFER_SIZE = 8 * 1024;
constexpr size_t MAX_ALLOWED_FRAME_SIZE = 128 * 1024;

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

// GoAway codes
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
        uint32_t sendWindow = INITIAL_WINDOW_SIZE;
        uint32_t recvWindow = INITIAL_WINDOW_SIZE;
        uint32_t unackedRecvBytes = 0;
        bool localClosed = false;
        bool remoteClosed = false;

        // FIX: استفاده از SendQueue
        SendQueue* pendingData = nullptr;

        OnStreamDataFn onData = nullptr;
        OnStreamCloseFn onClose = nullptr;
        void* arg = nullptr;

        //  SendQueue
        ~Stream() {
            if (pendingData) {
                delete pendingData;
                pendingData = nullptr;
            }
        }
    };
    using OnNewStreamFn = void(*)(void*, uint32_t streamId, Stream* newStream);



    MultiplexedTunnel(bool isClient = true);
    virtual ~MultiplexedTunnel();

    // API for streams
    uint32_t openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg);
    void sendToStream(uint32_t streamId, const uint8_t* data, size_t len);
    void closeStream(uint32_t streamId, bool rst = false);

    // Window Update
    void trySendWindowUpdate(uint32_t streamId, uint32_t consumedLength);

    //Ping/Pong
    void sendPing(bool isACK, uint32_t value);

    // API for connection
    void setOnNewStream(OnNewStreamFn fn, void* arg);

    // Override to handle multiplexing
    void onReceiveData(const uint8_t* data, size_t len) override;

    void shutdown(GoAwayCode code = GoAwayCode::Normal);

protected:
    void sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload = nullptr);
    void processPending(uint32_t streamId);
    void sendWindowUpdate(uint32_t streamId, uint32_t delta);

    // Handlers for received frames (حفظ شدند)
    void handleNewStream(uint32_t streamId, uint16_t flags);
    void handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags);
    void handleWindowUpdateFrame(uint32_t streamId, uint32_t length, uint16_t flags);
    void handlePingFrame(uint32_t streamId, uint32_t length, uint16_t flags);
    void handleGoAwayFrame(uint32_t length);


private:
    bool m_isClient;
    uint32_t m_nextStreamId;
    std::unordered_map<uint32_t, Stream> m_streams;
    OnNewStreamFn m_onNewStream = nullptr;
    void* m_newStreamArg = nullptr;

    //
    uint8_t* m_parseBuffer = nullptr;
    size_t m_parseBufferLength = 0;
    size_t m_parseBufferCapacity = 0;

    //
    bool initParseBuffer();
    void releaseParseBuffer();
    bool resizeParseBuffer(size_t newCapacity);
    bool initSendQueue(Stream& session);   //Lazy Initialization

    bool processFrame(const uint8_t* frameStart, size_t totalFrameSize, uint32_t payloadLength);
    size_t processFragmentedFrame(const uint8_t* data, size_t len, size_t& data_pos);
    void processZeroCopyFrames(const uint8_t* data, size_t len, size_t& data_pos);
    void storeRemainingData(const uint8_t* data, size_t len, size_t data_pos);

};

#endif // CLSMULTIPLEXEDTUNNEL_H
