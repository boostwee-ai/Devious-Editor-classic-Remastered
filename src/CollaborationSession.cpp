#include "CollaborationSession.hpp"
#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <algorithm>

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
CollaborationSession& CollaborationSession::get() {
    static CollaborationSession instance;
    return instance;
}

void CollaborationSession::init() {
    if (m_initialized) return;
    m_initialized = true;
    NetworkManager::get().init();
    NetworkManager::get().onUdpMessage = [this](const std::string& ip, const std::string& msg) {
        this->handleUdpMessage(ip, msg);
    };
}

bool CollaborationSession::isCollabEnabled() const {
    return Mod::get()->getSettingValue<bool>("allow-collab");
}

// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::onEnterEditor() {
    m_inEditor = true;
    m_discoveryTimer = 0.f;
    // Guard: GJAccountManager may not be ready on Windows if called too early
    auto* acctMgr = GJAccountManager::get();
    if (acctMgr) {
        m_localUsername = acctMgr->m_username;
    }
    if (m_localUsername.empty()) {
        m_localUsername = "Unknown";
    }
    if (isCollabEnabled()) {
        NetworkManager::get().startUdpDiscovery(54321);
    }
}

void CollaborationSession::onExitEditor() {
    m_inEditor = false;
    NetworkManager::get().stopUdpDiscovery();
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<DiscoveredUser> CollaborationSession::getDiscoveredUsers() {
    std::lock_guard<std::mutex> lock(m_usersMutex);
    return m_discoveredUsers;
}

void CollaborationSession::forceBroadcastNow() {
    if (!m_inEditor || !isCollabEnabled()) return;

    matjson::Value payload = matjson::Value::object();
    payload["type"]     = static_cast<int>(Packets::MessageType::DiscoveryRequest);
    payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    payload["user"]     = m_localUsername;

    NetworkManager::get().sendUdpBroadcast(payload.dump(matjson::NO_INDENTATION), 54321);
    m_discoveryTimer = 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::sendCollabInvite(const std::string& targetIp) {
    matjson::Value payload = matjson::Value::object();
    payload["type"]     = static_cast<int>(Packets::MessageType::CollabRequest);
    payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    payload["user"]     = m_localUsername;

    NetworkManager::get().sendUdpUnicast(payload.dump(matjson::NO_INDENTATION), targetIp, 54321);
    log::info("CollaborationSession: sent collab invite to {}", targetIp);
}

void CollaborationSession::sendCollabResponse(const std::string& targetIp, bool accepted) {
    matjson::Value payload = matjson::Value::object();
    payload["type"]     = static_cast<int>(Packets::MessageType::CollabResponse);
    payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    payload["user"]     = m_localUsername;
    payload["accepted"] = accepted;

    NetworkManager::get().sendUdpUnicast(payload.dump(matjson::NO_INDENTATION), targetIp, 54321);
    log::info("CollaborationSession: sent collab response ({}) to {}", accepted ? "accept" : "decline", targetIp);
}

// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::update(float dt) {
    if (!m_inEditor || !isCollabEnabled()) return;

    m_discoveryTimer += dt;
    if (m_discoveryTimer >= 2.0f) {
        m_discoveryTimer = 0.f;

        matjson::Value payload = matjson::Value::object();
        payload["type"]     = static_cast<int>(Packets::MessageType::DiscoveryRequest);
        payload["platform"] = static_cast<int>(Packets::getCurrentPlatform());
        payload["user"]     = m_localUsername;

        NetworkManager::get().sendUdpBroadcast(payload.dump(matjson::NO_INDENTATION), 54321);

        // Prune timed-out peers (no heartbeat for 10 seconds)
        std::lock_guard<std::mutex> lock(m_usersMutex);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        m_discoveredUsers.erase(
            std::remove_if(m_discoveredUsers.begin(), m_discoveredUsers.end(), [now](const DiscoveredUser& u) {
                return (now - u.lastSeen) > 10.0;
            }),
            m_discoveredUsers.end()
        );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::handleUdpMessage(const std::string& ip, const std::string& message) {
    if (message.empty() || message.length() > 1024) return;

    // All game/UI operations must happen on the main thread
    Loader::get()->queueInMainThread([this, ip, message] {
        log::debug("CollaborationSession: UDP from {}: {}", ip, message);

        try {
            auto parseResult = matjson::parse(message);
            if (!parseResult.isOk()) return;
            matjson::Value parsed = parseResult.unwrap();

            if (!parsed.contains("type") || !parsed.contains("platform")) return;

            int type = parsed["type"].asInt().unwrapOr(-1);

            auto sanitize = [](std::string str) {
                str.erase(std::remove_if(str.begin(), str.end(), [](unsigned char c) {
                    return c < 32 || c > 126;
                }), str.end());
                return str;
            };

            std::string username  = sanitize(parsed["user"].asString().unwrapOr("Unknown"));
            std::string safeIp    = sanitize(ip);
            Packets::Platform plat = static_cast<Packets::Platform>(parsed["platform"].asInt().unwrapOr(2));

            // ── Discovery heartbeat ──────────────────────────────────────────
            if (type == static_cast<int>(Packets::MessageType::DiscoveryRequest)) {
                if (username == m_localUsername) return; // ignore own packets

                std::lock_guard<std::mutex> lock(m_usersMutex);
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                bool found = false;
                for (auto& user : m_discoveredUsers) {
                    if (user.ip == safeIp) {
                        user.lastSeen = now;
                        user.username = username;
                        user.platform = plat;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    log::debug("CollaborationSession: discovered {} ({})", username, safeIp);
                    m_discoveredUsers.push_back({username, safeIp, plat, static_cast<double>(now)});
                }
            }

            // ── Incoming collaboration invite ────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::CollabRequest)) {
                log::info("CollaborationSession: collab invite from {} ({})", username, safeIp);
                if (onCollabInviteReceived) {
                    onCollabInviteReceived(username, safeIp);
                }
            }

            // ── Response to our own invite ───────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::CollabResponse)) {
                bool accepted = parsed["accepted"].asBool().unwrapOr(false);
                log::info("CollaborationSession: {} {} our invite", username, accepted ? "accepted" : "declined");
                if (onCollabResponseReceived) {
                    onCollabResponseReceived(username, accepted);
                }
            }

        } catch (...) {
            // malformed JSON — ignore
        }
    });
}
