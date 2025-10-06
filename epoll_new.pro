TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt
#baraye rafe ye bug #mohem search beshe

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
        clsSendQueue.cpp \
        clsServer.cpp \
        clsSocketList.cpp \
        clsTCPSocket.cpp \
        clsTimer.cpp \
        main.cpp \
        tlsf.c

HEADERS += \
    SocketContext.h \
    clsBufferPool.h \
    clsDNSLookup.h \
    clsEpollReactor.h \
    clsSendQueue.h \
    clsServer.h \
    clsGCList.h \
    clsSocketList.h \
    clsTCPSocket.h \
    clsTimer.h \
    epoll.h

