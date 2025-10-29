#include "clsEpollReactor.h"
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>

// Global callbacks برای سرور (برای streamهای جدید)
void onServerStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    std::cout << "Server: Received on stream " << streamId << ": " << std::string((const char*)data, len) << std::endl;
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);
    const char* response = "Hello from server!";
    tunnel->sendToStream(streamId, (const uint8_t*)response, strlen(response));
}

void onServerStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Server: Stream " << streamId << " closed." << std::endl;
}

// Factory برای accept
TCPSocket* acceptCallback(void* ctx) {
    MultiplexedTunnel* tunnel = new MultiplexedTunnel(false);  // سرور
    uint32_t streamId = tunnel->openStream(onServerStreamData, onServerStreamClose, tunnel);
    const char* message = "Hello from server!";
    //tunnel->sendToStream(streamId, (const uint8_t*)message, strlen(message));

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
