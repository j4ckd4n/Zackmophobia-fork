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

**🚀 Features**

Ghost Identification: Reads the ghost's name and type from its structured IL2CPP data, and displays live GhostAI world coordinates.

Proximity Radar: North-up/player-up ghost/player/bone/cursed-object markers with facing arrows, player sanity and life states, and private/offline host-only kill/revive requests.

Overlay Controls: Compact draggable charcoal panels, adjustable UI scale and panel/radar opacity, renderer-only ghost visibility, and experimental local-PC interactions while dead.

Experimental Controls: Host-only ghost possession with third-/first-person camera and adjustable movement speed; opt-in expanded pickup in the lobby and missions.

Infinite Stamina: Suppresses local sprint drain during native stamina updates without changing recovery or difficulty restrictions.

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

## Ghost Information Compatibility

The ghost display was updated against Steam build **25811160**, with IL2CPP metadata version **31**.
The current enum includes Kormos (28), Aswang (29), and Deildegast (30); unknown IDs
are displayed without indexing outside the type table.

The recovered x64 layout embeds a 0x40-byte value in `GhostInfo` at offset `0x28`.
Its second type ID is at `GhostInfo + 0x2C`, its name reference at `+0x48`, and
its GhostAI reference at `+0x68`. `GhostAI + 0x38` points back to GhostInfo.
Startup checks the metadata's `GhostInfo.ghost` offset and the Init parameter's
value size before enabling reads. These checks detect some layout changes, not
every possible future change; this is not a version-independent SDK.

Coordinates come from **GhostAI**, not GhostInfo, through
`Component.get_transform` and `Transform.get_position_Injected`. They are world
coordinates, not a room label or the favorite room. Invalid positions display
`N/A`, and destroying the ghost clears its cached identity and position. The
render thread uses an owned, mutex-protected snapshot instead of borrowing
temporary string storage. The display toggle hides this snapshot; observation
continues so re-enabling it shows current information, including after late injection.

The installed artifacts examined had these SHA-256 digests:

```text
GameAssembly.dll:    e300eda993f2479922628f1b44f59e60c156c240f5a07c7289c4bd0fe4b790c8
global-metadata.dat: a1d282afc5a7dada6617f30f0b8a595a8bc5ce32f27cd81baf38903711c68253
```

Il2CppDumper recovered the current metadata and enum. REA's CLI/Rizin fallback
provided static native evidence after the MCP provider remained unconfigured
in the running server and a Ghidra CLI import exceeded its startup deadline:

- `GhostAI.Init` at RVA `0x2022A70` copies the 0x40-byte argument into GhostInfo:
  `ev_bcbbc65748d395b3eebc7b5d5ca71344f5fc1794776116e814cccfac210a41d4`.
- `Transform.get_position_Injected` at RVA `0x5205290` forwards the instance
  and explicit output pointer:
  `ev_ac8c5c27ade3caf6d36fab684590d222e30e3f7bb7cda5656b0f4d9598351208`.

These observations establish static layout and calling-convention evidence.
User-reported runtime results and remaining verification limits are described
below.

Focused mock-hook regression tests are in `scripts\GhostInfoTests.cpp`. From
an x64 Developer PowerShell:

```powershell
New-Item -ItemType Directory -Force .\x64\GhostInfoTests | Out-Null
cl /nologo /EHsc /std:c++20 /Fe:x64\GhostInfoTests\GhostInfoTests.exe /Fo:x64\GhostInfoTests\ scripts\GhostInfoTests.cpp Hooks\GhostHooks.cpp
if ($LASTEXITCODE -eq 0) { .\x64\GhostInfoTests\GhostInfoTests.exe }
```

## Proximity Radar and Player Controls

The separate **Radar** window is enabled by default. **HOME** hides both overlay
windows; **Show Radar** in the main window toggles just the radar. Its adjustable
5-100 m circular X/Z view centers on the local player's networked avatar. Choose
north-up (world +Z up and +X right) or player-up; there is no floorplan. Player-up
rotates both positions and arrows using the local heading, falling back explicitly
to north-up when that heading is unavailable. Red marks the ghost, gold the
bone, green living players, gray dead players, and white the local player.
Out-of-range entities are hidden, not clamped to the rim. Height differences
over 1.5 m are labeled; hovering a marker shows horizontal distance and height.
Ghost proximity below the radar is a 3D distance.

