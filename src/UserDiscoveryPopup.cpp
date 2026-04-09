#include "UserDiscoveryPopup.hpp"

using namespace geode::prelude;

UserDiscoveryPopup* UserDiscoveryPopup::create() {
    auto ret = new UserDiscoveryPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool UserDiscoveryPopup::init() {
    if (!CCLayer::init()) return false;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // Create a darken background layer manually
    auto darken = CCLayerColor::create(ccc4(0, 0, 0, 150));
    if (!darken) return false;
    this->addChild(darken);

    m_mainLayer = CCLayer::create();
    if (!m_mainLayer) return false;
    this->addChild(m_mainLayer);

    // Create background (standard GD popup size)
    auto bg = CCScale9Sprite::create("GJ_square01.png", { 0, 0, 80, 80 });
    if (!bg) return false;
    bg->setContentSize({ 300.f, 220.f });
    bg->setPosition(winSize / 2);
    m_mainLayer->addChild(bg);

    // Title
    auto title = CCLabelBMFont::create("Local Users", "goldFont.fnt");
    if (title) {
        title->setPosition(winSize.width / 2, winSize.height / 2 + 90.f);
        m_mainLayer->addChild(title);
    }

    // Close Button
    auto closeSprite = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    if (closeSprite) {
        auto closeBtn = CCMenuItemSpriteExtra::create(
            closeSprite,
            this,
            menu_selector(UserDiscoveryPopup::onClose)
        );
        if (closeBtn) {
            auto closeMenu = CCMenu::create();
            closeMenu->addChild(closeBtn);
            closeMenu->setPosition({ winSize.width / 2 - 140.f, winSize.height / 2 + 100.f });
            m_mainLayer->addChild(closeMenu);
        }
    }

    // List Background
    auto listBg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
    if (listBg) {
        listBg->setColor(ccc3(0, 0, 0));
        listBg->setOpacity(75);
        listBg->setContentSize({ 260.f, 140.f });
        listBg->setPosition(winSize / 2 + ccp(0, -10));
        m_mainLayer->addChild(listBg);
    }

    // Scroll Layer
    m_scrollLayer = ScrollLayer::create({ 260.f, 140.f });
    if (!m_scrollLayer) return false;
    // Position it so its bottom-left corner aligns with the listBg's bottom-left corner
    m_scrollLayer->setPosition((winSize / 2 + ccp(0, -10)) - ccp(130.f, 70.f));
    m_mainLayer->addChild(m_scrollLayer);

    m_listMenu = CCMenu::create();
    m_listMenu->setPosition(ccp(0, 0));
    m_scrollLayer->m_contentLayer->addChild(m_listMenu);

    updateList();

    // Refresh button
    auto refreshBtnSprite = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    if (refreshBtnSprite) {
        refreshBtnSprite->setScale(0.8f);
        auto refreshBtn = CCMenuItemSpriteExtra::create(
            refreshBtnSprite,
            this,
            menu_selector(UserDiscoveryPopup::onRefresh)
        );
        if (refreshBtn) {
            auto menu = CCMenu::create();
            menu->addChild(refreshBtn);
            menu->setPosition({ winSize.width / 2 + 125.f, winSize.height / 2 - 85.f });
            m_mainLayer->addChild(menu);
        }
    }

    // Use setTouchEnabled(true) — this is the ONLY safe way to register touch on
    // Windows. Manual addTargetedDelegate / removeDelegate calls are NOT safe
    // because Cocos2d-x on Windows calls removeDelegate during node destruction
    // automatically, causing a double-free / dangling pointer crash.
    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    return true;
}

void UserDiscoveryPopup::updateList() {
    if (!m_listMenu) return;
    log::debug("Starting user list population...");
    m_listMenu->removeAllChildren();
    
    auto users = CollaborationSession::get().getDiscoveredUsers();
    float height = std::max(140.f, static_cast<float>(users.size() * 35.f));
    
    m_listMenu->setContentSize({ 260.f, height });
    if (m_scrollLayer && m_scrollLayer->m_contentLayer) {
        m_scrollLayer->m_contentLayer->setContentSize({ 260.f, height });
    }
    
    float y = height - 20.f;

    if (users.empty()) {
        log::debug("No users found, showing empty label");
        auto label = CCLabelBMFont::create("No users found...", "goldFont.fnt");
        if (label) {
            label->setScale(0.6f);
            label->setPosition(ccp(130.f, height / 2));
            m_listMenu->addChild(label);
        }
    }

    for (const auto& user : users) {
        log::debug("Building UI for user: {} ({})", user.username, user.ip);

        auto nameLabel = CCLabelBMFont::create(user.username.c_str(), "bigFont.fnt");
        if (nameLabel) {
            nameLabel->setScale(0.45f);
            nameLabel->setAnchorPoint(ccp(0, 0.5f));
            nameLabel->setPosition(ccp(15.f, y));
            m_listMenu->addChild(nameLabel);
        }

        std::string info = (user.platform == Packets::Platform::Windows ? "Win" : "Mac");
        info += " | " + user.ip;
        
        auto infoLabel = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
        if (infoLabel) {
            infoLabel->setScale(0.6f);
            infoLabel->setAnchorPoint(ccp(0, 0.5f));
            infoLabel->setColor(ccc3(200, 200, 200));
            infoLabel->setPosition(ccp(15.f, y - 12.f));
            m_listMenu->addChild(infoLabel);
        }

        log::debug("Creating invite button for {}", user.ip);
        
        auto inviteBtnSprite = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
        if (inviteBtnSprite) {
            inviteBtnSprite->setScale(0.5f);
            
            auto btnLabel = CCLabelBMFont::create("Invite", "goldFont.fnt");
            if (btnLabel) {
                btnLabel->setScale(0.8f);
                btnLabel->setPosition(inviteBtnSprite->getContentSize() / 2);
                inviteBtnSprite->addChild(btnLabel);
            }

            auto inviteBtn = CCMenuItemSpriteExtra::create(
                inviteBtnSprite,
                this,
                menu_selector(UserDiscoveryPopup::onInvite)
            );
            
            if (inviteBtn) {
                inviteBtn->setID(user.ip);
                inviteBtn->setPosition(ccp(210.f, y - 5.f));
                m_listMenu->addChild(inviteBtn);
            }
        }

        y -= 35.f;
    }

    log::debug("Finished building user list UI.");
    if (m_scrollLayer) {
        m_scrollLayer->moveToTop();
    }
}

void UserDiscoveryPopup::onRefresh(cocos2d::CCObject*) {
    updateList();
}

void UserDiscoveryPopup::onInvite(cocos2d::CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    std::string targetIp = btn->getID();
    
    FLAlertLayer::create(
        "Invite Sent", 
        "Sending collaboration request to " + targetIp + ".\n(TCP handshaking in development!)", 
        "OK"
    )->show();
}

UserDiscoveryPopup::~UserDiscoveryPopup() {
    // setTouchEnabled(true) was used in init(), so Cocos2d-x will automatically
    // remove us from the dispatcher during node destruction.
    // We do NOT call removeDelegate manually — that caused the Windows crash.
}

void UserDiscoveryPopup::onClose(cocos2d::CCObject*) {
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);
    this->removeFromParentAndCleanup(true);
}

void UserDiscoveryPopup::keyBackClicked() {
    onClose(nullptr);
}

void UserDiscoveryPopup::show() {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    // retain() before adding so autorelease() doesn't delete us prematurely.
    // The scene's addChild gives us an owning reference, so this is safe.
    this->retain();
    scene->addChild(this, 100);
    this->release();
}
