#include "clsMultiplexedTunnel.h"
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <vector>
#include "clsEpollReactor.h"


bool MultiplexedTunnel::initParseBuffer() {
    if (m_parseBuffer)
        return true;
    /**/
    //m_SocketContext.rBuffer = (char*)m_pReactor->bufferPool()->allocate(SLAB_SIZE);
    if (getReactor() && getReactor()->bufferPool()) {
        m_parseBuffer = (uint8_t*)getReactor()->bufferPool()->allocate(PARSE_BUFFER_SIZE);
        if (m_parseBuffer) {
            m_parseBufferCapacity = PARSE_BUFFER_SIZE;
            m_parseBufferLength = 0;
            return true;
        }
    }

    m_parseBuffer = nullptr;
    m_parseBufferCapacity = 0;
    return false;
}

void MultiplexedTunnel::releaseParseBuffer() {
    /**/
    if (m_parseBuffer) {
        if (getReactor() && getReactor()->bufferPool()) {
            getReactor()->bufferPool()->deallocate(m_parseBuffer);
        }
        m_parseBuffer = nullptr;
        m_parseBufferCapacity = 0;
        m_parseBufferLength = 0;
    }

}

bool MultiplexedTunnel::resizeParseBuffer(size_t newCapacity) {
    if (newCapacity <= m_parseBufferCapacity)
        return true;

    if (getReactor() && getReactor()->bufferPool()) {
        void* newBuf = getReactor()->bufferPool()->reallocate(m_parseBuffer, newCapacity);

        if (newBuf) {
            m_parseBuffer = (uint8_t*)newBuf;
            m_parseBufferCapacity = newCapacity;
            printf("Parse buffer resized to %zu bytes.\n", newCapacity);
            return true;
        } else {
            // Reallocation failed
            printf("Error: Parse buffer reallocation failed for size %zu.\n", newCapacity);
        }
    }
    return false;
}

bool MultiplexedTunnel::initSendQueue(Stream& session) {
    if (session.pendingData) {
        return true;
    }

    //
    if (getReactor() && getReactor()->bufferPool()) {
        // create
        session.pendingData = new SendQueue(*getReactor()->bufferPool());

        if (session.pendingData) {
            return true;
        } else {
            // failed allocate
            shutdown(GoAwayCode::InternalError);
            return false;
        }
    } else {
        // Reactor is not found
        shutdown(GoAwayCode::InternalError);
        return false;
    }
}

// --- Public API ---

MultiplexedTunnel::MultiplexedTunnel(bool isClient) :
    m_isClient(isClient),
    m_nextStreamId(isClient ? 1 : 2),
    m_parseBufferLength(0),
    m_parseBufferCapacity(0)
{
}

MultiplexedTunnel::~MultiplexedTunnel() {
    releaseParseBuffer();
}

void MultiplexedTunnel::setOnNewStream(OnNewStreamFn fn, void* arg) {
    m_onNewStream = fn;
    m_newStreamArg = arg;
}

uint32_t MultiplexedTunnel::openStream(OnStreamDataFn onData, OnStreamCloseFn onClose, void* arg) {
    uint32_t id = m_nextStreamId;

    if (id + 2 < id) {
        shutdown(GoAwayCode::InternalError);
        return 0;
    }
    m_nextStreamId += 2;

    Stream& s = m_streams[id];
    s.id = id;
    s.onData = onData;
    s.onClose = onClose;
    s.arg = arg;

    // create Queue
    if (getReactor() && getReactor()->bufferPool()) {
        s.pendingData = new SendQueue(*getReactor()->bufferPool());
    } else {
        m_streams.erase(id);
        return 0;
    }

    sendFrame(FrameType::WindowUpdate, FrameFlags::SYN, id, 0);
    return id;
}