Arrows use the projected world-space forward vector from Unity rotation,
preferring the player's head GameObject over body rotation. A dot indicates
unavailable heading. Bone arrows represent object orientation, not movement.
Player markers use the Player component's transform, never the corpse/ragdoll;
whether dead players' networked avatars continue following their free-roaming
specters is **not verified**. Positions are observed during a match. The bone
is rediscovered among active scene objects and omitted once its Collectable
collection flag is set, avoiding the old pre-spawn position cache.

Named cursed-object markers enumerate active `CursedItem` instances, including
multiple instances of the same type. Recognized types are Ouija board, music box,
tarot cards, summoning circle, haunted mirror, voodoo doll, and monkey paw.
Inactive objects are not enumerated; this does not guarantee markers for carried,
hidden, or disabled objects.

The player table shows `100 - insanity`, alive/dead/dying state, and pending
requests. Unity/Photon observation runs on game-thread callbacks at
most five times per second; rendering only reads synchronized owned snapshots.
Observations older than 1.5 seconds hide markers and disable actions. Leaving or
changing a room, initializing/destroying a ghost, or encountering an observation
access violation clears the relevant cached state and queued requests. Layout
checks detect some incompatible updates, not all of them.

Kill/revive requires an active match and either offline mode or master-client
authority in a non-visible/private room. Kill requires confirmation. The UI
queues a request; the game thread rechecks authority, session generation,
target identity, and life state before invoking `BeginDeathSequence(SnappedNeck)`
or `RevivePlayer(false)`. It does not forge RPC sender information or write life
flags. A request remains pending until the requested state is observed or ten
seconds elapse; it is never automatically retried. Game-side ownership/sender
checks can reject a request. Native action access violations disable subsequent
actions and are reported in the overlay/log.

**Set sanity** opens a 0-100% control for each living player, including yourself.
Apply sends a one-time native `NetworkedUpdatePlayerSanity` RPC with
`insanity = 100 - sanity` through `PhotonView.RPC(All)`. It does not fabricate
sender information, write remote replicas directly, or continuously lock sanity.
Normal drain continues. The examined receiver accepts its owner or the lowest
actor ID; remote controls are therefore disabled for a promoted master who does
not satisfy that native sender rule. Requests recheck authority, identity,
generation, life state and range on the game thread. A matching local observation
does not independently verify another client's acceptance. Failures are reported
and requests are never retried.

Additional static REA evidence for the installed build:

- Death/revive wrappers and sanity arithmetic:
  `ev_f600a23098517fe1b3816f85b775e74fc5690833c9cd72da9ab783d92f1c6a11`.
- Lifecycle flags, local-owner checks, and revive coroutine:
  `ev_c56589f43498edb4188b86f64b558ba6c45050cab912208122227d1bdcda9f47`.
- Head GameObject access and rotation-injected ABI:
  `ev_cc496633428db457553515ac9b4702bb5da0fd5499e6d5460fdf7ad0f0f81d9b`.
- `Collectable.CollectNetworked` sets `+0x9C`; Player.SetPosition writes the
  Player component's transform; PCPlayer.SetHeadTransform accesses its head:
  `ev_ed5b5ca2c3211ddf425146b5b37bf1bad707f8ca9a1fbf975848089b1403d8b7`.
- Player lookup dereferences the Network singleton, so observation avoids
  calling it during lobby setup:
  `ev_1fdb34a90b7555ac4dabda276ead1bd494f2b1da6552ce9c3f7d3348e475bc36`.

The direct MCP Rizin call also failed with `ERR_REA_WINDOWS_NATIVE`; the CLI
fallback used the supplied Cutter Rizin executable successfully. Its bounded
disassembly is static evidence, not an in-game test. Ghidra import and managed
inspection limits remain unresolved. The user confirmed radar/cursed-object
markers, panel controls, and ghost visibility without changing AI behavior
in-game. Dead-player tracking/interactions, bone collection, remote-player
action acceptance, map transitions, possession and expanded pickup still require
private/offline runtime verification. No game was launched or injected by the
assistant.

## Overlay Appearance and World Options

The compact charcoal style uses square panels, thin borders, and grayscale
controls inspired by the supplied reference. Panels remain draggable and
resizable. Appearance controls adjust UI scale, panel opacity, and radar opacity;
the radar fill stays more opaque than its parent panel. These settings are
session-only. HOME still hides both panels without resetting settings.
Control explanations and disabled-action reasons appear on hover rather than
as paragraphs in the panel. Errors and essential warnings remain visible.

