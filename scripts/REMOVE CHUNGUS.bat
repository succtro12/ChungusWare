@echo off
setlocal
set "CW_EXTERMINATOR=%~f0"
powershell.exe -NoProfile -Command "$s=[IO.File]::ReadAllText($env:CW_EXTERMINATOR); & ([scriptblock]::Create($s.Substring($s.LastIndexOf('# POWERSHELL PAYLOAD')+20)))"
exit /b %errorlevel%
# POWERSHELL PAYLOAD

$ErrorActionPreference='Stop'
function Get-CwFileHash([string]$path){
  $stream=[IO.File]::OpenRead($path)
  $hash=[Security.Cryptography.SHA256]::Create()
  try {[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','')}
  finally {$hash.Dispose();$stream.Dispose()}
}
try {
  $self=[IO.Path]::GetFullPath($env:CW_EXTERMINATOR)
  if([IO.Path]::GetFileName($self) -ne 'REMOVE CHUNGUS.bat'){throw 'Unexpected exterminator name'}
  $root=[IO.Path]::GetDirectoryName($self)
  if(!(Test-Path -LiteralPath (Join-Path $root 'Minecraft.Windows.exe') -PathType Leaf)){throw 'Place REMOVE CHUNGUS.bat in the actual Minecraft runtime folder'}
  if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Close Minecraft normally first'}
  if((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Ambiguous runtime path'}
  $ini=Join-Path $root 'ReShade.ini'
  if(Test-Path -LiteralPath $ini){
    $text=[IO.File]::ReadAllText($ini)
    if($text -match '(?im)^ProxyLibrary\s*=\s*bedrock_rr_loader\.dll\s*$'){throw 'First clear the ChungusWare proxy setting in ReShade.ini as documented. ReShade will be preserved.'}
  }
  $owned=ConvertFrom-Json '[{"path":"ASSET_NOTES.md","size":2825,"sha256":"e20623e66d21d98c0f9bddeceb12f1a7b5bd9aae541529f0e56bf92d1a47a722"},{"path":"bedrock-rr-streamline/bedrock_delivery_watchdog.exe","size":119296,"sha256":"6775db34b9df75dbe126eb4b310099a321d58b8259cfd176bdfc6f87b80e046c"},{"path":"bedrock-rr-streamline/PresentMon.exe","size":980320,"sha256":"b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af"},{"path":"bedrock_rr_activation.dll","size":319488,"sha256":"74789490efc74c813fd8febf51f2a5a6a99e2ba833fdef812dd45037ae1366d1"},{"path":"bedrock_rr_bridge.dll","size":5991936,"sha256":"22d893d4e0babba543e5b7489a16743f1b33b055e6b3c93e1f828beca0dbdd8f"},{"path":"bedrock_rr_capture.dll","size":461824,"sha256":"e31602e69715e40907d87afceafae7a44155afd102b47f5360bcdf3b955cd663"},{"path":"bedrock_rr_discovery.dll","size":347136,"sha256":"c3e0b2be04bf77b997a2435ed3b47c664930acf80f3ca4cd2d1fcec48b33ac88"},{"path":"bedrock_rr_loader.dll","size":144896,"sha256":"2e01dcbed22e1983f3d65239c09492be29cd186077632ee84fcb8bfd9c71247d"},{"path":"bedrock_rr_ngx_observer.dll","size":310784,"sha256":"58ab92a3c67f268cc18cecf49b86d30a9d3972dcbdcf511caa6ac246ca0777f8"},{"path":"bedrock_rr_quality.dll","size":328192,"sha256":"aefea2abf271459675df048c2e9b119ecf3c0abb6e7b9778907ca4c5900d8ad6"},{"path":"ChungusWare/assets/containment.png","size":68206,"sha256":"482d07bdfa636eec30c29cf76ec498c6590ac62849b7422c43293738387f0c32"},{"path":"INSTALL CHUNGUS.bat","size":14879,"sha256":"084b49129262a6fad59ef45e48ab75ba2eafc15332ad9b3fc1e414e5dc89977b"},{"path":"LICENSE","size":1087,"sha256":"0f4df60527a13f27b8c296bd2b6b99acc122eee5f8d042107b53d56538b498b3"},{"path":"notices/ImGui.txt","size":1083,"sha256":"c80c5789748d955c4a650562baa0d750494e2c7128c2ca66aaebe2e3c4e198bf"},{"path":"notices/MinHook.txt","size":4446,"sha256":"4f21f857550d7be854da6ea5f2da4e6775ca4e3fbb535e4f3d961c47d0bf3335"},{"path":"notices/NVAPI.txt","size":1270,"sha256":"52f7f5b6b1dc4a4dfb075ca150d5d9f5e6334cbcaf45a08873fd8b78bfa79874"},{"path":"notices/NVIDIA-RTX-SDK.txt","size":26727,"sha256":"3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294"},{"path":"notices/PresentMon.txt","size":1086,"sha256":"b1043ee2efacbd24a1ad830128a61f40dc3d27e2b9b24d7d3631950b88f2d66e"},{"path":"notices/ReShade.txt","size":1481,"sha256":"237ded5b8344f820113efab1e65e91e1f159d9202c5b4856606a0590d3ffdab0"},{"path":"notices/Streamline.txt","size":1591,"sha256":"7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d"},{"path":"notices/W95FA-OFL.txt","size":4696,"sha256":"ca122f3bda0154f692817450168cb650a7fc59ef96c93582acd2e7a744d464d3"},{"path":"THIRD_PARTY_NOTICES.md","size":1946,"sha256":"b8d30c9bc3fb7d727dd25fdc1dc2a4e0fac53405e15d6755c228d4606bb1f1cb"},{"path":"ChungusWare/package-manifest.json","size":3633,"sha256":"74498cffb5476682b9f2fe541cd7d7660ebea7e1e24e0a5778be90c3e9ef979a"}]'
  $targets=@()
  foreach($file in $owned){
    $path=[IO.Path]::GetFullPath((Join-Path $root $file.path))
    if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe file path'}
    $ancestor=[IO.Path]::GetDirectoryName($path)
    while($ancestor.Length -ge $root.Length){
      if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Reparse-point ownership ambiguity'}
      if($ancestor -eq $root){break};$ancestor=[IO.Path]::GetDirectoryName($ancestor)
    }
    if(Test-Path -LiteralPath $path){
      $item=Get-Item -LiteralPath $path
      if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Unsafe file: $($file.path)"}
      if((Get-CwFileHash $path) -ne $file.sha256){throw "Ownership/hash mismatch: $($file.path). Nothing removed."}
      $targets+=$path
    }
  }
  Write-Host 'CHUNGUSWARE REMOVAL'
  Write-Host '-----------------------------------'
  Write-Host 'the containment subject will be removed.'
  Write-Host 'Removing:'
  foreach($path in $targets){Write-Host ('  '+$path.Substring($root.Length+1))}
  Write-Host '  REMOVE CHUNGUS.bat (self only)'
  Write-Host 'Preserving: Minecraft, BetterRTX, ReShade, worlds, user settings, external NVIDIA prerequisites'
  Write-Host 'Press Enter to proceed. Ctrl+C to abort.'
  $null=Read-Host
  # Recheck the entire set after confirmation, before the first deletion.
  foreach($file in $owned){$path=Join-Path $root $file.path;if(Test-Path -LiteralPath $path){if((Get-CwFileHash $path) -ne $file.sha256){throw 'File changed during confirmation; aborting'}}}
  foreach($path in $targets){Remove-Item -LiteralPath $path}
  foreach($directory in @('ChungusWare/assets','ChungusWare','bedrock-rr-streamline')){
    $path=Join-Path $root $directory
    if((Test-Path -LiteralPath $path -PathType Container) -and !(Get-ChildItem -LiteralPath $path -Force)){Remove-Item -LiteralPath $path}
  }
  Write-Host 'containment chamber empty.'
  Write-Host 'no further action is required.'
  # Hidden delayed PowerShell child removes ONLY this exact executing batch.
  $child='Start-Sleep -Milliseconds 1000; Remove-Item -LiteralPath $env:CW_EXTERMINATOR'
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($child))
  Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-EncodedCommand',$encoded) -WindowStyle Hidden
  exit 0
} catch {Write-Host ('ABORT: '+$_.Exception.Message);exit 1}
