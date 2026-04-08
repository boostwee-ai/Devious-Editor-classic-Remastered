#include "UserDiscoveryPopup.hpp"

using namespace geode::prelude;

UserDiscoveryPopup* UserDiscoveryPopup::create() {
    auto ret = new UserDiscoveryPopup();
    if (ret && ret->init(300.f, 220.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool UserDiscoveryPopup::setup() {
    this->setTitle("Local Users");

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // Background for the list
    auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
    bg->setColor({ 0, 0, 0 });
    bg->setOpacity(75);
    bg->setContentSize({ 260.f, 140.f });
    bg->setPosition(m_mainLayer->getContentSize() / 2 + ccp(0, -10));
    m_mainLayer->addChild(bg);

    // Scroll Layer
    m_scrollLayer = ScrollLayer::create({ 260.f, 140.f });
    m_scrollLayer->setPosition(bg->getPosition() - bg->getContentSize() / 2);
    m_mainLayer->addChild(m_scrollLayer);

    m_listMenu = CCMenu::create();
    m_listMenu->setPosition({ 0, 0 });
    m_scrollLayer->m_contentLayer->addChild(m_listMenu);

    updateList();

    // Refresh button
    auto refreshBtnSprite = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    refreshBtnSprite->setScale(0.8f);
    auto refreshBtn = CCMenuItemSpriteExtra::create(
        refreshBtnSprite,
        nullptr,
        this,
        menu_selector(UserDiscoveryPopup::onRefresh)
    );
    
    auto menu = CCMenu::create();
    menu->addChild(refreshBtn);
    menu->setPosition({ m_mainLayer->getContentSize().width - 25, 25 });
    m_mainLayer->addChild(menu);

    return true;
}

void UserDiscoveryPopup::updateList() {
    m_listMenu->removeAllChildren();
    
    auto users = CollaborationSession::get().getDiscoveredUsers();
    float height = std::max(140.f, users.size() * 35.f);
    
    m_listMenu->setContentSize({ 260.f, height });
    m_scrollLayer->m_contentLayer->setContentSize({ 260.f, height });
    
    float y = height - 20.f;

    if (users.empty()) {
        auto label = CCLabelBMFont::create("No users found...", "goldFont.fnt");
        label->setScale(0.6f);
        label->setPosition({ 130.f, height / 2 });
        m_listMenu->addChild(label);
    }

    for (const auto& user : users) {
        // User Name
        auto nameLabel = CCLabelBMFont::create(user.username.c_str(), "bigFont.fnt");
        nameLabel->setScale(0.45f);
        nameLabel->setAnchorPoint({ 0, 0.5f });
        nameLabel->setPosition({ 15.f, y });
        m_listMenu->addChild(nameLabel);

        // Platform / IP
        std::string info = (user.platform == Packets::Platform::Windows ? "Win" : "Mac");
        info += " | " + user.ip;
        auto infoLabel = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
        infoLabel->setScale(0.6f);
        infoLabel->setAnchorPoint({ 0, 0.5f });
        infoLabel->setColor({ 200, 200, 200 });
        infoLabel->setPosition({ 15.f, y - 12.f });
        m_listMenu->addChild(infoLabel);

        // Invite Button
        auto inviteBtnSprite = ButtonSprite::create("Invite", 40, true, "goldFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
        auto inviteBtn = CCMenuItemSpriteExtra::create(
            inviteBtnSprite,
            nullptr,
            this,
            menu_selector(UserDiscoveryPopup::onInvite)
        );
        inviteBtn->setID(user.ip); // Store IP in ID for retrieval
        inviteBtn->setPosition({ 210.f, y - 5.f });
        m_listMenu->addChild(inviteBtn);

        y -= 35.f;
    }

    m_scrollLayer->moveToTop();
}

void UserDiscoveryPopup::onRefresh(cocos2d::CCObject*) {
    updateList();
}

void UserDiscoveryPopup::onInvite(cocos2d::CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    std::string targetIp = btn->getID();
    
    FLAlertLayer::create(
        "Invite Sent", 
        "Sending collaboration request to " + targetIp + ".\n(TCP handshaking in development!)", 
        "OK"
    )->show();
}