Both world options require an active private/offline match with host authority.
Unsupported layouts or missing native methods disable the corresponding option.

**Ghost visibility** enables only the current ghost model's existing renderers.
It does not request a hunt, change GhostAI state, switch appearance/materials, or
send interaction RPCs. Original per-renderer enabled states are captured; natural
`GhostModel.SetVisibility` requests update the restoration baseline. State
transitions suspend the override, and hunting defers entirely to normal
visibility/blinking. Disabling the option or changing the model/session restores
the baseline. The user confirmed this option works without changing AI behavior.
Shader/material effects remain game-controlled; not every appearance is verified.

**Dead interaction (experimental, PC only)** allows the completed-dead local
player to enter normal PC item/use/inventory and door-drag input paths.
`PCPropGrab + 0xE0` and the local controller's `+0x38` gate are temporarily
changed only around those callbacks and restored in a `finally` scope.
The authoritative `Player.dead` flag is never changed, and a genuine revival
retains its new controller state. Remote players and VR controllers are not
patched. Ordinary item permissions, locks, and network ownership/sender checks
remain intact; acceptance of every equipment/object type is not guaranteed.

END first requests restoration on the game thread. If restoration fails or the
game does not acknowledge cleanup within two seconds, unloading is cancelled
and logged; resume the game and retry END. Hook-detachment failure also leaves
the DLL loaded rather than freeing live callbacks.

Additional static REA evidence:

- Renderer-only `GhostModel.SetVisibility` (`0x208BA80`), PC input gates, and
  `PlayerDied`/`PlayerRevived` interaction-mode writes:
  `ev_4f2532468afddbf5f1c025564bf9cc3348c56f27effac3d609449e8bb7cd6287`.
- Interaction dispatch and ordinary pickup predicates:
  `ev_99f71692a37a43a5aab04defc51e5957571a40dc7de66b4366e371503a7d9808`.
- Active-only `Object.FindObjectsOfType(Type)` (`0x51F7640`):
  `ev_72096ed37a6295a1537749638575098a935da080833931e48a2eed9594e3b0c9`.
- Door interaction's local-controller death gate:
  `ev_34fa8a6b647eab0fe0f9438b15d9e11adc1ca77592ffcb588f093ac4e5a84ea1`.
- Completed local death sets `+0x38`; door physics uses that gate, and item
  death cleanup leaves a null inventory slot rather than an empty invalid index:
  `ev_0544004e477dbe9d41fbb0b3b3923ea10b8c97e7521d34749e0e73ec13537b22`.

Focused radar regressions cover projection and facing, authority/freshness,
queue/session changes, mocked native transforms and head access, destroyed
Unity objects, sanity,
collection, action dispatch/confirmation/timeouts, malformed rosters, and
observation access-violation shutdown. Additional cases cover player-up rotation,
multiple cursed items, reversible renderer state, hunt/transition suspension,
scoped local interaction gates, real revival, callback exceptions, and cleanup
acknowledgement/failure. The overlay also passed a headless ImGui smoke test;
that does not establish live rendering or game-side interaction acceptance.
From an x64 Developer PowerShell:

```powershell
New-Item -ItemType Directory -Force .\x64\RadarTests | Out-Null
cl /nologo /EHsc /std:c++20 /Fe:x64\RadarTests\RadarTests.exe /Fo:x64\RadarTests\ scripts\RadarTests.cpp
if ($LASTEXITCODE -eq 0) { .\x64\RadarTests\RadarTests.exe }
```

## Door and Ghost Actions

The **Controls > Actions** menu contains a one-shot **Unlock front door** action,
an interaction selector, and current-ghost special-ability activation. These
require an active match and private/offline host authority. UI requests carry a
session generation, expire after 750 ms and are revalidated on the game thread;
only one can be pending, with no automatic retries. Native failures disable
these actions and report the error.
Singleton discovery selects a non-literal static field of the singleton's own
type, rather than treating every offset-zero static field as a candidate.
Bindings are checked independently: missing ability or whisper metadata no
longer disables entrance unlock or nearby interaction. The Logs tab identifies
missing singleton/method bindings instead of a single generic build error.

