#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "NetworkMessages.hpp"

// We use JSON for string serialization over UDP
#include <Geode/utils/JsonValidation.hpp>

struct DiscoveredUser {
    std::string username;
    std::string ip;
    Packets::Platform platform;
    double lastSeen; // timestamp in seconds
};

class CollaborationSession {
public:
    static CollaborationSession& get();

    void init();
    void update(float dt);

    void onEnterEditor();
    void onExitEditor();

    bool isCollabEnabled() const;
    
    // Force an immediate broadcast without waiting for the 2s update tick.
    // Call this when the discovery popup is first opened.
    void forceBroadcastNow();

    std::vector<DiscoveredUser> getDiscoveredUsers();

private:
    CollaborationSession() = default;
    
    // Callbacks
    void handleUdpMessage(const std::string& ip, const std::string& message);
    
    bool m_initialized = false;
    bool m_inEditor = false;
    float m_discoveryTimer = 0.f;
    std::string m_localUsername;
    std::vector<DiscoveredUser> m_discoveredUsers;
    std::mutex m_usersMutex;
};
