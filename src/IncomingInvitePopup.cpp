#include "IncomingInvitePopup.hpp"
#include "CollaborationSession.hpp"

using namespace geode::prelude;

IncomingInvitePopup* IncomingInvitePopup::create(const std::string& fromUsername, const std::string& fromIp) {
    auto ret = new IncomingInvitePopup();
    if (ret && ret->init(fromUsername, fromIp)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool IncomingInvitePopup::init(const std::string& fromUsername, const std::string& fromIp) {
    if (!CCLayer::init()) return false;

    m_fromUsername = fromUsername;
    m_fromIp       = fromIp;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Darken overlay
    auto darken = CCLayerColor::create(ccc4(0, 0, 0, 150));
    if (!darken) return false;
    this->addChild(darken);

    m_mainLayer = CCLayer::create();
    if (!m_mainLayer) return false;
    this->addChild(m_mainLayer);

    // Background panel
    auto bg = CCScale9Sprite::create("GJ_square01.png", { 0, 0, 80, 80 });
    if (!bg) return false;
    bg->setContentSize({ 280.f, 160.f });
    bg->setPosition(winSize / 2);
    m_mainLayer->addChild(bg);

    // Title
    auto title = CCLabelBMFont::create("Collaboration Invite", "goldFont.fnt");
    if (title) {
        title->setScale(0.7f);
        title->setPosition(winSize.width / 2, winSize.height / 2 + 60.f);
        m_mainLayer->addChild(title);
    }

    // Body text
    std::string body = fromUsername + " wants to\ncollaborate with you!";
    auto bodyLabel = CCLabelBMFont::create(body.c_str(), "chatFont.fnt");
    if (bodyLabel) {
        bodyLabel->setScale(0.8f);
        bodyLabel->setAlignment(kCCTextAlignmentCenter);
        bodyLabel->setPosition(winSize / 2 + ccp(0, 15.f));
        m_mainLayer->addChild(bodyLabel);
    }

    // IP sub-label
    auto ipLabel = CCLabelBMFont::create(fromIp.c_str(), "chatFont.fnt");
    if (ipLabel) {
        ipLabel->setScale(0.5f);
        ipLabel->setColor(ccc3(180, 180, 180));
        ipLabel->setPosition(winSize / 2 + ccp(0, -5.f));
        m_mainLayer->addChild(ipLabel);
    }

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto buttonMenu = CCMenu::create();
    buttonMenu->setPosition(winSize / 2 + ccp(0, -42.f));
    m_mainLayer->addChild(buttonMenu);

    // Accept button (green GJ button)
    auto acceptSprite = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
    if (acceptSprite) {
        acceptSprite->setScale(0.75f);
        auto lbl = CCLabelBMFont::create("Accept", "goldFont.fnt");
        if (lbl) {
            lbl->setScale(0.7f);
            lbl->setPosition(acceptSprite->getContentSize() / 2);
            acceptSprite->addChild(lbl);
        }
        auto acceptBtn = CCMenuItemSpriteExtra::create(
            acceptSprite, this, menu_selector(IncomingInvitePopup::onAccept)
        );
        if (acceptBtn) {
            acceptBtn->setPosition(ccp(-55.f, 0));
            buttonMenu->addChild(acceptBtn);
        }
    }

    // Decline button (red GJ button)
    auto declineSprite = CCSprite::createWithSpriteFrameName("GJ_button_06.png");
    if (declineSprite) {
        declineSprite->setScale(0.75f);
        auto lbl = CCLabelBMFont::create("Decline", "goldFont.fnt");
        if (lbl) {
            lbl->setScale(0.7f);
            lbl->setPosition(declineSprite->getContentSize() / 2);
            declineSprite->addChild(lbl);
        }
        auto declineBtn = CCMenuItemSpriteExtra::create(
            declineSprite, this, menu_selector(IncomingInvitePopup::onDecline)
        );
        if (declineBtn) {
            declineBtn->setPosition(ccp(55.f, 0));
            buttonMenu->addChild(declineBtn);
        }
    }

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void IncomingInvitePopup::onAccept(cocos2d::CCObject*) {
    CollaborationSession::get().sendCollabResponse(m_fromIp, true);
    onClose(nullptr);
}

void IncomingInvitePopup::onDecline(cocos2d::CCObject*) {
    CollaborationSession::get().sendCollabResponse(m_fromIp, false);
    onClose(nullptr);
}

void IncomingInvitePopup::onClose(cocos2d::CCObject*) {
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);
    this->removeFromParentAndCleanup(true);
}

void IncomingInvitePopup::keyBackClicked() {
    // Pressing ESC/Back counts as declining
    onDecline(nullptr);
}

void IncomingInvitePopup::show() {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    this->retain();
    scene->addChild(this, 110); // higher Z than UserDiscoveryPopup (100)
    this->release();
}
