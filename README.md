
# ChungusWare

minecraft bedrock rtx panel. ray reconstruction preset f, internal resolution, frame generation and reflex. press **F8**. settings apply immediately.

<img width="2560" height="1440" alt="Screenshot (230)" src="https://github.com/user-attachments/assets/a4e3da6c-f3af-4c0d-93b8-c6ffd15e682e" />

<img width="2560" height="1440" alt="Screenshot (229)" src="https://github.com/user-attachments/assets/6d1ca47d-820e-441e-925f-9cc267c3b929" />

<img width="1496" height="761" alt="Screenshot 2026-10-04 140027" src="https://github.com/user-attachments/assets/bcef50d7-ccba-4fa7-95a9-d70a19cc814a" />

<img width="1919" height="983" alt="Screenshot 2026-10-04 140215" src="https://github.com/user-attachments/assets/f5db6daf-6c2e-4d91-aadd-5e10af3b4dab" />


## requirements

windows x64 and minecraft for windows with rtx. designed for NVIDIA RTX 50 series. other cards are untested. fg multipliers depend on hardware/runtime support.

tested with minecraft 1.26.5203.0, BetterRTX 1.4.4 and ReShade 6.8.0. future versions are untested. disable the RenoDX DLSS addon; it conflicts with managed fg.

## install

1. download **ChungusWare.zip** from [releases](https://github.com/succtro12/ChungusWare/releases). close minecraft and extract it.
2. copy the supplied files beside `Minecraft.Windows.exe`. xbox app: Manage → Files → Browse, or inside C:\XboxGames\Minecraft for Windows\Content
3. run **INSTALL CHUNGUS.bat**, review the changes and enter `y` to confirm.
4. launch minecraft. press F8, pray it works

setup downloads official NVIDIA Streamline 2.14.1 and ReShade 6.8.0, verifies hashes and backs up replaced providers. it stops on unknown loader conflicts or an unwritable folder. dont overwrite unrelated mods.

## controls

resolution presets: fraudulent dlaa (99.9%), quality, balanced, performance, ultra performance and caca mode (144p). dlaa is not 100% as it was causing some problems

fg: off or a supported multiplier. reflex is automatic with fg. diagnostics are under **witness protection**. enable **remember settings** to save selections between launches.

## remove

close minecraft. in `ReShade.ini`, disable the ChungusWare `[PROXY]` entry with `EnableProxyLibrary=0` and remove only `ProxyLibrary=bedrock_rr_loader.dll`. run **REMOVE CHUNGUS.bat**.

manual removal: delete the seven `bedrock_rr_*.dll` files and tools/mascot named in `ChungusWare/package-manifest.json`, plus both bats. only remove empty folders. keep prerequisite backups until any replaced provider is restored or no longer needed.

ReShade, NVIDIA prerequisites, BetterRTX, worlds and settings are preserved.

## problems / compatibility

this mod works with better rtx 1.4.4 and is recommended to pair with this mod
any dlss 5 mods currrently do not work, however reshade is available if u wanna add postfx

if F8 does nothing, run setup with minecraft closed. for rr/fg failures, check **witness protection**

if it doesn't work u have my condolences

## build

Visual Studio C++ x64 tools, Windows SDK and CMake 3.24+. run `Build.ps1` with its seven SDK/header directory parameters. `GameInputSdk` must contain `GameInput.h`.

dependencies: [DLSS](https://github.com/NVIDIA/DLSS), [MinHook](https://github.com/TsudaKageyu/minhook), [NVAPI](https://github.com/NVIDIA/nvapi), [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1), [ReShade 6.8.0](https://github.com/crosire/reshade/tree/v6.8.0), [ImGui 1.92.5 docking](https://github.com/ocornut/imgui/tree/v1.92.5-docking) and [GameInput v2 headers](https://www.nuget.org/packages/Microsoft.GameInput).

tested revisions: DLSS `374959484e79a640feaba44c93ac8cfb0a03f5b5`, MinHook `8af6b4acae5a9388fd742b56fa79ece89d96f823`, NVAPI `70d337db9186e968eab622f7e786de7e437faf3d`.

output: `dist/`. dependencies are not bundled in the repo. rebuilt binaries need updated ownership hashes for the install/removal scripts. the release zip is unchanged; this checkout only adjusts dependency-path configuration from the approved source snapshot.

###### license / legal

<sub>First-party source is furnished pursuant to the <a href="LICENSE">MIT License</a>, without warranty as therein specified. Third-party materials remain subject to their respective terms and notices; no grant herein shall be construed to confer rights therein. See <a href="THIRD_PARTY_NOTICES.md">third-party notices</a> and <a href="ASSET_NOTES.md">asset provenance</a>; retain applicable notices upon redistribution. Rights to the supplied mascot and embedded boom remain unverified; no representation of originality, authorization or clearance is made. The mascot is independently replaceable. Tahoma is loaded locally from Windows; the bundled fallback uses OFL-licensed W95FA. ChungusWare is an unofficial community project and is not affiliated with or endorsed by NVIDIA, Mojang/Microsoft, BetterRTX, Warner Bros., or their respective affiliates. This statement confers no rights.</sub>

release zip sha-256: `59ad9a71a20a7013f7f0d33bdd8ecf1a2b8bb815fcce24d2d41a9f6f59ca72e2`
