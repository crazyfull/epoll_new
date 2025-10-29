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

SOURCES += \
        clsBufferPool.cpp \
        clsDNSLookup.cpp \
        clsEpollReactor.cpp \
        clsIntrusiveList.cpp \
        clsMultiplexedTunnel.cpp \
        clsSendQueue.cpp \
        clsServer.cpp \
        clsSocketList.cpp \
        clsSocks5Proxy.cpp \
        clsTCPSocket.cpp \
        clsTimer.cpp \
        clsTimerManager.cpp \
        example_Tunnel_Server.cpp \
        example_Tunnel_client.cpp \
        main.cpp \
        tlsf.c

HEADERS += \
    SocketContext.h \
    clsBufferPool.h \
    clsDNSLookup.h \
    clsEpollReactor.h \
    clsIntrusiveList.h \
    clsMultiplexedTunnel.h \
    clsSendQueue.h \
    clsServer.h \
    clsGCList.h \
    clsSocketList.h \
    clsSocks5Proxy.h \
    clsTCPSocket.h \
    clsTimer.h \
    clsTimerManager.h \
    constants.h \
    epoll.h

