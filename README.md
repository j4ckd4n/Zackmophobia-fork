# Zackmophobia
Phasmophobia Internal Mod (Modular IL2CPP)
A high-performance C++ internal cheat for Phasmophobia. This project utilizes the Microsoft Detours library for function hooking and a custom IL2CPP discovery engine to bypass obfuscation and locate game methods at runtime.

**🚀 Features**

Ghost Identification: Deep-scans memory to extract the Ghost's Name and real Type (bypassing Spirit/Journal defaults).

Infinite Stamina: Modifies PlayerStamina offsets to lock stamina at maximum.

Speed Modifier: Adjusts FirstPersonController walk/run/sprint variables via direct memory manipulation.

Perfect Game: Forces the "perfect game" credit to be true and gives bonus 5000$ each match

Master Toggle: Integrated F6 hotkey to enable/disable all hooks instantly.

**📁 Project Structure**

/SDK: Contains the bridge to the Unity IL2CPP engine and safe memory string validators.

/Hooks: Modularized function hooks (Ghost-specific and Player-specific logic).

/Features: Global state management and toggle systems.

dllmain.cpp: The multi-threaded entry point of the DLL.

**🛠️ Technical Implementation**

The mod operates by:

Dynamic Linking: Locating GameAssembly.dll and mapping IL2CPP export functions.

Metadata Traversal: Using the IL2CPP API to traverse the game's assembly image and find method pointers for Update calls.

Detouring: Using Microsoft Detours to overwrite function entry points with jumps to our custom logic.

Memory Scanning: Implementing a "Backwards-Scan" logic to verify ghost types based on non-zero integer priority in the GhostInfo struct.

**🤖 AI Collaboration**

This project and README was developed with the assistance of Gemini and Github Copilot.
