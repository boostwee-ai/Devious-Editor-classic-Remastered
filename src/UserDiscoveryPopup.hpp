#pragma once

#include <Geode/Geode.hpp>
#include "CollaborationSession.hpp"

// We use FLAlertLayer directly to ensure maximum compatibility across SDK versions
class UserDiscoveryPopup : public geode::FLAlertLayer {
protected:
    bool init() override;
    void onRefresh(cocos2d::CCObject*);
    void onInvite(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);
    
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_listMenu = nullptr;

public:
    static UserDiscoveryPopup* create();
    void updateList();
    void show();
};
