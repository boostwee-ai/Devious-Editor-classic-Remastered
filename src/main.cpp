#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "CollaborationSession.hpp"
#include "NetworkManager.hpp"
#include "UserDiscoveryPopup.hpp"
#include "IncomingInvitePopup.hpp"

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
// UI Hook — collab button in the editor toolbar
// ─────────────────────────────────────────────────────────────────────────────
class $modify(CollabEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto* sprite = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
        if (!sprite) return true; // non-fatal
        sprite->setScale(0.8f);

        auto* btn = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(CollabEditorUI::onCollabButton));

        auto* menu = CCMenu::create();
        menu->setPosition({winSize.width - 25.f, winSize.height - 65.f});
        menu->addChild(btn);
        this->addChild(menu);
        return true;
    }

    void onCollabButton(CCObject*) {
        UserDiscoveryPopup::create()->show();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Editor Logic Hook — lifecycle + object hooks
// ─────────────────────────────────────────────────────────────────────────────
class $modify(CollabLevelEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;
        CollaborationSession::get().init();
        CollaborationSession::get().onEnterEditor();
        return true;
    }

    void update(float dt) {
        LevelEditorLayer::update(dt);
        CollaborationSession::get().update(dt);
    }

    void onExit() {
        CollaborationSession::get().onExitEditor();
        LevelEditorLayer::onExit();
    }

    // ── Object placement ──────────────────────────────────────────────────────
    void addObject(GameObject* obj) {
        GJBaseGameLayer::addObject(obj);
        // Guard: skip during init load (not yet in session) and during remote apply
        CollaborationSession::get().onLocalObjectPlaced(obj, this);
    }

    // ── Object deletion ───────────────────────────────────────────────────────
    void removeObject(GameObject* obj, bool undo) {
        CollaborationSession::get().onLocalObjectRemoved(obj);
        LevelEditorLayer::removeObject(obj, undo);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Module init: wire all session callbacks
// ─────────────────────────────────────────────────────────────────────────────
$execute {
    log::info("Devious Editor Classic Remastered initializing...");

    // Make sure the session singleton + network layer are available
    CollaborationSession::get().init();

    // ── Incoming invite ───────────────────────────────────────────────────────
    CollaborationSession::get().onCollabInviteReceived =
        [](const std::string& fromUser, const std::string& fromIp) {
            log::info("Showing incoming invite from {} ({})", fromUser, fromIp);
            IncomingInvitePopup::create(fromUser, fromIp)->show();
        };

    // ── Invite response ───────────────────────────────────────────────────────
    // Signature updated to (username, ip, accepted) so we have the peer's IP.
    CollaborationSession::get().onCollabResponseReceived =
        [](const std::string& fromUser, const std::string& fromIp, bool accepted) {
            if (accepted) {
                // Transition to Host state — TCP server was already started in sendCollabInvite
                CollaborationSession::get().setHostMode(fromIp, fromUser);
                FLAlertLayer::create(
                    "Session Starting",
                    "<cg>" + fromUser + "</c> accepted! Sending level data...",
                    "OK"
                )->show();
            } else {
                CollaborationSession::get().resetToIdle();
                FLAlertLayer::create(
                    "Declined",
                    "<cr>" + fromUser + "</c> declined your collaboration request.",
                    "OK"
                )->show();
            }
        };

    // ── TCP: guest connected → send full level init ───────────────────────────
    // Fired on background thread, so queue to main thread for safe GD access.
    NetworkManager::get().onTcpClientConnected = []() {
        Loader::get()->queueInMainThread([]() {
            auto* edl = LevelEditorLayer::get();
            if (edl) {
                CollaborationSession::get().sendLevelInitViaTcp(edl);
            } else {
                log::warn("CollabSession: TCP client connected but no active editor!");
            }
        });
    };

    // ── TCP: message received → dispatch to session (already main thread safe) ─
    NetworkManager::get().onTcpMessage = [](const std::string& data) {
        Loader::get()->queueInMainThread([data]() {
            auto* edl = LevelEditorLayer::get();
            CollaborationSession::get().handleTcpMessage(data, edl);
        });
    };

    // ── TCP: connection dropped ───────────────────────────────────────────────
    NetworkManager::get().onTcpDisconnected = []() {
        Loader::get()->queueInMainThread([]() {
            if (CollaborationSession::get().isInSession()) {
                FLAlertLayer::create(
                    "Connection Lost",
                    "The collaboration TCP connection was lost.",
                    "OK"
                )->show();
            }
        });
    };
}
