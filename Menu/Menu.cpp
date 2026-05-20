#include <windows.h>
#include <stdio.h>
#include <thread>
#include <algorithm>
#include "Menu.h"
#include "../Features/Features.h"
#include "../Hooks/Hooks.h"
#include "../Utils/Logger.h"

namespace Menu {

    struct MenuItem {
        const char* label;
        bool* state;
        bool isAction; // if true, triggers once on Enter instead of toggling
    };

    static MenuItem s_items[] = {
        { "Infinite Stamina",  &Features::bStaminaEnabled,   false },
        { "Speed Hack",        &Features::bSpeedEnabled,      false },
        { "Ghost Type Display",&Features::bGhostTypeEnabled,  false },
        { "Force Tarot (Sun)", &Features::bForceTarot,        false },
        { "Perfect Game",      &Features::bPerfectGame,       false },
        { "Bonus Reward",      &Features::bBonusReward,       false },
        { "No Kick",           &Features::bNoKick,            false },
        { "No Sanity Loss",    &Features::bNoSanityLoss,      false },
        { "Force Hunt [ACTION]",&Features::bForceHunting,     true  }, // Does not work yet. Need to intercept how the calls are made. ChangeState & Hunting are some prime candidates
    };

    static constexpr int ITEM_COUNT = sizeof(s_items) / sizeof(s_items[0]);
    // Row where the log panel starts: header(1) + blank(1) + items + blank(1) + speed(1) + blank(1) + separator(1) + blank(1)
    static constexpr int LOG_ROW_START = 3 + ITEM_COUNT + 4;
    static constexpr int LOG_COL_WIDTH = 80;

    static void SetCursorPos(int x, int y) {
        COORD c = { (SHORT)x, (SHORT)y };
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
    }

    static void HideCursor() {
        CONSOLE_CURSOR_INFO ci = { 1, FALSE };
        SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
    }

    static void DrawMenu(int selected) {
        SetCursorPos(0, 0);
        printf("=== Zackmophobia Menu (UP/DOWN = navigate, ENTER = toggle, + = speed+, - = speed-) ===\n\n");
        for (int i = 0; i < ITEM_COUNT; i++) {
            const char* tag = s_items[i].isAction ? "ACT" : (*s_items[i].state ? "ON " : "OFF");
            if (i == selected)
                printf(" >> [%s] %-40s <<\n", tag, s_items[i].label);
            else
                printf("    [%s] %-40s\n",    tag, s_items[i].label);
        }
        printf("\n    Speed Value: %.1f\n\n", Features::fSprintValue);
        printf("--- Log -----------------------------------------------------------------------\n\n");
    }

    static void DrawLog() {
        std::lock_guard<std::mutex> lock(Logger::gMutex);

        // draw player position first
        SetCursorPos(0, LOG_ROW_START - 1);
        printf("Player Position: X=%.2f Y=%.2f Z=%.2f (Source: %s)                    \n",
            Features::cPlayerPos[0], Features::cPlayerPos[1], Features::cPlayerPos[2],
            Features::cPlayerPosSource ? Features::cPlayerPosSource : "N/A");

        int row = LOG_ROW_START;
        for (const auto& line : Logger::gLogLines) {
            SetCursorPos(0, row);
            printf("%-*s", LOG_COL_WIDTH, line.c_str());
            ++row;
        }
        // Erase any leftover lines from previous (longer) log state
        for (int r = row; r < LOG_ROW_START + Logger::MAX_LOG_LINES; ++r) {
            SetCursorPos(0, r);
            printf("%-*s", LOG_COL_WIDTH, "");
        }
    }

    static void MenuThread() {
        HideCursor();
        // Increase console buffer and window size to reduce flicker when many log lines are printed.
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
                COORD newSize = csbi.dwSize;
                newSize.X = std::max<SHORT>(newSize.X, (SHORT)LOG_COL_WIDTH);
                newSize.Y = std::max<SHORT>(newSize.Y, (SHORT)1000); // large buffer for logs
                SetConsoleScreenBufferSize(hOut, newSize);

                COORD largest = GetLargestConsoleWindowSize(hOut);
                SHORT winH = (SHORT)std::min<int>(largest.Y, 60); // visible window height
                SMALL_RECT window = { 0, 0, (SHORT)(newSize.X - 1), (SHORT)(winH - 1) };
                SetConsoleWindowInfo(hOut, TRUE, &window);
            }
        }

        system("cls");

        int selected = 0;
        DrawMenu(selected);
        DrawLog();

        while (true) {
            bool redrawMenu = false;

            if (GetAsyncKeyState(VK_UP) & 1) {
                selected = (selected - 1 + ITEM_COUNT) % ITEM_COUNT;
                redrawMenu = true;
            }
            if (GetAsyncKeyState(VK_DOWN) & 1) {
                selected = (selected + 1) % ITEM_COUNT;
                redrawMenu = true;
            }
            if (GetAsyncKeyState(VK_RETURN) & 1) {
                if (s_items[selected].isAction) {
                    Features::bForceHunting = true;
                    Logger::Log("[MENU] Force Hunt requested.");
                } else {
                    *s_items[selected].state = !*s_items[selected].state;
                    Logger::Log("[MENU] %s: %s", s_items[selected].label, *s_items[selected].state ? "ON" : "OFF");
                }
                redrawMenu = true;
            }
            if (GetAsyncKeyState(VK_ADD) & 1) {
                Features::fSprintValue += 0.5f;
                redrawMenu = true;
            }
            if (GetAsyncKeyState(VK_SUBTRACT) & 1) {
                Features::fSprintValue -= 0.5f;
                if (Features::fSprintValue < 0.5f) Features::fSprintValue = 0.5f;
                redrawMenu = true;
            }

            // Always refresh log panel so hook-thread messages appear without corrupting the menu
            DrawLog();

            if (redrawMenu)
                DrawMenu(selected);

            Sleep(50);
        }
    }

    void Start() {
        std::thread(MenuThread).detach();
    }
}