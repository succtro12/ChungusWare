# ChungusWare

Serious Minecraft Bedrock RTX middleware. Questionable orange control panel.

Ray Reconstruction preset F, internal-resolution presets, NVIDIA-managed frame
generation and automatic Reflex. Press **F8**; settings apply immediately.
Fraudulent DLAA keeps almost every pixel. Caca Mode keeps considerably fewer.

## Install

Download **ChungusWare.zip** from [Releases](https://github.com/succtro12/ChungusWare/releases).

1. Close Minecraft and extract the ZIP.
2. Copy its contents beside `Minecraft.Windows.exe` (Xbox app: Manage → Files → Browse).
3. Run **INSTALL CHUNGUS.bat**, review the plan/terms and enter `y`.
4. Launch Minecraft. Press F8. Pray.

Setup downloads the pinned official NVIDIA Streamline 2.14.1 and ReShade 6.8.0
full-addon releases, verifies hashes, backs up replaced providers and configures
the loader. It stops on unknown `dxgi.dll`/proxy conflicts or an unwritable folder.
No administrator elevation, permission changes, service, registry setup or updater.
The graphics runtime and fictional messages do not need network access.

Windows x64, Minecraft for Windows RTX and supported NVIDIA hardware required.
FG multipliers depend on the GPU/runtime. Tested with Minecraft 1.26.5203.0,
RTX 5080, driver 616.56 and BetterRTX 1.4.4 Motion Blur. BetterRTX discovery adapts
at runtime; future updates can only be proven when they exist. Disable the RenoDX
DLSS addon; it conflicts with managed FG. Existing ReShade effects/settings stay.

## Remove / troubleshoot

Close Minecraft. In `ReShade.ini`, disable the ChungusWare `[PROXY]` entry
(`EnableProxyLibrary=0`) and remove only `ProxyLibrary=bedrock_rr_loader.dll`.
Run **REMOVE CHUNGUS.bat**. It checks ownership and preserves ReShade, NVIDIA
prerequisites, BetterRTX, worlds and settings. Manual removal: delete the seven
`bedrock_rr_*.dll` files listed by `ChungusWare/package-manifest.json`, its listed
tools/mascot and both BATs; remove directories only if empty. Keep prerequisite
backups until any replaced provider has been restored or is no longer needed.

F8 missing? Run the install BAT with the game closed. RR/FG unavailable? Check
prerequisites and **witness protection**. Report game/mod/GPU versions, chosen
modes and steps to reproduce. Inspect/redact local logs before sharing them.
No telemetry or automatic bug uploads. Haunted messages never use personal data.

## Build

Visual Studio C++ x64 tools, Windows SDK and CMake 3.24+. Run `Build.ps1` with
the seven SDK/header directory parameters it declares. Dependencies are external:
[DLSS](https://github.com/NVIDIA/DLSS), [MinHook](https://github.com/TsudaKageyu/minhook),
[NVAPI](https://github.com/NVIDIA/nvapi), [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1),
[ReShade 6.8.0 source](https://github.com/crosire/reshade/tree/v6.8.0),
[ImGui 1.92.5 docking headers](https://github.com/ocornut/imgui/tree/v1.92.5-docking),
and [Microsoft GameInput headers providing `GameInput::v2`](https://www.nuget.org/packages/Microsoft.GameInput).
`GameInputSdk` points directly to the folder containing `GameInput.h`.

Tested source revisions: DLSS `374959484e79a640feaba44c93ac8cfb0a03f5b5`,
MinHook `8af6b4acae5a9388fd742b56fa79ece89d96f823`,
NVAPI `70d337db9186e968eab622f7e786de7e437faf3d`.
The build script selects the production managed bridge, not the old experimental
bridge target. It generates `dist/`; no dependency binaries are committed.
Rebuilds need new ownership hashes before using the release install/removal scripts.
The release ZIP remains the exact approved bytes, not a rebuild of this checkout.
Only dependency-path configuration differs from the approved source snapshot.

## License and assets

First-party code is under [MIT](LICENSE). Third-party components and assets have
separate terms: [notices](THIRD_PARTY_NOTICES.md), [asset provenance](ASSET_NOTES.md).
Keep these notices with redistributed builds. The optional mascot PNG and embedded
user-supplied boom have **unverified rights**; neither is claimed original/licensed.
The mascot is replaceable without rebuilding the renderer. Tahoma is loaded from
Windows into memory; the bundled fallback atlas uses OFL-licensed W95FA.

ChungusWare is unofficial and is not affiliated with or endorsed by NVIDIA,
Mojang/Microsoft, BetterRTX, Warner Bros. or their respective affiliates.

Release ZIP SHA-256: `59ad9a71a20a7013f7f0d33bdd8ecf1a2b8bb815fcce24d2d41a9f6f59ca72e2`

maybe the real chungus was the chung we met along the way
