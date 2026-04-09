#pragma once

#include <string>
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef CURRENT_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket(s) ::close(s)
#endif

class NetworkManager {
public:
    static NetworkManager& get();

    bool init();
    void cleanup();

    // UDP Discovery
    // startUdpDiscovery opens the receive socket; sendUdpBroadcast uses a
    // separate, unbound send socket so the two never conflict.
    bool startUdpDiscovery(uint16_t port);
    void stopUdpDiscovery();

    // Broadcast data to every local-subnet directed-broadcast address found on
    // this machine.  Using directed-broadcast (e.g. 192.168.1.255) instead of
    // 255.255.255.255 is necessary for reliable cross-platform LAN delivery.
    void sendUdpBroadcast(const std::string& data, uint16_t port);

    // Send a packet directly to a specific peer IP (for invite/response messages).
    void sendUdpUnicast(const std::string& data, const std::string& ip, uint16_t port);

    // Called on the background receive thread; forward to CollaborationSession
    // which re-dispatches to the main thread.
    std::function<void(const std::string& fromIp, const std::string& data)> onUdpMessage;

private:
    NetworkManager() = default;
    ~NetworkManager();

    // Receive socket — bound to INADDR_ANY:port
    SOCKET m_recvSocket = INVALID_SOCKET;
    // Send socket  — unbound (ephemeral source port), SO_BROADCAST only
    SOCKET m_sendSocket = INVALID_SOCKET;

    std::atomic<bool> m_running{false};
    std::thread m_udpThread;

    void udpListenLoop();

    // Returns every subnet-directed broadcast address reachable from this host
    // (one per active non-loopback interface).  Falls back to 255.255.255.255.
    std::vector<in_addr> getBroadcastAddresses();

#ifdef CURRENT_OS_WINDOWS
    bool m_wsaInitialized = false;
#endif
};