Entrance unlock enumerates the current level's doors and selects only locked
doors using the native **Main** key type. It invokes `ToggleDoorLock(false,false)`,
which uses the game's normal unlock/collider/network paths. Secondary and
permanently locked doors are not selected. This is not a persistent lock bypass:
a later hunt can lock entrances again. First-unlock setup notifications retain
their native behavior. The door action is permitted during possession.

Interaction choices are **doll-style interaction**, **whisper**, and
**nearby light/flicker**. They use the current ghost's real components and native
target sensors; they do not fake EMF or select an invalid target. Native
eligibility, sound choice and target availability still apply, so a request may
have no visible/audible result. Doll-style interaction calls
`GhostActivity.NormalInteraction(true)`, matching the voodoo doll's non-heart
pin callbacks. It does not use/consume the doll, simulate its heart pin, or
reproduce its special Twins callback distribution. Light interaction can enter a native interaction
state, and ghost actions are disabled during possession, hunts, events or other
ongoing interactions rather than competing with those routines.

The special-ability button uses the actual type-specific dispatcher, not
`GhostActivity.GhostAbility`, whose name misleadingly covers randomized events.
Unavailable/unsupported abilities display a reason. Effects retain native
rules and can include ghost movement, sanity loss or a hunt; no other ghost's
ability is substituted. The recovered dispatcher supports:

| Ghost | Native ability / prerequisite |
| --- | --- |
| Wraith | Teleport toward an eligible living player; native navigation checks remain. |
| Phantom | Visit an eligible living player using native AI movement. |
| Poltergeist | Multi-throw eligible props; can drain nearby sanity. |
| Jinn | Powered fuse box required; delayed proximity/room-based sanity effect. |
| Demon | **Can start a hunt**; native level and hunt-entry guards remain. |
| Yurei | Eligible favourite-room door required; door interaction and qualifying sanity loss. |
| Obake | Accelerate fading of existing active fingerprints, **not** hunt shapeshifting. |

Other passive or untraced abilities are not offered; unavailable does not mean
that ghost has no special behavior elsewhere. These actions have static/mocked evidence only, not
live multiplayer or physics verification.

Relevant REA records:

- Native door lock/unlock and interaction wrappers:
  `ev_e2db21cde922eab028e42fd231263e709ebd6702715129ea8f4d039b15ebe6a1`,
  `ev_ea3c84e584094df08dd2313a24e7510561cc376b6b2fc797026020923e3c474c`.
- Voodoo doll pin branches and non-heart callbacks (`NormalInteraction(true)`):
  `ev_0f0014afda9dd325e581fec8fae1bafb1a9122a1bfeb769468b9adb90739625c`,
  `ev_cafc0efff1342008623445ee4b4780e1edd9a6e452882ebce74fae899fc7e43a`,
  `ev_d3d9f44ce22100a3d24aaf25996d9bf23f50e36d86033fcd936cf374e5296148`,
  `ev_ca424c14e6fe0e0c569bcc54fdc1e3d86a6810d59f5c94a22c6f8f78228b435b`.
- Ability dispatcher and native interaction guards:
  `ev_dfe585bb5d8e630174bf27d299ddaf10b65ec746145d625cbc6cbb55e66fd807`,
  `ev_c550cbc9951ac1bafc1b1dcd636c660380c6bb379b71e30777e9b629e0ec4168`.
- Poltergeist native sensor/throw/sanity path:
  `ev_83232da41a8fd8bf3b5fde089d21964dc33cc1aa7c384e96b03575ec36a66151`,
  `ev_0913965aed0ac61a01b3fb2c93bb32aac62c5cd5f6ffe369877eeba2567a1e9e`.
- Other native type entries, conditional prerequisites and delayed effects:
  `ev_5fa7f8e38dc943a319e88a5762869db72c702cf6d24b6fb4f43e1215813ea6d5`,
  `ev_57ef44ae9f462141de85823ed75caaac93bd2ae64f8a1b51b89fa49b6127b932`,
  `ev_05e24620811fb1ca52cfbee0893cc882ea969537599abe8a4a3880c10ac890c3`,
  `ev_1f5a0c0b222730e0e04257c37ef6deee8eeac69d29349b96e11c6fb2b66b0504`,
  `ev_36afa99f5d626e734fe3acba504931a096780d8050ad5a4ca0011595bb23962e`.

