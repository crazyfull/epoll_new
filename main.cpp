#include "clsTCPSocket.h"
#include "clsServer.h"
#include "clsTimer.h"

#include <malloc.h>

// ============================== Example =========
// Demonstrates inheritance only for non-hot paths + hot-path via fn pointer.
class Acceptor: protected TCPSocket
{
public:

    Acceptor() {
        //setOnData(&Acceptor::onDataTrampoline);
    }

    ~Acceptor() {
        std::printf("~Acceptor()\n");
    }


    void onClose() override {
        onAcceptorClose();
        //std::printf("[-] fd=%d closed\n", fd());
        // delete this;
    }

    virtual void onAcceptorClose() {}

    static const char * ClassName() {
        return "Acceptor";
    }

    TCPSocket* getSocketBase(){
        return getPointer();
    }


private:
    // non-virtual hot-path implementation



};

class Connector: public TCPSocket
{
public:

    Connector() {
        //setOnData(&Connector::onDataTrampoline);

    }

    ~Connector() {
        std::printf("~Connector()\n");
    }

    virtual void onConnectorClose() {}

    void onClose() override {
        onConnectorClose();
        //printf("onClose() fd=%d\n", fd());
        // delete this;
    }

    static const char * ClassName() {
        return "Connector";
    }

    bool coonect(const std::string &host, uint16_t port){
        return connectTo(host.c_str(), port);
    }

    TCPSocket::socketStatus getStatus(){
        return TCPSocket::getStatus();
    }



private:

    /*
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
* /
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

    static void onDataTrampoline(void *p, const uint8_t *d, size_t n)
    {
        static_cast<Connector*>(p)->onDataImpl(d, n);
    }
    */
};


class Proxy: public Acceptor{
public:
    Connector *m_pConnector;
    std::string buff;

    Proxy(){
        m_pConnector = new Connector;
        m_pConnector->setOnData(&Proxy::onConnectorReceiveData, this);
        m_pConnector->setOnClose(&Proxy::onConnectorClose, this);
        m_pConnector->setOnConnectFailed(&Proxy::onConnectFailed, this);
        m_pConnector->setOnConnecting(&Proxy::onConnecting, this);
        m_pConnector->setOnConnected(&Proxy::onConnected, this);

        Acceptor::setOnData(&Proxy::onAcceptorReceiveData, this);
    }

    ~Proxy(){
        printf("~Proxy()\n");
    }

    TCPSocket* getSocketBase(){
        return Acceptor::getSocketBase();
    }

    void onAccepted() override {
        printf("onAccepted() fd=%d\n", Acceptor::fd());

        m_pConnector->setReactor(Acceptor::getReactor());
        //m_pConnector->coonect("51.195.150.84", 80);
        m_pConnector->coonect("cl.mojz.ir", 443);
        //m_pConnector->coonect("192.168.1.10", 9000);

    }

    void onAcceptorClose() override {

        printf("onAcceptorClose() fd=%d\n", Acceptor::fd());
    }



private:
    static void onConnectorReceiveData(void *b, const uint8_t *data, size_t length)
    {
        static_cast<Proxy*>(b)->OnConnectorReceiveData(data, length);
    }

    static void onConnectorClose(void *b)
    {
        static_cast<Proxy*>(b)->OnConnectorClose();
    }

    static void onConnectFailed(void *b)
    {
        static_cast<Proxy*>(b)->OnConnectFailed();
    }

    static void onConnecting(void *p)
    {
        Proxy *pProxy = static_cast<Proxy*>(p);
        //printf("handleOnConnecting() fd=%d\n", this->fd());

        pProxy->onConnectorConnecting();
    }

    static void onConnected(void *b)
    {
        static_cast<Proxy*>(b)->OnConnected();
    }


    void OnConnected() {
        printf("onConnected() fd=%d\n", m_pConnector->fd());


        //std::string pck = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+";

        /*
        //http://ipv4.download.thinkbroadband.com:8080/5MB.zip
        //http://freetestdata.com/wp-content/uploads/2022/02/Free_Test_Data_1MB_JPG.jpg

        std::string pck = "GET /docker/udpgw HTTP/1.1\r\n"
                          "Host: 51.195.150.84\r\n"
                          //"connection: close\r\n"
                          "\r\n";
        */

        if(buff.length() > 0){
            m_pConnector->send(buff.c_str(), buff.length());
            buff.clear();
        }
    }

    void onConnectorConnecting() {
        printf("onConnectorConnecting() fd=%d\n", m_pConnector->fd());
    }

    void OnConnectFailed() {
        printf("onConnectFailed() fd=%d\n", m_pConnector->fd());
    }

    void OnConnectorClose()  {
        printf("onConnectorClose() fd=%d\n", m_pConnector->fd());
    }

    void OnConnectorReceiveData(const uint8_t *data, size_t length)
    {
       // printf("Connector::fd(): fd[%d] ClassName: %s\n", m_pConnector->fd(), Connector::ClassName());
       // printf("Acceptor::fd(): fd[%d] ClassName: %s\n", Acceptor::fd(), Acceptor::ClassName());


        if(Acceptor::getStatus() == TCPSocket::Connected) {
            printf("Acceptor::send length[%zu]\n", length );
            Acceptor::send(data, length);
        } else {
            printf("🔴 Acceptor not connected, dropping data\n");
        }
    }

    static void onAcceptorReceiveData(void *p, const uint8_t *data, size_t length)
    {
        static_cast<Proxy*>(p)->onAcceptorReceiveData(data, length);
    }

    void onAcceptorReceiveData(const uint8_t *data, size_t length)
    {
        /*
        if(Connector::getStatus() == 4){
            buff.append((const char*)data, length);
            return;
        }

        printf("getStatus [%u]\n", Connector::getStatus());
        printf("onAcceptorReceiveData: [%s] len[%zu]\n", data, length);

        //send(pckEcho.c_str(), pckEcho.length());
        std::string pck;
        pck.append((const char*)data, length);
        Connector::send(pck.c_str(), pck.length());

        */

        //printf("Acceptor data: len[%zu]\n", length);
        //printf("Connector::fd(): fd[%d] ClassName: %s\n", m_pConnector->fd(), Connector::ClassName());
        //printf("Acceptor::fd(): fd[%d] ClassName: %s\n", Acceptor::fd(), Acceptor::ClassName());


        if(m_pConnector->getStatus() == TCPSocket::Connected) {
            printf("Connector::send length[%zu]\n", length );

            m_pConnector->send(data, length);

        } else {
            // ذخیره در بافر تا زمانی که connector متصل شود
            buff.append((const char*)data, length);
        }

    }





};

TCPSocket* OnAccepted(void* p){
    //Server* srv = static_cast<Server*>(p);
    Proxy *newWebsocket = new Proxy;
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
    Server srv(maxfd, 1);


    srv.setUseGarbageCollector(false);
    srv.AddNewListener(8443, "0.0.0.0");
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




