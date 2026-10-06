# ChungusWare

cool chungus panel allows nicer visuals.

for minecraft bedrock rtx. ray reconstruction preset f, frame generation and reflex in one orange panel. press **F8**. settings change immediately.

internal res goes from fraudulent dlaa to quality, balanced, performance, ultra performance and caca mode (144p). fraudulent dlaa keeps 99.9% of the resolution. caca keeps 144 pixels of height and plays the boom. pick what looks good to u.

## install

get **ChungusWare.zip** from [releases](https://github.com/succtro12/ChungusWare/releases).

1. close minecraft and extract the zip.
2. copy the supplied files beside `Minecraft.Windows.exe`. xbox app: Manage → Files → Browse.
3. run **INSTALL CHUNGUS.bat**, read what it will change and enter `y`.
4. launch minecraft. press F8. thats it.

the bat gets official NVIDIA Streamline 2.14.1 and ReShade 6.8.0 full-addon, checks their hashes, backs up replaced providers and sets up the loader. if it finds an unknown `dxgi.dll`, another proxy or a folder it cant write to, it stops. dont overwrite random mods to get past that.

no services, registry setup, updater or permission changes. setup needs internet for the downloads. the graphics runtime and messages dont.

u need windows x64, minecraft for windows with rtx and supported NVIDIA hardware. available fg multipliers depend on the gpu/runtime. tested on minecraft 1.26.5203.0, RTX 5080, driver 616.56 and BetterRTX 1.4.4 Motion Blur.

BetterRTX works in the tested setup. discovery adapts at runtime; future updates still need testing. disable the RenoDX DLSS addon because it conflicts with managed fg. existing ReShade effects and settings stay.

## remove it

close minecraft. in `ReShade.ini`, set the ChungusWare `[PROXY]` entry to `EnableProxyLibrary=0` and remove only `ProxyLibrary=bedrock_rr_loader.dll`. run **REMOVE CHUNGUS.bat**.

it checks which files belong to chungusware. ReShade, NVIDIA prerequisites, BetterRTX, worlds and settings stay.

manual removal also works: delete the seven `bedrock_rr_*.dll` files named in `ChungusWare/package-manifest.json`, the tools/mascot listed there and both bats. only remove folders if empty. keep prerequisite backups until any replaced provider is restored or u no longer need it.

## if its broken

F8 does nothing? run the install bat with minecraft closed. rr or fg missing? check prerequisites and **witness protection**.

for a bug report, include game/mod/gpu versions, the modes u picked and how to make it happen again. check and redact logs before posting them. no telemetry or automatic bug uploads. the messages are static fiction; they dont read personal data.

## build it

Visual Studio C++ x64 tools, Windows SDK and CMake 3.24+. run `Build.ps1` with its seven SDK/header directory parameters. dependencies are external:

- [DLSS](https://github.com/NVIDIA/DLSS)
- [MinHook](https://github.com/TsudaKageyu/minhook)
- [NVAPI](https://github.com/NVIDIA/nvapi)
- [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1)
- [ReShade 6.8.0 source](https://github.com/crosire/reshade/tree/v6.8.0)
- [ImGui 1.92.5 docking headers](https://github.com/ocornut/imgui/tree/v1.92.5-docking)
- [Microsoft GameInput headers with `GameInput::v2`](https://www.nuget.org/packages/Microsoft.GameInput)

`GameInputSdk` points directly to the folder containing `GameInput.h`.

tested revisions: DLSS `374959484e79a640feaba44c93ac8cfb0a03f5b5`, MinHook `8af6b4acae5a9388fd742b56fa79ece89d96f823`, NVAPI `70d337db9186e968eab622f7e786de7e437faf3d`.

the script builds the production managed bridge into `dist/`. dependency binaries arent in the repo. if u rebuild, update ownership hashes before using the release install/removal scripts.

the release zip is the approved build, unchanged. this source checkout only adjusts dependency-path configuration from that snapshot.

###### license / legal

<sub>First-party source is furnished pursuant to the <a href="LICENSE">MIT License</a>, without warranty as therein specified. Third-party materials remain subject to their respective terms and notices; no grant herein shall be construed to confer rights therein. See <a href="THIRD_PARTY_NOTICES.md">third-party notices</a> and <a href="ASSET_NOTES.md">asset provenance</a>; retain applicable notices upon redistribution. Rights to the supplied mascot and embedded boom remain unverified; no representation of originality, authorization or clearance is made. The mascot is independently replaceable. Tahoma is loaded locally from Windows; the bundled fallback uses OFL-licensed W95FA. ChungusWare is an unofficial community project and is not affiliated with or endorsed by NVIDIA, Mojang/Microsoft, BetterRTX, Warner Bros., or their respective affiliates. This statement confers no rights.</sub>

release zip sha-256: `59ad9a71a20a7013f7f0d33bdd8ecf1a2b8bb815fcce24d2d41a9f6f59ca72e2`