```powershell
New-Item -ItemType Directory -Force .\x64\WorldActionTests | Out-Null
cl /nologo /EHsc /std:c++20 /Fe:x64\WorldActionTests\WorldActionTests.exe /Fo:x64\WorldActionTests\ scripts\WorldActionTests.cpp
if ($LASTEXITCODE -eq 0) { .\x64\WorldActionTests\WorldActionTests.exe }
```

## Local Stamina

The radar includes a local stamina bar with remaining/native maximum sprint
seconds and disabled/recovering/infinite status. Stale or invalid samples show
an unavailable reason, not a full bar. Local-controller sampling also runs from
the PC grab update, including in the mission-selection lobby when the concrete
stamina update is inactive. It does not require a networked avatar or Photon
ownership; unrelated invalid callbacks do not erase a valid local sample.
Read-only sampling does not claim that infinite stamina is active.
The examined build initializes/clamps capacity to
3 seconds; difficulty settings choose disabled, finite or unlimited sprint.

**Infinite Stamina** now wraps the concrete `PCStamina.Update` and
`VRStamina.Update`, because depletion happens after the base update returns.
For the local, living player it scopes the native drain gate at `+0x35`
off during that update and restores it immediately. Remaining time is at
`+0x40`; native regeneration, sprint events and difficulty restrictions remain.
This prevents further drain; it does not refill an exhausted player, clear
exhaustion, or enable sprint where the difficulty disables it.

The obsolete implementation wrote a float into a UnityEvent reference at
`+0x50`, not the stamina field. **Restart the game before using the updated DLL
if that old option was enabled**; unloading cannot reconstruct an already
corrupted event reference.

Static REA records for the base, PC and VR update/depletion paths:
`ev_804262dd1bce8a85d39b9d63ce7c7b98d7c0b43aabee310d6972123e77e8b962`,
`ev_91a497fa1c268df0284f08da8aa7277db4ea3ad0c2fd399d821627415eb70445`,
`ev_693b1707cd6463b553058b6557c6265e6085c2e83400ce73349e9f01bddd0bc2`,
`ev_3828a5ebed41a320779afbe6acfd7aad172e75798dcc106a0203ebe9de99cb42`.
Native-mock regressions cover depletion/recovery, lobby sampling without a
networked avatar, unrelated callbacks, death, disabled sprint, toggle-off and
scoped restoration, including sprint-state preservation. In-game stamina
behavior still needs verification.

```powershell
New-Item -ItemType Directory -Force .\x64\StaminaTests | Out-Null
cl /nologo /EHsc /std:c++20 /W4 /WX /Fe:x64\StaminaTests\StaminaTests.exe /Fo:x64\StaminaTests\ scripts\StaminaTests.cpp Hooks\StaminaHooks.cpp
if ($LASTEXITCODE -eq 0) { .\x64\StaminaTests\StaminaTests.exe }
```

## Experimental Ghost Control and Expanded Pickup

Both features default off. Ghost control requires private/offline host
authority; expanded pickup uses the local controller in both the lobby and
missions without requiring a ghost, host authority or networked avatar.
Use only with consenting participants. They are implemented and mock-tested,
**not confirmed in-game**.

**Ghost control:** enable **Control ghost** under World Options,
or press **F7**. The key can be rebound or reset there; bindings last until DLL
unload. Movement/look use the game's existing input bindings and sensitivities.
The host body stays in place and remains vulnerable; local death/dying releases
control rather than modifying death flags.

Possession no longer requires or starts a hunt, so hunt prevention does not block
camera/movement takeover. Non-hunting state ticks/transitions are paused; existing
non-hunting activity is exited through the native idle transition. If a hunt is
already running, control takes it over instead. **Start controlled hunt** (or the
existing Force Hunt action) explicitly requests a forced native hunt while
possessed; rejection leaves possession active. A further hunt can be requested
after a hunt ends, but not while one is running.

Hunt grace remains; movement starts after grace. Control
suppresses this ghost agent's autonomous destinations/warps in both public and
`_Injected` wrappers, moves it along the
NavMesh at the configured 0.2-6 m/s, and leaves native hunting checks, targeting,
kill sequences and multikill rules running. Only the ordinary hunt countdown is
held, not grace, incense, kill conditions or hunt-prevention cooldowns. A native
kill/end can therefore end the hunt normally. Control then stays active **without
hunting**, suppressing non-hunting AI decisions and autonomous hunt entry;
normal disappearance still applies unless the separate visibility
option is enabled.

