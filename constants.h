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
static constexpr size_t BUFFER_POOL_SIZE = 200 * (1024*1024);    //200M for 25K coonection
static constexpr size_t BACK_PRESSURE = 128*1024;                //1*(1024*1024); //1 MG
static constexpr size_t SLAB_SIZE = 8 * 1024;                    // 8KB socket buffer
//static constexpr size_t HIGH_WATERMARK = 64 * 1024;
static constexpr size_t LOW_WATERMARK = 64 * 1024;


// Timer Intervals (in milliseconds)
constexpr int UPDATE_CACHED_NOW      = 1000;
constexpr int DNS_TIMEOUT_INTERVAL_MS      = 200;
constexpr int GARBAGE_COLLECTOR_INTERVAL_MS = 10*1000;  // 10 seconds
constexpr int IDLE_CONNECTION_INTERVAL_MS = 30*1000;    // 30 seconds
constexpr int CLOSE_WAIT_INTERVAL_MS = 10*1000;          // 10 seconds
constexpr int CLOSING_TIMEOUT_SECS = 60;                // 60 seconds

// Keep-Alive socket
//
constexpr int KEEPIDLE_SECS = 240;   // 4 min delay baraye shoro keep laive
constexpr int KEEPINTVL_SECS = 30;   // 30 sanie fasle beyne ersale har Probe
constexpr int KEEPCNT_MAX = 4;       // maximum 4 ersal

//KEEPIDLE_SECS + (KEEPCNT_MAX * KEEPINTVL_SECS) = 6 min

#endif // CONSTANTS_H
