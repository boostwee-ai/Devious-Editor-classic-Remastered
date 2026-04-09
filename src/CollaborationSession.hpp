#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <atomic>
#include "NetworkMessages.hpp"
#include <Geode/utils/JsonValidation.hpp>

// Forward declaration — avoids pulling in full Geode headers here.
namespace cocos2d { class CCNode; }
class GameObject;
class LevelEditorLayer;

// ─────────────────────────────────────────────────────────────────────────────
struct DiscoveredUser {
    std::string username;
    std::string ip;
    Packets::Platform platform;
    double lastSeen; // Unix timestamp (seconds)
};

// Snapshot of the GD level settings used to detect changes
struct CachedLevelSettings {
    int  bg        = -1;
    int  ground    = -1;
    int  speed     = -1;
    int  gameMode  = -1;
    bool platformer  = false;
    bool twoPlayer   = false;
    bool operator==(const CachedLevelSettings& o) const {
        return bg == o.bg && ground == o.ground && speed == o.speed
            && gameMode == o.gameMode && platformer == o.platformer
            && twoPlayer == o.twoPlayer;
    }
    bool operator!=(const CachedLevelSettings& o) const { return !(*this == o); }
};

// ─────────────────────────────────────────────────────────────────────────────
class CollaborationSession {
public:
    static CollaborationSession& get();

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init();

    // Called by LevelEditorLayer hooks
    void onEnterEditor();
    void onExitEditor();
    void update(float dt);

    // ── Discovery ─────────────────────────────────────────────────────────────
    bool isCollabEnabled() const;
    void forceBroadcastNow();
    std::vector<DiscoveredUser> getDiscoveredUsers();

    // ── Invite flow ───────────────────────────────────────────────────────────
    void sendCollabInvite(const std::string& targetIp);
    void sendCollabResponse(const std::string& targetIp, bool accepted);

    // ── Session control ───────────────────────────────────────────────────────
    // Called from main.cpp when the responder accepts.
    void setHostMode(const std::string& guestIp, const std::string& guestUsername);
    // Called from IncomingInvitePopup on Accept.
    void startGuestSession(const std::string& hostIp, const std::string& hostUsername);

    bool isInSession() const;
    bool isHost()         const { return m_state == SessionState::Host; }
    bool isGuest()        const { return m_state == SessionState::Guest; }
    bool isApplyingRemote() const { return m_applyingRemoteChange; }
    void resetToIdle(); // cancel WaitingForResponse (invite declined)

    // ── Object sync (called by editor hooks in main.cpp) ──────────────────────
    // Pass the LevelEditorLayer so session can serialize objects without storing a pointer.
    void onLocalObjectPlaced(GameObject* obj, LevelEditorLayer* edl);
    void onLocalObjectRemoved(GameObject* obj);

    // ── TCP handling ──────────────────────────────────────────────────────────
    // Call from main.cpp onTcpClientConnected callback.
    void sendLevelInitViaTcp(LevelEditorLayer* edl);
    // Call from main.cpp onTcpMessage callback (already on main thread).
    void handleTcpMessage(const std::string& data, LevelEditorLayer* edl);

    // ── Callbacks wired by main.cpp ───────────────────────────────────────────
    // (username, ip, accepted)
    std::function<void(const std::string&, const std::string&, bool)> onCollabResponseReceived;
    // (fromUsername, fromIp)
    std::function<void(const std::string&, const std::string&)> onCollabInviteReceived;

private:
    CollaborationSession() = default;

    enum class SessionState { Idle, WaitingForResponse, Host, Guest };
    SessionState m_state  = SessionState::Idle;
    std::string  m_peerIp;
    std::string  m_peerUsername;

    // ── Discovery state ───────────────────────────────────────────────────────
    bool m_initialized    = false;
    bool m_inEditor       = false;
    float m_discoveryTimer = 0.f;
    std::string m_localUsername;
    std::vector<DiscoveredUser> m_discoveredUsers;
    std::mutex  m_usersMutex;

    // ── Session timers ────────────────────────────────────────────────────────
    float m_cursorTimer   = 0.f;
    float m_settingsTimer = 0.f;
    CachedLevelSettings m_lastSettings;

    // ── Object UID tracking ───────────────────────────────────────────────────
    std::unordered_map<std::string, GameObject*> m_uidToObject;
    std::unordered_map<GameObject*, std::string> m_objectToUid;
    uint64_t m_uidCounter = 0;

    // Flag: suppress re-broadcasting when we're the ones creating/deleting a remote object
    bool m_applyingRemoteChange = false;

    // ── Internals ─────────────────────────────────────────────────────────────
    void handleUdpMessage(const std::string& ip, const std::string& msg);
    void broadcastCursorPosition(LevelEditorLayer* edl);
    void checkAndSyncSettings(LevelEditorLayer* edl);
    CachedLevelSettings readLevelSettings(LevelEditorLayer* edl);
    void applyLevelSettings(const CachedLevelSettings& s, LevelEditorLayer* edl);
    void applyObjectPlace(const std::string& uid, int oid, float x, float y, float rot, float sx, float sy, LevelEditorLayer* edl);
    void applyObjectDelete(const std::string& uid, LevelEditorLayer* edl);

    std::string serializeAllObjects(LevelEditorLayer* edl);
    static std::string fmtFloat(float f);

    void sendUdpToSession(const std::string& data);
};
