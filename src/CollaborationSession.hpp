#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
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

    // Send a collaboration invite to a specific peer by IP.
    void sendCollabInvite(const std::string& targetIp);

    // Send an accept/decline response back to the inviting peer.
    void sendCollabResponse(const std::string& targetIp, bool accepted);

    // Set by main.cpp — called on the main thread when a remote invite arrives.
    // Args: (fromUsername, fromIp)
    std::function<void(const std::string&, const std::string&)> onCollabInviteReceived;

    // Set by main.cpp — called on the main thread with the invitee's response.
    // Args: (fromUsername, accepted)
    std::function<void(const std::string&, bool)> onCollabResponseReceived;

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