void MultiplexedTunnel::sendToStream(uint32_t streamId, const uint8_t* data, size_t len) {
    if (len == 0) {
        return;
    }

    auto it = m_streams.find(streamId);
    if (it == m_streams.end() || it->second.localClosed)
        return;

    Stream &session = it->second;

    //
    if (session.sendWindow == 0) {
        if (!initSendQueue(session))
            return;
        session.pendingData->push(data, len);
        //printf("sendWindow is fulled, stream %u. Pending size: %zu\n", streamId, session.pendingData->count());
        return;
    }

    //
    size_t pos = 0;
    while (pos < len) {
        uint32_t chunk = std::min<uint32_t>((uint32_t)len - pos, session.sendWindow);

        if (chunk == 0) {
            if (!initSendQueue(session))
                break;

            session.pendingData->push(data + pos, len - pos);
            break;
        }

        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, data + pos);
        session.sendWindow -= chunk;
        pos += chunk;
    }

    if (getSocketContext() && getSocketContext()->writeQueue->size() > BACK_PRESSURE_LIMIT) {
        printf("WARNING: Backpressure limit hit. Write queue size: %zu\n", getSocketContext()->writeQueue->size());
    }
}

void MultiplexedTunnel::closeStream(uint32_t streamId, bool rst) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    if (s.localClosed)
        return;
    s.localClosed = true;

    FrameFlags flags = rst ? FrameFlags::RST : FrameFlags::FIN;
    sendFrame(FrameType::Data, flags, streamId, 0);

    if (s.remoteClosed) {
        if (s.onClose) s.onClose(s.arg, streamId);
        m_streams.erase(it);
    }
}

void MultiplexedTunnel::trySendWindowUpdate(uint32_t streamId, uint32_t consumedLength) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    Stream& s = it->second;
    s.unackedRecvBytes += consumedLength;

    if (s.unackedRecvBytes >= WINDOW_UPDATE_THRESHOLD) {
        uint32_t delta = s.unackedRecvBytes;
        s.unackedRecvBytes = 0;
        sendWindowUpdate(streamId, delta);
    }
}

void MultiplexedTunnel::sendWindowUpdate(uint32_t streamId, uint32_t delta) {
    if (streamId == 0 || delta == 0)
        return;

    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        it->second.recvWindow += delta;
    }
    sendFrame(FrameType::WindowUpdate, FrameFlags(0), streamId, delta);
}

// FIX: متد sendPing حفظ شد.
void MultiplexedTunnel::sendPing(bool isACK, uint32_t value) {
    FrameFlags flags = isACK ? FrameFlags(ACK) : FrameFlags(0);
    sendFrame(FrameType::Ping, flags, 0, value);
}


void MultiplexedTunnel::shutdown(GoAwayCode code) {
    uint32_t codeVal = static_cast<uint32_t>(code);
    sendFrame(FrameType::GoAway, FrameFlags(0), 0, codeVal);

    // Close all active streams (حفظ منطق قبلی)
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

    close(true);
}

// --- Protected/Private Helpers ---

void MultiplexedTunnel::sendFrame(FrameType type, FrameFlags flags, uint32_t streamId, uint32_t length, const uint8_t* payload) {
    uint8_t header[HEADER_SIZE];

    header[0] = YAMUX_VERSION;
    header[1] = static_cast<uint8_t>(type);

    uint16_t flagVal = static_cast<uint16_t>(flags);
    uint16_t netFlags = htons(flagVal);
    memcpy(header + 2, &netFlags, 2);

    uint32_t netStreamId = htonl(streamId);
    memcpy(header + 4, &netStreamId, 4);

    uint32_t netLength = htonl(length);
    memcpy(header + 8, &netLength, 4);

    send(header, HEADER_SIZE);
    if (length > 0 && payload)
        send(payload, length);
}


