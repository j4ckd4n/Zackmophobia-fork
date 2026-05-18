# Zackmophobia
Phasmophobia Internal Mod (Modular IL2CPP)
A high-performance C++ internal cheat for Phasmophobia. This project utilizes the Microsoft Detours library for function hooking and a custom IL2CPP discovery engine to bypass obfuscation and locate game methods at runtime.

---

> **⚠️ Disclaimer ⚠️**
>
> _This repository is intended for educational, research, and open-source development purposes only. The project focuses on reverse engineering, runtime analysis, and experimentation with game behavior and software injection techniques to better understand how games and related systems operate._
> 
> _This project is not intended to promote cheating, malicious behavior, or disruption of online experiences. Please do not use these tools in multiplayer or online environments where they may negatively affect other players or violate a game's terms of service._
> 
> _By using this repository, you acknowledge that you are responsible for ensuring your usage complies with applicable laws, platform policies, and game terms of service._

---

**🐛 Bugs**

- Console "selected" arrows stick instead of being cleared when choosing another function.
- Console writes can accidentally overwrite previous log entries or merge entirely, causing confusion in the output.
- "Infinite Sprint" can cause the game to crash.
- During testing, there have been cases where the other player might experience your character lagging when utilizing the speed features.
- The injection tool does not work as intented at this time, causing the game to crash. Recommend using a third-party tool for DLL injection.

**🚀 Features**

Ghost Identification: Deep-scans memory to extract the Ghost's Name and real type.

Infinite Stamina: Modifies PlayerStamina offsets to lock stamina at maximum.

Speed Modifier: Adjusts FirstPersonController walk/run/sprint variables via direct memory manipulation.

Perfect Game: Forces the "perfect game" credit to be true and gives bonus 5000$ each match

- "Perfect Game" hook only affects the current player. It does not affect others at this time.

Keybind Toggling: F4, F5, and F6 used as keybinds to toggle different mods

**📁 Project Structure**

/SDK: Contains the bridge to the Unity IL2CPP engine and safe memory string validators.

/Hooks: Modularized function hooks (Ghost-specific and Player-specific logic).

/Features: Global state management and toggle systems.

/Menu: Gives you a list of available functions that can be utilized in-game.

dllmain.cpp: The multi-threaded entry point of the DLL.

**🛠️ Technical Implementation**

The mod operates by:

Dynamic Linking: Locating GameAssembly.dll and mapping IL2CPP export functions.

Metadata Traversal: Using the IL2CPP API to traverse the game's assembly image and find method pointers for function calls.

Detouring: Using Microsoft Detours to overwrite function entry points with jumps to our custom logic.

**🤖 AI Collaboration**

This project and README was developed with the assistance of Gemini and Github Copilot.

## Runtime Controls

You can use up and down arrow keys to select the available tools within the console window.
The controls are active while you are playing the game so if you have a secondary screen, you can move the console window to the
secondary display and monitor the changes being made.

## Logging

The DLL now writes runtime logs to `Zackmophobia.log` in the game process folder and to `OutputDebugString`.

This lets you verify hook lifecycle without relying only on the console:

- hook resolution status at startup
- hook transaction commit
- one-time "hook is executing" markers per hook
- toggle transitions and key feature overrides

## Build

Open `TryAgainHook.sln` in Visual Studio 2022 and build `Debug|x64` (or `Release|x64`).

Expected DLL output:

- `x64/Debug/Zackmophobia.dll`

## Launch Helper

A launcher helper source is included at:

- `Launcher/LaunchWithDll.cpp`

It uses Detours `DetourCreateProcessWithDllExW` to start a target executable with `Zackmophobia.dll` loaded at process start.

### Compile helper quickly (x64 Developer PowerShell)

Important: use x64 Native Tools / x64 Developer PowerShell. If you compile as x86, linking against `Lib\detours.lib` (x64) will fail.

```powershell
cl /EHsc /std:c++20 /Fe:Launcher\LaunchWithDll.exe Launcher\LaunchWithDll.cpp /I. /link /MACHINE:X64 /LIBPATH:Lib detours.lib
```

If you are in a normal PowerShell window, first initialize the VS build environment for x64:

```powershell
& "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /EHsc /std:c++20 /Fe:Launcher\LaunchWithDll.exe Launcher\LaunchWithDll.cpp /I. /link /MACHINE:X64 /LIBPATH:Lib detours.lib
```

### Example usage

```powershell
Launcher\LaunchWithDll.exe "C:\Program Files (x86)\Steam\steamapps\common\Phasmophobia\Phasmophobia.exe" "D:\git\Zackmophobia\x64\Debug\Zackmophobia.dll"
```

## Safety Fixes Applied

- `hkGetBonus` and `hkIsPerfect` now call originals when toggle is OFF
- `GetRewardAmount` hook typedef now matches `int` return type
- no-kick and media reward hooks are now toggle-gated (`F6`) and call originals when disabled
