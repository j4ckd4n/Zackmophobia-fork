#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>

#include "Menu.h"
#include "../Features/Features.h"
#include "../Features/Radar.h"
#include "../Features/GhostControl.h"
#include "../Features/Pickup.h"
#include "../Features/Stamina.h"
#include "../Features/WorldActions.h"
#include "../Hooks/Hooks.h"
#include "../Utils/Logger.h"
#include "../kiero/kiero.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "../Hooks/detours.h"

#pragma comment(lib, "../Lib/detours.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Menu {
    using Present_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);

    static Present_t oPresent = nullptr;
    static HWND gWindow = nullptr;
    static WNDPROC gOriginalWndProc = nullptr;
    static ID3D11Device* gDevice = nullptr;
    static ID3D11DeviceContext* gContext = nullptr;
    static ID3D11RenderTargetView* gRenderTargetView = nullptr;
    static bool gImGuiInitialized = false;
    static bool gMenuVisible = true;
    static bool gRadarVisible = true;
    static bool gHookInstalled = false;
    static std::atomic<bool> gShutdownRequested = false;
    static float gUiScale = 1.0f;
    static float gAppliedScale = 1.0f;
    static float gPanelOpacity = 0.84f;
    static float gRadarOpacity = 0.97f;
    static bool gPlayerUp = false;
    static bool gRebinding = false;
    static bool gBindArmed = false;
    static bool gControlKeyDown = false;

    static std::string ControlKeyName()
    {
        const int key = GhostControl::key.load();
        char name[64]{};
        UINT scan = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
        if (key >= VK_PRIOR && key <= VK_DOWN) scan |= 0x100;
        if (GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name))) return name;
        return "Key " + std::to_string(key);
    }

    static bool BindableKey(int key)
    {
        return key >= VK_BACK && key <= 0xFE && key != VK_ESCAPE && key != VK_HOME && key != VK_END &&
            key != VK_F4 && key != VK_F5 && key != VK_F6 && key != VK_SHIFT && key != VK_CONTROL &&
            key != VK_MENU && key != VK_LSHIFT && key != VK_RSHIFT && key != VK_LCONTROL &&
            key != VK_RCONTROL && key != VK_LMENU && key != VK_RMENU && key != VK_LWIN && key != VK_RWIN;
    }

    static void PollControlKey()
    {
        const bool focused = GetForegroundWindow() == gWindow;
        const bool down = (GetAsyncKeyState(GhostControl::key.load()) & 0x8000) != 0;
        if (gRebinding && focused) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) gRebinding = false;
            else {
                int pressed = 0;
                for (int key = VK_BACK; key <= 0xFE; ++key)
                    if (BindableKey(key) && (GetAsyncKeyState(key) & 0x8000)) { pressed = key; break; }
                if (!gBindArmed) gBindArmed = pressed == 0;
                else if (pressed) {
                    GhostControl::key.store(pressed);
                    gRebinding = false;
                    gControlKeyDown = true;
                    Logger::Log("[MENU] Ghost-control key rebound to VK %d.", pressed);
                    return;
                }
            }
        } else if (focused && down && !gControlKeyDown &&
            !(gMenuVisible && ImGui::GetIO().WantTextInput)) {
            GhostControl::desired.store(!GhostControl::desired.load());
        }
        gControlKeyDown = down;
    }

    struct MenuItem {
        const char* label;
        bool* state;
    };

    static MenuItem s_items[] = {
        { "Speed Hack",        &Features::bSpeedEnabled },
        { "Ghost Type Display",&Features::bGhostTypeEnabled },
        { "Force Tarot (Sun)", &Features::bForceTarot },
        { "Perfect Game",      &Features::bPerfectGame },
        { "Bonus Reward",      &Features::bBonusReward },
        { "No Kick",           &Features::bNoKick },
        { "No Sanity Loss",    &Features::bNoSanityLoss },
    };

    static constexpr size_t ITEM_COUNT = sizeof(s_items) / sizeof(s_items[0]);

    static void CleanupRenderTarget()
    {
        if (gRenderTargetView) {
            gRenderTargetView->Release();
            gRenderTargetView = nullptr;
        }
    }

    static void CreateRenderTarget(IDXGISwapChain* swapChain)
    {
        if (gRenderTargetView || !swapChain || !gDevice) {
            return;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) && backBuffer) {
            gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRenderTargetView);
            backBuffer->Release();
        }
    }

    static void ApplyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        // Rebuild from unscaled values, never multiply the previous frame's style.
        style = ImGuiStyle();
        ImGui::StyleColorsDark(&style);
        style.WindowRounding = style.ChildRounding = style.PopupRounding = 0.0f;
        style.FrameRounding = style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0.0f;
        style.WindowPadding = ImVec2(10.0f, 9.0f);
        style.FramePadding = ImVec2(6.0f, 3.0f);
        style.ItemSpacing = ImVec2(7.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
        style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.ScrollbarSize = 11.0f;

        ImVec4* colors = style.Colors;
        // Keep controls monochrome; color is reserved for meaningful radar markers.
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            const float gray = colors[i].x * 0.30f + colors[i].y * 0.59f + colors[i].z * 0.11f;
            colors[i] = ImVec4(gray, gray, gray, colors[i].w);
        }
        colors[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.56f, 0.56f, 1.0f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.035f, 0.035f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
        colors[ImGuiCol_Border] = colors[ImGuiCol_Separator] = ImVec4(0.36f, 0.36f, 0.36f, 1.0f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
        colors[ImGuiCol_CheckMark] = colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        gAppliedScale = gUiScale;
        style.ScaleAllSizes(gAppliedScale);
        // ImGui truncates scaled sizes; sub-1x scaling must not erase separators/borders.
        style.SeparatorSize = (std::max)(1.0f, style.SeparatorSize);
        style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize =
            style.FrameBorderSize = (std::max)(1.0f, gAppliedScale);
        style.FontScaleMain = gAppliedScale;
    }

    static bool BeginPanel(const char* name, bool* visible, const ImVec2& size,
        const ImVec2& position, float opacity, float& previousScale)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 maximum((std::max)(1.0f, display.x), (std::max)(1.0f, display.y));
        ImGui::SetNextWindowSize(ImVec2(size.x * gAppliedScale, size.y * gAppliedScale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2((std::min)(260.0f * gAppliedScale, maximum.x), (std::min)(180.0f * gAppliedScale, maximum.y)), maximum);
        ImGui::SetNextWindowBgAlpha(opacity);
        const bool open = ImGui::Begin(name, visible, ImGuiWindowFlags_NoCollapse);
        if (previousScale > 0.0f && previousScale != gAppliedScale) {
            const ImVec2 current = ImGui::GetWindowSize();
            const float ratio = gAppliedScale / previousScale;
            ImGui::SetWindowSize(ImVec2((std::min)(current.x * ratio, maximum.x),
                (std::min)(current.y * ratio, maximum.y)));
        }
        previousScale = gAppliedScale;
        // Retain user placement and resizing, only recovering windows outside the viewport.
        const ImVec2 currentSize = ImGui::GetWindowSize();
        const ImVec2 currentPos = ImGui::GetWindowPos();
        const ImVec2 clamped((std::max)(0.0f, (std::min)(currentPos.x, maximum.x - currentSize.x)),
            (std::max)(0.0f, (std::min)(currentPos.y, maximum.y - currentSize.y)));
        if (clamped.x != currentPos.x || clamped.y != currentPos.y) ImGui::SetWindowPos(clamped);
        return open;
    }

    static void ItemHint(const char* text)
    {
        if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", text);
    }

    static void DrawWorldOptions(const Radar::Snapshot& data)
    {
        const bool fresh = Radar::IsFresh(data, GetTickCount64());
        const bool localDead = std::any_of(data.players.begin(), data.players.end(), [](const Radar::Player& player) {
            return player.local && player.lifeValid && player.dead && !player.dying;
        });
        bool visible = Features::bGhostVisible.load();
        ImGui::BeginDisabled(!visible && (!fresh || !data.actionsAllowed || !data.ghostVisibilityAvailable));
        if (ImGui::Checkbox("Show ghost (local)", &visible)) Features::bGhostVisible.store(visible);
        ImGui::EndDisabled();
        ItemHint("Local visuals only. Normal hunt visibility takes priority.");
        ImGui::TextDisabled("Visual: %s",
            !fresh ? "unavailable / stale" : data.ghostVisibilityActive ? "override active" : visible ? "requested / waiting" : "native");
        bool interact = Features::bDeadInteraction.load();
        ImGui::BeginDisabled(!interact && (!fresh || !data.actionsAllowed || !data.deadInteractionAvailable || !localDead));
        if (ImGui::Checkbox("Interact while dead (PC)", &interact)) Features::bDeadInteraction.store(interact);
        ImGui::EndDisabled();
        ItemHint("Experimental. Remain dead; native item and ownership restrictions still apply.");
        ImGui::TextDisabled("Interaction: %s", !fresh ? "unavailable / stale" :
            data.deadInteractionActive ? "active" : interact ? "requested / waiting" : "native");
        if (!fresh) ImGui::TextDisabled("Waiting for match data.");
        if (!data.worldOptionsMessage.empty()) ImGui::TextWrapped("%s", data.worldOptionsMessage.c_str());

        ImGui::SeparatorText("Ghost control");
        const auto control = GhostControl::Read();
        const uint64_t now = GetTickCount64();
        const bool controlFresh = control.sampledAt && now >= control.sampledAt && now - control.sampledAt <= 1500;
        bool controlling = GhostControl::desired.load();
        ImGui::BeginDisabled(!controlling && (!controlFresh || !control.available || control.restoring));
        if (ImGui::Checkbox("Control ghost", &controlling)) GhostControl::desired.store(controlling);
        ImGui::EndDisabled();
        ItemHint("No hunt required. Your body stays stationary and vulnerable. HOME hides the overlay to resume movement.");
        ImGui::TextDisabled("Control: %s", !controlFresh ? "unavailable / stale" : control.restoring ? "restoration pending" :
            control.active ? (control.hunting ? "controlled hunt" : "controlled, not hunting") : "native AI");
        ImGui::BeginDisabled(!controlFresh || !control.active || control.hunting || control.restoring);
        if (ImGui::Button("Start controlled hunt")) GhostControl::huntRequested.store(true);
        ImGui::EndDisabled();
        const std::string bindLabel = gRebinding ? "Press a key (Escape cancels)##control_bind" :
            "Key: " + ControlKeyName() + "##control_bind";
        if (ImGui::Button(bindLabel.c_str())) { gRebinding = true; gBindArmed = false; }
        ImGui::SameLine();
        if (ImGui::Button("Reset to F7")) { GhostControl::key.store(VK_F7); gRebinding = false; }
        if (gRebinding) ImGui::TextWrapped("Release held keys first. HOME, END and F4-F6 are reserved.");
        float movementSpeed = GhostControl::speed.load();
        if (ImGui::SliderFloat("Ghost speed", &movementSpeed, 0.2f, 6.0f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp))
            GhostControl::speed.store(movementSpeed);
        bool first = GhostControl::firstPerson.load();
        if (ImGui::Checkbox("First-person view", &first)) GhostControl::firstPerson.store(first);
        ItemHint("Experimental; the camera may clip the ghost model.");
        if (!first) {
            float distance = GhostControl::cameraDistance.load();
            if (ImGui::SliderFloat("Camera distance", &distance, 0.75f, 5.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp))
                GhostControl::cameraDistance.store(distance);
        }
        if (!control.message.empty()) ImGui::TextWrapped("%s", control.message.c_str());

        ImGui::SeparatorText("Pickup");
        const auto pickup = Pickup::Read();
        bool pickupEnabled = Pickup::desired.load();
        ImGui::BeginDisabled(!pickupEnabled && (!pickup.available || pickup.restorePending));
        if (ImGui::Checkbox("Expanded pickup (experimental)", &pickupEnabled)) Pickup::desired.store(pickupEnabled);
        ImGui::EndDisabled();
        ItemHint("Grab/drop collider targets, including in the lobby. Your own hierarchy is always excluded. Multiplayer sync is best-effort.");
        int pickupScope = static_cast<int>(Pickup::scope.load());
        if (ImGui::Combo("Pickup scope", &pickupScope, "Equipment\0Props\0Actors\0Everything\0"))
            Pickup::scope.store(static_cast<Pickup::Scope>(pickupScope));
        ItemHint("Equipment: equipment only. Props: non-actors, including equipment and fixtures. Actors: players/ghosts. Everything: all except yourself. Changing scope drops excluded targets.");
        ImGui::TextDisabled("Pickup: %s", pickup.restorePending ? "restoration pending" :
            pickup.active ? "holding" : pickupEnabled ? "enabled" : "off");
        if (pickupEnabled) ImGui::TextDisabled("May desync or break the map; changes persist.");
        if ((pickupEnabled || pickup.restorePending) && !pickup.message.empty())
            ImGui::TextWrapped("%s", pickup.message.c_str());
    }

    static void SubmitRadarAction(int actorId, uint64_t generation, Radar::Action action, int sanity = 100)
    {
        const bool queued = Radar::Queue(actorId, generation, action, GetTickCount64(), sanity);
        Logger::Log("[MENU] Radar action for actor %d %s.", actorId, queued ? "queued" : "rejected");
    }

    static void DrawRadarWindow()
    {
        if (!gRadarVisible) return;
        static float previousScale = 0.0f;
        if (!BeginPanel("Radar", &gRadarVisible, ImVec2(390.0f, 540.0f),
            ImVec2(430.0f, 35.0f), gRadarOpacity, previousScale)) {
            ImGui::End();
            return;
        }
        const auto data = Radar::Read();
        const bool fresh = Radar::IsFresh(data, GetTickCount64());
        const float scale = gAppliedScale;
        static float range = 25.0f;
        ImGui::SetNextItemWidth(155.0f * scale);
        ImGui::SliderFloat("Range", &range, 5.0f, 100.0f, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
        int orientation = gPlayerUp ? 1 : 0;
        ImGui::SetNextItemWidth(155.0f * scale);
        if (ImGui::Combo("Orientation", &orientation, "North-up\0Player-up\0")) gPlayerUp = orientation == 1;
        const bool headingAvailable = fresh && data.center.headingValid && std::isfinite(data.center.heading);
        const float viewHeading = gPlayerUp && headingAvailable ? data.center.heading : 0.0f;
        if (gPlayerUp && !headingAvailable)
            ImGui::TextWrapped("North-up fallback: local heading unavailable.");
        else ImGui::TextDisabled("%s | arrows: facing", gPlayerUp ? "Player-up" : "North-up (+Z)");

        const float size = (std::max)(1.0f, (std::min)(260.0f * scale, ImGui::GetContentRegionAvail().x));
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const ImVec2 center(top.x + size * 0.5f, top.y + size * 0.5f);
        const float radius = (std::max)(1.0f, size * 0.5f - 18.0f * scale);
        ImGui::InvisibleButton("radar_canvas", ImVec2(size, size));
        const bool canvasHovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(top, ImVec2(top.x + size, top.y + size), true);
        draw->AddRect(top, ImVec2(top.x + size, top.y + size), IM_COL32(90, 90, 90, 255));
        draw->AddCircle(center, radius, IM_COL32(135, 135, 135, 255), 64);
        draw->AddCircle(center, radius * 0.5f, IM_COL32(60, 60, 60, 255), 64);
        draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(60, 60, 60, 255));
        draw->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(60, 60, 60, 255));
        const float northX = -std::sin(viewHeading), northY = -std::cos(viewHeading);
        const ImVec2 north(center.x + northX * (radius + 9.0f * scale),
            center.y + northY * (radius + 9.0f * scale));
        const ImVec2 northText = ImGui::CalcTextSize("N");
        draw->AddText(ImVec2(north.x - northText.x * 0.5f, north.y - northText.y * 0.5f),
            IM_COL32(245, 245, 245, 255), "N");
        draw->AddLine(ImVec2(center.x + northX * (radius - 7.0f * scale), center.y + northY * (radius - 7.0f * scale)),
            ImVec2(center.x + northX * radius, center.y + northY * radius), IM_COL32(245, 245, 245, 255), 2.0f * scale);

        const auto marker = [&](const Radar::Pose& pose, ImU32 color, const char* label, const char* name, bool cursed = false) {
            float x = 0, y = 0;
            if (!pose.positionValid || !Radar::Project(data.center.position, pose.position, range, x, y, viewHeading)) return;
            const ImVec2 point(center.x + x * radius, center.y + y * radius);
            if (cursed) {
                const float r = 5.0f * scale;
                draw->AddQuadFilled(ImVec2(point.x, point.y - r), ImVec2(point.x + r, point.y),
                    ImVec2(point.x, point.y + r), ImVec2(point.x - r, point.y), color);
            } else if (pose.headingValid && std::isfinite(pose.heading)) {
                const float dx = std::sin(pose.heading - viewHeading) * scale;
                const float dy = -std::cos(pose.heading - viewHeading) * scale;
                draw->AddTriangleFilled(
                    ImVec2(point.x + dx * 8, point.y + dy * 8),
                    ImVec2(point.x - dx * 5 - dy * 4, point.y - dy * 5 + dx * 4),
                    ImVec2(point.x - dx * 5 + dy * 4, point.y - dy * 5 - dx * 4), color);
            } else {
                draw->AddCircleFilled(point, 4 * scale, color);
            }
            draw->AddText(ImVec2(point.x + 8 * scale, point.y - 6 * scale), color, label);
            const float elevation = pose.position.y - data.center.position.y;
            if (std::fabs(elevation) > 1.5f) {
                char height[24];
                snprintf(height, sizeof(height), "%+.1fm", elevation);
                draw->AddText(ImVec2(point.x + 8 * scale, point.y + 7 * scale), color, height);
            }
            if (canvasHovered && ImGui::IsMouseHoveringRect(ImVec2(point.x - 8 * scale, point.y - 8 * scale),
                ImVec2(point.x + 8 * scale, point.y + 8 * scale))) {
                const float dx = pose.position.x - data.center.position.x;
                const float dz = pose.position.z - data.center.position.z;
                ImGui::SetTooltip("%s | %.1f m horizontal | %+.1f m height%s", name,
                    std::sqrt(dx * dx + dz * dz), elevation, pose.headingValid ? "" : " | heading unavailable");
            }
        };
        if (fresh && data.center.positionValid) {
            marker(data.ghost, IM_COL32(255, 95, 95, 255), "G", "Ghost");
            marker(data.bone, IM_COL32(255, 205, 70, 255), "B", "Bone");
            for (const auto& object : data.cursedObjects)
                marker(object.pose, IM_COL32(205, 135, 255, 255), "C", object.name.c_str(), true);
            for (const auto& player : data.players) {
                const std::string label = player.local ? "You" : std::to_string(player.actorId);
                marker(player.pose, player.local ? IM_COL32(255, 255, 255, 255) :
                    (player.dead ? IM_COL32(155, 155, 175, 255) : IM_COL32(75, 220, 165, 255)),
                    label.c_str(), player.name.c_str());
            }
        } else {
            draw->AddText(ImVec2(top.x + 10 * scale, center.y), IM_COL32(220, 220, 220, 255),
                fresh ? "Waiting for local position" : "Waiting for fresh data");
        }
        draw->PopClipRect();
        ImGui::TextWrapped("G red: ghost | B gold: bone | C violet diamond: cursed");
        ImGui::TextDisabled("Players: green / dead gray / you white");
        ImGui::TextDisabled("Hover for labels | beyond range hidden");
        if (fresh && !data.cursedObjects.empty() && ImGui::TreeNode("Cursed objects", "Cursed objects (%zu)", data.cursedObjects.size())) {
            for (const auto& object : data.cursedObjects)
                ImGui::Text("%s%s", object.name.c_str(), object.pose.positionValid ? "" : " [position unavailable]");
            ImGui::TreePop();
        }
        if (fresh && data.ghost.positionValid && data.center.positionValid) {
            const float dx = data.ghost.position.x - data.center.position.x;
            const float dy = data.ghost.position.y - data.center.position.y;
            const float dz = data.ghost.position.z - data.center.position.z;
            ImGui::Text("Ghost proximity: %.1f m (3D)", std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        const auto stamina = Stamina::Read();
        if (stamina.available) {
            char label[96];
            snprintf(label, sizeof(label), "Stamina %.1f / %.1f s%s", stamina.current, stamina.maximum,
                !stamina.sprintEnabled ? " (sprint disabled)" : stamina.exhausted ? " (recovering)" :
                stamina.infiniteActive ? " (infinite)" : "");
            ImGui::ProgressBar(stamina.normalized, ImVec2(-1, 0), label);
        } else ImGui::TextDisabled("Stamina: %s", stamina.reason);
        ImGui::SeparatorText("Players");
        static int killActor = 0;
        static uint64_t killGeneration = 0;
        static std::string killName;
        bool confirmKill = false;
        if (fresh && ImGui::BeginTable("radar_players", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollX, ImVec2(0, 0), 350.0f * scale)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Sanity", ImGuiTableColumnFlags_WidthFixed, 46 * scale);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 43 * scale);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100 * scale);
            ImGui::TableHeadersRow();
            for (const auto& player : data.players) {
                ImGui::PushID(player.actorId);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d: %s%s", player.actorId, player.name.c_str(), player.local ? " (you)" : "");
                ImGui::TableNextColumn();
                if (player.sanityValid) ImGui::Text("%.0f%%", player.sanity);
                else ImGui::TextUnformatted("N/A");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(!player.lifeValid ? "N/A" : player.dying ? "Dying" : player.dead ? "Dead" : "Alive");
                ImGui::TableNextColumn();
                const bool allowed = data.actionsAllowed && player.lifeValid && !player.dying && !player.pending;
                ImGui::BeginDisabled(!allowed || player.dead);
                if (ImGui::SmallButton("Kill")) {
                    killActor = player.actorId;
                    killGeneration = data.generation;
                    killName = player.name;
                    confirmKill = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!allowed || !player.dead);
                if (ImGui::SmallButton("Revive"))
                    SubmitRadarAction(player.actorId, data.generation, Radar::Action::Revive);
                ImGui::EndDisabled();
                if (player.pending) ImGui::TextUnformatted("Pending");
                ImGui::BeginDisabled(!allowed || player.dead || !player.sanityValid || !data.sanityAvailable ||
                    (!player.local && !data.remoteSanityAllowed));
                static int sanity = 100;
                if (ImGui::SmallButton("Set sanity")) {
                    sanity = static_cast<int>(std::lround(player.sanity));
                    ImGui::OpenPopup("sanity_editor");
                }
                ImGui::EndDisabled();
                if (ImGui::BeginPopup("sanity_editor")) {
                    ImGui::Text("Sanity: %s", player.name.c_str());
                    ImGui::SliderInt("Amount", &sanity, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::BeginDisabled(!allowed || player.dead || !player.sanityValid || !data.sanityAvailable ||
                        (!player.local && !data.remoteSanityAllowed));
                    if (ImGui::Button("Apply sanity")) {
                        SubmitRadarAction(player.actorId, data.generation, Radar::Action::SetSanity, sanity);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!fresh) ImGui::TextUnformatted("Waiting for fresh game-thread observations.");
        if (fresh && !data.sanityAvailable) ImGui::TextDisabled("Sanity controls unavailable; see logs.");
        if (fresh && data.actionsAllowed && !data.remoteSanityAllowed)
            ImGui::TextDisabled("Sanity: self only in this host configuration.");
        ImGui::TextWrapped("%s", data.actionRestriction.c_str());
        if (!data.message.empty()) ImGui::TextWrapped("%s", data.message.c_str());
        if (confirmKill) ImGui::OpenPopup("Confirm player kill");
        if (ImGui::BeginPopupModal("Confirm player kill", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Kill %s in this private/offline match?", killName.c_str());
            ImGui::BeginDisabled(!fresh || !data.actionsAllowed || data.generation != killGeneration);
            if (ImGui::Button("Confirm")) {
                SubmitRadarAction(killActor, killGeneration, Radar::Action::Kill);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    static void DrawWorldActions()
    {
        using Kind = WorldActions::Kind;
        const auto data = WorldActions::Read();
        const uint64_t now = GetTickCount64();
        const bool fresh = data.sampledAt && now >= data.sampledAt && now - data.sampledAt <= 750;
        const bool ready = fresh && data.available && !data.faulted && !data.pending;
        const auto submit = [&](Kind kind) {
            const bool queued = WorldActions::Queue(kind, data.generation, GetTickCount64());
            Logger::Log("[MENU] World action %d %s.", static_cast<int>(kind), queued ? "queued" : "rejected");
        };
        const auto& door = data.options[static_cast<size_t>(Kind::UnlockEntrance)];
        ImGui::SeparatorText("Entrance");
        ImGui::BeginDisabled(!ready || !door.available);
        if (ImGui::Button("Unlock front door")) submit(Kind::UnlockEntrance);
        ImGui::EndDisabled();
        ItemHint(door.available ? "Unlock once; later hunts can lock it again." : door.reason.c_str());

        ImGui::SeparatorText("Ghost interaction");
        static int selected = 0;
        const Kind interactions[]{Kind::NormalInteraction, Kind::Whisper, Kind::NearbyLight};
        const auto& interaction = data.options[static_cast<size_t>(interactions[selected])];
        const char* preview = interaction.label.empty() ? "Choose interaction" : interaction.label.c_str();
        if (ImGui::BeginCombo("Interaction", preview)) {
            for (int index = 0; index < static_cast<int>(std::size(interactions)); ++index) {
                const auto& option = data.options[static_cast<size_t>(interactions[index])];
                const char* label = option.label.empty() ?
                    (index == 0 ? "Doll-style interaction" : index == 1 ? "Whisper" : "Light / flicker") : option.label.c_str();
                if (ImGui::Selectable(label, selected == index)) selected = index;
                if (selected == index) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const auto& chosen = data.options[static_cast<size_t>(interactions[selected])];
        ImGui::BeginDisabled(!ready || !chosen.available);
        if (ImGui::Button("Interact")) submit(interactions[selected]);
        ImGui::EndDisabled();
        ItemHint(chosen.available ? "Uses native nearby targets." : chosen.reason.c_str());

        ImGui::SeparatorText("Ghost ability");
        const auto& ability = data.options[static_cast<size_t>(Kind::SpecialAbility)];
        if (!ability.label.empty()) ImGui::TextWrapped("%s", ability.label.c_str());
        ImGui::BeginDisabled(!ready || !ability.available);
        if (ImGui::Button("Activate ability")) submit(Kind::SpecialAbility);
        ImGui::EndDisabled();
        ItemHint(ability.available ? "Native effect; may move the ghost or drain sanity." : ability.reason.c_str());
        if (!fresh) ImGui::TextDisabled("Waiting for match data.");
        if (!data.message.empty()) ImGui::TextWrapped("%s", data.message.c_str());
    }

    static void DrawMenuWindow()
    {
        static float previousScale = 0.0f;
        if (!BeginPanel("Zackmophobia", &gMenuVisible, ImVec2(390.0f, 430.0f),
            ImVec2(25.0f, 35.0f), gPanelOpacity, previousScale)) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("HOME / overlay");
        ImGui::SameLine();
        ImGui::Checkbox("Show Radar", &gRadarVisible);
        ImGui::Separator();

        const auto radar = Radar::Read();
        if (Radar::IsFresh(radar, GetTickCount64()) && radar.center.positionValid)
            ImGui::Text("Player Position: %.2f, %.2f, %.2f", radar.center.position.x, radar.center.position.y, radar.center.position.z);
        else ImGui::TextUnformatted("Player Position: N/A");
        ImGui::Separator();

        if (ImGui::BeginTabBar("Mods")) {
            if (ImGui::BeginTabItem("Controls")) {
                if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
                    bool infinite = Stamina::infinite.load();
                    if (ImGui::Checkbox("Infinite Stamina", &infinite)) Stamina::infinite.store(infinite);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Prevents native stamina drain. Exhaustion must recover normally; disabled sprint stays disabled.");
                    for (size_t i = 0; i < ITEM_COUNT; ++i) {
                        ImGui::Checkbox(s_items[i].label, s_items[i].state);
                    }
                }

                if (ImGui::CollapsingHeader("World options", ImGuiTreeNodeFlags_DefaultOpen))
                    DrawWorldOptions(radar);

                if (ImGui::CollapsingHeader("Actions")) {
                    if (ImGui::Button("Force Hunt", ImVec2(-1.0f, 27.0f * gAppliedScale))) {
                        Features::bForceHunting = true;
                        Logger::Log("[MENU] Force Hunt requested.");
                    }
                    DrawWorldActions();
                }
                
                if (ImGui::CollapsingHeader("Player Modifiers")) {
                    ImGui::SliderFloat("Sprint Value", &Features::fSprintValue, 0.5f, 20.0f, "%.1f");
                    ItemHint("Sprint speed multiplier; default 6.0x.");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Ghost")) {
                Features::GhostDisplay ghost;
                {
                    std::lock_guard<std::mutex> lock(Features::ghostDisplayMutex);
                    ghost = Features::ghostDisplay;
                }
                if (!Features::bGhostTypeEnabled) {
                    ghost = {};
                    ImGui::TextUnformatted("Ghost Type Display is disabled.");
                }
                ImGui::Text("Ghost Type: %s", ghost.type ? ghost.type : "N/A");
                ImGui::Text("Ghost Name: %s", ghost.name.empty() ? "N/A" : ghost.name.c_str());
                ImGui::Text("Ghost Type ID: %d", ghost.typeId);
                ImGui::Text("Ghost State: %s", ghost.state ? ghost.state : "N/A");

                ImGui::SeparatorText("Ghost Position");
                if (ghost.positionValid) {
                    ImGui::Text("Position: %.2f, %.2f, %.2f", ghost.position[0], ghost.position[1], ghost.position[2]);
                } else {
                    ImGui::TextUnformatted("Position: N/A");
                }
                ImGui::Text("Source: %s", ghost.positionSource ? ghost.positionSource : "N/A");

                ImGui::SeparatorText("Historical States");
                for (int i = 0; i < 10; i++) {
                    ImGui::Text("%d: %s", i, ghost.historicalStates[i] ? ghost.historicalStates[i] : "N/A");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Level")) {
                if (Radar::IsFresh(radar, GetTickCount64()) && radar.bone.positionValid)
                    ImGui::Text("Bone Position: %.2f, %.2f, %.2f", radar.bone.position.x, radar.bone.position.y, radar.bone.position.z);
                else ImGui::TextUnformatted("Bone Position: N/A");
                ImGui::SeparatorText("Cursed objects");
                if (!Radar::IsFresh(radar, GetTickCount64())) ImGui::TextUnformatted("Waiting for fresh data.");
                else if (radar.cursedObjects.empty()) ImGui::TextUnformatted("None observed.");
                else for (const auto& object : radar.cursedObjects) {
                    ImGui::TextUnformatted(object.name.c_str());
                    if (object.pose.positionValid)
                        ImGui::Text("  %.2f, %.2f, %.2f", object.pose.position.x, object.pose.position.y, object.pose.position.z);
                    else ImGui::TextDisabled("  Position unavailable");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Logs")) {
                ImGui::BeginChild("log_panel", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
                std::vector<std::string> logs;
                {
                    std::lock_guard<std::mutex> lock(Logger::gMutex);
                    logs.assign(Logger::gLogLines.begin(), Logger::gLogLines.end());
                }
                for (const auto& line : logs) ImGui::TextUnformatted(line.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("UI")) {
                ImGui::SeparatorText("Display");
                ImGui::SliderFloat("UI scale", &gUiScale, 0.65f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Panel opacity", &gPanelOpacity, 0.2f, 1.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Radar opacity", &gRadarOpacity, 0.2f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ItemHint("Opacity changes backgrounds only. Drag title bars to move; corners to resize.");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    static void ShutdownImGui()
    {
        if (!gImGuiInitialized) {
            return;
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupRenderTarget();

        if (gOriginalWndProc && gWindow) {
            SetWindowLongPtr(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalWndProc));
            gOriginalWndProc = nullptr;
        }

        if (gContext) {
            gContext->Release();
            gContext = nullptr;
        }

        if (gDevice) {
            gDevice->Release();
            gDevice = nullptr;
        }

        gImGuiInitialized = false;
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (gImGuiInitialized && gMenuVisible) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
                return TRUE;
            }
        }

        if (gOriginalWndProc) {
            return CallWindowProc(gOriginalWndProc, hWnd, msg, wParam, lParam);
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    static HRESULT __stdcall hkPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        if (gShutdownRequested.load()) {
            return oPresent ? oPresent(swapChain, syncInterval, flags) : S_OK;
        }

        if (!gImGuiInitialized && swapChain) {
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(swapChain->GetDesc(&desc))) {
                gWindow = desc.OutputWindow;
            }

            if (gWindow && SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&gDevice))) && gDevice) {
                gDevice->GetImmediateContext(&gContext);
                CreateRenderTarget(swapChain);

                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                io.IniFilename = nullptr;
                ApplyStyle();

                ImGui_ImplWin32_Init(gWindow);
                ImGui_ImplDX11_Init(gDevice, gContext);

                gOriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
                gImGuiInitialized = true;
                Logger::Log("[SYSTEM] ImGui overlay initialized.");
            }
        }

        if (gImGuiInitialized) {
            static bool homeWasDown = false;
            const bool homeDown = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
            if (homeDown && !homeWasDown && !gRebinding && GetForegroundWindow() == gWindow) {
                gMenuVisible = !gMenuVisible;
            }
            homeWasDown = homeDown;
            PollControlKey();
            GhostControl::inputCaptured.store(GetForegroundWindow() != gWindow || gRebinding ||
                (gMenuVisible && (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard)));

            if (gMenuVisible) {
                if (!gRenderTargetView) {
                    CreateRenderTarget(swapChain);
                }

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                if (gUiScale != gAppliedScale) ApplyStyle();
                ImGui::NewFrame();
                DrawMenuWindow();
                DrawRadarWindow();
                GhostControl::inputCaptured.store(GetForegroundWindow() != gWindow || gRebinding ||
                    ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard);
                ImGui::Render();

                gContext->OMSetRenderTargets(1, &gRenderTargetView, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
        }

        return oPresent ? oPresent(swapChain, syncInterval, flags) : S_OK;
    }

    static void OverlayThread()
    {
        Logger::Log("[SYSTEM] Waiting for D3D11 renderer.");

        while (!gShutdownRequested.load()) {
            if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success) {
                auto methods = kiero::getMethodsTable();
                if (methods && methods[8]) {
                    oPresent = reinterpret_cast<Present_t>(methods[8]);

                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());
                    DetourAttach(reinterpret_cast<PVOID*>(&oPresent), hkPresent);
                    if (DetourTransactionCommit() == NO_ERROR) {
                        gHookInstalled = true;
                        Logger::Log("[SYSTEM] ImGui Present hook installed.");
                        return;
                    }

                    Logger::Log("[ERROR] ImGui Present hook transaction failed.");
                    return;
                }
            }

            Sleep(500);
        }
    }

    void Start()
    {
        gShutdownRequested = false;
        std::thread(OverlayThread).detach();
    }

    void Shutdown()
    {
        if (gShutdownRequested.exchange(true)) {
            return;
        }

        Logger::Log("[SYSTEM] Shutting down menu overlay.");

        if (gHookInstalled && oPresent) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(reinterpret_cast<PVOID*>(&oPresent), hkPresent);
            if (DetourTransactionCommit() == NO_ERROR) {
                Logger::Log("[SYSTEM] Present hook detached.");
            } else {
                Logger::Log("[ERROR] Present hook detach failed.");
            }
            gHookInstalled = false;
        }

        ShutdownImGui();
        kiero::shutdown();
    }
}
