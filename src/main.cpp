#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "CollaborationSession.hpp"

#include "UserDiscoveryPopup.hpp"

using namespace geode::prelude;

// UI Hook
class $modify(CollabEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        // We will add the UI button in the top right corner
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Placeholder sprite
        auto collabSprite = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
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

    void onCollabButton(CCObject* sender) {
        UserDiscoveryPopup::create()->show();
    }
};

// Editor Logic Hook
class $modify(CollabLevelEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;

        CollaborationSession::get().init();
        CollaborationSession::get().onEnterEditor();

        // Optional: Create cursor line sprite
        // m_fields->remoteCursor = CCLayerColor::create({255, 255, 255, 255}, 2.f, 2000.f);
        // this->m_objectLayer->addChild(m_fields->remoteCursor);

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

$execute {
    log::info("Devious Editor Classic Remastered initializing...");
}
