#include "clsMultiplexedTunnel.h"
#include <iostream>
#include <cmath> // for std::min (though std::min in <algorithm> is better)

// Assume these are provided by TCPSocket/Epoll environment
// #define BACK_PRESSURE_LIMIT 4 * 1024 * 1024

// --- Public API ---

MultiplexedTunnel::MultiplexedTunnel(bool isClient) : m_isClient(isClient), m_nextStreamId(isClient ? 1 : 2) {
    //
}

void MultiplexedTunnel::setOnNewStream(OnNewStreamFn fn, void* arg) {
    m_onNewStream = fn;
    m_newStreamArg = arg;
}

uint32_t MultiplexedTunnel::openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg) {
    uint32_t id = m_nextStreamId;

    // Check for stream ID exhaustion (assuming uint32 max is the limit)
    if (id + 2 < id) {
        // Stream ID overflow, should send GoAway
        shutdown(GoAwayCode::InternalError);
        return 0;
    }
    m_nextStreamId += 2; // Increment by 2 to keep parity

    auto& s = m_streams[id];
    s.id = id;
    s.onData = onData;
    s.onClose = onClose;
    s.arg = arg;

    // Send SYN frame (Data type with SYN flag, length=0)
    sendFrame(FrameType::Data, FrameFlags::SYN, id, 0);
    return id;
}

void MultiplexedTunnel::sendToStream(uint32_t streamId, const uint8_t* data, size_t len) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end() || it->second.localClosed)
        return;

    auto& s = it->second;
    if (len == 0) return;

    // Queue if window low
    if (s.sendWindow == 0) {
        s.pendingData.emplace_back(data, data + len);
        // printf("MultiplexedTunnel::sendToStream, stream %u blocked. sendWindow: %u\n", streamId, s.sendWindow);
        return;
    }

    // Split into chunks based on window
    size_t pos = 0;
    while (pos < len) {
        uint32_t chunk = std::min<uint32_t>((uint32_t)len - pos, s.sendWindow);

        if (chunk == 0) {
            // Buffer the rest (occurs if sendWindow was exactly 0)
            s.pendingData.emplace_back(data + pos, data + len);
            break;
        }

        // Send the chunk
        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, data + pos);
        s.sendWindow -= chunk;
        pos += chunk;
    }

    // Backpressure check (Assuming getSocketContext()->writeQueue exists)
    if (getSocketContext() && getSocketContext()->writeQueue->size() > BACK_PRESSURE_LIMIT) {
        // pause_reading(); // Should be done at reactor level if needed
        // printf("WARNING: Backpressure limit hit. Write queue size: %zu\n", getSocketContext()->writeQueue->size());
    }
}

void MultiplexedTunnel::closeStream(uint32_t streamId, bool rst) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    if (s.localClosed) return;
    s.localClosed = true;

    FrameFlags flags = rst ? FrameFlags::RST : FrameFlags::FIN;
    sendFrame(FrameType::Data, flags, streamId, 0); // Send FIN or RST

    if (s.remoteClosed) {
        if (s.onClose) s.onClose(s.arg, streamId);
        m_streams.erase(it); // Fully closed
    }
}

// FIX for Yamux: Delta is in Length field, no payload
void MultiplexedTunnel::sendWindowUpdate(uint32_t streamId, uint32_t delta) {
    if (streamId == 0 || delta == 0) return;
    // Yamux uses Length field for Delta value. Payload is null/empty.
    sendFrame(FrameType::WindowUpdate, FrameFlags(0), streamId, delta, nullptr);
}

void MultiplexedTunnel::sendPing(bool isACK, uint32_t value) {
    FrameFlags flags = isACK ? FrameFlags::ACK : FrameFlags(0);

    // Yamux uses Length field for StreamID (which is 0 for Ping/GoAway)
    // and Ping value is encoded in the 4-byte payload.
    // However, looking at yamux/const.go (header.Length() being 4 bytes)
    // and spec.md, the opaque value is sent in the payload.

    uint32_t netValue = htonl(value); // Ensure value is Big-Endian for payload

    // StreamID is 0 for PING
    sendFrame(FrameType::Ping, flags, 0, 4, reinterpret_cast<const uint8_t*>(&netValue));
}


