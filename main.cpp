#include "clsTCPSocket.h"
#include "clsServer.h"
#include "clsTimer.h"

#include <malloc.h>
#include "clsSocks5Proxy.h"


TCPSocket* OnAccepted(void* p){
    //Server* srv = static_cast<Server*>(p);
    Socks5Proxy *newWebsocket = new Socks5Proxy;
    return newWebsocket->getSocketBase();
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
    Server srv(maxfd, 2);


    srv.setUseGarbageCollector(false);
    srv.AddNewListener(1080, "0.0.0.0");
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
    /*
    Acceptor* outbound = new Acceptor();
    outbound->setReactor(srv.getRoundRobinShard());
    //outbound->connectTo("51.195.150.84", 80);

    //outbound->connectTo("51.195.150.84", 80);
    outbound->connectTo("192.168.1.11", 5001);
    //}
*/
    /**/
    for(;;){
        char needExit = getchar();
        if(needExit == 'e'){
            exit(0);
        }

        if(needExit == 'r'){
            //outbound->close();
            goto A;
        }

        //outbound->resume_reading();
        //srv.getRoundRobinShard()->test();

    }



    //end
    getchar();
    exit(0);
}




