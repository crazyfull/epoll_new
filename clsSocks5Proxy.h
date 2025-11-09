#ifndef CLSSOCKS5PROXY_H
#define CLSSOCKS5PROXY_H

#include "clsTCPSocket.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Assuming TCPSocket is defined in "clsTCPSocket.h" with methods like send, fd, getStatus, setOnData, etc.
// This class is designed to be complete, high-performance, and handle fragmented packets via a state machine.
// No inheritance; using composition with TCPSocket instances for acceptor and connector.

enum class Socks5State {
    Greeting,          // Waiting for client greeting
    MethodSelection,   // Sending method selection response
    Request,           // Waiting for client request
    Connecting,        // Connecting to target
    Connected,         // Forwarding data
    Error              // Error state
};

class Socks5Proxy {
public:
    TCPSocket acceptor;
    TCPSocket connector;
    std::vector<uint8_t> m_clientBuffer;  // Buffer for incoming client data (high-performance, resizable)
    std::vector<uint8_t> m_connectorBuffer;  // Buffer for pending data to connector if not yet connected
    Socks5State state;
    bool supportsNoAuth;  // From greeting

    Socks5Proxy() : state(Socks5State::Greeting), supportsNoAuth(false) {

        connector.setOnData(&Socks5Proxy::onConnectorReceiveDataTrampoline, this);
        connector.setOnClose(&Socks5Proxy::onConnectorCloseTrampoline, this);
        connector.setOnConnectFailed(&Socks5Proxy::onConnectFailedTrampoline, this);
        connector.setOnConnecting(&Socks5Proxy::onConnectingTrampoline, this);
        connector.setOnConnected(&Socks5Proxy::onConnectedTrampoline, this);
        connector.setOnPause(&Socks5Proxy::onConnectorPauseTrampoline, this);
        connector.setOnResume(&Socks5Proxy::onConnectorResumeTrampoline, this);

        acceptor.setOnData(&Socks5Proxy::onAcceptorReceiveDataTrampoline, this);
        acceptor.setOnClose(&Socks5Proxy::onAcceptorCloseTrampoline, this);
        acceptor.setOnAccepted(&Socks5Proxy::onAcceptedTrampoline, this);
        acceptor.setOnPause(&Socks5Proxy::onAcceptorPauseTrampoline, this);
        acceptor.setOnResume(&Socks5Proxy::onAcceptorResumeTrampoline, this);
    }

    ~Socks5Proxy() {
        printf("~Socks5Proxy()\n");
    }

    TCPSocket* getSocketBase(){
        return acceptor.getPointer();
    }

    // Call this when a new connection is accepted (e.g., from external accept loop)
    /*
    void initAccepted(int clientFd, EpollReactor* reactor) {

        if (!acceptor.adoptFd(clientFd)) {
            printf("Failed to adopt fd=%d\n", clientFd);
            return;
        }

        acceptor.setReactor(reactor);
        state = Socks5State::Greeting;
        clientBuffer.clear();
        connectorBuffer.clear();
        supportsNoAuth = false;
        // Trigger onAccepted logic
        onAccepted();
    }
*/

public:
    bool isPropagatingPause = false;
    bool isPropagatingResume = false;

    // Static trampolines for callbacks
    static void onConnectorReceiveDataTrampoline(void* p, const uint8_t* data, size_t length) {
        static_cast<Socks5Proxy*>(p)->OnConnectorReceiveData(data, length);
    }