void MultiplexedTunnel::shutdown(GoAwayCode code) {
    uint32_t codeVal = static_cast<uint32_t>(code);
    // YAMUX: GoAway uses Length field to specify the error code. StreamID is 0.
    // The spec.md is contradictory: "Length should be set to one of the following" vs
    // "GoAway: Length is the code". We follow Go's implementation that sends the code in the length field.
    // We send a minimal GoAway frame (StreamID 0, Length = code).

    // Go's implementation uses 4-byte payload for code and length=4. Let's follow that:
    uint32_t netCode = htonl(codeVal);
    sendFrame(FrameType::GoAway, FrameFlags(0), 0, 4, reinterpret_cast<const uint8_t*>(&netCode));

    // Close all streams locally
    for (auto& pair : m_streams) {
        auto& s = pair.second;
        if (s.onClose) s.onClose(s.arg, s.id);
    }
    m_streams.clear();

    // Close the underlying TCP socket
    close(false);
}

// --- Frame Encoding/Decoding ---

void MultiplexedTunnel::sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload) {
    uint8_t header[HEADER_SIZE];

    // FIX: Version is 0
    header[0] = YAMUX_VERSION;
    header[1] = static_cast<uint8_t>(type);

    // Flags (16 bits) - Network Byte Order
    uint16_t flagVal = static_cast<uint16_t>(flags);
    uint16_t netFlags = htons(flagVal);
    memcpy(header + 2, &netFlags, 2);

    // StreamID (32 bits) - Network Byte Order
    uint32_t netStreamId = htonl(streamId);
    memcpy(header + 4, &netStreamId, 4);

    // Length (32 bits) - Network Byte Order
    uint32_t netLength = htonl(length);
    memcpy(header + 8, &netLength, 4);

    // Send header and payload
    send(header, HEADER_SIZE);
    if (length > 0 && payload) send(payload, length);
}

void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {
    m_parseBuffer.insert(m_parseBuffer.end(), data, data + len);
    size_t pos = 0;

    while (pos + HEADER_SIZE <= m_parseBuffer.size()) {
        uint8_t ver = m_parseBuffer[pos++];
        if (ver != YAMUX_VERSION) {
            // printf("Protocol Error: Invalid Version %u\n", ver);
            shutdown(GoAwayCode::ProtocolError);
            pos -= HEADER_SIZE; // Revert pos to prevent buffer overflow issue
            break;
        }

        auto type = static_cast<FrameType>(m_parseBuffer[pos++]);

        // Flags (16 bits) - Host Byte Order
        uint16_t netFlags;
        memcpy(&netFlags, m_parseBuffer.data() + pos, 2); pos += 2;
        uint16_t flags = ntohs(netFlags);

        // StreamID (32 bits) - Host Byte Order
        uint32_t netStreamId;
        memcpy(&netStreamId, m_parseBuffer.data() + pos, 4); pos += 4;
        uint32_t streamId = ntohl(netStreamId);

        // Length/Delta (32 bits) - Host Byte Order
        uint32_t netLength;
        memcpy(&netLength, m_parseBuffer.data() + pos, 4); pos += 4;
        uint32_t length = ntohl(netLength);

        // Check if full payload is available
        if (pos + length > m_parseBuffer.size()) {
            pos -= HEADER_SIZE; // Rollback
            break; // Partial data
        }

        const uint8_t* payload = length > 0 ? m_parseBuffer.data() + pos : nullptr;
        pos += length;

        switch (type) {
        case FrameType::Data:
            handleDataFrame(streamId, payload, length, flags);
            break;
        case FrameType::WindowUpdate:
            // YAMUX: length is delta
            handleWindowUpdateFrame(streamId, payload, length);
            break;
        case FrameType::Ping:
            // YAMUX: length is 4, payload is value
            handlePingFrame(streamId, payload, length, flags);
            break;
        case FrameType::GoAway:
            // YAMUX: length is 4, payload is code
            handleGoAwayFrame(payload, length);
            break;
        default:
            // printf("Protocol Error: Invalid Frame Type %u\n", (uint32_t)type);
            shutdown(GoAwayCode::ProtocolError);
            // Do not return here, continue to consume buffer
        }
    }

    if (pos > 0) m_parseBuffer.erase(m_parseBuffer.begin(), m_parseBuffer.begin() + pos);
}

