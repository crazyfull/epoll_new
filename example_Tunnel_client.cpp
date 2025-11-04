//client
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include "clsTimer.h"
#include <iostream>
#include <unistd.h> // for usleep
#include <cstring>  // for strlen

uint32_t STREAM_ID = 0; // Global to store the stream ID

//MultiplexedTunnel* gTunnel;
int gCounter = 0;

void onClientStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::string input((const char*)data, len);
    printf("onClientStreamData: [%u] [%s]\n\n", streamId, input.c_str());

}

void onClientStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Client: Stream " << streamId << " closed." << std::endl;
}

void sendInitialData(MultiplexedTunnel* tunnel, uint32_t streamId) {

    usleep(100 * 1000);


    int count = 0;
    for(;;){
        count++;
        std::string message = "Message #" + std::to_string(count) + " from client!";
        tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());

        if(count > 120*1000){//120*1000
            break;
        }
    }

    // یک Ping برای تست ارسال کن
    tunnel->sendPing(false, 12345);
}

// این تابع پس از اتصال اولیه TCP فراخوانی می‌شود
void onClientConnected(void* arg) {



    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::cout << "Client connected successfully. Opening Stream..." << std::endl;

    //
    STREAM_ID = tunnel->openStream(onClientStreamData, onClientStreamClose, tunnel);

    //usleep(100 * 1000); // 100ms delay to allow server to send ACK
    sendInitialData(tunnel, STREAM_ID);

    //send first message
    /*
    const char* message = "Hello from client c++ !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cout << "Client: Sending initial data on stream " << STREAM_ID << std::endl;
    tunnel->sendToStream(STREAM_ID, (const uint8_t*)message, strlen(message));
    */

}

//47.75 Go
//46.93
int main() {
    // create epoll
    Server srv(1024, 1);
    srv.setUseGarbageCollector(false);
    if (!srv.start()) {
        printf("start failed\n");
        return 1;
    }

    // create MultiplexedTunnel client
    MultiplexedTunnel* tunnel = new MultiplexedTunnel(true); // client mode
    tunnel->setReactor(srv.getRoundRobinShard());
    tunnel->setOnConnected(onClientConnected, tunnel);


    // connect to yamux server
    //51.195.150.84
    //127.0.0.1
    if (tunnel->connectTo("127.0.0.1", 6060)) {
        std::cout << "Attempting to connect to 127.0.0.1:6060..." << std::endl;
    } else {
        std::cout << "Connect failed immediately." << std::endl;
        delete tunnel;
        return 1;
    }

    /*
    printf("waiting for start timer\n");
    getchar();


    // inja say mishe ke 1000 ta packet be serevr ersal beshe, har 20 mili secount ye packet be serevr ersal mishe ama
    Timer Timer1;
    Timer1.setReactor(srv.getRoundRobinShard());
    Timer1.start(1, [&Timer1, tunnel] () {
        //
        gCounter++;
        if(gCounter >= 1*1000){
            Timer1.stop();
        }

        std::string message = "Message #" + std::to_string(gCounter) + " from client!";
        tunnel->sendToStream(1, (const uint8_t*)message.c_str(), message.length());

    });
    */
    // 4. نگه داشتن برنامه در حال اجرا
    printf("\nPress ENTER to stop the client and close the stream...\n");
    getchar();

    if (STREAM_ID != 0 && tunnel->getStatus() == TCPSocket::Connected) {
        // ارسال پیام خداحافظی و بستن Stream
        const char* final_message = "Client is saying goodbye!";
        tunnel->sendToStream(STREAM_ID, (const uint8_t*)final_message, strlen(final_message));
        tunnel->closeStream(STREAM_ID);
    }

    printf("Press ENTER to terminate the program.\n");
    getchar();

    delete tunnel;
    return 0;
}
