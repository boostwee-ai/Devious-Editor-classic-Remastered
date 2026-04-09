#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>
#include <vector>
#include <cstring>

using namespace geode::prelude;

#ifdef CURRENT_OS_WINDOWS
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}
NetworkManager::~NetworkManager() { cleanup(); }

// ─────────────────────────────────────────────────────────────────────────────
// Init / Cleanup
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::init() {
#ifdef CURRENT_OS_WINDOWS
    if (!m_wsaInitialized) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
            log::error("NetworkManager: WSAStartup failed");
            return false;
        }
        m_wsaInitialized = true;
    }
#endif
    return true;
}

void NetworkManager::cleanup() {
    stopUdpDiscovery();
    stopTcpServer();
    disconnectTcp();
#ifdef CURRENT_OS_WINDOWS
    if (m_wsaInitialized) { WSACleanup(); m_wsaInitialized = false; }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP — interface enumeration
// ─────────────────────────────────────────────────────────────────────────────
std::vector<in_addr> NetworkManager::getBroadcastAddresses() {
    std::vector<in_addr> addrs;

#ifdef CURRENT_OS_WINDOWS
    ULONG bufSize = 15 * 1024;
    std::vector<BYTE> buf(bufSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(adapters, &bufSize) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufSize);
        adapters = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    }
    if (GetAdaptersInfo(adapters, &bufSize) == NO_ERROR) {
        for (auto* a = adapters; a; a = a->Next) {
            for (auto* ip = &a->IpAddressList; ip; ip = ip->Next) {
                in_addr ipv4{}, mask{};
                if (inet_pton(AF_INET, ip->IpAddress.String, &ipv4) != 1) continue;
                if (inet_pton(AF_INET, ip->IpMask.String, &mask)    != 1) continue;
                if ((ntohl(ipv4.s_addr) >> 24) == 127) continue;
                if (ipv4.s_addr == 0) continue;
                in_addr bcast{};
                bcast.s_addr = (ipv4.s_addr & mask.s_addr) | (~mask.s_addr);
                addrs.push_back(bcast);
            }
        }
    }
#else
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)     continue;
            if (!ifa->ifa_broadaddr)               continue;
            addrs.push_back(reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr)->sin_addr);
        }
        freeifaddrs(ifap);
    }
#endif
    if (addrs.empty()) {
        in_addr fb{};
        fb.s_addr = 0xFFFFFFFF;
        addrs.push_back(fb);
    }
    return addrs;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP — discovery
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::startUdpDiscovery(uint16_t port) {
    if (m_udpRunning) return true;

    // Receive socket
    m_recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_recvSocket == INVALID_SOCKET) return false;
    int yes = 1;
    setsockopt(m_recvSocket, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
    setsockopt(m_recvSocket, SOL_SOCKET, SO_REUSEADDR,  (char*)&yes, sizeof(yes));
#ifndef CURRENT_OS_WINDOWS
    setsockopt(m_recvSocket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    sockaddr_in ra{};
    ra.sin_family      = AF_INET;
    ra.sin_port        = htons(port);
    ra.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(m_recvSocket, (sockaddr*)&ra, sizeof(ra)) == SOCKET_ERROR) {
        closesocket(m_recvSocket); m_recvSocket = INVALID_SOCKET; return false;
    }

    // Unbound send socket
    m_sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sendSocket == INVALID_SOCKET) {
        closesocket(m_recvSocket); m_recvSocket = INVALID_SOCKET; return false;
    }
    setsockopt(m_sendSocket, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));

    m_udpRunning = true;
    m_udpThread  = std::thread(&NetworkManager::udpListenLoop, this);
    log::info("NetworkManager: UDP discovery started on :{}", port);
    return true;
}

void NetworkManager::stopUdpDiscovery() {
    m_udpRunning = false;
    if (m_recvSocket != INVALID_SOCKET) { closesocket(m_recvSocket); m_recvSocket = INVALID_SOCKET; }
    if (m_sendSocket != INVALID_SOCKET) { closesocket(m_sendSocket); m_sendSocket = INVALID_SOCKET; }
    if (m_udpThread.joinable()) m_udpThread.join();
}

void NetworkManager::sendUdpBroadcast(const std::string& data, uint16_t port) {
    if (m_sendSocket == INVALID_SOCKET) return;
    for (const auto& bcast : getBroadcastAddresses()) {
        sockaddr_in d{};
        d.sin_family = AF_INET; d.sin_port = htons(port); d.sin_addr = bcast;
        sendto(m_sendSocket, data.c_str(), (int)data.size(), 0, (sockaddr*)&d, sizeof(d));
    }
}

void NetworkManager::sendUdpUnicast(const std::string& data, const std::string& ip, uint16_t port) {
    if (m_sendSocket == INVALID_SOCKET) return;
    sockaddr_in d{};
    d.sin_family = AF_INET; d.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &d.sin_addr) != 1) return;
    sendto(m_sendSocket, data.c_str(), (int)data.size(), 0, (sockaddr*)&d, sizeof(d));
}

