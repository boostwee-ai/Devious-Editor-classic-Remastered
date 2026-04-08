#pragma once

#include <string>
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef CURRENT_OS_WINDOWS
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

class NetworkManager {
public:
    static NetworkManager& get();

    bool init();
    void cleanup();

    // UDP Discovery
    bool startUdpDiscovery(uint16_t port);
    void stopUdpDiscovery();
    void sendUdpBroadcast(const std::string& data, uint16_t port);
    
    std::function<void(const std::string& fromIp, const std::string& data)> onUdpMessage;

private:
    NetworkManager() = default;
    ~NetworkManager();

    SOCKET m_udpSocket = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::thread m_udpThread;
    
    void udpListenLoop();

#ifdef CURRENT_OS_WINDOWS
    bool m_wsaInitialized = false;
#endif
};
