#pragma once

#include <string>
#include <vector>
#include "NetworkMessages.hpp"

// We use JSON for string serialization over UDP
#include <Geode/utils/JsonValidation.hpp>

class CollaborationSession {
public:
    static CollaborationSession& get();

    void init();
    void update(float dt);

    void onEnterEditor();
    void onExitEditor();

    bool isCollabEnabled() const;

private:
    CollaborationSession() = default;
    
    // Callbacks
    void handleUdpMessage(const std::string& ip, const std::string& message);
    
    bool m_inEditor = false;
    float m_discoveryTimer = 0.f;
};