A separate local Camera copies the original camera's settings. Third-person
distance is adjustable, with a physics linecast to shorten obstructed views;
first-person is experimental and may clip ghost models. Camera-specific
postprocessing and the original body's audio-listener perspective are not
transferred. Input respects native movement/look gates and pauses while ImGui
captures it or the game loses focus;
HOME hides the overlay. Pickup/use callbacks are blocked for the possessed
host, and expanded pickup is dropped before possession starts.

Toggling off restores the original camera and native player input, ends a
remaining controlled hunt through native state exit, and returns the AI to idle
to resume normal decisions.
Session/identity changes, death and lost authority also release control.
Authority loss does **not** send an unauthorized hunt-ending RPC. Temporary
player movement/rotation gates are scoped to the original controller update;
the authoritative player life state is untouched. Failed camera/player-state
restoration blocks END unload for retry.

Possession's obfuscated state-machine hook is deliberately restricted to the
examined binary's PE timestamp `0x6AC7D141`, image size `0x06EAC000`, recovered
method addresses, and runtime field/type checks. A future update disables it
until re-investigated. These are compatibility guards, not a full-file digest
verification. Camera/navigation APIs remain metadata-bound.

The first possession build was reported to require an externally forced hunt
and to fight AI movement. Static tracing showed public NavMeshAgent wrappers
calling engine icalls directly rather than going through their `_Injected`
siblings, bypassing the original detours. The revised implementation hooks both
paths and reapplies controlled facing after the native hunt tick. These fixes
are mock-tested but still need in-game verification.

**Expanded pickup:** enable **Pick up anything (experimental)**, then use
the game's normal pickup binding to grab/drop the first collider target within
2.5 metres, including triggers. This is an
experimental transform/physics grabber, **not** a way to turn every fixture into
networked inventory equipment. Actors, equipment, fixed fixtures and static
scene roots are eligible. It handles multi-part hierarchies and compound
rigidbodies without the previous 128-transform/32-body/view quotas, preferring
the hit collider's attached rigidbody rather than unrelated ancestors.
Memory-validity, game-thread, rooting and restoration safeguards remain.

Multiplayer support uses existing active, position-enabled
`Photon.Pun.PhotonTransformView` or `Photon.Pun.PhotonRigidbodyView` observers
registered on the object's PhotonView and matching the moved transform/body.
Objects move locally immediately; eligible remote views receive genuine
`PhotonView.RequestOwnership` requests while held, without a second pickup
press. Ownership is not reverted on drop. Missing metadata, Fixed ownership,
denied/lost ownership and unknown/custom observers no longer reject local
movement, but synchronization is **unverified** in those cases. A detected
PhotonView does not prove replication, and even supported native observers
still need multiplayer verification.

Dropping, disabling, local-controller/session changes and unload restore temporary
rigidbody kinematic flags, including partial compound-grab failure.
Observed native transitions to dynamic physics after ownership acquisition are
preserved on release; unchanged kinematic fixtures remain kinematic.
**Positions are not rolled back.** Non-networked
fixture movement can be local-only, scripts or remote authority can move objects
back, and relocating actors or fixtures can break the map or session.

Static REA evidence for the current implementation:

- Public `SetDestination`, `set_destination`, and `Warp` wrapper bypasses;
  sanity owner-only setter and authenticated native broadcast:
  `ev_abf4a6ba0c05a907f6db58dac7a8baf569ecaf2f165a8904d10c2c1417407901`,
  `ev_7df0b0e372d876a9be6f0a6c3ecd526f527e9b761b20b429fb155ad927d93abd`.
- Hunt-entry flag and sanity sender validation (lowest actor ID):
  `ev_723956b10b06513778d41f7b52ed2b2fc4c5a4639f690c413210d25d193e9a29`.
- Hunt-state switch, constructor and state-machine interface dispatch:
  `ev_e8e68aa6dcc4e00899b9d100189fda5105310d009d5ce22226cc2b07c5cb7edf`.
- Hunt tick (`0x213FE90`), entry/exit interface mapping, and MouseLook scaling:
  `ev_14875a351ee677d7c4d26c3bc274689738ac59a5788a9036abc15c49b82eb626`.
- Hunt duration at state `+0x88`, separate grace at `+0x8C`, and end checks:
  `ev_1d39aa16addb526b11eca4b20214c6e0816c20a0fbfc4d920a2bfd52ba2852a5`.
