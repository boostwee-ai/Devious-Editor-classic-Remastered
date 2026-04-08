#pragma once

#include <Geode/Geode.hpp>
#include "CollaborationSession.hpp"

// Using a custom CCLayer to avoid FLAlertLayer inheritance issues in specific SDK versions
class UserDiscoveryPopup : public cocos2d::CCLayer {
protected:
    bool init() override;
    void onRefresh(cocos2d::CCObject*);
    void onInvite(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);
    
    // Keypad support for ESC/Back
    void keyBackClicked() override;

    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_listMenu = nullptr;
    cocos2d::CCLayer* m_mainLayer = nullptr;

public:
    static UserDiscoveryPopup* create();
    void updateList();
    void show();
};
