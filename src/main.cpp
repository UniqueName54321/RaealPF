#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelPage.hpp>

using namespace geode::prelude;

static void showRaealPF(std::function<void()> playNormally) {
    createQuickPopup(
        "RaealPF",
        "RaealPF UI placeholder.",
        "Cancel", "Play",
        [playNormally = std::move(playNormally)](auto, bool play) {
            if (play) {
                playNormally();
            }
        }
    );
}

class $modify(RaealLevelInfoLayer, LevelInfoLayer) {
    void onPlay(CCObject* sender) {
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->LevelInfoLayer::onPlay(sender);
        });
    }
};

class $modify(RaealLevelPage, LevelPage) {
    void onPlay(CCObject* sender) {
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->LevelPage::onPlay(sender);
        });
    }
};

class $modify(RaealEditLevelLayer, EditLevelLayer) {
    void onPlay(CCObject* sender) {
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->EditLevelLayer::onPlay(sender);
        });
    }
};