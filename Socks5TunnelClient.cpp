#include "clsServer.h"
#include "clsMultiplexedTunnel.h"
#include "Socks5LocalForwarder.h" // کلاس جدید
#include <sys/resource.h>
#include <cstdio>

// اشاره‌گر سراسری به تونل فعال
MultiplexedTunnel* g_tunnel = nullptr;


// این کلاس اتصال پایه تونل به سرور را مدیریت می‌کند
class TunnelClientEndpoint : public MultiplexedTunnel {
public:
    // سمت کلاینت، isClient = true
    TunnelClientEndpoint() : MultiplexedTunnel(true) {}

    void onConnected() override {
        printf("[Client] Tunnel base connection established to server. fd=%d\n", fd());
        g_tunnel = this; // ثبت تونل در اشاره‌گر سراسری
    }

    void onClose() override {
        printf("[Client] Tunnel base connection lost. fd=%d\n", fd());
        g_tunnel = nullptr;

        // TODO: منطق اتصال مجدد باید اینجا پیاده‌سازی شود
        // برای سادگی، فعلاً فقط می‌بندیم.
    }
};


// Factory function برای شنونده SOCKS5 محلی
TCPSocket* OnLocalSocksAccepted(void* p) {
    if (g_tunnel == nullptr || g_tunnel->getStatus() != TCPSocket::Connected) {
        std::fprintf(stderr, "[Client] Rejecting local SOCKS: Tunnel is not active.\n");
        return nullptr; // رد کردن اتصال
    }

    printf("[Client] New local SOCKS connection accepted.\n");
    return (new Socks5LocalForwarder(g_tunnel));
}


int main()
{
    //51.195.150.84
    //127.0.0.1
    const char* remote_tunnel_ip = "51.195.150.84";
    int remote_tunnel_port = 9191;
    int local_socks_port = 1080;

    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    int maxfd = (int)r.rlim_cur;

    Server srv(maxfd,1 );
    srv.setUseGarbageCollector(false);

    // ۱. راه‌اندازی شنونده محلی SOCKS5
    srv.AddNewListener(local_socks_port, "0.0.0.0");
    srv.setOnAccepted(OnLocalSocksAccepted, &srv);

    // ۲. شروع اتصال به سرور تونل
    EpollReactor* mainReactor = srv.getRoundRobinShard(); //
    if (!mainReactor) {
        std::fprintf(stderr, "[Client] Could not get a reactor shard.\n");
        return 1;
    }

    TunnelClientEndpoint* tunnelSocket = new TunnelClientEndpoint();
    tunnelSocket->setReactor(mainReactor); //

    printf("[Client] Connecting to remote tunnel server at %s:%d...\n", remote_tunnel_ip, remote_tunnel_port);
    if (!tunnelSocket->connectTo(remote_tunnel_ip, remote_tunnel_port)) {
        //
        std::fprintf(stderr, "[Client] connectTo call failed immediately.\n");
        delete tunnelSocket;
        return 1;
    }

    // ۳. راه‌اندازی سرور (که هم لیسنر 1080 و هم اتصال تونل را مدیریت کند)
    if(!srv.start()) {
        std::fprintf(stderr, "[Client] Server/Reactor start failed\n");
        return 1;
    }

    std::fprintf(stderr, "[Client] SOCKS5 Tunnel Client running. Listening on 127.0.0.1:%d\n", local_socks_port);

    getchar();

    srv.stop();
    return 0;
}
