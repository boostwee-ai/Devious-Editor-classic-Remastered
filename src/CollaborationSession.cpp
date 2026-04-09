#include "CollaborationSession.hpp"
#include "NetworkManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cmath>

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
// Remote cursor tag range: 10000 … 10009 (won't clash with GD's object tags)
static constexpr int CURSOR_BASE_TAG = 10000;

// ─────────────────────────────────────────────────────────────────────────────
std::string CollaborationSession::fmtFloat(float f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", f);
    return buf;
}

void CollaborationSession::sendUdpToSession(const std::string& data) {
    if (m_peerIp.empty()) return;
    NetworkManager::get().sendUdpUnicast(data, m_peerIp, 54321);
}

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
CollaborationSession& CollaborationSession::get() {
    static CollaborationSession instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::init() {
    if (m_initialized) return;
    m_initialized = true;

    NetworkManager::get().init();

    NetworkManager::get().onUdpMessage = [this](const std::string& ip, const std::string& msg) {
        handleUdpMessage(ip, msg);
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor lifecycle
// ─────────────────────────────────────────────────────────────────────────────
bool CollaborationSession::isCollabEnabled() const {
    return Mod::get()->getSettingValue<bool>("allow-collab");
}

void CollaborationSession::onEnterEditor() {
    m_inEditor       = true;
    m_discoveryTimer = 0.f;
    m_cursorTimer    = 0.f;
    m_settingsTimer  = 0.f;

    auto* acct = GJAccountManager::get();
    m_localUsername = (acct && !acct->m_username.empty()) ? acct->m_username : "Unknown";

    if (isCollabEnabled()) {
        NetworkManager::get().startUdpDiscovery(54321);
    }
}

void CollaborationSession::onExitEditor() {
    if (isInSession()) {
        // Notify peer the session is ending
        matjson::Value p = matjson::Value::object();
        p["type"] = static_cast<int>(Packets::MessageType::SessionEnd);
        p["user"] = m_localUsername;
        sendUdpToSession(p.dump(matjson::NO_INDENTATION));
    }
    m_inEditor  = false;
    m_state     = SessionState::Idle;
    m_peerIp    = "";
    m_peerUsername = "";
    m_uidToObject.clear();
    m_objectToUid.clear();
    m_uidCounter = 0;
    NetworkManager::get().stopUdpDiscovery();
    NetworkManager::get().disconnectTcp();
    NetworkManager::get().stopTcpServer();
}

bool CollaborationSession::isInSession() const {
    return m_state == SessionState::Host || m_state == SessionState::Guest;
}

// ─────────────────────────────────────────────────────────────────────────────
// Session control
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::setHostMode(const std::string& guestIp, const std::string& guestUsername) {
    m_state        = SessionState::Host;
    m_peerIp       = guestIp;
    m_peerUsername = guestUsername;
    log::info("CollaborationSession: HOST session with {} ({})", guestUsername, guestIp);
}

void CollaborationSession::startGuestSession(const std::string& hostIp, const std::string& hostUsername) {
    m_state        = SessionState::Guest;
    m_peerIp       = hostIp;
    m_peerUsername = hostUsername;
    log::info("CollaborationSession: GUEST session with {} ({})", hostUsername, hostIp);

    // Connect on a background thread so the UI doesn't block
    std::thread([hostIp]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // let host server start accept()
        if (!NetworkManager::get().connectTcp(hostIp, 54322)) {
            log::error("CollaborationSession: failed to connect TCP to host");
        }
    }).detach();
}

void CollaborationSession::resetToIdle() {
    m_state = SessionState::Idle;
    m_peerIp.clear();
    m_peerUsername.clear();
    NetworkManager::get().stopTcpServer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Discovery
// ─────────────────────────────────────────────────────────────────────────────
std::vector<DiscoveredUser> CollaborationSession::getDiscoveredUsers() {
    std::lock_guard<std::mutex> lock(m_usersMutex);
    return m_discoveredUsers;
}

void CollaborationSession::forceBroadcastNow() {
    if (!m_inEditor || !isCollabEnabled()) return;
    matjson::Value p = matjson::Value::object();
    p["type"]     = static_cast<int>(Packets::MessageType::DiscoveryRequest);
    p["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    p["user"]     = m_localUsername;
    NetworkManager::get().sendUdpBroadcast(p.dump(matjson::NO_INDENTATION), 54321);
    m_discoveryTimer = 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Invite flow
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::sendCollabInvite(const std::string& targetIp) {
    matjson::Value p = matjson::Value::object();
    p["type"]     = static_cast<int>(Packets::MessageType::CollabRequest);
    p["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    p["user"]     = m_localUsername;
    NetworkManager::get().sendUdpUnicast(p.dump(matjson::NO_INDENTATION), targetIp, 54321);

    m_state = SessionState::WaitingForResponse;
    // Start TCP server optimistically — guest will connect if they accept
    NetworkManager::get().startTcpServer(54322);
    log::info("CollaborationSession: invite sent to {}, TCP server ready", targetIp);
}

void CollaborationSession::sendCollabResponse(const std::string& targetIp, bool accepted) {
    matjson::Value p = matjson::Value::object();
    p["type"]     = static_cast<int>(Packets::MessageType::CollabResponse);
    p["platform"] = static_cast<int>(Packets::getCurrentPlatform());
    p["user"]     = m_localUsername;
    p["accepted"] = accepted;
    NetworkManager::get().sendUdpUnicast(p.dump(matjson::NO_INDENTATION), targetIp, 54321);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update (called every frame from LevelEditorLayer hook)
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::update(float dt) {
    auto* edl = LevelEditorLayer::get();

    // ── Discovery heartbeat ──────────────────────────────────────────────────
    if (m_inEditor && isCollabEnabled()) {
        m_discoveryTimer += dt;
        if (m_discoveryTimer >= 2.0f) {
            m_discoveryTimer = 0.f;
            matjson::Value p = matjson::Value::object();
            p["type"]     = static_cast<int>(Packets::MessageType::DiscoveryRequest);
            p["platform"] = static_cast<int>(Packets::getCurrentPlatform());
            p["user"]     = m_localUsername;
            NetworkManager::get().sendUdpBroadcast(p.dump(matjson::NO_INDENTATION), 54321);

            // Prune timed-out peers
            std::lock_guard<std::mutex> lock(m_usersMutex);
            auto now = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            m_discoveredUsers.erase(
                std::remove_if(m_discoveredUsers.begin(), m_discoveredUsers.end(),
                    [now](const DiscoveredUser& u) { return (now - u.lastSeen) > 10.0; }),
                m_discoveredUsers.end());
        }
    }

    if (!isInSession() || !edl) return;

    // ── Cursor broadcast (every 150 ms) ──────────────────────────────────────
    m_cursorTimer += dt;
    if (m_cursorTimer >= 0.15f) {
        m_cursorTimer = 0.f;
        broadcastCursorPosition(edl);
    }

    // ── Settings change detection (every 500 ms, host only) ──────────────────
    if (isHost()) {
        m_settingsTimer += dt;
        if (m_settingsTimer >= 0.5f) {
            m_settingsTimer = 0.f;
            checkAndSyncSettings(edl);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cursor position broadcast
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::broadcastCursorPosition(LevelEditorLayer* edl) {
    if (!edl || !edl->m_objectLayer) return;
    auto objLayerPos = edl->m_objectLayer->getPosition();
    auto winSize     = CCDirector::sharedDirector()->getWinSize();
    float worldX = -objLayerPos.x + winSize.width  / 2.f;
    float worldY = -objLayerPos.y + winSize.height / 2.f;

    matjson::Value p = matjson::Value::object();
    p["type"] = static_cast<int>(Packets::MessageType::CursorMove);
    p["x"]    = static_cast<double>(worldX);
    p["y"]    = static_cast<double>(worldY);
    p["user"] = m_localUsername;
    sendUdpToSession(p.dump(matjson::NO_INDENTATION));
}

// ─────────────────────────────────────────────────────────────────────────────
// Level settings read / write / sync
// ─────────────────────────────────────────────────────────────────────────────
CachedLevelSettings CollaborationSession::readLevelSettings(LevelEditorLayer* edl) {
    CachedLevelSettings s{};
    if (!edl) return s;

    // Field names verified against Geode SDK / GD 2.2081 bindings:
    auto* ls = edl->m_levelSettings;
    if (!ls) return s;

    s.bg         = ls->m_startBG;
    s.ground     = ls->m_startGround;
    s.speed      = static_cast<int>(ls->m_startSpeed);
    s.gameMode   = static_cast<int>(ls->m_startMode);
    s.platformer = ls->m_platformerMode;
    s.twoPlayer  = ls->m_twoPlayerMode;
    return s;
}

void CollaborationSession::applyLevelSettings(const CachedLevelSettings& s, LevelEditorLayer* edl) {
    if (!edl) return;
    auto* ls = edl->m_levelSettings;
    if (!ls) return;

    ls->m_startBG       = s.bg;
    ls->m_startGround   = s.ground;
    ls->m_startSpeed    = static_cast<Speed>(s.speed);
    ls->m_startMode     = static_cast<PlayerMode>(s.gameMode);
    ls->m_platformerMode = s.platformer;
    ls->m_twoPlayerMode = s.twoPlayer;
    m_lastSettings = s;
}

void CollaborationSession::checkAndSyncSettings(LevelEditorLayer* edl) {
    auto current = readLevelSettings(edl);
    if (current != m_lastSettings) {
        m_lastSettings = current;
        matjson::Value p = matjson::Value::object();
        p["type"]       = static_cast<int>(Packets::MessageType::LevelSettings);
        p["bg"]         = current.bg;
        p["ground"]     = current.ground;
        p["speed"]      = current.speed;
        p["gameMode"]   = current.gameMode;
        p["platformer"] = current.platformer;
        p["twoPlayer"]  = current.twoPlayer;
        sendUdpToSession(p.dump(matjson::NO_INDENTATION));
        log::debug("CollaborationSession: syncing settings to peer");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Object serialization (host → guest initial dump)
// ─────────────────────────────────────────────────────────────────────────────
std::string CollaborationSession::serializeAllObjects(LevelEditorLayer* edl) {
    if (!edl || !edl->m_objectLayer) return "";
    std::string result;
    auto* children = edl->m_objectLayer->getChildren();
    if (!children) return "";

    // CCARRAY_FOREACH removed in Geode v5 — use CCArrayExt range-for instead
    for (auto* obj : CCArrayExt<GameObject*>(children)) {
        if (!obj) continue;
        // Skip cursor indicator nodes (added by us, not real objects)
        if (obj->getTag() >= CURSOR_BASE_TAG && obj->getTag() < CURSOR_BASE_TAG + 10) continue;

        // Build GD-format object string: key,value pairs
        std::string s;
        s += "1,"  + std::to_string(obj->m_objectID);
        s += ",2," + fmtFloat(obj->getPositionX());
        s += ",3," + fmtFloat(obj->getPositionY());
        float rot = obj->getRotation();
        if (std::fabsf(rot) > 0.01f)
            s += ",6," + fmtFloat(rot);
        float sx = obj->getScaleX(), sy = obj->getScaleY();
        if (std::fabsf(sx - 1.f) > 0.01f)
            s += ",32," + fmtFloat(sx);
        if (std::fabsf(sy - 1.f) > 0.01f)
            s += ",33," + fmtFloat(sy);
        // Flip
        if (obj->m_isFlipX) s += ",4,1";
        if (obj->m_isFlipY) s += ",5,1";
        s += ",";
        result += s + ";";
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP: host sends initial level to guest
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::sendLevelInitViaTcp(LevelEditorLayer* edl) {
    if (!edl) return;
    log::info("CollaborationSession: sending LevelInit to guest...");

    auto settings = readLevelSettings(edl);
    std::string objects = serializeAllObjects(edl);

    matjson::Value payload = matjson::Value::object();
    payload["msgType"]   = static_cast<int>(Packets::MessageType::LevelInit);
    payload["bg"]        = settings.bg;
    payload["ground"]    = settings.ground;
    payload["speed"]     = settings.speed;
    payload["gameMode"]  = settings.gameMode;
    payload["platformer"]= settings.platformer;
    payload["twoPlayer"] = settings.twoPlayer;
    payload["objects"]   = objects; // semicolon-separated GD object strings

    bool ok = NetworkManager::get().sendTcp(payload.dump(matjson::NO_INDENTATION));
    log::info("CollaborationSession: LevelInit sent ({} bytes), ok={}", objects.size(), ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// TCP: guest receives and applies initial level
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::handleTcpMessage(const std::string& data, LevelEditorLayer* edl) {
    try {
        auto res = matjson::parse(data);
        if (!res.isOk()) return;
        auto msg = res.unwrap();

        int msgType = msg["msgType"].asInt().unwrapOr(-1);
        if (msgType != static_cast<int>(Packets::MessageType::LevelInit)) return;
        if (!edl) return;

        log::info("CollaborationSession: applying LevelInit from host");

        // 1. Apply level settings
        CachedLevelSettings s{};
        s.bg         = msg["bg"].asInt().unwrapOr(1);
        s.ground     = msg["ground"].asInt().unwrapOr(1);
        s.speed      = msg["speed"].asInt().unwrapOr(0);
        s.gameMode   = msg["gameMode"].asInt().unwrapOr(0);
        s.platformer = msg["platformer"].asBool().unwrapOr(false);
        s.twoPlayer  = msg["twoPlayer"].asBool().unwrapOr(false);
        applyLevelSettings(s, edl);

        // 2. Clear existing objects from guest's editor
        {
            // Collect first so we don't mutate while iterating
            std::vector<GameObject*> toRemove;
            auto* children = edl->m_objectLayer->getChildren();
            if (children) {
                for (auto* obj : CCArrayExt<GameObject*>(children)) {
                    if (obj && (obj->getTag() < CURSOR_BASE_TAG || obj->getTag() >= CURSOR_BASE_TAG + 10))
                        toRemove.push_back(obj);
                }
            }
            m_applyingRemoteChange = true;
            for (auto* obj : toRemove) {
                m_objectToUid.erase(obj);
                edl->removeObject(obj, true);
            }
            m_applyingRemoteChange = false;
            m_uidToObject.clear();
        }

        // 3. Create host's objects
        std::string objects = msg["objects"].asString().unwrapOr("");
        if (!objects.empty()) {
            m_applyingRemoteChange = true;
            // createObjectsFromString accepts a semicolon-separated list of GD object strings
            edl->createObjectsFromString(objects, true, true);
            m_applyingRemoteChange = false;
        }

        log::info("CollaborationSession: LevelInit applied successfully");

    } catch (...) {
        log::error("CollaborationSession: exception applying LevelInit");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Object sync — local side
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::onLocalObjectPlaced(GameObject* obj, LevelEditorLayer* edl) {
    if (!isInSession() || m_applyingRemoteChange || !obj) return;

    std::string uid = (isHost() ? "H" : "G") + std::to_string(++m_uidCounter);
    m_uidToObject[uid] = obj;
    m_objectToUid[obj] = uid;

    matjson::Value p = matjson::Value::object();
    p["type"] = static_cast<int>(Packets::MessageType::ObjectPlace);
    p["uid"]  = uid;
    p["oid"]  = static_cast<int>(obj->m_objectID);
    p["x"]    = static_cast<double>(obj->getPositionX());
    p["y"]    = static_cast<double>(obj->getPositionY());
    p["rot"]  = static_cast<double>(obj->getRotation());
    p["sx"]   = static_cast<double>(obj->getScaleX());
    p["sy"]   = static_cast<double>(obj->getScaleY());
    p["fx"]   = obj->m_isFlipX;
    p["fy"]   = obj->m_isFlipY;
    sendUdpToSession(p.dump(matjson::NO_INDENTATION));
}

void CollaborationSession::onLocalObjectRemoved(GameObject* obj) {
    if (!isInSession() || m_applyingRemoteChange || !obj) return;

    auto it = m_objectToUid.find(obj);
    if (it == m_objectToUid.end()) return; // object had no UID (pre-session object)

    std::string uid = it->second;
    m_uidToObject.erase(uid);
    m_objectToUid.erase(it);

    matjson::Value p = matjson::Value::object();
    p["type"] = static_cast<int>(Packets::MessageType::ObjectDelete);
    p["uid"]  = uid;
    sendUdpToSession(p.dump(matjson::NO_INDENTATION));
}

// ─────────────────────────────────────────────────────────────────────────────
// Object sync — remote side
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::applyObjectPlace(const std::string& uid, int oid,
    float x, float y, float rot, float sx, float sy, LevelEditorLayer* edl)
{
    if (!edl || !edl->m_objectLayer) return;

    // Build GD object string for createObjectsFromString
    std::string s;
    s += "1,"  + std::to_string(oid);
    s += ",2," + fmtFloat(x);
    s += ",3," + fmtFloat(y);
    if (std::fabsf(rot)       > 0.01f) s += ",6,"  + fmtFloat(rot);
    if (std::fabsf(sx - 1.f)  > 0.01f) s += ",32," + fmtFloat(sx);
    if (std::fabsf(sy - 1.f)  > 0.01f) s += ",33," + fmtFloat(sy);
    s += ",";

    int before = edl->m_objectLayer->getChildrenCount();
    m_applyingRemoteChange = true;
    edl->createObjectsFromString(s + ";", true, true);
    m_applyingRemoteChange = false;

    // Associate the newly created child(ren) with the remote UID
    int after = edl->m_objectLayer->getChildrenCount();
    auto* children = edl->m_objectLayer->getChildren();
    for (int i = before; i < after && children; i++) {
        auto* obj = dynamic_cast<GameObject*>(children->objectAtIndex(i));
        if (obj) {
            m_uidToObject[uid] = obj;
            m_objectToUid[obj] = uid;
        }
    }
}

void CollaborationSession::applyObjectDelete(const std::string& uid, LevelEditorLayer* edl) {
    auto it = m_uidToObject.find(uid);
    if (it == m_uidToObject.end() || !edl) return;

    auto* obj = it->second;
    m_objectToUid.erase(obj);
    m_uidToObject.erase(it);

    m_applyingRemoteChange = true;
    edl->removeObject(obj, true); // removeObject(obj, undo) is the correct GD 2.2 API
    m_applyingRemoteChange = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP message dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void CollaborationSession::handleUdpMessage(const std::string& ip, const std::string& message) {
    if (message.empty() || message.size() > 4096) return;

    Loader::get()->queueInMainThread([this, ip, message] {
        try {
            auto res = matjson::parse(message);
            if (!res.isOk()) return;
            auto parsed = res.unwrap();
            if (!parsed.contains("type")) return;

            int type = parsed["type"].asInt().unwrapOr(-1);
            auto* edl = LevelEditorLayer::get();

            auto sanitize = [](std::string s) {
                s.erase(std::remove_if(s.begin(), s.end(),
                    [](unsigned char c){ return c < 32 || c > 126; }), s.end());
                return s;
            };
            std::string username = sanitize(parsed["user"].asString().unwrapOr("Unknown"));
            std::string safeIp   = sanitize(ip);

            // ── Discovery ────────────────────────────────────────────────────
            if (type == static_cast<int>(Packets::MessageType::DiscoveryRequest)) {
                if (username == m_localUsername) return;
                Packets::Platform plat = static_cast<Packets::Platform>(
                    parsed["platform"].asInt().unwrapOr(2));

                std::lock_guard<std::mutex> lock(m_usersMutex);
                auto now = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                bool found = false;
                for (auto& u : m_discoveredUsers) {
                    if (u.ip == safeIp) { u.lastSeen=now; u.username=username; u.platform=plat; found=true; break; }
                }
                if (!found) {
                    log::debug("CollaborationSession: discovered {} ({})", username, safeIp);
                    m_discoveredUsers.push_back({username, safeIp, plat, now});
                }
            }

            // ── Incoming invite ──────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::CollabRequest)) {
                if (onCollabInviteReceived) onCollabInviteReceived(username, safeIp);
            }

            // ── Invite response ──────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::CollabResponse)) {
                bool accepted = parsed["accepted"].asBool().unwrapOr(false);
                if (onCollabResponseReceived) onCollabResponseReceived(username, safeIp, accepted);
            }

            // ── The following require an active session ───────────────────────
            else if (!isInSession()) {
                return; // ignore session messages if we're not in a session
            }

            // ── Cursor position ──────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::CursorMove)) {
                if (!edl || !edl->m_objectLayer) return;
                float wx = static_cast<float>(parsed["x"].asDouble().unwrapOr(0.0));
                float wy = static_cast<float>(parsed["y"].asDouble().unwrapOr(0.0));

                // Find or create the cursor label node in m_objectLayer
                int tag = CURSOR_BASE_TAG;
                auto* cursor = edl->m_objectLayer->getChildByTag(tag);
                if (!cursor) {
                    auto* lbl = CCLabelBMFont::create(
                        (m_peerUsername + " \xe2\x96\xb6").c_str(), "goldFont.fnt");
                    if (!lbl) lbl = CCLabelBMFont::create(m_peerUsername.c_str(), "bigFont.fnt");
                    if (lbl) {
                        lbl->setScale(0.45f);
                        lbl->setColor(ccc3(255, 165, 0)); // orange
                        lbl->setTag(tag);
                        lbl->setZOrder(9999); // draw above everything
                        edl->m_objectLayer->addChild(lbl);
                        cursor = lbl;
                    }
                }
                if (cursor) cursor->setPosition(ccp(wx, wy));
            }

            // ── Object placed ────────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::ObjectPlace)) {
                if (!edl) return;
                std::string uid = parsed["uid"].asString().unwrapOr("");
                int   oid = parsed["oid"].asInt().unwrapOr(1);
                float x   = static_cast<float>(parsed["x"].asDouble().unwrapOr(0.0));
                float y   = static_cast<float>(parsed["y"].asDouble().unwrapOr(0.0));
                float rot = static_cast<float>(parsed["rot"].asDouble().unwrapOr(0.0));
                float sx  = static_cast<float>(parsed["sx"].asDouble().unwrapOr(1.0));
                float sy  = static_cast<float>(parsed["sy"].asDouble().unwrapOr(1.0));
                if (!uid.empty()) applyObjectPlace(uid, oid, x, y, rot, sx, sy, edl);
            }

            // ── Object deleted ───────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::ObjectDelete)) {
                if (!edl) return;
                std::string uid = parsed["uid"].asString().unwrapOr("");
                if (!uid.empty()) applyObjectDelete(uid, edl);
            }

            // ── Level settings changed ────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::LevelSettings)) {
                if (!edl) return;
                CachedLevelSettings s{};
                s.bg         = parsed["bg"].asInt().unwrapOr(0);
                s.ground     = parsed["ground"].asInt().unwrapOr(0);
                s.speed      = parsed["speed"].asInt().unwrapOr(0);
                s.gameMode   = parsed["gameMode"].asInt().unwrapOr(0);
                s.platformer = parsed["platformer"].asBool().unwrapOr(false);
                s.twoPlayer  = parsed["twoPlayer"].asBool().unwrapOr(false);
                applyLevelSettings(s, edl);
            }

            // ── Session ending ────────────────────────────────────────────────
            else if (type == static_cast<int>(Packets::MessageType::SessionEnd)) {
                FLAlertLayer::create(
                    "Session Ended",
                    "<cy>" + username + "</c> has left the collaboration session.\nAll changes are saved to your level.",
                    "OK"
                )->show();
                m_state = SessionState::Idle;
                m_peerIp = "";
                m_peerUsername = "";
                // Remove cursor indicator
                if (edl && edl->m_objectLayer) {
                    auto* c = edl->m_objectLayer->getChildByTag(CURSOR_BASE_TAG);
                    if (c) c->removeFromParent();
                }
                // Clean up UID maps (objects remain in editor for host to keep)
                m_uidToObject.clear();
                m_objectToUid.clear();
                NetworkManager::get().disconnectTcp();
            }

        } catch (...) {
            // malformed JSON — ignore
        }
    });
}
