#include "clsTCPSocket.h"
#include "clsServer.h"
#include "clsTimer.h"

#include <malloc.h>

// ============================== Example =========
// Demonstrates inheritance only for non-hot paths + hot-path via fn pointer.
class WsEcho: public TCPSocket
{
public:

    WsEcho() {
        setOnData(&WsEcho::onDataTrampoline);
    }

    void onConnected() override {
        std::printf("onConnected %d\n", fd());

        return;
        //std::string pck = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+";

        /**/
        //http://ipv4.download.thinkbroadband.com:8080/5MB.zip
        //http://freetestdata.com/wp-content/uploads/2022/02/Free_Test_Data_1MB_JPG.jpg

        std::string pck = "GET /docker/udpgw HTTP/1.1\r\n"
                          "Host: 51.195.150.84\r\n"
                          //"connection: close\r\n"
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
    bool fisrt = true;
    // non-virtual hot-path implementation
    void onDataImpl(const uint8_t *data, size_t length)
    {

        std::printf("onDataImpl len[%zu]\n", length);
        /*
        if(fisrt){
            std::printf("onDataImpl[%s] len[%zu]\n", data, length);
            fisrt = false;
        }else{
            //
        }
*/
        //581   1762968 [1763563]

        //http://dl.mojz.ir/docker/radiussh.tar.gz
        //http://51.195.150.84/docker/RadiuSSH

        //46144932  [46145617]

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
//DNSLookup dnsLookup(srv.getRoundRobinShard());

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



    srv.getRoundRobinShard()->getIPbyName("freetestdata.com", cbResolve, nullptr);

    /*
    dnsLookup.setTimeout(3);
    dnsLookup.setCache_ttl_sec(1);
    dnsLookup.setMaxRetries(1);


    dnsLookup.resolve("freetestdata.com", cbResolve, nullptr);
    dnsLookup.resolve("sh02.mojz.ir", cbResolve, nullptr);

    dnsLookup.resolve("facebook.com", cbResolve, nullptr);
getchar();
    dnsLookup.reset_socket();

    getchar();



    for(;;){
        //dnsLookup.maintenance();
        printf("resolve:\n");
        srv.getRoundRobinShard()->getIPbyName("sv1.mojz.ir", cbResolve, nullptr, DNSLookup::A);
        getchar();
    }
*/

    //connect

    //for(int i = 0; i < 1;i++){

    WsEcho* outbound = new WsEcho();
    outbound->setReactor(srv.getRoundRobinShard());
    //outbound->connectTo("51.195.150.84", 80);
    outbound->connectTo("51.195.150.84", 5001);
    //}

    /**/
    for(;;){
        getchar();
        outbound->resume_reading();
    }



    //end
    getchar();
    exit(0);
}




