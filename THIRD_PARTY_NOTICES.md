# Third-party components

Included notices are actual upstream text, not substitute license terms.
- Windows Tahoma: system-local display prerequisite only. No Microsoft font
  or Tahoma glyph atlas is redistributed.
- W95FA Regular fallback derived glyph atlas: SIL OFL 1.1, notices/W95FA-OFL.txt.
- MinHook statically linked in project DLLs: BSD terms and Hacker Disassembler
  terms, notices/MinHook.txt.
- Dear ImGui headers used by hidden developer tooling: MIT, notices/ImGui.txt.
- NVAPI statically linked import/library code: MIT, notices/NVAPI.txt.
- ReShade addon API headers used by the bridge: upstream BSD-3-Clause OR MIT;
  notices/ReShade.txt includes the upstream BSD notice. ReShade runtime is an
  external prerequisite and is NOT bundled.
- Streamline headers/framework: upstream license in notices/Streamline.txt.
  Proprietary NVIDIA DLSS/NGX/Streamline runtime DLLs are NOT bundled; obtain
  the official SDK release and apply its licenses, including RTX SDK/Reflex
  terms, independently. notices/NVIDIA-RTX-SDK.txt is informational SDK text.
- DLSS SDK build dependencies: upstream terms in notices/NVIDIA-RTX-SDK.txt.
  The SDK headers/libraries are externally supplied rather than committed.
- Intel PresentMon console executable: upstream MIT terms in notices/PresentMon.txt.
  No PresentMon UI/service/installer is included. The small project watchdog
  only closes this process's transient ETW capture; no persistent service.
- Windows WIC/COM/D3D12/WinMM/GameInput are legitimate game/system prerequisites;
  no Microsoft OS binaries or redistributable installers are bundled.
- C++ runtime is linked in Release/static mode; no separate debug runtime ships.
- BetterRTX, Minecraft executable, materials and resource packs are NOT bundled.

Read ASSET_NOTES.md for the explicit unresolved mascot AND embedded boom rights.
Their inclusion is an author decision, not a legal certification.
