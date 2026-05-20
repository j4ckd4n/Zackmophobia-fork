#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

#include "Menu.h"
#include "../Features/Features.h"
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
    static bool gHookInstalled = false;

    struct MenuItem {
        const char* label;
        bool* state;
    };

    static MenuItem s_items[] = {
        { "Infinite Stamina",  &Features::bStaminaEnabled },
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
        style.WindowRounding = 10.0f;
        style.FrameRounding = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 8.0f;
        style.WindowPadding = ImVec2(14.0f, 14.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        style.WindowBorderSize = 0.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.96f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.13f, 0.17f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.25f, 0.75f, 0.62f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.75f, 0.62f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.17f, 0.62f, 0.50f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.28f, 0.34f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
    }

    static void DrawMenuWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(560.0f, 620.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.96f);

        if (!ImGui::Begin("Zackmophobia", &gMenuVisible, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("HOME toggles the overlay");
        ImGui::Separator();

        ImGui::Text("Player Position: %.2f, %.2f, %.2f", Features::cPlayerPos[0], Features::cPlayerPos[1], Features::cPlayerPos[2]);
        ImGui::Text("Source: %s", Features::cPlayerPosSource ? Features::cPlayerPosSource : "N/A");
        ImGui::Separator();

        if (ImGui::BeginTabBar("Mods")) {
            if (ImGui::BeginTabItem("All Mods")) {
                if (ImGui::CollapsingHeader("Features")) {
                    for (size_t i = 0; i < ITEM_COUNT; ++i) {
                        ImGui::Checkbox(s_items[i].label, s_items[i].state);
                    }
                }

                if (ImGui::CollapsingHeader("Actions")) {
                    ImGui::TextUnformatted("Force Hunt will trigger the ghost's hunting state on the next update.");
                    if (ImGui::Button("Force Hunt", ImVec2(-1.0f, 34.0f))) {
                        Features::bForceHunting = true;
                        Logger::Log("[MENU] Force Hunt requested.");
                    }
                }
                
                if (ImGui::CollapsingHeader("Player Modifiers")) {
                    ImGui::TextUnformatted("Sprint Value controls the multiplier for player sprint speed. Default is 6.0x.");
                    ImGui::SliderFloat("Sprint Value", &Features::fSprintValue, 0.5f, 20.0f, "%.1f");
                }

                if (ImGui::CollapsingHeader("Logs", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextUnformatted("Recent log messages from the game will appear here.");
                    ImGui::BeginChild("log_panel", ImVec2(0.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
                    std::vector<std::string> logs;
                    {
                        std::lock_guard<std::mutex> lock(Logger::gMutex);
                        logs.assign(Logger::gLogLines.begin(), Logger::gLogLines.end());
                    }
                    for (const auto& line : logs) {
                        ImGui::TextUnformatted(line.c_str());
                    }
                    ImGui::EndChild();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Ghost Info")) {
                ImGui::TextUnformatted("This tab should populate once the map is loaded.");
                ImGui::Separator();
                ImGui::Text("Ghost Type: %s", Features::cGhostType ? Features::cGhostType : "N/A");
                ImGui::Text("Ghost Name: %s", Features::cGhostName ? Features::cGhostName : "N/A");
                ImGui::Text("Ghost Type ID: %d", Features::cGhostTypeId);
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
            if (GetAsyncKeyState(VK_HOME) & 1) {
                gMenuVisible = !gMenuVisible;
            }

            if (gMenuVisible) {
                if (!gRenderTargetView) {
                    CreateRenderTarget(swapChain);
                }

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                DrawMenuWindow();
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

        while (true) {
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
        std::thread(OverlayThread).detach();
    }
}
