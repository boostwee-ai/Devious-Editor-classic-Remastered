#pragma once

#include <Geode/Geode.hpp>
#include <string>

// Shown on the receiving end when a collaboration invite arrives.
// Has Accept and Decline buttons; sends a CollabResponse back over UDP.
class IncomingInvitePopup : public cocos2d::CCLayer {
protected:
    bool init(const std::string& fromUsername, const std::string& fromIp);
    virtual ~IncomingInvitePopup() = default;

    void onAccept(cocos2d::CCObject*);
    void onDecline(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);
    void keyBackClicked() override;

    cocos2d::CCLayer* m_mainLayer = nullptr;
    std::string m_fromUsername;
    std::string m_fromIp;

public:
    static IncomingInvitePopup* create(const std::string& fromUsername, const std::string& fromIp);
    void show();
};