void printBinaryString(const uint8_t* data, size_t len) {

    printf("onReceiveData [");

    for (int i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            std::cout << data[i];
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("]\n");
}

void printBinaryString(const std::string& data) {
    printf("onReceiveData [");
    for (char c : data) {
        if (c >= 32 && c <= 126) {
            std::cout << c;
        } else {
            std::cout << ".";
        }
    }
    std::cout << std::dec << std::endl;
    printf("]\n");
}

void printHexDump(const std::string& data) {
    const int bytesPerLine = 16;

    for (size_t i = 0; i < data.size(); i += bytesPerLine) {
        // آفست
        std::cout << std::hex << std::setw(8) << std::setfill('0') << i << ": ";

        // بایت‌های هگز
        for (int j = 0; j < bytesPerLine; j++) {
            if (i + j < data.size()) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (static_cast<unsigned int>(data[i + j]) & 0xFF) << " ";
            } else {
                std::cout << "   ";
            }
        }

        // کاراکترهای قابل چاپ
        std::cout << " | ";
        for (int j = 0; j < bytesPerLine && i + j < data.size(); j++) {
            char c = data[i + j];
            if (c >= 32 && c <= 126) {
                std::cout << c;
            } else {
                std::cout << ".";
            }
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}

/*
 std::string input((char*)data, len);
    printBinaryString(input);

    //printBinaryString(data, len);

00 01 00 02 00 00 00 01 00 00 00 00
00 00 00 00 00 00 00 01 00 00 00 17 msg


00 01 00 02 00 00 00 01 00 00 00 00

*/

/*
void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {

    std::string input((char*)data, len);
    //printBinaryString(input);
    printHexDump(input);

    // FIX: استفاده از initParseBuffer()
    if (!m_parseBuffer) {
        if (!initParseBuffer()) {
            shutdown(GoAwayCode::InternalError);
            return;
        }
    }

    // printf("onReceiveData usedsize: %zu, newlength: %zu, BufferCapacity: %zu\n", m_parseBufferLength , len, m_parseBufferCapacity);

    // FIX: کپی داده به بافر از BufferPool
    if (m_parseBufferLength + len > m_parseBufferCapacity) {
        printf("Protocol Error: Parse buffer overflow.\n");
        shutdown(GoAwayCode::ProtocolError);
        return;
    }

    memcpy(m_parseBuffer + m_parseBufferLength, data, len);
    m_parseBufferLength += len;

    size_t pos = 0;

    while (pos + HEADER_SIZE <= m_parseBufferLength) {

        uint8_t ver = m_parseBuffer[pos++];
        if (ver != YAMUX_VERSION) {
            shutdown(GoAwayCode::ProtocolError);
            break;
        }

        auto type = static_cast<FrameType>(m_parseBuffer[pos++]);

        // Flags
        uint16_t netFlags;
        memcpy(&netFlags, m_parseBuffer + pos, 2); pos += 2;
        uint16_t flags = ntohs(netFlags);

        // StreamID
        uint32_t netStreamId;
        memcpy(&netStreamId, m_parseBuffer + pos, 4); pos += 4;
        uint32_t streamId = ntohl(netStreamId);

        // Length/Delta
        uint32_t netLength;
        memcpy(&netLength, m_parseBuffer + pos, 4); pos += 4;
        uint32_t length = ntohl(netLength);

        const uint8_t* payload = nullptr;

        if (type == FrameType::Data) {
            if (pos + length > m_parseBufferLength) {
                pos -= HEADER_SIZE;
                break; // Partial data
            }
            if (length > 0) {
                payload = m_parseBuffer + pos;
            }
            pos += length;
        }

        switch (type) {
        case FrameType::Data:
            handleDataFrame(streamId, payload, length, flags);
            break;
        case FrameType::WindowUpdate:
            handleWindowUpdateFrame(streamId, length, flags);
            break;
        case FrameType::Ping:
            handlePingFrame(streamId, length, flags);
            break;
        case FrameType::GoAway:
            handleGoAwayFrame(length);
            break;
        default:
            shutdown(GoAwayCode::ProtocolError);
            return;
        }
    }

    // FIX: فشرده‌سازی بافر (Compact) - کاهش کپی داده
    if (pos > 0) {
        if (pos < m_parseBufferLength) {
            memmove(m_parseBuffer, m_parseBuffer + pos, m_parseBufferLength - pos);
        }
        m_parseBufferLength -= pos;
    }
}
*/


/* OK
void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {
    /*
    std::string input((char*)data, len);
    printBinaryString(input);
    //* /

    size_t data_pos = 0; // پوزیشنی که از بافر ورودی (data) خوانده‌ایم.

    // ۱. فریم ناقصی که از قبل در m_parseBuffer مانده است را تکمیل می‌کنیم.
    if (m_parseBufferLength > 0) {

        if (!m_parseBuffer) {
            if (!initParseBuffer()) {
                shutdown(GoAwayCode::InternalError);
                return;
            }
        }

        // ابتدا فرض می‌کنیم که فقط می‌خواهیم هدر را کامل کنیم تا طول فریم را بدانیم.
        size_t neededForHeader = (m_parseBufferLength < HEADER_SIZE) ? (HEADER_SIZE - m_parseBufferLength) : 0;
        size_t copyLen = std::min(neededForHeader, len - data_pos);

        // تکمیل هدر از داده ورودی (Single-Copy)
        if (copyLen > 0) {
            memcpy(m_parseBuffer + m_parseBufferLength, data + data_pos, copyLen);
            m_parseBufferLength += copyLen;
            data_pos += copyLen;

            if (m_parseBufferLength < HEADER_SIZE) {
                // هدر هنوز کامل نشده، منتظر داده بیشتر می‌مانیم.
                printf("01\n");
                return;
            }
        }

        // --- هدر اکنون در m_parseBuffer کامل است ---

        uint32_t netLength;
        memcpy(&netLength, m_parseBuffer + 8, 4);
        uint32_t payloadLength = ntohl(netLength);

        uint8_t type_val = m_parseBuffer[1];
        size_t totalFrameSize = HEADER_SIZE;
        if (type_val == static_cast<uint8_t>(FrameType::Data)) {
            totalFrameSize += payloadLength;
        }

        size_t neededForFrame = totalFrameSize - m_parseBufferLength;

        // تکمیل مابقی فریم (Payload) از داده ورودی (Single-Copy)
        if (neededForFrame > 0) {
            size_t remainingInput = len - data_pos;
            copyLen = std::min(neededForFrame, remainingInput);

            // بررسی ظرفیت: m_parseBuffer باید بتواند یک فریم کامل (نه کل بافر ورودی) را در خود جای دهد.
            if (m_parseBufferCapacity < totalFrameSize) {
                printf("Protocol Error: Frame size %zu exceeds buffer capacity %zu\n", totalFrameSize, m_parseBufferCapacity);
                shutdown(GoAwayCode::InternalError);
                printf("02\n");
                return;
            }

            if (copyLen > 0) {
                // کپی مابقی فریم
                memcpy(m_parseBuffer + m_parseBufferLength, data + data_pos, copyLen);
                m_parseBufferLength += copyLen;
                data_pos += copyLen;
            }

            if (m_parseBufferLength < totalFrameSize) {
                // فریم کامل نشد، منتظر داده بیشتر می‌مانیم.
                //printf("03\n");
                return;
            }
        }

        // --- فریم اول اکنون در m_parseBuffer کامل شده و آماده پردازش است ---

        // پردازش فریم کامل شده از m_parseBuffer

        size_t pos_fragment = 0;

        uint8_t ver = m_parseBuffer[pos_fragment++];
        auto type = static_cast<FrameType>(m_parseBuffer[pos_fragment++]);

        uint16_t netFlags;
        memcpy(&netFlags, m_parseBuffer + pos_fragment, 2); pos_fragment += 2;
        uint16_t flags = ntohs(netFlags);

        uint32_t netStreamId;
        memcpy(&netStreamId, m_parseBuffer + pos_fragment, 4); pos_fragment += 4;
        uint32_t streamId = ntohl(netStreamId);

        pos_fragment += 4; // پرش از Length/Delta

        const uint8_t* payload = nullptr;
        if (type == FrameType::Data) {
            if (payloadLength > 0) {
                payload = m_parseBuffer + pos_fragment;
            }
            pos_fragment += payloadLength;
        }

        // فراخوانی Handler
        switch (type) {
        case FrameType::Data:
            handleDataFrame(streamId, payload, payloadLength, flags);
            break;
        case FrameType::WindowUpdate:
            handleWindowUpdateFrame(streamId, payloadLength,flags);
            break;
        case FrameType::Ping:
            handlePingFrame(streamId, payloadLength, flags);
            break;
        case FrameType::GoAway:
            handleGoAwayFrame(payloadLength);
            break;
        default:
            shutdown(GoAwayCode::ProtocolError);
            return;
        }

        // خالی کردن بافر Fragment
        m_parseBufferLength = 0;
    }

    // ۲. پردازش Zero-Copy Streaming روی باقیمانده داده ورودی

    const uint8_t* currentDataPtr = data + data_pos;
    size_t currentLen = len - data_pos;

    size_t pos = 0;

    while (pos + HEADER_SIZE <= currentLen) {

        // پیش‌بینی طول فریم برای بررسی کامل بودن آن
        uint32_t netLength;
        memcpy(&netLength, currentDataPtr + pos + 8, 4);
        uint32_t length = ntohl(netLength);

        uint8_t type_val = currentDataPtr[pos + 1];

        if (type_val == static_cast<uint8_t>(FrameType::Data)) {
            if (pos + HEADER_SIZE + length > currentLen) {
                break; // داده ناقص است، باید Fragment را ذخیره کنیم.
            }
        }

        // --- تجزیه هدر ---

        uint8_t ver = currentDataPtr[pos++];
        if (ver != YAMUX_VERSION) {
            shutdown(GoAwayCode::ProtocolError);
            break;
        }

        auto type = static_cast<FrameType>(currentDataPtr[pos++]);

        uint16_t netFlags;
        memcpy(&netFlags, currentDataPtr + pos, 2); pos += 2;
        uint16_t flags = ntohs(netFlags);

        uint32_t netStreamId;
        memcpy(&netStreamId, currentDataPtr + pos, 4); pos += 4;
        uint32_t streamId = ntohl(netStreamId);

        pos += 4; // پرش از Length/Delta

        const uint8_t* payload = nullptr;

        if (type == FrameType::Data) {
            if (length > 0) {
                // !!! Zero-Copy !!!: پوینتر payload مستقیماً به currentDataPtr اشاره می‌کند.
                payload = currentDataPtr + pos;
            }
            pos += length;
        }

        // فراخوانی Handler
        switch (type) {
        case FrameType::Data:
            handleDataFrame(streamId, payload, length, flags);
            break;
        case FrameType::WindowUpdate:
            handleWindowUpdateFrame(streamId, length,flags);
            break;
        case FrameType::Ping:
            handlePingFrame(streamId, length, flags);
            break;
        case FrameType::GoAway:
            handleGoAwayFrame(length);
            break;
        default:
            shutdown(GoAwayCode::ProtocolError);
            return;
        }
    }

    // ۳. ذخیره فریم ناقص نهایی
    size_t remaining = currentLen - pos;

    if (remaining > 0) {
        // یک فریم ناقص مانده است. باید در m_parseBuffer ذخیره شود.

        if (!m_parseBuffer) {
            if (!initParseBuffer()) {
                shutdown(GoAwayCode::InternalError);
                return;
            }
        }

        if (remaining > m_parseBufferCapacity) {
            // این نباید در حالت نرمال اتفاق بیفتد مگر بافری بزرگتر از PARSE_BUFFER_SIZE دریافت شود.
            shutdown(GoAwayCode::InternalError);
            return;
        }

        // کپی باقیمانده به m_parseBuffer
        memcpy(m_parseBuffer, currentDataPtr + pos, remaining);
        m_parseBufferLength = remaining;
    }
    // اگر remaining == 0، m_parseBufferLength در ابتدای تابع ۰ بود.
}
*/

void MultiplexedTunnel::onReceiveData(const uint8_t* data, size_t len) {

    size_t data_pos = 0;

    // 1. تکمیل فریم ناقص قبلی
    if (m_parseBufferLength > 0) {
        if (!initParseBuffer()) {
            shutdown(GoAwayCode::InternalError);
            return;
        }

        // اگر تکمیل و پردازش فریم Fragment شده موفقیت‌آمیز نبود، تابع به دلیل نیاز به داده بیشتر برمی‌گردد.
        if (!processFragmentedFrame(data, len, data_pos)) {
            return;
        }
    }

    // 2. پردازش Zero-Copy Streaming روی باقیمانده داده ورودی
    processZeroCopyFrames(data, len, data_pos);

    // 3. ذخیره فریم ناقص نهایی
    storeRemainingData(data, len, data_pos);
}

bool MultiplexedTunnel::processFrame(const uint8_t* frameStart, size_t totalFrameSize, uint32_t payloadLength) {
    size_t pos = 0;

    uint8_t ver = frameStart[pos++];
    if (ver != YAMUX_VERSION) {
        shutdown(GoAwayCode::ProtocolError);
        return false;
    }

    auto type = static_cast<FrameType>(frameStart[pos++]);

    uint16_t netFlags;
    memcpy(&netFlags, frameStart + pos, 2); pos += 2;
    uint16_t flags = ntohs(netFlags);

    uint32_t netStreamId;
    memcpy(&netStreamId, frameStart + pos, 4); pos += 4;
    uint32_t streamId = ntohl(netStreamId);

    pos += 4; // پرش از Length/Delta

    const uint8_t* payload = nullptr;

    if (type == FrameType::Data) {
        if (payloadLength > 0) {
            payload = frameStart + pos;
        }
        // pos += payloadLength; // نیازی به افزایش در اینجا نیست
    }

    // فراخوانی Handler
    switch (type) {
    case FrameType::Data:
        handleDataFrame(streamId, payload, payloadLength, flags);
        break;
    case FrameType::WindowUpdate:
        handleWindowUpdateFrame(streamId, payloadLength, flags); // Length is Delta
        break;
    case FrameType::Ping:
        handlePingFrame(streamId, payloadLength, flags); // Length is Value
        break;
    case FrameType::GoAway:
        handleGoAwayFrame(payloadLength); // Length is Code
        break;
    default:
        shutdown(GoAwayCode::ProtocolError);
        return false;
    }
    return true;
}

size_t MultiplexedTunnel::processFragmentedFrame(const uint8_t* data, size_t len, size_t& data_pos) {

    // 1. تکمیل هدر (در صورت نیاز)
    if (m_parseBufferLength < HEADER_SIZE) {
        size_t neededForHeader = HEADER_SIZE - m_parseBufferLength;
        size_t copyLen = std::min(neededForHeader, len - data_pos);

        memcpy(m_parseBuffer + m_parseBufferLength, data + data_pos, copyLen);
        m_parseBufferLength += copyLen;
        data_pos += copyLen;

        if (m_parseBufferLength < HEADER_SIZE) {
            return false; // هدر هنوز کامل نشده
        }
    }

    // 2. تعیین طول فریم کامل
    uint32_t netLength;
    memcpy(&netLength, m_parseBuffer + 8, 4);
    uint32_t payloadLength = ntohl(netLength);
    uint8_t type_val = m_parseBuffer[1];

    size_t totalFrameSize = HEADER_SIZE;
    if (type_val == static_cast<uint8_t>(FrameType::Data)) {
        totalFrameSize += payloadLength;
    }

    // 3. بررسی ظرفیت (با فرض ** عدم تغییر سایز بافر در حال حاضر**)
    if (m_parseBufferCapacity < totalFrameSize) {
        if (totalFrameSize > MAX_ALLOWED_FRAME_SIZE) {
            printf("Protocol Error: Frame size %zu exceeds max allowed limit.\n", totalFrameSize);
            shutdown(GoAwayCode::ProtocolError);
            return false;
        }

        if (!resizeParseBuffer(totalFrameSize)) {
            shutdown(GoAwayCode::InternalError);
            return false;
        }
    }

    // 4. تکمیل Payload (در صورت نیاز)
    size_t neededForFrame = totalFrameSize - m_parseBufferLength;
    if (neededForFrame > 0) {
        size_t remainingInput = len - data_pos;
        size_t copyLen = std::min(neededForFrame, remainingInput);

        memcpy(m_parseBuffer + m_parseBufferLength, data + data_pos, copyLen);
        m_parseBufferLength += copyLen;
        data_pos += copyLen;

        if (m_parseBufferLength < totalFrameSize) {
            return false; // فریم کامل نشد
        }
    }

    // 5. پردازش فریم کامل شده
    bool success = processFrame(m_parseBuffer, totalFrameSize, payloadLength);
    m_parseBufferLength = 0; // خالی کردن بافر Fragment

    return success;
}

void MultiplexedTunnel::processZeroCopyFrames(const uint8_t* data, size_t len, size_t& data_pos) {
    const uint8_t* currentDataPtr = data + data_pos;
    size_t currentLen = len - data_pos;
    size_t pos = 0;

    while (pos + HEADER_SIZE <= currentLen) {

        // پیش‌بینی طول فریم برای بررسی کامل بودن آن
        uint32_t netLength;
        memcpy(&netLength, currentDataPtr + pos + 8, 4);
        uint32_t payloadLength = ntohl(netLength);

        uint8_t type_val = currentDataPtr[pos + 1];

        size_t totalFrameSize = HEADER_SIZE;
        if (type_val == static_cast<uint8_t>(FrameType::Data)) {
            totalFrameSize += payloadLength;
        }

        if (pos + totalFrameSize > currentLen) {
            break; // داده ناقص است، برای ذخیره‌سازی Fragment آماده می‌شویم.
        }

        // پردازش فریم با Zero-Copy
        if (!processFrame(currentDataPtr + pos, totalFrameSize, payloadLength)) {
            return; // خطای پروتکلی، پردازش متوقف می‌شود.
        }

        pos += totalFrameSize; // حرکت به فریم بعدی
    }
    data_pos += pos; // به‌روزرسانی پوزیشن خوانده شده از بافر ورودی
}

void MultiplexedTunnel::storeRemainingData(const uint8_t* data, size_t len, size_t data_pos) {
    size_t remaining = len - data_pos;

    if (remaining > 0) {
        // یک فریم ناقص مانده است. باید در m_parseBuffer ذخیره شود.
        if (!m_parseBuffer) {
            if (!initParseBuffer()) {
                shutdown(GoAwayCode::InternalError);
                return;
            }
        }

        if (remaining > m_parseBufferCapacity) {
            // این حالت نشان‌دهنده دریافت یک بلوک داده بزرگتر از انتظار است.
            shutdown(GoAwayCode::InternalError);
            return;
        }

        // کپی باقیمانده به m_parseBuffer
        memcpy(m_parseBuffer, data + data_pos, remaining);
        m_parseBufferLength = remaining;
    }
}

void MultiplexedTunnel::handleNewStream(uint32_t streamId, uint16_t flags)
{
    if (!(flags & FrameFlags::SYN))
        return; // Data for non-existent stream without SYN, ignore.

    // New remote stream
    bool isRemoteIdValid = m_isClient ? (streamId % 2 == 0) : (streamId % 2 == 1);
    if (streamId == 0 || !isRemoteIdValid)
        return;

    auto& s = m_streams[streamId];
    s.id = streamId;

    if (m_onNewStream) {
        //callback
        m_onNewStream(m_newStreamArg, streamId, &s);
    }

    // Send ACK if not RST
    if (!(flags & FrameFlags::RST)) {
        sendFrame(FrameType::WindowUpdate, FrameFlags::ACK, streamId, 0);
    }

}


void MultiplexedTunnel::handleDataFrame(uint32_t streamId, const uint8_t* payload, uint32_t length, uint16_t flags) {
    auto it = m_streams.find(streamId);
    bool isNew = (it == m_streams.end());

    if (isNew) {
        handleNewStream(streamId, flags);

        it = m_streams.find(streamId); // Re-find iterator
        if (it == m_streams.end())
            return;
    }

    auto& s = it->second;
    if (s.remoteClosed)
        return;

    if (length > 0) {
        if (length > s.recvWindow) {
            // Flow Control Error
            // printf("Flow Control Error: Recv Window Exceeded on Stream %u (Len %u > Window %u)\n", streamId, length, s.recvWindow);
            closeStream(streamId, true); // RST the stream
            return;
        }

        s.recvWindow -= length;
        //printf("handleDataFrame stream: [%u] new sendWindow : [%d] recvWindow : [%d] \n", streamId, s.sendWindow, s.recvWindow);
        if (s.onData){
            s.onData(s.arg, streamId, payload, length);

            //Window Update
            trySendWindowUpdate(streamId, length);
        }

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

// FIX
void MultiplexedTunnel::handleWindowUpdateFrame(uint32_t streamId, uint32_t length, uint16_t flags) {
    if (streamId == 0)
        return; // Must not be for Session

    auto it = m_streams.find(streamId);
    bool isNew = (it == m_streams.end());

    if (isNew) {
        handleNewStream(streamId, flags);
    }

    if (length > 0) {
        uint32_t delta = length; // Delta is in the length field in Yamux

        //auto it = m_streams.find(streamId);
        if (it == m_streams.end())
            return;

        auto& s = it->second;
        s.sendWindow += delta;
        //printf("get Window update for stream: [%u] new sendWindow : [%d] recvWindow : [%d] \n", streamId, s.sendWindow, s.recvWindow);
        processPending(streamId);
    }
}


// FIX: handlePingFrame (حفظ منطق قبلی)
void MultiplexedTunnel::handlePingFrame(uint32_t streamId, uint32_t length, uint16_t flags) {
    if (streamId != 0) {
        shutdown(GoAwayCode::ProtocolError);
        return;
    }

    if (!(flags & FrameFlags::ACK)) {
        sendPing(true, length);
        //printf(">>>>>>>>>>>>>>>>>> replay ping\n");
    }
}

// FIX: handleGoAwayFrame (حفظ منطق قبلی)
void MultiplexedTunnel::handleGoAwayFrame(uint32_t code) {
    printf("GoAway received. Code: %u\n", code);

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

    close(true);
}

void MultiplexedTunnel::processPending(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    auto& s = it->second;
    // FIX: چک کردن null بودن SendQueue
    if (!s.pendingData)
        return;

    while (!s.pendingData->empty() && s.sendWindow > 0) {
        //printf("processPending sendWindow is 0, stream %u. Pending size: %zu\n", streamId, s.pendingData->count());

        SendQueue::Buffer& buf = s.pendingData->front();
        uint32_t chunk = std::min<uint32_t>((uint32_t)buf.len, s.sendWindow);

        sendFrame(FrameType::Data, FrameFlags(0), streamId, chunk, (const uint8_t*)buf.data);
        s.sendWindow -= chunk;

        if (chunk < buf.len) {
            // Partial send
            memmove(buf.data, (char*)buf.data + chunk, buf.len - chunk);
            buf.len -= chunk;
            break;
        }
        // Entire buffer sent
        s.pendingData->pop_front();
    }
}
