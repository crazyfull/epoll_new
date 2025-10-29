#include "clsMultiplexedTunnel.h"
#include "constants.h"
// #include "constants.h" // فرض می‌شود این فایل شامل تعریف BACK_PRESSURE و ... است

MultiplexedTunnel::MultiplexedTunnel(bool isClient) : m_isClient(isClient), m_nextStreamId(isClient ? 1 : 2) {
    //
}

void MultiplexedTunnel::setOnNewStream(OnNewStreamFn fn, void* arg) {
    m_onNewStream = fn;
    m_newStreamArg = arg;
}

uint32_t MultiplexedTunnel::openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg) {
    uint32_t id = m_nextStreamId;
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
    if (len == 0)
        return;

    // Queue if window low
    if (s.sendWindow == 0) {
        s.pendingData.emplace_back(data, data + len);
        return;
    }

    // Split into chunks based on window
    size_t pos = 0;
    while (pos < len) {
        uint32_t chunk = std::min<uint32_t>((uint32_t)len - pos, s.sendWindow);
        if (chunk == 0) {
            // Buffer the rest
            s.pendingData.emplace_back(data + pos, data + len);
            break;
        }

        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, data + pos);
        s.sendWindow -= chunk;
        pos += chunk;
    }

    // Check backpressure (assuming getSocketContext()->writeQueue exists and size() returns size_t)
    if (getSocketContext() && getSocketContext()->writeQueue->size() > BACK_PRESSURE) {
        pause_reading(); // Assuming this is defined in TCPSocket
    }
}

void MultiplexedTunnel::closeStream(uint32_t streamId, bool rst) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    if (s.localClosed) return;
    s.localClosed = true;

    FrameFlags flags = rst ? FrameFlags::RST : FrameFlags::FIN;
    sendFrame(FrameType::Data, flags, streamId, 0);

    if (s.remoteClosed) {
        if (s.onClose) s.onClose(s.arg, streamId);
        m_streams.erase(it); // Fully closed
    }
}


void MultiplexedTunnel::sendWindowUpdate(uint32_t streamId, uint32_t delta) {
    uint8_t payload[4];
    memcpy(payload, &delta, 4);
    sendFrame(FrameType::WindowUpdate, FrameFlags(0), streamId, 4, payload);
}

/*
void MultiplexedTunnel::sendWindowUpdate(uint32_t streamId, uint32_t delta) {
    // delta باید داخل payload 4 بایتی فرستاده بشه (network byte order)
    uint32_t be = htonl(delta);
    sendFrame(FrameType::WindowUpdate, FrameFlags(0), streamId, 4, reinterpret_cast<const uint8_t*>(&be));
}
*/

void MultiplexedTunnel::shutdown(GoAwayCode code) {
    uint32_t codeVal = static_cast<uint32_t>(code);
    sendFrame(FrameType::GoAway, FrameFlags(0), 0, codeVal); // StreamID 0 for connection
    close(false); // Graceful close (Assuming this is defined in TCPSocket)
}

void MultiplexedTunnel::sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload) {
    uint8_t header[HEADER_SIZE];

    header[0] = 1; // Version
    header[1] = static_cast<uint8_t>(type);

    // FIX: Convert to Network Byte Order (Big-Endian)
    uint16_t flagVal = static_cast<uint16_t>(flags);
    uint16_t netFlags = htons(flagVal);
    memcpy(header + 2, &netFlags, 2);

    uint32_t netStreamId = htonl(streamId);
    memcpy(header + 4, &netStreamId, 4);

    uint32_t netLength = htonl(length);
    memcpy(header + 8, &netLength, 4);

    // send is an assumed method from TCPSocket to enqueue data for sending
    send(header, HEADER_SIZE);
    if (length > 0 && payload) send(payload, length);
}

void MultiplexedTunnel::processPending(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    while (!s.pendingData.empty() && s.sendWindow > 0) {
        auto& buf = s.pendingData.front();
        uint32_t chunk = std::min<uint32_t>((uint32_t)buf.size(), s.sendWindow);

        // This is a Data frame, no special flags needed for queued data
        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, buf.data());
        s.sendWindow -= chunk;

        if (chunk < buf.size()) {
            buf.erase(buf.begin(), buf.begin() + chunk);
            break;
        }
        s.pendingData.pop_front();
    }
}