void NetworkManager::udpListenLoop() {
    char buf[2048];
    sockaddr_in from{};
#ifdef CURRENT_OS_WINDOWS
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    while (m_udpRunning) {
        int n = recvfrom(m_recvSocket, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
        if (n > 0) {
            buf[n] = '\0';
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            if (onUdpMessage) onUdpMessage(std::string(ip), std::string(buf, n));
        } else {
            if (!m_udpRunning) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP helpers
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::recvAllTcp(char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = recv(m_tcpConn, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP Server (host side)
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::startTcpServer(uint16_t port) {
    if (m_tcpServerRunning) return true;

    m_tcpServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_tcpServerSocket == INVALID_SOCKET) {
        log::error("NetworkManager: TCP server socket failed"); return false;
    }
    int yes = 1;
    setsockopt(m_tcpServerSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(m_tcpServerSocket, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        closesocket(m_tcpServerSocket); m_tcpServerSocket = INVALID_SOCKET;
        log::error("NetworkManager: TCP bind failed on :{}", port); return false;
    }
    if (listen(m_tcpServerSocket, 1) == SOCKET_ERROR) {
        closesocket(m_tcpServerSocket); m_tcpServerSocket = INVALID_SOCKET;
        log::error("NetworkManager: TCP listen failed"); return false;
    }
    m_tcpServerRunning = true;
    m_tcpAcceptThread  = std::thread(&NetworkManager::tcpAcceptLoop, this);
    log::info("NetworkManager: TCP server listening on :{}", port);
    return true;
}

void NetworkManager::stopTcpServer() {
    m_tcpServerRunning = false;
    if (m_tcpServerSocket != INVALID_SOCKET) {
        closesocket(m_tcpServerSocket); m_tcpServerSocket = INVALID_SOCKET;
    }
    if (m_tcpAcceptThread.joinable()) m_tcpAcceptThread.join();
}

void NetworkManager::tcpAcceptLoop() {
    while (m_tcpServerRunning) {
        sockaddr_in clientAddr{};
#ifdef CURRENT_OS_WINDOWS
        int clientLen = sizeof(clientAddr);
#else
        socklen_t clientLen = sizeof(clientAddr);
#endif
        SOCKET client = accept(m_tcpServerSocket, (sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) {
            if (!m_tcpServerRunning) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        // Accept only the first client; close any previous connection
        if (m_tcpConn != INVALID_SOCKET) {
            closesocket(m_tcpConn);
            m_tcpConnRunning = false;
            if (m_tcpRecvThread.joinable()) m_tcpRecvThread.join();
        }
        m_tcpConn = client;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        log::info("NetworkManager: TCP guest connected from {}", ip);

        m_tcpConnRunning = true;
        m_tcpRecvThread  = std::thread(&NetworkManager::tcpRecvLoop, this);

        if (onTcpClientConnected) onTcpClientConnected();
        // Only accept one client per server session
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP Client (guest side)
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::connectTcp(const std::string& ip, uint16_t port) {
    if (m_tcpConn != INVALID_SOCKET) disconnectTcp();

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in sa{};
    sa.sin_family = AF_INET; sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        closesocket(s); return false;
    }
    // Try connecting for up to 5 seconds
    if (::connect(s, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        log::error("NetworkManager: TCP connect to {}:{} failed", ip, port);
        closesocket(s); return false;
    }
    m_tcpConn        = s;
    m_tcpConnRunning = true;
    m_tcpRecvThread  = std::thread(&NetworkManager::tcpRecvLoop, this);
    log::info("NetworkManager: TCP connected to {}:{}", ip, port);
    return true;
}

void NetworkManager::disconnectTcp() {
    m_tcpConnRunning = false;
    if (m_tcpConn != INVALID_SOCKET) { closesocket(m_tcpConn); m_tcpConn = INVALID_SOCKET; }
    if (m_tcpRecvThread.joinable()) m_tcpRecvThread.join();
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP shared receive loop (runs for whichever side has a live connection)
// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::tcpRecvLoop() {
    while (m_tcpConnRunning && m_tcpConn != INVALID_SOCKET) {
        // Read 4-byte big-endian length prefix
        uint32_t netLen = 0;
        if (!recvAllTcp(reinterpret_cast<char*>(&netLen), 4)) break;
        uint32_t msgLen = ntohl(netLen);

        if (msgLen == 0 || msgLen > 8 * 1024 * 1024) { // sanity: max 8 MB
            log::warn("NetworkManager: TCP message length {} rejected", msgLen);
            break;
        }
        std::string msg(msgLen, '\0');
        if (!recvAllTcp(msg.data(), (int)msgLen)) break;

        if (onTcpMessage) onTcpMessage(msg);
    }
    log::info("NetworkManager: TCP connection closed");
    m_tcpConnRunning = false;
    if (m_tcpConn != INVALID_SOCKET) { closesocket(m_tcpConn); m_tcpConn = INVALID_SOCKET; }
    if (onTcpDisconnected) onTcpDisconnected();
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP send — length-prefixed, mutex-protected
// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::sendTcp(const std::string& data) {
    if (m_tcpConn == INVALID_SOCKET) return false;
    std::lock_guard<std::mutex> lock(m_tcpSendMutex);

    uint32_t netLen = htonl((uint32_t)data.size());
    // Send length prefix
    if (send(m_tcpConn, reinterpret_cast<char*>(&netLen), 4, 0) != 4) return false;
    // Send body
    int total = 0;
    while (total < (int)data.size()) {
        int n = send(m_tcpConn, data.c_str() + total, (int)data.size() - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}
