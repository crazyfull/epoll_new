#include "clsEpollReactor.h"
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>
uint32_t count = 0;

void onClientStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    std::cout << "Client: Received on stream " << streamId << ": " << std::string((const char*)data, len) << std::endl;

    /*
    count++;
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);
    std::string message = "Message #" + std::to_string(count) + " from client!";
    tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());
*/

}

void onClientStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Client: Stream " << streamId << " closed." << std::endl;
}

//
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

    for(;;){
        count++;
        //usleep(1*200);
        //getchar();
        //const char* message = "Hello from clientttt!!!!!";
        std::string message = "Message #" + std::to_string(count) + " from client!";
        tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());
        if(count > 5*1000){
            break;
        }
    }
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
    if (tunnel->connectTo("127.0.0.1", 6060)) { //51.195.150.84  127.0.0.1
        std::cout << "Connecting..." << std::endl;
    } else {
        std::cout << "Connect failed." << std::endl;
    }

  /*
    getchar();

    uint32_t streamId = 1;
    std::string message = "Message # Z from client!";
    tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());



    getchar();


*/
    printf("Press enter to stop.-------------------------------------------------------------------------------------------------------\n");
    getchar();
    delete tunnel;
    return 0;
}
