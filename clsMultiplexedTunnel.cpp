#include "clsMultiplexedTunnel.h"
#include "constants.h"
#include <cstdio>

MultiplexedTunnel::MultiplexedTunnel(bool isClient)
    : m_isClient(isClient),
    m_nextStreamId(isClient ? 1 : 2) {}

uint32_t MultiplexedTunnel::openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg) {
    uint32_t id = m_nextStreamId;
    m_nextStreamId += 2;

    auto& s = m_streams[id];
    s.id = id;
    s.onData = onData;
    s.onClose = onClose;
    s.arg = arg;

    sendFrame(FrameType::Data, FrameFlags::SYN, id, 0);
    return id;
}

void MultiplexedTunnel::sendToStream(uint32_t streamId, const uint8_t* data, size_t len) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end() || it->second.localClosed || len == 0)
        return;

    auto& s = it->second;

    while (len > 0) {
        if (s.sendWindow == 0) {
            s.pendingData.emplace_back(data, data + len);
            return;
        }
        uint32_t chunk = std::min<uint32_t>(len, s.sendWindow);
        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, data);
        s.sendWindow -= chunk;
        data += chunk;
        len -= chunk;
    }
}

void MultiplexedTunnel::closeStream(uint32_t streamId, bool rst) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;
    auto& s = it->second;
    if (s.localClosed) return;

    s.localClosed = true;
    sendFrame(FrameType::Data, rst ? FrameFlags::RST : FrameFlags::FIN, streamId, 0);

    if (s.remoteClosed && s.onClose)
        s.onClose(s.arg, streamId);
    if (s.remoteClosed)
        m_streams.erase(it);
}

void MultiplexedTunnel::sendWindowUpdate(uint32_t streamId, uint32_t delta) {
    uint32_t be = htonl(delta);
    sendFrame(FrameType::WindowUpdate, FrameFlags(0), streamId, sizeof(delta), reinterpret_cast<const uint8_t*>(&be));
}

void MultiplexedTunnel::shutdown(GoAwayCode code) {
    uint32_t be = htonl(static_cast<uint32_t>(code));
    sendFrame(FrameType::GoAway, FrameFlags(0), 0, sizeof(be), reinterpret_cast<const uint8_t*>(&be));
    close(false);
}

void MultiplexedTunnel::sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload) {
    uint8_t header[HEADER_SIZE];
    header[0] = 1; // Version
    header[1] = static_cast<uint8_t>(type);
    uint16_t beFlags = htons(static_cast<uint16_t>(flags));
    uint32_t beId = htonl(streamId);
    uint32_t beLen = htonl(length);
    memcpy(header + 2, &beFlags, 2);
    memcpy(header + 4, &beId, 4);
    memcpy(header + 8, &beLen, 4);

    send(header, HEADER_SIZE);
    if (length > 0 && payload)
        send(payload, length);
}

void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {
    m_parseBuffer.insert(m_parseBuffer.end(), data, data + len);
    size_t pos = 0;

    while (m_parseBuffer.size() - pos >= HEADER_SIZE) {
        uint8_t ver = m_parseBuffer[pos];
        if (ver != 1) {
            shutdown(GoAwayCode::ProtocolError);
            return;
        }

        FrameType type = static_cast<FrameType>(m_parseBuffer[pos + 1]);
        uint16_t flags = ntohs(*reinterpret_cast<const uint16_t*>(&m_parseBuffer[pos + 2]));
        uint32_t streamId = ntohl(*reinterpret_cast<const uint32_t*>(&m_parseBuffer[pos + 4]));
        uint32_t length = ntohl(*reinterpret_cast<const uint32_t*>(&m_parseBuffer[pos + 8]));

        if (m_parseBuffer.size() - pos < HEADER_SIZE + length)
            break; // incomplete

        const uint8_t* payload = length > 0 ? &m_parseBuffer[pos + HEADER_SIZE] : nullptr;

        switch (type) {
        case FrameType::Data:          handleDataFrame(streamId, payload, length, flags); break;
        case FrameType::WindowUpdate:  handleWindowUpdateFrame(streamId, payload, length); break;
        case FrameType::Ping:          handlePingFrame(payload, length, flags); break;
        case FrameType::GoAway:        handleGoAwayFrame(payload, length); break;
        }

        pos += HEADER_SIZE + length;
    }

    if (pos > 0)
        m_parseBuffer.erase(m_parseBuffer.begin(), m_parseBuffer.begin() + pos);
}

