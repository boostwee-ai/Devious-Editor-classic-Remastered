#include "CollaborationSession.hpp"
#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <algorithm>

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
    m_localUsername = GJAccountManager::get()->m_username;
    if (isCollabEnabled()) {
        NetworkManager::get().startUdpDiscovery(54321); // Default discovery port
    }
}

void CollaborationSession::onExitEditor() {
    m_inEditor = false;
    NetworkManager::get().stopUdpDiscovery();
}

std::vector<DiscoveredUser> CollaborationSession::getDiscoveredUsers() {
    std::lock_guard<std::mutex> lock(m_usersMutex);
    return m_discoveredUsers;
}

void CollaborationSession::update(float dt) {
    if (!m_inEditor || !isCollabEnabled()) return;

    m_discoveryTimer += dt;
    // Broadcast presence every 2 seconds
    if (m_discoveryTimer >= 2.0f) {
        m_discoveryTimer = 0.f;
        
        // Simple JSON payload for discovery
        matjson::Value payload = matjson::Value::object();
        payload["type"] = static_cast<int>(Packets::MessageType::DiscoveryRequest);
        payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
        payload["user"] = m_localUsername;

        NetworkManager::get().sendUdpBroadcast(payload.dump(matjson::NO_INDENTATION), 54321);

        // Prune old users (timed out)
        std::lock_guard<std::mutex> lock(m_usersMutex);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        m_discoveredUsers.erase(
            std::remove_if(m_discoveredUsers.begin(), m_discoveredUsers.end(), [now](const DiscoveredUser& user) {
                return (now - user.lastSeen) > 10.0; // 10 second timeout
            }),
            m_discoveredUsers.end()
        );
    }
}

void CollaborationSession::handleUdpMessage(const std::string& ip, const std::string& message) {
    if (message.empty() || message.length() > 1024) return;
    // Basic verification and platform check
    try {
        auto parseResult = matjson::parse(message);
        if (!parseResult.isOk()) return;
        matjson::Value parsed = parseResult.unwrap();
        if (!parsed.contains("type") || !parsed.contains("platform")) return;

        Packets::Platform remotePlatform = static_cast<Packets::Platform>(parsed["platform"].asInt().unwrapOr(-1));
        std::string username = parsed["user"].asString().unwrapOr("Unknown");

        int type = parsed["type"].asInt().unwrapOr(-1);
        if (type == static_cast<int>(Packets::MessageType::DiscoveryRequest)) {
            // Someone is broadcasting. Add them to the UI list if they aren't us.
            if (username == m_localUsername) return;

            std::lock_guard<std::mutex> lock(m_usersMutex);
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            bool found = false;
            for (auto& user : m_discoveredUsers) {
                if (user.ip == ip) {
                    user.lastSeen = now;
                    user.username = username;
                    user.platform = remotePlatform;
                    found = true;
                    break;
                }
            }

            if (!found) {
                m_discoveredUsers.push_back({username, ip, remotePlatform, static_cast<double>(now)});
            }
        }

    } catch (...) {
        // invalid json
    }
}
