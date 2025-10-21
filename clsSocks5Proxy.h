#ifndef CLSSOCKS5PROXY_H
#define CLSSOCKS5PROXY_H

#include "clsTCPSocket.h"
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
    std::vector<uint8_t> clientBuffer;  // Buffer for incoming client data (high-performance, resizable)
    std::vector<uint8_t> connectorBuffer;  // Buffer for pending data to connector if not yet connected
    Socks5State state;
    bool supportsNoAuth;  // From greeting

    Socks5Proxy() : state(Socks5State::Greeting), supportsNoAuth(false) {

        connector.setOnData(&Socks5Proxy::onConnectorReceiveDataTrampoline, this);
        connector.setOnClose(&Socks5Proxy::onConnectorCloseTrampoline, this);
        connector.setOnConnectFailed(&Socks5Proxy::onConnectFailedTrampoline, this);
        connector.setOnConnecting(&Socks5Proxy::onConnectingTrampoline, this);
        connector.setOnConnected(&Socks5Proxy::onConnectedTrampoline, this);

        acceptor.setOnData(&Socks5Proxy::onAcceptorReceiveDataTrampoline, this);
        acceptor.setOnClose(&Socks5Proxy::onAcceptorCloseTrampoline, this);
        acceptor.setOnAccepted(&Socks5Proxy::onAcceptedTrampoline, this);
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

private:
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

    // Implementation methods
    void OnAccepted() {
        printf("onAccepted() fd=%d\n", acceptor.fd());
        connector.setReactor(acceptor.getReactor());

        // Example: connect to upstream
        // connector.connectTo("51.195.150.84", 80);
        //connector.connectTo("cl.mojz.ir", 443);
        // connector.connectTo("192.168.1.10", 9000);
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
        if (!connectorBuffer.empty()) {
            connector.send(connectorBuffer.data(), connectorBuffer.size());
            connectorBuffer.clear();
        }
    }

    void OnConnectFailed() {
        printf("onConnectFailed() fd=%d\n", connector.fd());
        state = Socks5State::Error;
        SendErrorReply(0x04);  // Host unreachable
        acceptor.close();
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
        clientBuffer.insert(clientBuffer.end(), data, data + length);

        // Process based on state (state machine for fragmentation)
        while (!clientBuffer.empty()) {
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

    bool ProcessGreeting() {
        if (clientBuffer.size() < 3)
            return false;  // Min: VER + NMETHODS + at least 1 method

        if (clientBuffer[0] != 0x05) {
            state = Socks5State::Error;
            SendErrorReply(0xFF);  // No acceptable methods
            acceptor.close();
            return false;
        }

        uint8_t nmethods = clientBuffer[1];
        if (clientBuffer.size() < 2 + nmethods)
            return false;  // Wait for full methods

        // Check if no-auth (0x00) is supported
        supportsNoAuth = false;
        for (size_t i = 0; i < nmethods; ++i) {
            if (clientBuffer[2 + i] == 0x00) {
                supportsNoAuth = true;
                break;
            }
        }

        // Consume the greeting
        clientBuffer.erase(clientBuffer.begin(), clientBuffer.begin() + 2 + nmethods);

        // Send method selection
        uint8_t response[2] = {0x05, supportsNoAuth ? 0x00 : 0xFF};
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
        if (clientBuffer.size() < 4) return false;  // Min: VER + CMD + RSV + ATYP

        if (clientBuffer[0] != 0x05) {
            state = Socks5State::Error;
            SendErrorReply(0x01);  // General failure
            acceptor.close();
            return false;
        }

        uint8_t cmd = clientBuffer[1];
        if (cmd != 0x01) {  // Only support CONNECT
            state = Socks5State::Error;
            SendErrorReply(0x07);  // Command not supported
            acceptor.close();
            return false;
        }

        uint8_t atyp = clientBuffer[3];
        size_t addrLen = 0;
        if (atyp == 0x01) addrLen = 4;  // IPv4
        else if (atyp == 0x03) {  // Domain
            if (clientBuffer.size() < 5) return false;
            addrLen = clientBuffer[4] + 1;  // Len byte + domain
        } else if (atyp == 0x04) addrLen = 16;  // IPv6
        else {
            state = Socks5State::Error;
            SendErrorReply(0x08);  // Address type not supported
            acceptor.close();
            return false;
        }

        size_t totalLen = 4 + addrLen + 2;  // + port
        if (clientBuffer.size() < totalLen) return false;  // Wait for full request

        // Extract address and port
        std::string host;
        uint16_t port = 0;
        size_t offset = 4;

        if (atyp == 0x01) {  // IPv4
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, clientBuffer.data() + offset, ipStr, INET_ADDRSTRLEN);
            host = ipStr;
            offset += 4;
        } else if (atyp == 0x03) {  // Domain
            uint8_t domainLen = clientBuffer[offset];
            offset++;
            host.assign(reinterpret_cast<const char*>(clientBuffer.data() + offset), domainLen);
            offset += domainLen;
        } else if (atyp == 0x04) {  // IPv6
            char ipStr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, clientBuffer.data() + offset, ipStr, INET6_ADDRSTRLEN);
            host = ipStr;
            offset += 16;
        }

        port = (clientBuffer[offset] << 8) | clientBuffer[offset + 1];

        // Consume the request
        clientBuffer.erase(clientBuffer.begin(), clientBuffer.begin() + totalLen);

        // Connect to target
        if (!connector.connectTo(host.c_str(), port)) {
            state = Socks5State::Error;
            SendErrorReply(0x01);  // General failure
            acceptor.close();
            return false;
        }

        state = Socks5State::Connecting;
        return true;
    }

    void ForwardToConnector() {
        if (connector.getStatus() == TCPSocket::Connected && state == Socks5State::Connected) {
            //printf("connector::send length[%zu]\n", clientBuffer.size());
            connector.send(clientBuffer.data(), clientBuffer.size());
        } else {
            // Buffer if not connected yet
            connectorBuffer.insert(connectorBuffer.end(), clientBuffer.begin(), clientBuffer.end());
        }
        clientBuffer.clear();
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