void MultiplexedTunnel::handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags) {
    printf("MultiplexedTunnel::handleDataFrame streamId: %d, flags: %d, payload: [%s]\n", streamId, flags, payload);
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) {
        // parity check (opposite parity opens)
        bool valid = (m_isClient && (streamId % 2 == 0)) || (!m_isClient && (streamId % 2 == 1));
        if (!(flags & FrameFlags::SYN) || !valid)
            return;
        auto& s = m_streams[streamId];
        s.id = streamId;
        sendFrame(FrameType::Data, FrameFlags::ACK, streamId, 0);
        it = m_streams.find(streamId);
    }

    auto& s = it->second;
    if (length > 0) {
        if (length > s.recvWindow) {
            sendFrame(FrameType::Data, FrameFlags::RST, streamId, 0);
            return;
        }
        s.recvWindow -= length;
        if (s.onData)
            s.onData(s.arg, streamId, payload, length);
        if (s.recvWindow < LOW_WATERMARK) {
            uint32_t delta = INITIAL_WINDOW_SIZE - s.recvWindow;
            s.recvWindow = INITIAL_WINDOW_SIZE;
            sendWindowUpdate(streamId, delta);
        }
    }

    if (flags & (FrameFlags::FIN | FrameFlags::RST)) {
        s.remoteClosed = true;
        if (s.localClosed && s.onClose)
            s.onClose(s.arg, streamId);
        if (s.localClosed)
            m_streams.erase(it);
    }
}

void MultiplexedTunnel::handleWindowUpdateFrame(uint32_t streamId, const uint8_t* payload, uint32_t length) {
    if (length != 4 || !payload) return;
    uint32_t delta = ntohl(*reinterpret_cast<const uint32_t*>(payload));
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;
    it->second.sendWindow += delta;
    processPending(streamId);
}

void MultiplexedTunnel::handlePingFrame(const uint8_t* payload, uint32_t length, uint16_t flags) {
    if (length != 4 || !payload) return;
    uint32_t val = ntohl(*reinterpret_cast<const uint32_t*>(payload));
    if (flags & FrameFlags::ACK)
        printf("Ping response: %u\n", val);
    else {
        uint32_t be = htonl(val);
        sendFrame(FrameType::Ping, FrameFlags::ACK, 0, 4, reinterpret_cast<const uint8_t*>(&be));
    }
}

void MultiplexedTunnel::handleGoAwayFrame(const uint8_t* payload, uint32_t length) {
    uint32_t code = length == 4 ? ntohl(*reinterpret_cast<const uint32_t*>(payload)) : 0;
    printf("Received GoAway: code=%u\n", code);
    for (auto& [id, s] : m_streams)
        if (s.onClose) s.onClose(s.arg, id);
    m_streams.clear();
    close(true);
}

void MultiplexedTunnel::processPending(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;
    auto& s = it->second;
    while (!s.pendingData.empty() && s.sendWindow > 0) {
        auto& buf = s.pendingData.front();
        uint32_t chunk = std::min<uint32_t>(buf.size(), s.sendWindow);
        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, buf.data());
        s.sendWindow -= chunk;
        if (chunk < buf.size()) {
            buf.erase(buf.begin(), buf.begin() + chunk);
            break;
        }
        s.pendingData.pop_front();
    }
}
