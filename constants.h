#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <cstddef>

// Epoll and Socket Constants
static constexpr int MAX_EVENTS = 1024;             // batch epoll_wait
static constexpr int LISTEN_BACKLOG = 4096;
//static constexpr int IDLE_TIMEOUT_SEC = 30;       // graceful idle GC


// DNS Lookup Configuration Constants
constexpr unsigned int DNS_LOOKUP_TIMEOUT_SEC = 1;   // 1 second
constexpr unsigned int DNS_CACHE_TTL_SEC = 5*60;     // 5 minutes
constexpr unsigned int DNS_MAX_RETRIES = 3;


//buffer config
static constexpr size_t BUFFER_POOL_SIZE = 10 * (1024*1024);    //100 MG
static constexpr size_t BACK_PRESSURE = 128*1024;               //1*(1024*1024); //1 MG
static constexpr size_t SLAB_SIZE = 8 * 1024;                   // 8KB socket buffer
//static constexpr size_t HIGH_WATERMARK = 64 * 1024;
static constexpr size_t LOW_WATERMARK = 64 * 1024;


// Timer Intervals (in milliseconds)
constexpr int DNS_TIMEOUT_INTERVAL_MS      = 200;
constexpr int GARBAGE_COLLECTOR_INTERVAL_MS = 10*1000;  // 10 seconds
constexpr int IDLE_CONNECTION_INTERVAL_MS = 30*1000;     // 30 seconds
constexpr int STALLED_CONNECTION_INTERVAL_MS = 10*1000;   // 10 seconds


#endif // CONSTANTS_H
