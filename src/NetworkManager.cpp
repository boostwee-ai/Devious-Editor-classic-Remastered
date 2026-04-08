#include "NetworkManager.hpp"
#include <iostream>

#ifdef CURRENT_OS_WINDOWS
#pragma comment(lib, "ws2_32.lib")
#endif

NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::~NetworkManager() {
    cleanup();
}

bool NetworkManager::init() {
#ifdef CURRENT_OS_WINDOWS
    if (!m_wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
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

bool NetworkManager::startUdpDiscovery(uint16_t port) {
    if (m_running) return true;

    m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udpSocket == INVALID_SOCKET) return false;

    // Allow broadcast
    int broadcastEnable = 1;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));

    // Allow port reuse so multiple instances on same machine could listen (useful for testing)
    int reuse = 1;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in recvAddr{};
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(port);
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(m_udpSocket, (sockaddr*)&recvAddr, sizeof(recvAddr)) == SOCKET_ERROR) {
#ifdef CURRENT_OS_WINDOWS
        closesocket(m_udpSocket);
#else
        close(m_udpSocket);
#endif
        m_udpSocket = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_udpThread = std::thread(&NetworkManager::udpListenLoop, this);
    return true;
}

void NetworkManager::stopUdpDiscovery() {
    m_running = false;
    if (m_udpSocket != INVALID_SOCKET) {
#ifdef CURRENT_OS_WINDOWS
        closesocket(m_udpSocket);
#else
        close(m_udpSocket);
#endif
        m_udpSocket = INVALID_SOCKET;
    }
    if (m_udpThread.joinable()) {
        m_udpThread.join();
    }
}

void NetworkManager::sendUdpBroadcast(const std::string& data, uint16_t port) {
    if (m_udpSocket == INVALID_SOCKET) return;

    sockaddr_in sendAddr{};
    sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(port);
#ifdef CURRENT_OS_WINDOWS
    sendAddr.sin_addr.s_addr = INADDR_BROADCAST;
#else
    sendAddr.sin_addr.s_addr = 0xFFFFFFFF; // 255.255.255.255
#endif

    sendto(m_udpSocket, data.c_str(), static_cast<int>(data.length()), 0, (sockaddr*)&sendAddr, sizeof(sendAddr));
}

void NetworkManager::udpListenLoop() {
    char buffer[1024];
    sockaddr_in senderAddr{};
#ifdef CURRENT_OS_WINDOWS
    int senderAddrSize = sizeof(senderAddr);
#else
    socklen_t senderAddrSize = sizeof(senderAddr);
#endif

    while (m_running) {
        int bytesRead = recvfrom(m_udpSocket, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&senderAddr, &senderAddrSize);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &senderAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
            std::string fromIp(ipStr);

            if (onUdpMessage) {
                onUdpMessage(fromIp, std::string(buffer));
            }
        } else {
            // Socket error or closed
            if (!m_running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