void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {
    // Append to parse buffer if partial
    m_parseBuffer.insert(m_parseBuffer.end(), data, data + len);
    size_t pos = 0;
    while (pos + HEADER_SIZE <= m_parseBuffer.size()) {
        uint8_t ver = m_parseBuffer[pos++];
        if (ver != 1) { shutdown(GoAwayCode::ProtocolError); return; }

        auto type = static_cast<FrameType>(m_parseBuffer[pos++]);

        // FIX: Read and Convert from Network Byte Order
        uint16_t netFlags;
        memcpy(&netFlags, m_parseBuffer.data() + pos, 2); pos += 2;
        uint16_t flags = ntohs(netFlags);

        uint32_t netStreamId;
        memcpy(&netStreamId, m_parseBuffer.data() + pos, 4); pos += 4;
        uint32_t streamId = ntohl(netStreamId);

        uint32_t netLength;
        memcpy(&netLength, m_parseBuffer.data() + pos, 4); pos += 4;
        uint32_t length = ntohl(netLength);

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
            // length is delta for WindowUpdate
            handleWindowUpdateFrame(streamId, payload, length);
            break;
        case FrameType::Ping:
            // length is opaque value for Ping
            handlePingFrame(payload, length, static_cast<FrameFlags>(flags));
            break;
        case FrameType::GoAway:
            // length is code for GoAway
            handleGoAwayFrame(payload, length);
            break;
        }
    }

    // Remove processed, keep leftover
    if (pos > 0) m_parseBuffer.erase(m_parseBuffer.begin(), m_parseBuffer.begin() + pos);
}

void MultiplexedTunnel::handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags) {
    // printf("handleDataFrame flags: [%d] streamId: [%d] payload: [%s]\n", flags, streamId, payload);

    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) {
        if (flags & FrameFlags::SYN) {
            // New remote stream

            // Check for valid ID parity (Remote ID must have opposite parity of local initiator IDs)
            // Client creates ODD IDs, so it expects EVEN IDs. (streamId % 2 == 0)
            // Server creates EVEN IDs, so it expects ODD IDs. (streamId % 2 == 1)
            bool isRemoteIdValid = m_isClient ? (streamId % 2 == 0) : (streamId % 2 == 1);

            if (streamId == 0 || !isRemoteIdValid) {
                // Ignore invalid SYN frame
                return;
            }

            auto& s = m_streams[streamId];
            s.id = streamId;

            // FIX: Notify application layer about the new stream
            if (m_onNewStream) {
                m_onNewStream(m_newStreamArg, streamId, &s);
            }

            // Send ACK if not RST
            if (!(flags & FrameFlags::RST)) {
                sendFrame(FrameType::Data, FrameFlags::ACK, streamId, 0);
            }

            it = m_streams.find(streamId);
            if (it == m_streams.end()) return; // Should not happen if m_onNewStream didn't remove it.
        } else {
            // Data frame for non-existent stream (ignore or RST)
            return;
        }
    }

    auto& s = it->second;
    if (s.remoteClosed)
        return; // Ignore data on closed stream

    if (length > 0) {
        if (length > s.recvWindow) {
            // Flow Control Error: Sender violated the window size
            sendFrame(FrameType::Data, FrameFlags::RST, streamId, 0);
            s.remoteClosed = true;
            s.localClosed = true;
            if (s.onClose) s.onClose(s.arg, streamId);
            m_streams.erase(it);
            return;
        }

        s.recvWindow -= length;
        if (s.onData){
            // printf("onData streamId: [%d] payload: [%s]\n", streamId, payload);
            s.onData(s.arg, streamId, payload, length);
        }

        // ATTENTION: Removed auto-window update here. Application must call sendWindowUpdate(delta)
        // after it has CONSUMED 'delta' bytes of data to maintain correct flow control.
        // sendWindowUpdate(streamId, length);
    }

    if (flags & FrameFlags::ACK) {
        // Stream establishment confirmed. (Optional: check if local SYN was sent)
    }

    if (flags & FrameFlags::FIN || flags & FrameFlags::RST) {
        s.remoteClosed = true;
        if (s.onClose && !s.localClosed) {
            s.onClose(s.arg, streamId); // Notify close if not already closed locally
        }
        if (s.localClosed) m_streams.erase(it);
    }
}


void MultiplexedTunnel::handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length) {

    if (length != 4 || streamId == 0)
        return;
    uint32_t delta;
    memcpy(&delta, payload, 4);

    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    s.sendWindow += delta;
    processPending(streamId);
}


void MultiplexedTunnel::handlePingFrame(const uint8_t* payload, uint32_t length, FrameFlags flags) {
    if (length != 4) return;

    uint32_t value;
    memcpy(&value, payload, 4);

    if (flags & FrameFlags::ACK) {
        // پاسخ دریافت شده
    } else {
        // درخواست ping → پاسخ بده
        sendFrame(FrameType::Ping, FrameFlags::ACK, 0, 4, payload);
    }
}


void MultiplexedTunnel::handleGoAwayFrame(const uint8_t* payload, uint32_t length) {
    if (length != 4) return;
    uint32_t code;
    memcpy(&code, payload, 4);

    for (auto& [id, s] : m_streams) {
        if (s.onClose) s.onClose(s.arg, s.id);
    }
    m_streams.clear();
    close(true);
}
