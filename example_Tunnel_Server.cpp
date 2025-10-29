#include "clsEpollReactor.h"
#include "clsMultiplexedTunnel.h"
#include "clsServer.h"
#include <iostream>

// Global callbacks برای سرور (برای streamهای جدید)
void onServerStreamData(void* arg, uint32_t streamId, const uint8_t* data, size_t len) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::string received_message((const char*)data, len);
    std::cout << "Server: Received on stream " << streamId << ": " << received_message << std::endl;

    // Echo: داده دریافتی را به همان Stream برگشت می‌دهد
    tunnel->sendToStream(streamId, data, len);

    // نکته مهم: Flow Control
    // پس از مصرف (Echo کردن) داده‌ها، باید Window Update را ارسال کنیم تا کلاینت بداند
    // فضای کافی برای ارسال مجدد وجود دارد.
    tunnel->sendWindowUpdate(streamId, len);
}

void onServerStreamClose(void* arg, uint32_t streamId) {
    std::cout << "Server: Stream " << streamId << " closed." << std::endl;
}

void onServerNewStream(void* arg, uint32_t streamId, MultiplexedTunnel::Stream* newStream) {
    MultiplexedTunnel* tunnel = static_cast<MultiplexedTunnel*>(arg);

    std::cout << "Server: New incoming stream " << streamId << " accepted." << std::endl;

    // تنظیم کالبک‌های onData و onClose برای Stream جدید
    newStream->onData = onServerStreamData;
    newStream->onClose = onServerStreamClose;
    newStream->arg = tunnel; // پوینتر به تونل را به عنوان آرگومان کالبک‌ها پاس می‌دهیم
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