// --- Frame Handlers ---

void MultiplexedTunnel::handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags) {
    auto it = m_streams.find(streamId);
    bool isNew = (it == m_streams.end());

    if (isNew) {
        if (!(flags & FrameFlags::SYN)) return; // Data for non-existent stream without SYN, ignore.

        // New remote stream
        bool isRemoteIdValid = m_isClient ? (streamId % 2 == 0) : (streamId % 2 == 1);
        if (streamId == 0 || !isRemoteIdValid) return;

        auto& s = m_streams[streamId];
        s.id = streamId;

        if (m_onNewStream) {
            m_onNewStream(m_newStreamArg, streamId, &s);
        }

        // Send ACK if not RST
        if (!(flags & FrameFlags::RST)) {
            sendFrame(FrameType::Data, FrameFlags::ACK, streamId, 0);
        }
        it = m_streams.find(streamId); // Re-find iterator
        if (it == m_streams.end()) return;
    }

    auto& s = it->second;
    if (s.remoteClosed) return;

    if (length > 0) {
        if (length > s.recvWindow) {
            // Flow Control Error
            // printf("Flow Control Error: Recv Window Exceeded on Stream %u (Len %u > Window %u)\n", streamId, length, s.recvWindow);
            closeStream(streamId, true); // RST the stream
            return;
        }

        s.recvWindow -= length;
        if (s.onData) s.onData(s.arg, streamId, payload, length);

        // NOTE: Application is responsible for calling sendWindowUpdate(delta)
    }

    if (flags & FrameFlags::ACK) {
        // Stream established. (For C++ implementation, this can signal go-ahead)
    }

    if (flags & FrameFlags::FIN || flags & FrameFlags::RST) {
        s.remoteClosed = true;
        if (s.onClose && !s.localClosed) {
            s.onClose(s.arg, streamId);
        }
        if (s.localClosed || flags & FrameFlags::RST) {
            m_streams.erase(it); // Fully closed or hard reset
        }
    }
}

// FIX for Yamux: Delta is in Length field
void MultiplexedTunnel::handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length) {
    if (streamId == 0) return; // Must not be for Session

    uint32_t delta = length; // Delta is in the length field in Yamux

    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    s.sendWindow += delta;
    processPending(streamId);
}

void MultiplexedTunnel::handlePingFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags) {
    if (streamId != 0 || length != 4) return; // Must be session-level (StreamID 0) and 4-byte payload

    uint32_t netValue;
    memcpy(&netValue, payload, 4);
    uint32_t value = ntohl(netValue); // Value is Big-Endian

    if (flags & FrameFlags::ACK) {
        // Pong received. You can use 'value' for RTT calculation here.
        // printf("Pong received: %u\n", value);
    } else {
        // Ping request → send Pong (ACK)
        // printf("Ping received: %u. Sending Pong.\n", value);
        sendPing(true, value);
    }
}

void MultiplexedTunnel::handleGoAwayFrame(const uint8_t* payload, uint32_t length) {
    if (length != 4) return;

    uint32_t netCode;
    memcpy(&netCode, payload, 4);
    uint32_t code = ntohl(netCode);

    // printf("GoAway received. Code: %u\n", code);

    // Close all active streams
    std::vector<uint32_t> ids;
    for (const auto& pair : m_streams) ids.push_back(pair.first);

    for (uint32_t id : ids) {
        auto it = m_streams.find(id);
        if (it != m_streams.end()) {
            auto& s = it->second;
            if (s.onClose) s.onClose(s.arg, s.id);
            m_streams.erase(it);
        }
    }

    // Close the underlying TCP socket immediately
    close(true);
}

void MultiplexedTunnel::processPending(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    while (!s.pendingData.empty() && s.sendWindow > 0) {
        auto& buf = s.pendingData.front();
        uint32_t chunk = std::min<uint32_t>((uint32_t)buf.size(), s.sendWindow);

        // Send the chunk
        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, buf.data());
        s.sendWindow -= chunk;

        if (chunk < buf.size()) {
            // Partial send, keep the rest in the front
            buf.erase(buf.begin(), buf.begin() + chunk);
            break;
        }
        s.pendingData.pop_front(); // Entire buffer sent
    }
}
