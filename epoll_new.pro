TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt
#baraye rafe ye bug #mohem search beshe

#accept4: Too many open files
#accept4: Too many open files
#accept4: Too many open files

#graceful shutdown piade sazi nashode
# age az biroon close call beshe ama safe ersal khali nabashe dorost nist

#پیشنهاد: در EpollReactor::maintenance() یا TimerManager، سوکت‌هایی که status == Closing و مدت طولانی (مثل >30s) در این حالت هستن رو چک کنید و close(true) کنید.

#kheyli mohem set beshe baraye proxy server
# اینطوری کانکشن‌های کوتاه‌مدت هزاران TIME_WAIT نمی‌سازن.
# net.ipv4.tcp_tw_reuse = 1
# net.ipv4.tcp_fin_timeout = 10

#LIBS += -luring -lssl -lcrypto
LIBS += -static -static-libgcc -static-libstdc++

#source Headers
INCLUDEPATH += $$PWD/src
DEPENDPATH += $$PWD/src


SOURCES += \
        Socks5LocalForwarder.cpp \
        Socks5StreamHandler.cpp \
        Socks5TunnelClient.cpp \
        Socks5TunnelServer.cpp \
        src/clsBufferPool.cpp \
        src/clsDNSLookup.cpp \
        src/clsEpollReactor.cpp \
        src/clsIntrusiveList.cpp \
        src/clsMultiplexedTunnel.cpp \
        src/clsSendQueue.cpp \
        src/clsServer.cpp \
        src/clsSocketList.cpp \
        clsSocks5Proxy.cpp \
        src/clsTCPSocket.cpp \
        src/clsTimer.cpp \
        src/clsTimerManager.cpp \
        example_Tunnel_Server.cpp \
        example_Tunnel_client.cpp \
        main.cpp \
        src/tlsf.c

HEADERS += \
    Socks5LocalForwarder.h \
    Socks5StreamHandler.h \
    src/SocketContext.h \
    src/clsBufferPool.h \
    src/clsDNSLookup.h \
    src/clsEpollReactor.h \
    src/clsIntrusiveList.h \
    src/clsMultiplexedTunnel.h \
    src/clsSendQueue.h \
    src/clsServer.h \
    src/clsGCList.h \
    src/clsSocketList.h \
    clsSocks5Proxy.h \
    src/clsTCPSocket.h \
    src/clsTimer.h \
    src/clsTimerManager.h \
    src/constants.h \
    src/epoll.h

