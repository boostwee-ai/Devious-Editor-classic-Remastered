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
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)
#define closesocket(s)  ::close(s)
#endif

class NetworkManager {
public:
    static NetworkManager& get();

    bool init();
    void cleanup();

    // ── UDP ──────────────────────────────────────────────────────────────────

    // Start the UDP receive loop on `port`.  Also creates the unbound send socket.
    bool startUdpDiscovery(uint16_t port);
    void stopUdpDiscovery();

    // Broadcast to every local subnet's directed broadcast address.
    void sendUdpBroadcast(const std::string& data, uint16_t port);

    // Send directly to a specific peer IP (invites, live object sync, cursor, etc.)
    void sendUdpUnicast(const std::string& data, const std::string& ip, uint16_t port);

    // Fired on the background thread → CollaborationSession re-queues to main thread.
    std::function<void(const std::string& fromIp, const std::string& data)> onUdpMessage;

    // ── TCP ──────────────────────────────────────────────────────────────────

    // Host: start listening for exactly one incoming connection.
    bool startTcpServer(uint16_t port);
    void stopTcpServer();

    // Guest: connect to the host.  Non-blocking call — connection runs in background.
    bool connectTcp(const std::string& ip, uint16_t port);
    void disconnectTcp();

    // Send a length-prefixed message over the established TCP connection.
    // Thread-safe: can be called from any thread.
    bool sendTcp(const std::string& data);

    // Fired (on background thread) when a complete TCP message is received.
    std::function<void(const std::string& data)> onTcpMessage;

    // Fired (on background thread) when a new TCP client connects (host only).
    std::function<void()> onTcpClientConnected;

    // Fired (on background thread) when the TCP connection drops.
    std::function<void()> onTcpDisconnected;

    bool isTcpConnected() const { return m_tcpConn != INVALID_SOCKET; }

private:
    NetworkManager() = default;
    ~NetworkManager();

    // ── UDP internals ────────────────────────────────────────────────────────
    SOCKET m_recvSocket = INVALID_SOCKET;
    SOCKET m_sendSocket = INVALID_SOCKET;
    std::atomic<bool> m_udpRunning{false};
    std::thread m_udpThread;
    void udpListenLoop();
    std::vector<in_addr> getBroadcastAddresses();

    // ── TCP internals ────────────────────────────────────────────────────────
    SOCKET m_tcpServerSocket = INVALID_SOCKET;
    SOCKET m_tcpConn         = INVALID_SOCKET;
    std::atomic<bool> m_tcpServerRunning{false};
    std::atomic<bool> m_tcpConnRunning{false};
    std::thread m_tcpAcceptThread;
    std::thread m_tcpRecvThread;
    std::mutex  m_tcpSendMutex;

    void tcpAcceptLoop();
    void tcpRecvLoop();

    // Receive exactly `len` bytes into `buf`.  Returns false on error/disconnect.
    bool recvAllTcp(char* buf, int len);

#ifdef CURRENT_OS_WINDOWS
    bool m_wsaInitialized = false;
#endif
};
