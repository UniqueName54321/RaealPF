#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelPage.hpp>
#include <imgui-cocos.hpp>

using namespace geode::prelude;

namespace {
    std::function<void()> s_playNormally;
    bool s_pathfind = true;
    bool s_pathfindError = false;

    void closeRaealPF() {
        ImGuiCocos::get().setVisible(false);
        s_playNormally = {};
    }

    void setupRaealPFStyle() {
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 12.f;
        style.FrameRounding = 6.f;
        style.WindowBorderSize = 1.f;
        style.WindowPadding = ImVec2(22.f, 20.f);
        style.FramePadding = ImVec2(12.f, 9.f);
        style.ItemSpacing = ImVec2(10.f, 12.f);
        auto* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.96f, 0.91f, 1.f, 1.f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.65f, 0.59f, 0.76f, 1.f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.04f, 0.16f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.75f, 0.27f, 0.91f, 0.85f);
        colors[ImGuiCol_Button] = ImVec4(0.31f, 0.15f, 0.47f, 1.f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.66f, 0.23f, 0.70f, 1.f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.86f, 0.31f, 0.70f, 1.f);
        colors[ImGuiCol_Separator] = ImVec4(0.34f, 0.81f, 0.88f, 0.55f);
    }

    void drawRaealPF() {
        auto& io = ImGui::GetIO();
        auto size = ImVec2(
            std::min(480.f, io.DisplaySize.x - 20.f),
            std::min(300.f, io.DisplaySize.y - 20.f)
        );
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0.f, 0.f), io.DisplaySize, IM_COL32(12, 3, 27, 175)
        );
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f)
        );
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("RaealPF", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        );

        ImGui::TextColored(ImVec4(1.f, 0.42f, 0.83f, 1.f), "R A E A L P F");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 55.f);
        bool close = ImGui::Button("X", ImVec2(32.f, 0.f));
        ImGui::TextDisabled("YOUR LEVEL  /  YOUR PATH");
        ImGui::Separator();

        bool playWasActive = !s_pathfind;
        if (playWasActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.23f, 0.63f, 1.f));
        }
        if (ImGui::Button("Play", ImVec2(100.f, 0.f))) {
            s_pathfind = false;
        }
        if (playWasActive) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        bool pathfindWasActive = s_pathfind;
        if (pathfindWasActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.23f, 0.63f, 1.f));
        }
        if (ImGui::Button("Pathfind", ImVec2(100.f, 0.f))) {
            s_pathfind = true;
        }
        if (pathfindWasActive) {
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        bool play = false;
        if (s_pathfind) {
            ImGui::TextColored(ImVec4(0.40f, 0.91f, 0.94f, 1.f), "PATHFIND");
            ImGui::TextWrapped("Pathfinding is not available yet.");
            if (ImGui::Button("Pathfind##action", ImVec2(140.f, 0.f))) {
                s_pathfindError = true;
            }
            if (s_pathfindError) {
                ImGui::TextColored(ImVec4(1.f, 0.40f, 0.64f, 1.f),
                    "Error: pathfinding is not implemented.");
            }
        } else {
            ImGui::TextColored(ImVec4(0.40f, 0.91f, 0.94f, 1.f), "PLAY");
            play = ImGui::Button("Play##action", ImVec2(140.f, 0.f));
        }
        close |= ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::End();

        if (play) {
            auto callback = std::move(s_playNormally);
            closeRaealPF();
            Loader::get()->queueInMainThread(std::move(callback));
        } else if (close) {
            closeRaealPF();
        }
    }

    void showRaealPF(std::function<void()> playNormally) {
        s_playNormally = std::move(playNormally);
        s_pathfind = true;
        s_pathfindError = false;
        ImGuiCocos::get().setVisible(true);
    }
}

$on_mod(Loaded) {
    auto& imgui = ImGuiCocos::get();
    imgui.setup(setupRaealPFStyle).draw(drawRaealPF);
    imgui.setInputMode(ImGuiCocos::InputMode::Blocking);
    imgui.setVisible(false);
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