    static void onConnectorCloseTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnectorClose();
    }

    static void onConnectFailedTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnectFailed();
    }

    static void onConnectingTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnecting();
    }

    static void onConnectedTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnected();
    }

    static void onAcceptorReceiveDataTrampoline(void* p, const uint8_t* data, size_t length) {
        static_cast<Socks5Proxy*>(p)->OnAcceptorReceiveData(data, length);
    }

    static void onAcceptorCloseTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnAcceptorClose();
    }

    static void onAcceptedTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnAccepted();
    }

    static void onAcceptorPauseTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnAcceptorPause();
    }

    static void onAcceptorResumeTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnAcceptorResume();
    }

    static void onConnectorPauseTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnectorPause();
    }

    static void onConnectorResumeTrampoline(void* p) {
        static_cast<Socks5Proxy*>(p)->OnConnectorResume();
    }

    // Implementation methods
    void OnAccepted() {
        printf("onAccepted() fd=%d\n", acceptor.fd());
        connector.setReactor(acceptor.getReactor());
    }

    void OnAcceptorClose() {
        printf("onAcceptorClose() fd=%d\n", acceptor.fd());
        connector.close();
    }

    void OnConnecting() {
        printf("onConnecting() fd=%d\n", connector.fd());
    }

    void OnConnected() {
        printf("onConnected() fd=%d\n", connector.fd());
        state = Socks5State::Connected;

        // Send success reply to client
        std::vector<uint8_t> reply(10, 0);  // Max size for reply
        reply[0] = 0x05;  // SOCKS5
        reply[1] = 0x00;  // Succeeded
        reply[2] = 0x00;  // Reserved
        reply[3] = 0x01;  // Assume IPv4 for bound address (can be dummy)

        // Bound address and port can be 0.0.0.0:0 for simplicity
        acceptor.send(reply.data(), 10);

        // Flush any buffered data to connector
        if (!m_connectorBuffer.empty()) {
            connector.send(m_connectorBuffer.data(), m_connectorBuffer.size());
            m_connectorBuffer.clear();
        }
    }

    void OnConnectFailed() {
        printf("onConnectFailed() fd=%d\n", connector.fd());
        state = Socks5State::Error;
        SendErrorReply(0x04);  // Host unreachable
        acceptor.close(true);
       // connector.close(true);
    }

    void OnConnectorClose() {
        printf("onConnectorClose() fd=%d\n", connector.fd());
        acceptor.close();
    }

    void OnConnectorReceiveData(const uint8_t* data, size_t length) {
        if (acceptor.getStatus() == TCPSocket::Connected && state == Socks5State::Connected) {
            //printf("acceptor::send length[%zu]\n", length);
            acceptor.send(data, length);
        } else {
            printf("🔴 acceptor not connected or invalid state, dropping data\n");
        }
    }

    void OnAcceptorReceiveData(const uint8_t* data, size_t length) {



        // Append to buffer to handle fragments
        m_clientBuffer.insert(m_clientBuffer.end(), data, data + length);
/*
        std::cout << "OnAcceptorReceiveData() Buffer (as numbers): ";
        for (uint8_t byte : m_clientBuffer) {
            std::cout << static_cast<unsigned int>(byte) << " ";
        }
        std::cout << std::endl;
*/
        // Process based on state (state machine for fragmentation)
        while (!m_clientBuffer.empty()) {
            if (state == Socks5State::Greeting) {
                if (!ProcessGreeting())
                    break;

            } else if (state == Socks5State::Request) {
                if (!ProcessRequest())
                    break;

            } else if (state == Socks5State::Connecting || state == Socks5State::Connected) {
                ForwardToConnector();
                break;  // All data forwarded
            } else {
                // Error state: drop
                break;
            }
        }
    }


    void OnAcceptorPause() {
        if (isPropagatingPause)
            return;
        isPropagatingPause = true;
        printf("Acceptor paused, propagating to connector\n");
        connector.pause_reading();
        isPropagatingPause = false;
    }

    void OnAcceptorResume() {
        if (isPropagatingResume)
            return;
        isPropagatingResume = true;
        printf("Acceptor resumed, propagating to connector\n");
        connector.resume_reading();
        isPropagatingResume = false;
    }

    void OnConnectorPause() {
        if (isPropagatingPause)
            return;
        isPropagatingPause = true;
        printf("Connector paused, propagating to acceptor\n");
        acceptor.pause_reading();
        isPropagatingPause = false;
    }

    void OnConnectorResume() {
        if (isPropagatingResume)
            return;
        isPropagatingResume = true;
        printf("Connector resumed, propagating to acceptor\n");
        acceptor.resume_reading();
        isPropagatingResume = false;
    }

    bool ProcessGreeting() {
        /*
        std::cout << "ProcessGreeting() Buffer (as numbers): ";
        for (uint8_t byte : m_clientBuffer) {
            std::cout << static_cast<unsigned int>(byte) << " ";
        }
        std::cout << std::endl;
        */

        if (m_clientBuffer.size() < 3)
            return false;  // Min: VER + NMETHODS + at least 1 method

        if (m_clientBuffer[0] != 0x05) {
            state = Socks5State::Error;
            SendErrorReply(0xFF);  // No acceptable methods
            acceptor.close();
            return false;
        }

        uint8_t nmethods = m_clientBuffer[1];
        if (m_clientBuffer.size() < 2 + nmethods)
            return false;  // Wait for full methods

        // Check if no-auth (0x00) is supported
        supportsNoAuth = false;
        for (size_t i = 0; i < nmethods; ++i) {
            if (m_clientBuffer[2 + i] == 0x00) {
                supportsNoAuth = true;
                break;
            }
        }

        // Consume the greeting
        m_clientBuffer.erase(m_clientBuffer.begin(), m_clientBuffer.begin() + 2 + nmethods);

        // Send method selection
        uint8_t response[2] = {0x05, static_cast<uint8_t>(supportsNoAuth ? 0x00 : 0xFF)};
        //uint8_t response[2] = {0x05, supportsNoAuth ? 0x00 : 0xFF};
        acceptor.send(response, 2);

        if (!supportsNoAuth) {
            state = Socks5State::Error;
            acceptor.close();
            return false;
        }

        state = Socks5State::Request;
        return true;
    }

    bool ProcessRequest() {
        /*
        std::cout << "ProcessRequest() Buffer (as numbers): ";
        for (uint8_t byte : m_clientBuffer) {
            std::cout << static_cast<unsigned int>(byte) << " ";
        }
        std::cout << std::endl;
*/

        if (m_clientBuffer.size() < 4)
            return false;  // Min: VER + CMD + RSV + ATYP

        if (m_clientBuffer[0] != 0x05) {
            state = Socks5State::Error;
            SendErrorReply(0x01);  // General failure
            acceptor.close();
            return false;
        }

        uint8_t cmd = m_clientBuffer[1];
        if (cmd != 0x01) {  // Only support CONNECT
            state = Socks5State::Error;
            SendErrorReply(0x07);  // Command not supported
            acceptor.close();
            return false;
        }

        uint8_t atyp = m_clientBuffer[3];
        size_t addrLen = 0;
        if (atyp == 0x01) addrLen = 4;  // IPv4
        else if (atyp == 0x03) {  // Domain
            if (m_clientBuffer.size() < 5) return false;
            addrLen = m_clientBuffer[4] + 1;  // Len byte + domain
        } else if (atyp == 0x04) addrLen = 16;  // IPv6
        else {
            state = Socks5State::Error;
            SendErrorReply(0x08);  // Address type not supported
            acceptor.close();
            return false;
        }

        size_t totalLen = 4 + addrLen + 2;  // + port
        if (m_clientBuffer.size() < totalLen) return false;  // Wait for full request

        // Extract address and port
        std::string host;
        uint16_t port = 0;
        size_t offset = 4;

        if (atyp == 0x01) {  // IPv4
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, m_clientBuffer.data() + offset, ipStr, INET_ADDRSTRLEN);
            host = ipStr;
            offset += 4;
        } else if (atyp == 0x03) {  // Domain
            uint8_t domainLen = m_clientBuffer[offset];
            offset++;
            host.assign(reinterpret_cast<const char*>(m_clientBuffer.data() + offset), domainLen);
            offset += domainLen;
        } else if (atyp == 0x04) {  // IPv6
            char ipStr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, m_clientBuffer.data() + offset, ipStr, INET6_ADDRSTRLEN);
            host = ipStr;
            offset += 16;
        }

        port = (m_clientBuffer[offset] << 8) | m_clientBuffer[offset + 1];

        // Consume the request
        m_clientBuffer.erase(m_clientBuffer.begin(), m_clientBuffer.begin() + totalLen);

        // Connect to target
        printf("connectTo: [%s] port: [%d]\n", host.c_str(), port);
        if (!connector.connectTo(host.c_str(), port)) {
            state = Socks5State::Error;
            SendErrorReply(0x01);  // General failure
            acceptor.close(true);
            return false;
        }

        state = Socks5State::Connecting;
        return true;
    }

    void ForwardToConnector() {
        if (connector.getStatus() == TCPSocket::Connected && state == Socks5State::Connected) {
            //printf("connector::send length[%zu]\n", clientBuffer.size());
            connector.send(m_clientBuffer.data(), m_clientBuffer.size()); //inja bayad pause rresume anjam beshe
        } else {
            // Buffer if not connected yet
            m_connectorBuffer.insert(m_connectorBuffer.end(), m_clientBuffer.begin(), m_clientBuffer.end());
        }
        m_clientBuffer.clear();
    }

    void SendErrorReply(uint8_t errorCode) {
        std::vector<uint8_t> reply(10, 0);
        reply[0] = 0x05;  // SOCKS5
        reply[1] = errorCode;
        reply[2] = 0x00;  // Reserved
        reply[3] = 0x01;  // IPv4
        // Rest zero
        acceptor.send(reply.data(), 10);
    }
};

#endif // CLSSOCKS5PROXY_H
