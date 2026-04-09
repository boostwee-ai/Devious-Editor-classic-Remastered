#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "CollaborationSession.hpp"
#include "UserDiscoveryPopup.hpp"
#include "IncomingInvitePopup.hpp"

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
// Editor UI Hook — adds the collab button to the editor toolbar
// ─────────────────────────────────────────────────────────────────────────────
class $modify(CollabEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto collabSprite = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
        if (!collabSprite) return true; // non-fatal — button just won't show
        collabSprite->setScale(0.8f);

        auto collabBtn = CCMenuItemSpriteExtra::create(
            collabSprite,
            this,
            menu_selector(CollabEditorUI::onCollabButton)
        );

        auto menu = CCMenu::create();
        menu->setPosition({ winSize.width - 25.f, winSize.height - 65.f });
        menu->addChild(collabBtn);
        this->addChild(menu);

        return true;
    }

    void onCollabButton(CCObject*) {
        UserDiscoveryPopup::create()->show();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Editor Logic Hook — manages collab session lifetime with the editor
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
        LevelEditorLayer::onExit();
        CollaborationSession::get().onExitEditor();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Module init — wire up the session callbacks that need UI access
// ─────────────────────────────────────────────────────────────────────────────
$execute {
    log::info("Devious Editor Classic Remastered initializing...");

    // Ensure the session singleton and network layer are ready (idempotent)
    CollaborationSession::get().init();

    // ── Incoming invite ───────────────────────────────────────────────────────
    // Called on the main thread when a remote peer sends a CollabRequest.
    CollaborationSession::get().onCollabInviteReceived = [](const std::string& fromUser, const std::string& fromIp) {
        log::info("Showing incoming invite from {} ({})", fromUser, fromIp);
        IncomingInvitePopup::create(fromUser, fromIp)->show();
    };

    // ── Invite response ───────────────────────────────────────────────────────
    // Called on the main thread when the invitee accepts or declines.
    CollaborationSession::get().onCollabResponseReceived = [](const std::string& fromUser, bool accepted) {
        std::string title = accepted ? "Accepted!" : "Declined";
        std::string body  = accepted
            ? "<cg>" + fromUser + "</c> accepted your collaboration request!\n(Sync coming soon)"
            : "<cr>" + fromUser + "</c> declined your collaboration request.";

        FLAlertLayer::create(title.c_str(), body, "OK")->show();
    };
}