- Native `BeginDeathSequence` calls and multikill configuration branches:
  `ev_15341b11dd546b6ebefb73bdc3fae7dd5c9ea1c3d3758d3f9dc1c043d568fb92`.
- `ResetHuntEffects` native network cleanup and hunt entry effects:
  `ev_faaea3126035db7e5d97a157958fa0f254e5b0a02f0282b0368a4fe76f44944e`.
- Navigation/camera injected ABI:
  `ev_5ce79c238c94ad8751324679058d50f6512b9a16ffcdd4b00c1e995270c6cd5c`.
- Pickup raycast/transform/rigidbody bindings:
  `ev_262837fa5c84178382c3365049812fa0ae43a0d97b2d427c9bd93ea50ba2c6c5`,
  `ev_864706b650ad65bec10ef1f6056634529974277c00a607d540df212dcf956739`.
- Native ownership request and registered observer serialization:
  `ev_12fa320c401aeffa125d1830333beaa8460e7aa3494da9aee2a0f4d1f0a92f5e`,
  `ev_002be86bf1db7b9198189551ef119a589c8c70b8c672c6c32957d04648ec9f6c`.
- Native rigidbody observer receive/send paths:
  `ev_3a645e9baa3ff7b37def94d7afa36e9e42cfb79ffee94393e15269feee546383`,
  `ev_5e3382ccf47aaee60ddccbf51dba75045d1d594297c173c85b72ee40775525db`.

The misleadingly named `StartHuntingTimer` is a hunt-prevention cooldown, not
the hunt-duration timer, and is left untouched. Static evidence and mocks do not
prove live camera rendering, remote replication or every ghost-specific kill
behavior.

Focused regressions:

```powershell
New-Item -ItemType Directory -Force .\x64\GhostControlTests, .\x64\PickupTests | Out-Null
cl /nologo /EHsc /std:c++20 /Fe:x64\GhostControlTests\GhostControlTests.exe /Fo:x64\GhostControlTests\ scripts\GhostControlTests.cpp Lib\detours.lib
if ($LASTEXITCODE -eq 0) { .\x64\GhostControlTests\GhostControlTests.exe }
cl /nologo /EHsc /std:c++20 /Fe:x64\PickupTests\PickupTests.exe /Fo:x64\PickupTests\ scripts\PickupTests.cpp
if ($LASTEXITCODE -eq 0) { .\x64\PickupTests\PickupTests.exe }
```

## Build

Open `TryAgainHook.sln` in Visual Studio 2022 and build `Debug|x64` (or `Release|x64`).

Expected DLL output:

- `x64/Debug/Zackmophobia.dll`

## Launch Helper

A launcher helper source is included at:

- `Launcher/LaunchWithDll.cpp`

It starts the game normally, waits until `GameAssembly.dll` is loaded, then injects `Zackmophobia.dll` using a remote `LoadLibraryW` call.

This delayed injection avoids early-startup crashes that can happen when injecting too early in Unity startup.

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

## Auto Injection Script

To streamline rebuild + launch + inject, use:

- `scripts/AutoInject.ps1`

What it does:

- Builds `TryAgainHook.sln` (`Debug|x64` by default)
- Creates a timestamped shadow copy of `Zackmophobia.dll` under `x64/Injected/`
- Starts the game through `LaunchWithDll.exe` with that copied DLL

Using a shadow copy avoids build-output file lock issues because the game loads the copied DLL, not `x64/Debug/Zackmophobia.dll` directly.

### Usage

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\AutoInject.ps1
```

Optional parameters:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\AutoInject.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\scripts\AutoInject.ps1 -SkipBuild
powershell -ExecutionPolicy Bypass -File .\scripts\AutoInject.ps1 -GameExe "C:\Program Files (x86)\Steam\steamapps\common\Phasmophobia\Phasmophobia.exe"
```

### Recommended workflow

1. Run `AutoInject.ps1`.
2. Play/test.
3. Press `END` to unload the currently injected DLL.
4. Re-run `AutoInject.ps1` for the next iteration.

## Safety Fixes Applied

- `hkGetBonus` and `hkIsPerfect` now call originals when toggle is OFF
- `GetRewardAmount` hook typedef now matches `int` return type
- no-kick and media reward hooks are now toggle-gated (`F6`) and call originals when disabled
