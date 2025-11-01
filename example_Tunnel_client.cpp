#include "clsEpollReactor.h"
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>
#include <unistd.h> // for usleep
#include <cstring>  // for strlen

uint32_t STREAM_ID = 0; // Global to store the stream ID

void onClientStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::string input((const char*)data, len);
    printf("onClientStreamData: [%u] [%s]\n\n", streamId, input.c_str());

    // *** Flow Control: باید Window Update را پس از مصرف داده بفرستیم ***
    // این کار به سرور اجازه می‌دهد تا به ارسال ادامه دهد.
    tunnel->sendWindowUpdate(streamId, (uint32_t)len);
}

void onClientStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Client: Stream " << streamId << " closed." << std::endl;
}

void sendInitialData(MultiplexedTunnel* tunnel, uint32_t streamId) {
    const char* message = "Hello from client c++ !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cout << "Client: Sending initial data on stream " << streamId << std::endl;
    tunnel->sendToStream(streamId, (const uint8_t*)message, strlen(message));

    usleep(100 * 1000);


    int count = 0;
    for(;;){
        count++;
        //usleep(1*200);
        //getchar();
        //const char* message = "Hello from clientttt!!!!!";
        std::string message = "Message #" + std::to_string(count) + " from client!";
        tunnel->sendToStream(streamId, (const uint8_t*)message.c_str(), message.length());
        if(count > 6*1000){
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

    // Stream را باز کن (ارسال SYN)
    STREAM_ID = tunnel->openStream(onClientStreamData, onClientStreamClose, tunnel);

    // توجه: ارسال داده‌های اولیه باید **پس از دریافت ACK** از سرور انجام شود.
    // در این پیاده‌سازی ساده، ما بلافاصله ارسال می‌کنیم، اما یک تاخیر کوچک برای کمک به Handshake قرار می‌دهیم.
    // در یک پیاده‌سازی پیشرفته‌تر، باید وضعیت Stream را برای ACK بررسی کنید.
    usleep(100 * 1000); // 100ms delay to allow server to send ACK
    sendInitialData(tunnel, STREAM_ID);
}


int main() {
    // 1. راه‌اندازی Epoll Server (فقط برای ایجاد رآکتور)
    Server srv(1024, 1);
    srv.setUseGarbageCollector(false);
    if (!srv.start()) {
        printf("start failed\n");
        return 1;
    }

    // 2. ایجاد MultiplexedTunnel در حالت کلاینت
    MultiplexedTunnel* tunnel = new MultiplexedTunnel(true); // client mode
    tunnel->setReactor(srv.getRoundRobinShard());
    tunnel->setOnConnected(onClientConnected, tunnel);

    // 3. اتصال
    if (tunnel->connectTo("127.0.0.1", 6060)) {
        std::cout << "Attempting to connect to 127.0.0.1:6060..." << std::endl;
    } else {
        std::cout << "Connect failed immediately." << std::endl;
        delete tunnel;
        return 1;
    }

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
