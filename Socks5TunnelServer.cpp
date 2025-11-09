#include "clsServer.h"
#include "clsMultiplexedTunnel.h"
#include "Socks5StreamHandler.h" // کلاس جدید
#include <sys/resource.h>
#include <cstdio>

// این کلاس اتصال پایه تونل را مدیریت می‌کند
// از آنجایی که clsMultiplexedTunnel از clsTCPSocket ارث می‌برد،
// ما فقط باید از clsMultiplexedTunnel ارث ببریم.
class TunnelServerEndpoint : public MultiplexedTunnel {
public:
    // سمت سرور، isClient = false
    TunnelServerEndpoint() : MultiplexedTunnel(false) {}

    // این تابع زمانی فراخوانی می‌شود که سوکت توسط EpollReactor پذیرفته شود
    void onAccepted() override {
        printf("[Server] Tunnel base connection accepted. fd=%d\n", fd());

        // Callback برای استریم‌های جدیدی که کلاینت باز می‌کند
        setOnNewStream(&TunnelServerEndpoint::HandleNewStream, this);
    }

    // این تابع مجازی از TCPSocket می‌آید و توسط MultiplexedTunnel بازنویسی شده است.
    // ما نیازی به بازنویسی مجدد آن نداریم، مگر اینکه بخواهیم لاگ بگیریم.
    // void onReceiveData(const uint8_t* data, size_t len) override {
    //     printf("Server OnRecv %zu bytes\n", len);
    //     MultiplexedTunnel::onReceiveData(data, len); // فراخوانی پارسر
    // }

    void onClose() override {
        printf("[Server] Tunnel base connection closed. fd=%d\n", fd());
        // MultiplexedTunnel::~MultiplexedTunnel() به طور خودکار استریم‌ها را پاک نمی‌کند
        // ما باید خودمان هندلرها را ببندیم (اگرچه با رفتن tunnel، هندلرها نیز از کار می‌افتند)
    }

    // Callback استاتیک برای ایجاد هندلر SOCKS5
    static void HandleNewStream(void* arg, uint32_t streamId, MultiplexedTunnel::Stream* newStream) {
        TunnelServerEndpoint* self = static_cast<TunnelServerEndpoint*>(arg);
        printf("[Server] New stream #%u opened. Creating SOCKS5 handler.\n", streamId);

        // ایجاد هندلر SOCKS5 برای این استریم خاص
        new Socks5StreamHandler(self, newStream);
    }
};


// Factory function برای clsServer
TCPSocket* OnTunnelAccepted(void* p) {
    return new TunnelServerEndpoint();
}


int main6()
{
    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    int maxfd = (int)r.rlim_cur;
    std::fprintf(stderr, "[Server] maxfd: %d\n", maxfd);

    Server srv(maxfd,1);
    srv.setUseGarbageCollector(false); // با توجه به کد شما

    int tunnel_port = 9191; // پورتی که کلاینت به آن وصل می‌شود
    srv.setOnAccepted(OnTunnelAccepted, &srv); //

    srv.AddNewListener(tunnel_port, "0.0.0.0");

    if(!srv.start()) {
        std::fprintf(stderr, "[Server] start failed\n");
        return 1;
    }

    std::fprintf(stderr, "[Server] SOCKS5 Tunnel Server listening on port %d\n", tunnel_port);
    getchar();

    srv.stop();
    return 0;
}
