param(
    [Parameter(Mandatory=$true)][string]$DlssSdk,
    [Parameter(Mandatory=$true)][string]$MinHookSource,
    [Parameter(Mandatory=$true)][string]$ReShadeSdk,
    [Parameter(Mandatory=$true)][string]$ImGuiHeaders,
    [Parameter(Mandatory=$true)][string]$NvApiSdk,
    [Parameter(Mandatory=$true)][string]$StreamlineSdk,
    [Parameter(Mandatory=$true)][string]$GameInputSdk,
    [string]$BuildDirectory=(Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference='Stop'
$vsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!(Test-Path -LiteralPath $vsWhere)){throw 'Install Visual Studio C++ x64 build tools first'}
$vsRoot=(& $vsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if(!$vsRoot){throw 'Visual Studio C++ x64 build tools not found'}
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$options=@(('-DDLSS_SDK:PATH='+$DlssSdk),('-DMINHOOK_SOURCE:PATH='+$MinHookSource),
    ('-DRESHADE_SDK:PATH='+$ReShadeSdk),('-DIMGUI_HEADERS:PATH='+$ImGuiHeaders),
    ('-DNVAPI_SDK:PATH='+$NvApiSdk),('-DSTREAMLINE_SDK:PATH='+$StreamlineSdk),
    ('-DGAMEINPUT_SDK:PATH='+$GameInputSdk),'-DCHUNGUSWARE_TEST_CONTROL=OFF')
& $cmake -S $PSScriptRoot -B $BuildDirectory -A x64 @options
if($LASTEXITCODE){throw 'CMake configuration failed'}
$targets=@('bedrock_rr_bridge_managed','bedrock_rr_capture','bedrock_rr_ngx_observer',
    'bedrock_rr_activation','bedrock_rr_quality','bedrock_rr_discovery','bedrock_rr_dxgi','bedrock_delivery_watchdog')
& $cmake --build $BuildDirectory --config Release --target @targets --parallel
if($LASTEXITCODE){throw 'Compilation failed'}
$dist=Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($name in @('bedrock_rr_capture','bedrock_rr_ngx_observer','bedrock_rr_activation','bedrock_rr_quality','bedrock_rr_discovery')){
    Copy-Item -LiteralPath (Join-Path $BuildDirectory ('Release/'+$name+'.dll')) -Destination $dist
}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release/bedrock_rr_bridge_managed.dll') -Destination (Join-Path $dist 'bedrock_rr_bridge.dll')
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release/dxgi.dll') -Destination (Join-Path $dist 'bedrock_rr_loader.dll')
New-Item -ItemType Directory -Path (Join-Path $dist 'bedrock-rr-streamline') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release/bedrock_delivery_watchdog.exe') -Destination (Join-Path $dist 'bedrock-rr-streamline')
New-Item -ItemType Directory -Path (Join-Path $dist 'ChungusWare/assets') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'assets/containment.png') -Destination (Join-Path $dist 'ChungusWare/assets')
Write-Output 'Built project files in dist/. External providers/ReShade/PresentMon are separate prerequisites.'
Write-Output 'Release scripts pin release ownership hashes; regenerate those hashes before packaging a local rebuild.'
