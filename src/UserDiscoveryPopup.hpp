#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "CollaborationSession.hpp"

class UserDiscoveryPopup : public geode::Popup {
protected:
    bool setup() override;
    void onRefresh(cocos2d::CCObject*);
    void onInvite(cocos2d::CCObject*);
    
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_listMenu = nullptr;

public:
    static UserDiscoveryPopup* create();
    void updateList();
};
