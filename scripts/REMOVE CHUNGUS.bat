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
  $owned=ConvertFrom-Json '[{"path":"bedrock-rr-streamline/bedrock_delivery_watchdog.exe","size":119296,"sha256":"6775db34b9df75dbe126eb4b310099a321d58b8259cfd176bdfc6f87b80e046c"},{"path":"bedrock-rr-streamline/PresentMon.exe","size":980320,"sha256":"b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af"},{"path":"bedrock_rr_activation.dll","size":319488,"sha256":"74789490efc74c813fd8febf51f2a5a6a99e2ba833fdef812dd45037ae1366d1"},{"path":"bedrock_rr_bridge.dll","size":5979648,"sha256":"d3c27edf2e9e0fa9fc56f60634742d6b19f3bef21c0c49f52f31940da77f3eb0"},{"path":"bedrock_rr_capture.dll","size":461824,"sha256":"e7b831f241a36c9f08286ef92c469f52f6b5a3d7cbfd0c3591a506e48a045f0e"},{"path":"bedrock_rr_discovery.dll","size":347136,"sha256":"c3e0b2be04bf77b997a2435ed3b47c664930acf80f3ca4cd2d1fcec48b33ac88"},{"path":"bedrock_rr_loader.dll","size":144896,"sha256":"2e01dcbed22e1983f3d65239c09492be29cd186077632ee84fcb8bfd9c71247d"},{"path":"bedrock_rr_ngx_observer.dll","size":310784,"sha256":"58ab92a3c67f268cc18cecf49b86d30a9d3972dcbdcf511caa6ac246ca0777f8"},{"path":"bedrock_rr_quality.dll","size":328192,"sha256":"aefea2abf271459675df048c2e9b119ecf3c0abb6e7b9778907ca4c5900d8ad6"},{"path":"ChungusWare/assets/containment.png","size":68206,"sha256":"482d07bdfa636eec30c29cf76ec498c6590ac62849b7422c43293738387f0c32"},{"path":"INSTALL CHUNGUS.bat","size":14780,"sha256":"0f2b5cae3b97e7c2e539862a6a5215131b73f04a97222dca41f5692c298216dd"},{"path":"ChungusWare/package-manifest.json","sha256":"8ee46bfc1ef9db9a6b996682bff69d94e3aa7103a78d5abd80ff3d54e564a636"}]'
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
