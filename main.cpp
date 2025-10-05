#include "clsTCPSocket.h"
#include "clsServer.h"
#include "clsTimer.h"

#include <malloc.h>

// ============================== Example: WebSocket-like handler =========
// Demonstrates inheritance only for non-hot paths + hot-path via fn pointer.
class WsEcho: public TCPSocket
{
public:

    WsEcho() {
        setOnData(&WsEcho::onDataTrampoline);
    }

    void onConnected() override {
        std::printf("onConnected %d\n", fd());

        //std::string pck = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+";

        /**/
        //http://ipv4.download.thinkbroadband.com:8080/5MB.zip
        //http://freetestdata.com/wp-content/uploads/2022/02/Free_Test_Data_1MB_JPG.jpg

        std::string pck = "GET /wp-content/uploads/2022/02/Free_Test_Data_1MB_JPG.jpg HTTP/1.1\r\n"
                          "Host: freetestdata.com\r\n"
                          "\r\n";

        send(pck.c_str(), pck.length());
    }

    void onConnecting() override {
        std::printf("onConnecting %d\n", fd());
    }

    void onConnectFailed() override {
        std::printf("onConnectFailed %d\n", fd());
    }

    void onAccepted() override {
        std::printf("[+] fd=%d onAccepted\n", fd());
    }

    void onClose() override {
        std::printf("[-] fd=%d closed\n", fd());
        //delete this;
    }

    static const char * ClassName() {
        return "WsEcho";
    }

private:
    // non-virtual hot-path implementation
    void onDataImpl(const uint8_t *data, size_t length)
    {
        if(length < 650){
            std::printf("onDataImpl[%s]\n", data);
        }else{
            std::printf("onDataImpl len[%zu]\n", length);
        }


        //echo
        send(data, length);
        //usleep(1000);
    }

    static void onDataTrampoline(TCPSocket *b, const uint8_t *d, size_t n)
    {
        static_cast<WsEcho*>(b)->onDataImpl(d, n);
    }
};

TCPSocket* OnAccepted(void* p){
    Server* srv = static_cast<Server*>(p);
    auto newWebsocket = new WsEcho;
    return newWebsocket;
}

// ============================== Example main ===========================
#include <sys/resource.h>
#include "clsDNSLookup.h"

void cbResolve(const char *hostname, char **ips, size_t count, DNSLookup::QUERY_TYPE qtype, void *p)
{
    if(count == 0){
        printf("type: [%u] hostname: [%s] not result\n", qtype, hostname);
    }else{
        printf("type: [%u] hostname: [%s] ip: [%s] count[%zu]\n",qtype , hostname, ips[0], count);
    }

    printf("\n");
}

Server srv(1024, 4);
DNSLookup dnsLookup(srv.getRoundRobinShard());

int main()
{

    //in to libwrench hast max ro az onja begir
    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    int maxfd = (int)r.rlim_cur;
    std::fprintf(stderr, "maxfd: %d\n", maxfd);


    srv.setUseGarbageCollector(false);
    srv.AddNewListener(8080, "0.0.0.0");
    srv.setOnAccepted(OnAccepted, &srv);


    if(!srv.start())
    {
        std::fprintf(stderr, "start failed\n");
        return 1;
    }

    //std::printf("listening on %d\n", DEFAULT_PORT);
    // Run forever
    /*
    for(;;){
        getchar();
        srv.stop();

        getchar();
        srv.AddNewListener(8080, "0.0.0.0");
        srv.start();

    }

    while(true)
        std::this_thread::sleep_for(std::chrono::seconds(3600));
    */
    //for(;;)

    Timer timer;
    timer.setReactor(srv.getRoundRobinShard());
    timer.start(200, [] {
        dnsLookup.maintenance();
        //std::fprintf(stderr, "Periodic timer tick: %ld\n", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    });


    dnsLookup.setTimeout(3);
    dnsLookup.setCache_ttl_sec(1);

    dnsLookup.resolve("freetestdata.com", cbResolve, nullptr);
    dnsLookup.resolve("sh02.mojz.ir", cbResolve, nullptr);
    dnsLookup.resolve("facebook.com", cbResolve, nullptr);
    getchar();

    Timer timer2;
    timer2.setReactor(srv.getRoundRobinShard());
    timer2.start(1000, [] {
        dnsLookup.resolve("freetestdata.com", cbResolve, nullptr, DNSLookup::A);
    });


    for(;;){
        //dnsLookup.maintenance();
        printf("resolve:\n");
        dnsLookup.resolve("sh02.mojz.ir", cbResolve, nullptr, DNSLookup::A);
        getchar();
    }




    getchar();
    timer.stop();

    //connect


    //for(int i = 0; i < 1;i++){

    WsEcho* outbound = new WsEcho();
    outbound->setReactor(srv.getRoundRobinShard());
    outbound->connectTo("freetestdata.com", 80);
    //outbound->connectTo("127.0.0.1", 8080);
    //}

    getchar();
    outbound->close();



    //end
    getchar();
}




