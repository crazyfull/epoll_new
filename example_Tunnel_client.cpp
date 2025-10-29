#include "clsEpollReactor.h"
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>

void onClientStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    std::cout << "Client: Received on stream " << streamId << ": " << std::string((const char*)data, len) << std::endl;
}

void onClientStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Client: Stream " << streamId << " closed." << std::endl;
}

// Callback برای connected (برای منتظر connect موفق)
void onClientConnected(void* arg) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);
    std::cout << "Client connected successfully." << std::endl;

    // حالا stream باز کن
    uint32_t streamId = tunnel->openStream(onClientStreamData, onClientStreamClose, tunnel);
    const char* message = "Hello from client!";
    tunnel->sendToStream(streamId, (const uint8_t*)message, strlen(message));

    // برای تست، بعد ۲ ثانیه stream رو ببند (می‌تونی loop بذاری)
    // اما در تولید، از timer استفاده کن
    // std::this_thread::sleep_for(std::chrono::seconds(2)); // اگر نیاز به sleep داری
    // tunnel->closeStream(streamId);
}


int main() {
    Server srv(1024, 1);
    srv.setUseGarbageCollector(false);
    if (!srv.start()) {
        printf("start failed\n");
        return 1;
    }

    MultiplexedTunnel* tunnel = new MultiplexedTunnel(true);  // کلاینت mode
    tunnel->setReactor(srv.getRoundRobinShard());  // مثل کد تو
    tunnel->setOnConnected(onClientConnected, tunnel);  // callback برای connect موفق
    usleep(10*1000);
    if (tunnel->connectTo("51.195.150.84", 6060)) {
        std::cout << "Connecting..." << std::endl;
    } else {
        std::cout << "Connect failed." << std::endl;
    }

    /**/
    uint32_t streamId = 1;
    int i = 0;
    for(;;){
        i++;
        usleep(1*100);
        //getchar();
        const char* message = "Hello from client!!!!!";
        tunnel->sendToStream(streamId, (const uint8_t*)message, strlen(message));
        if(i > 15*1000){
            break;
        }
    }

    printf("Press enter to stop.\n");
    getchar();
    delete tunnel;
    return 0;
}
