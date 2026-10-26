#include <cassert>
#include "../Menu/Menu.cpp"
#include "../Menu/imgui/imgui_internal.h"

GhostControl::Snapshot GhostControl::Read() { return {}; }
Pickup::Snapshot Pickup::Read() { return {}; }
Recovery::Snapshot Recovery::Read() { return {}; }
bool Recovery::Queue(uint64_t) { return false; }
Stamina::Snapshot Stamina::Read() { return {}; }
WorldActions::Snapshot WorldActions::Read() { return {}; }
bool WorldActions::Queue(Kind, uint64_t, uint64_t) { return false; }

namespace {
    void Frame(bool menuVisible, bool radarVisible, bool focused = true) {
        Menu::gMenuVisible = menuVisible;
        Menu::gRadarVisible = radarVisible;
        Menu::UpdateOverlayInput(focused);
        ImGui::NewFrame();
        Menu::DrawOverlayWindows();
        Menu::UpdateOverlayInput(focused);
        ImGui::Render();
    }

    void AssertPassiveRadar() {
        auto* radar = ImGui::FindWindowByName("Radar");
        auto* menu = ImGui::FindWindowByName("Zackmophobia");
        assert(radar && radar->Active && (!menu || !menu->Active));
        assert((radar->Flags & ImGuiWindowFlags_NoInputs) == ImGuiWindowFlags_NoInputs);
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
        assert(!Features::bOverlayInput.load() && !GhostControl::inputCaptured.load());
        assert(!ImGui::GetIO().MouseDrawCursor && !ImGui::GetIO().WantCaptureMouse);
        assert(!ImGui::GetIO().WantCaptureKeyboard);
        assert(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange);
        assert(Radar::requests.empty());
    }
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 1000);
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Menu::ApplyStyle();
    Radar::snapshot.sampledAt = GetTickCount64();
    Radar::snapshot.center.positionValid = true;
    Radar::snapshot.ghost.positionValid = true;
    Radar::snapshot.ghost.position = {3, 0, 3};
    Radar::snapshot.generation = 1;
    Radar::snapshot.actionsAllowed = true;
    Radar::Player player;
    player.actorId = 1;
    player.name = "Fixture player";
    player.local = player.lifeValid = player.pose.positionValid = true;
    Radar::snapshot.players.push_back(player);

    Frame(false, true);
    Frame(false, true);
    AssertPassiveRadar();

    const ImVec2 originalPosition = ImGui::FindWindowByName("Radar")->Pos;
    io.AddMousePosEvent(originalPosition.x + 50, originalPosition.y + 10);
    io.AddMouseButtonEvent(0, true);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    Frame(false, true);
    io.AddMousePosEvent(originalPosition.x + 150, originalPosition.y + 80);
    Frame(false, true);
    io.AddMouseButtonEvent(0, false);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    Frame(false, true);
    AssertPassiveRadar();
    assert(ImGui::FindWindowByName("Radar")->Pos.x == originalPosition.x);
    assert(ImGui::FindWindowByName("Radar")->Pos.y == originalPosition.y);

    Frame(true, true);
    Frame(true, true);
    auto* menu = ImGui::FindWindowByName("Zackmophobia");
    auto* radar = ImGui::FindWindowByName("Radar");
    assert(menu->Active && radar->Active && !(radar->Flags & ImGuiWindowFlags_NoInputs));
    assert(Features::bOverlayInput.load() && GhostControl::inputCaptured.load() && io.MouseDrawCursor);
    assert(!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange));

    ImGui::NewFrame();
    const ImGuiID confirmation = radar->GetID("Confirm player kill");
    ImGui::OpenPopupEx(confirmation, ImGuiPopupFlags_None);
    Menu::DrawOverlayWindows();
    ImGui::Render();
    assert(ImGui::IsPopupOpen(confirmation, ImGuiPopupFlags_None));
    Frame(false, true);
    assert(!ImGui::IsPopupOpen(confirmation, ImGuiPopupFlags_None));
    Frame(false, true);
    AssertPassiveRadar();

    Frame(true, true, false);
    assert(!Features::bOverlayInput.load() && !io.MouseDrawCursor);
    Frame(false, false);
    assert(!menu->Active && !radar->Active);
    Frame(true, false);
    assert(menu->Active && !radar->Active);

    ImGui::DestroyContext();
    puts("[OK] Passive radar rendering, input release, popup cancellation and visibility regressions passed.");
}
