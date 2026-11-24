#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class $modify(RaealLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) {
            return false;
        }

        if (auto menu = getChildByID("left-side-menu")) {
            auto sprite = ButtonSprite::create("R");
            sprite->setScale(0.7f);
            auto button = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(RaealLevelInfoLayer::onRaealPF)
            );
            button->setID("raealpf-button"_spr);
            menu->addChild(button);
            menu->updateLayout();
        } else {
            log::warn("Could not find LevelInfoLayer left-side-menu");
        }

        return true;
    }

    void onRaealPF(CCObject*) {
        FLAlertLayer::create("RaealPF", "RaealPF UI placeholder.", "OK")->show();
    }
};