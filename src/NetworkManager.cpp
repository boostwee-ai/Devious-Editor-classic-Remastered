#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

#ifdef CURRENT_OS_WINDOWS
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

// ─────────────────────────────────────────────────────────────────────────────
NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::~NetworkManager() {
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::init() {
#ifdef CURRENT_OS_WINDOWS
    if (!m_wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            log::error("WSAStartup failed");
            return false;
        }
        m_wsaInitialized = true;
    }
#endif
    return true;
}

void NetworkManager::cleanup() {
    stopUdpDiscovery();
#ifdef CURRENT_OS_WINDOWS
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface enumeration — returns directed broadcast addresses for every
// active, non-loopback interface.  Both Windows and macOS paths are covered.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<in_addr> NetworkManager::getBroadcastAddresses() {
    std::vector<in_addr> addrs;

#ifdef CURRENT_OS_WINDOWS
    // Use GetAdaptersInfo for IPv4 broadcast calculation
    ULONG bufSize = 15 * 1024;
    std::vector<BYTE> buf(bufSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());

    if (GetAdaptersInfo(adapters, &bufSize) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufSize);
        adapters = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    }

    if (GetAdaptersInfo(adapters, &bufSize) == NO_ERROR) {
        for (auto* a = adapters; a != nullptr; a = a->Next) {
            for (auto* ipInfo = &a->IpAddressList; ipInfo != nullptr; ipInfo = ipInfo->Next) {
                in_addr ip{}, mask{};
                if (inet_pton(AF_INET, ipInfo->IpAddress.String, &ip) != 1) continue;
                if (inet_pton(AF_INET, ipInfo->IpMask.String, &mask) != 1) continue;
                // Skip loopback and 0.0.0.0
                if ((ntohl(ip.s_addr) >> 24) == 127) continue;
                if (ip.s_addr == 0) continue;
                // directed broadcast = (ip & mask) | (~mask)
                in_addr bcast{};
                bcast.s_addr = (ip.s_addr & mask.s_addr) | (~mask.s_addr);
                addrs.push_back(bcast);
                char tmp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &bcast, tmp, sizeof(tmp));
                log::debug("NetworkManager: broadcast addr = {}", tmp);
            }
        }
    }
#else
    // macOS / Linux
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return addrs;

    for (auto* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;  // must support broadcast
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!ifa->ifa_broadaddr) continue;

        auto* bcast = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
        addrs.push_back(bcast->sin_addr);
        char tmp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &bcast->sin_addr, tmp, sizeof(tmp));
        log::debug("NetworkManager: broadcast addr = {}", tmp);
    }
    freeifaddrs(ifap);
#endif

    // Always fall back to limited broadcast so we have at least one target
    if (addrs.empty()) {
        in_addr fallback{};
        fallback.s_addr = 0xFFFFFFFF; // 255.255.255.255
        addrs.push_back(fallback);
        log::warn("NetworkManager: no interfaces found, falling back to 255.255.255.255");
    }

    return addrs;
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::startUdpDiscovery(uint16_t port) {
    if (m_running) return true;

    // ── Receive socket ────────────────────────────────────────────────────────
    m_recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_recvSocket == INVALID_SOCKET) {
        log::error("NetworkManager: failed to create recv socket");
        return false;
    }

    int yes = 1;
    setsockopt(m_recvSocket, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
    setsockopt(m_recvSocket, SOL_SOCKET, SO_REUSEADDR,  (char*)&yes, sizeof(yes));
#ifndef CURRENT_OS_WINDOWS
    // SO_REUSEPORT is required on macOS to allow multiple processes to bind
    // the same UDP port (e.g. multiple GD instances for testing)
    setsockopt(m_recvSocket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in recvAddr{};
    recvAddr.sin_family      = AF_INET;
    recvAddr.sin_port        = htons(port);
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(m_recvSocket, (sockaddr*)&recvAddr, sizeof(recvAddr)) == SOCKET_ERROR) {
        log::error("NetworkManager: bind failed on port {}", port);
        closesocket(m_recvSocket);
        m_recvSocket = INVALID_SOCKET;
        return false;
    }

    // ── Send socket ───────────────────────────────────────────────────────────
    // Deliberately NOT bound to a specific port so the OS picks an ephemeral
    // source port.  This avoids the "socket is already bound" conflict and also
    // prevents self-reception of our own broadcasts on most platforms.
    m_sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sendSocket == INVALID_SOCKET) {
        log::error("NetworkManager: failed to create send socket");
        closesocket(m_recvSocket);
        m_recvSocket = INVALID_SOCKET;
        return false;
    }
    setsockopt(m_sendSocket, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));

    m_running    = true;
    m_udpThread  = std::thread(&NetworkManager::udpListenLoop, this);
    log::info("NetworkManager: UDP discovery started on port {}", port);
    return true;
}

void NetworkManager::stopUdpDiscovery() {
    m_running = false;

    // Closing the recv socket unblocks the blocking recvfrom() in the thread
    if (m_recvSocket != INVALID_SOCKET) {
        closesocket(m_recvSocket);
        m_recvSocket = INVALID_SOCKET;
    }
    if (m_sendSocket != INVALID_SOCKET) {
        closesocket(m_sendSocket);
        m_sendSocket = INVALID_SOCKET;
    }
    if (m_udpThread.joinable()) {
        m_udpThread.join();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::sendUdpBroadcast(const std::string& data, uint16_t port) {
    if (m_sendSocket == INVALID_SOCKET) return;

    auto broadcastAddrs = getBroadcastAddresses();
    for (const auto& bcast : broadcastAddrs) {
        sockaddr_in dest{};
        dest.sin_family      = AF_INET;
        dest.sin_port        = htons(port);
        dest.sin_addr        = bcast;

        int sent = sendto(m_sendSocket, data.c_str(), static_cast<int>(data.length()),
                          0, (sockaddr*)&dest, sizeof(dest));
        if (sent == SOCKET_ERROR) {
            char tmp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &bcast, tmp, sizeof(tmp));
            log::warn("NetworkManager: sendto {} failed", tmp);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::sendUdpUnicast(const std::string& data, const std::string& ip, uint16_t port) {
    if (m_sendSocket == INVALID_SOCKET) return;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) != 1) {
        log::error("NetworkManager: invalid target IP '{}'", ip);
        return;
    }

    int sent = sendto(m_sendSocket, data.c_str(), static_cast<int>(data.length()),
                      0, (sockaddr*)&dest, sizeof(dest));
    if (sent == SOCKET_ERROR) {
        log::warn("NetworkManager: unicast sendto {} failed", ip);
    } else {
        log::debug("NetworkManager: unicast {} bytes -> {}:{}", sent, ip, port);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::udpListenLoop() {
    char buffer[1024];
    sockaddr_in senderAddr{};
#ifdef CURRENT_OS_WINDOWS
    int senderAddrSize = sizeof(senderAddr);
#else
    socklen_t senderAddrSize = sizeof(senderAddr);
#endif

    while (m_running) {
        int bytesRead = recvfrom(m_recvSocket, buffer, sizeof(buffer) - 1, 0,
                                 (sockaddr*)&senderAddr, &senderAddrSize);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';

            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &senderAddr.sin_addr, ipStr, sizeof(ipStr));

            if (onUdpMessage) {
                onUdpMessage(std::string(ipStr), std::string(buffer, bytesRead));
            }
        } else {
            // Either socket was closed (m_running == false) or a transient error
            if (!m_running) break;
            // Brief sleep avoids a hot-spin on persistent errors
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    log::debug("NetworkManager: UDP listen loop exited");
}
