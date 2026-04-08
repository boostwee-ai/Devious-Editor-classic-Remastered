#include "CollaborationSession.hpp"
#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

CollaborationSession& CollaborationSession::get() {
    static CollaborationSession instance;
    return instance;
}

void CollaborationSession::init() {
    NetworkManager::get().init();
    NetworkManager::get().onUdpMessage = [this](const std::string& ip, const std::string& msg) {
        this->handleUdpMessage(ip, msg);
    };
}

bool CollaborationSession::isCollabEnabled() const {
    return Mod::get()->getSettingValue<bool>("allow-collab");
}

void CollaborationSession::onEnterEditor() {
    m_inEditor = true;
    m_discoveryTimer = 0.f;
    if (isCollabEnabled()) {
        NetworkManager::get().startUdpDiscovery(54321); // Default discovery port
    }
}

void CollaborationSession::onExitEditor() {
    m_inEditor = false;
    NetworkManager::get().stopUdpDiscovery();
}

void CollaborationSession::update(float dt) {
    if (!m_inEditor || !isCollabEnabled()) return;

    m_discoveryTimer += dt;
    // Broadcast presence every 2 seconds
    if (m_discoveryTimer >= 2.0f) {
        m_discoveryTimer = 0.f;
        
        // Simple JSON payload for discovery
        matjson::Value payload = matjson::Object();
        payload["type"] = static_cast<int>(Packets::MessageType::DiscoveryRequest);
        payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
        payload["user"] = GJAccountManager::get()->m_username;

        NetworkManager::get().sendUdpBroadcast(payload.dump(matjson::NO_INDENTATION), 54321);
    }
}

void CollaborationSession::handleUdpMessage(const std::string& ip, const std::string& message) {
    // Basic verification and platform check
    try {
        matjson::Value parsed = matjson::parse(message);
        if (!parsed.contains("type") || !parsed.contains("platform")) return;

        Packets::Platform remotePlatform = static_cast<Packets::Platform>(parsed["platform"].asInt().unwrapOr(-1));
        Packets::Platform localPlatform = Packets::getCurrentPlatform();

        // Enforce same-OS constraint!
        if (remotePlatform != localPlatform) {
            // Ignore messages from other OS platforms
            return;
        }

        int type = parsed["type"].asInt().unwrapOr(-1);
        if (type == static_cast<int>(Packets::MessageType::DiscoveryRequest)) {
            // Someone is broadcasting. Add them to the UI list if they aren't us.
            // (Handling omitted for brevity, would dispatch to main thread UI)
        }

    } catch (...) {
        // invalid json
    }
}
