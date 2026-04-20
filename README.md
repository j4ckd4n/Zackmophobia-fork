# Zackmophobia
Phasmophobia Internal Mod (Modular IL2CPP)
A high-performance C++ internal cheat for Phasmophobia. This project utilizes the Microsoft Detours library for function hooking and a custom IL2CPP discovery engine to bypass obfuscation and locate game methods at runtime.

**🚀 Features**

Ghost Identification: Deep-scans memory to extract the Ghost's Name and real type.

Infinite Stamina: Modifies PlayerStamina offsets to lock stamina at maximum.

Speed Modifier: Adjusts FirstPersonController walk/run/sprint variables via direct memory manipulation.

Perfect Game: Forces the "perfect game" credit to be true and gives bonus 5000$ each match

Keybind Toggling: F4, F5, and F6 used as keybinds to toggle different mods

**📁 Project Structure**

/SDK: Contains the bridge to the Unity IL2CPP engine and safe memory string validators.

/Hooks: Modularized function hooks (Ghost-specific and Player-specific logic).

/Features: Global state management and toggle systems.

/Menu: In Progress....

dllmain.cpp: The multi-threaded entry point of the DLL.

**🛠️ Technical Implementation**

The mod operates by:

Dynamic Linking: Locating GameAssembly.dll and mapping IL2CPP export functions.

Metadata Traversal: Using the IL2CPP API to traverse the game's assembly image and find method pointers for function calls.

Detouring: Using Microsoft Detours to overwrite function entry points with jumps to our custom logic.

**🤖 AI Collaboration**

This project and README was developed with the assistance of Gemini and Github Copilot.
