//server
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>

// Global callbacks برای سرور (برای streamهای جدید)
void onServerStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::string received_message((const char*)data, len);
   // std::cout << "Server: Received on stream " << streamId << ": " << received_message << std::endl;

    // Echo
    tunnel->sendToStream(streamId, data, len);

}

void onServerStreamClose(void* arg, uint32_t streamId) {
    std::cout << "onServerStreamClose " << streamId << " closed." << std::endl;
}

void onServerNewStream(void* arg, uint32_t streamId, MultiplexedTunnel::Stream* newStream) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::cout << "Server: New incoming stream " << streamId << " accepted." << std::endl;

    // callbacks
    newStream->onData = onServerStreamData;
    newStream->onClose = onServerStreamClose;
    newStream->arg = tunnel;

    //
    std::string message = "Message # hello from c++ server!";
    //tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());
    //tunnel->sendPing(true, 8855);

}

// Factory برای accept
TCPSocket* acceptCallback(void* ctx) {
    MultiplexedTunnel* tunnel = new MultiplexedTunnel(false);
    tunnel->setOnNewStream(onServerNewStream, tunnel);


    return tunnel;
}

int main3() {
    Server srv(1024, 1);
    srv.setUseGarbageCollector(false);
    srv.AddNewListener(6060, "0.0.0.0");
    srv.setOnAccepted(acceptCallback, &srv);

    if (!srv.start()) {
        printf("start failed\n");
        return 1;
    }

    printf("Server running on port 6060. Press enter to stop.\n");
    getchar();
    return 0;
}
