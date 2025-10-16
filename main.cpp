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

    ~WsEcho() {
       // std::printf("~WsEcho()\n");
    }

    void onConnected() override {
        std::printf("onConnected() fd: %d\n", fd());

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
        delete this;
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

        //echo
        std::string pckEcho;
        pckEcho.append((const char*)data,length);
        pckEcho.append("-");
        //auto now = std::chrono::high_resolution_clock::now();
        //auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        pckEcho.append(std::to_string(fd()));

        send(pckEcho.c_str(), pckEcho.length());
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


//DNSLookup dnsLookup(srv.getRoundRobinShard());

int main()
{

    //in to libwrench hast max ro az onja begir
    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    int maxfd = (int)r.rlim_cur;
    std::fprintf(stderr, "maxfd: %d\n", maxfd);
    Server srv(maxfd, 4);


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
A:
    WsEcho* outbound = new WsEcho();
    outbound->setReactor(srv.getRoundRobinShard());
    //outbound->connectTo("51.195.150.84", 80);

    //outbound->connectTo("51.195.150.84", 80);
    outbound->connectTo("192.168.1.11", 5001);
    //}

    /**/
    for(;;){
        char needExit = getchar();
        if(needExit == 'e'){
            exit(0);
        }

        if(needExit == 'r'){
            outbound->close();
            goto A;
        }

        //outbound->resume_reading();
        srv.getRoundRobinShard()->test();
    }



    //end
    getchar();
    exit(0);
}




