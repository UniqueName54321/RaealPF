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
    struct Fields {
        bool playNormally = false;
    };

    void onPlay(CCObject* sender) {
        if (m_fields->playNormally) {
            m_fields->playNormally = false;
            LevelInfoLayer::onPlay(sender);
            return;
        }
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->m_fields->playNormally = true;
            self->LevelInfoLayer::onPlay(sender);
        });
    }
};

class $modify(RaealLevelPage, LevelPage) {
    struct Fields {
        bool playNormally = false;
    };

    void onPlay(CCObject* sender) {
        if (m_fields->playNormally) {
            m_fields->playNormally = false;
            LevelPage::onPlay(sender);
            return;
        }
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->m_fields->playNormally = true;
            self->LevelPage::onPlay(sender);
        });
    }
};

class $modify(RaealEditLevelLayer, EditLevelLayer) {
    struct Fields {
        bool playNormally = false;
    };

    void onPlay(CCObject* sender) {
        if (m_fields->playNormally) {
            m_fields->playNormally = false;
            EditLevelLayer::onPlay(sender);
            return;
        }
        showRaealPF([self = Ref(this), sender = Ref(sender)] {
            self->m_fields->playNormally = true;
            self->EditLevelLayer::onPlay(sender);
        });
    }
};