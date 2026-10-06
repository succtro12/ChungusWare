@echo off
setlocal
set "CW_SETUP_SELF=%~f0"
powershell.exe -NoProfile -Command "$s=[IO.File]::ReadAllText($env:CW_SETUP_SELF); & ([scriptblock]::Create($s.Substring($s.LastIndexOf('# POWERSHELL PAYLOAD')+20)))"
set "CW_SETUP_RESULT=%errorlevel%"
if not "%CW_SETUP_RESULT%"=="0" (echo Setup stopped. See the reason above. & timeout /t 15 /nobreak >nul 2>nul)
exit /b %CW_SETUP_RESULT%
# POWERSHELL PAYLOAD
$ErrorActionPreference='Stop'
function FileHash([string]$path) {
  $stream=[IO.File]::OpenRead($path);$hasher=[Security.Cryptography.SHA256]::Create()
  try {[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
  finally {$stream.Dispose();$hasher.Dispose()}
}
function SafePath([string]$path) {
  $full=[IO.Path]::GetFullPath($path)
  if(!$full.Equals($root,[StringComparison]::OrdinalIgnoreCase) -and !$full.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes the runtime folder'}
  $node=$full
  while($node){if(Test-Path -LiteralPath $node){if((Get-Item -LiteralPath $node -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Linked path is ambiguous: '+$node)}};$parent=[IO.Path]::GetDirectoryName($node);if($parent -eq $node){break};$node=$parent}
  return $full
}
$tempFiles=New-Object 'System.Collections.Generic.List[string]'
$changed=New-Object 'System.Collections.Generic.List[object]'
$completed=$false
try {
  $self=[IO.Path]::GetFullPath($env:CW_SETUP_SELF);$root=[IO.Path]::GetDirectoryName($self)
  if([IO.Path]::GetFileName($self) -ne 'INSTALL CHUNGUS.bat'){throw 'Keep the script name INSTALL CHUNGUS.bat'}
  $null=SafePath $self
  if(!(Test-Path -LiteralPath (Join-Path $root 'Minecraft.Windows.exe') -PathType Leaf)){throw 'Copy the supplied files beside Minecraft.Windows.exe first'}
  if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Close Minecraft normally before setup'}
  $core=ConvertFrom-Json '[{"path":"bedrock_rr_activation.dll","sha256":"74789490efc74c813fd8febf51f2a5a6a99e2ba833fdef812dd45037ae1366d1"},{"path":"bedrock_rr_bridge.dll","sha256":"d3c27edf2e9e0fa9fc56f60634742d6b19f3bef21c0c49f52f31940da77f3eb0"},{"path":"bedrock_rr_capture.dll","sha256":"e7b831f241a36c9f08286ef92c469f52f6b5a3d7cbfd0c3591a506e48a045f0e"},{"path":"bedrock_rr_discovery.dll","sha256":"c3e0b2be04bf77b997a2435ed3b47c664930acf80f3ca4cd2d1fcec48b33ac88"},{"path":"bedrock_rr_loader.dll","sha256":"2e01dcbed22e1983f3d65239c09492be29cd186077632ee84fcb8bfd9c71247d"},{"path":"bedrock_rr_ngx_observer.dll","sha256":"58ab92a3c67f268cc18cecf49b86d30a9d3972dcbdcf511caa6ac246ca0777f8"},{"path":"bedrock_rr_quality.dll","sha256":"aefea2abf271459675df048c2e9b119ecf3c0abb6e7b9778907ca4c5900d8ad6"}]'
  foreach($file in $core){$path=SafePath (Join-Path $root $file.path);if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (FileHash $path) -ne $file.sha256){throw ('Missing or changed ChungusWare file: '+$file.path)}}
  $probe=SafePath (Join-Path $root ('cw-write-check-'+[Guid]::NewGuid().ToString('N')+'.tmp'))
  try {$handle=[IO.File]::Open($probe,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write);$handle.Dispose()}
  catch {throw 'Minecraft runtime folder is not writable. Use Xbox app Manage > Files > Browse to locate a writable installation. Setup will not change permissions or take ownership.'}
  finally {if(Test-Path -LiteralPath $probe){Remove-Item -LiteralPath $probe}}
  $dxgi=SafePath (Join-Path $root 'dxgi.dll')
  $needReShade=!(Test-Path -LiteralPath $dxgi -PathType Leaf)
  if(!$needReShade -and (FileHash $dxgi) -ne '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'){throw 'Existing dxgi.dll is not the tested official ReShade 6.8.0 full-addon binary. Identify/back up it manually; setup will not overwrite another mod.'}
  $ini=SafePath (Join-Path $root 'ReShade.ini')
  $iniExists=Test-Path -LiteralPath $ini -PathType Leaf
  $iniBytes=$null;$text=''
  if($iniExists){$iniBytes=[IO.File]::ReadAllBytes($ini);$text=[IO.File]::ReadAllText($ini)}
  $sections=[regex]::Matches($text,'(?ms)^\[PROXY\]\r?\n.*?(?=^\[|\z)')
  if($sections.Count -gt 1){throw 'Duplicate ReShade PROXY sections; resolve manually'}
  if($sections.Count -eq 1){
    $section=$sections[0].Value
    foreach($key in @('EnableProxyLibrary','ProxyLibrary')){if([regex]::Matches($section,'(?m)^'+$key+'=').Count -gt 1){throw 'Duplicate ReShade proxy keys; resolve manually'}}
    $proxy=[regex]::Match($section,'(?m)^ProxyLibrary=([^\r\n]*)')
    if($proxy.Success -and $proxy.Groups[1].Value.Trim() -notin @('','bedrock_rr_loader.dll')){throw 'Another ReShade proxy is configured; nothing will be overwritten'}
    $newSection=[regex]::Replace($section,'(?m)^(EnableProxyLibrary|ProxyLibrary)=[^\r\n]*\r?\n?','').TrimEnd()+"`r`nEnableProxyLibrary=1`r`nProxyLibrary=bedrock_rr_loader.dll`r`n`r`n"
    if($section -match '(?m)^EnableProxyLibrary=1\r?$' -and $section -match '(?m)^ProxyLibrary=bedrock_rr_loader.dll\r?$'){$newText=$text}else{$newText=$text.Remove($sections[0].Index,$sections[0].Length).Insert($sections[0].Index,$newSection)}
  } else {$newText=$text.TrimEnd()+"`r`n`r`n[PROXY]`r`nEnableProxyLibrary=1`r`nProxyLibrary=bedrock_rr_loader.dll`r`n"}
  $iniBefore=$null;if($iniExists){$iniBefore=FileHash $ini}
  $dependencies=ConvertFrom-Json '[{"path":"bedrock-rr-streamline/sl.interposer.dll","sha256":"8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c"},{"path":"bedrock-rr-streamline/sl.common.dll","sha256":"82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d"},{"path":"bedrock-rr-streamline/sl.dlss_g.dll","sha256":"f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c"},{"path":"bedrock-rr-streamline/sl.reflex.dll","sha256":"0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0"},{"path":"bedrock-rr-streamline/sl.pcl.dll","sha256":"f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582"},{"path":"bedrock-rr-streamline/nvngx_dlssg.dll","sha256":"ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82"},{"path":"nvngx_dlss.dll","sha256":"3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983"},{"path":"nvngx_dlssd.dll","sha256":"4bc7ea5fcb2f32cf86bc2cb072e8d2860914a0be511cbdcb2fc206c1d9d83b80"}]'
  $plan=@()
  if($needReShade){$plan+=@{file=@{path='dxgi.dll';sha256='0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'};destination=$dxgi;existed=$false;before=$null}}
  foreach($file in $dependencies){
    $destination=SafePath (Join-Path $root $file.path);$exists=Test-Path -LiteralPath $destination -PathType Leaf
    $before=$null;if($exists){$before=FileHash $destination}
    if($before -eq $file.sha256){Write-Host ('verified: '+$file.path);continue}
    if($exists){
      Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
      $signature=Get-AuthenticodeSignature -LiteralPath $destination
      if($signature.Status -ne 'Valid' -or !$signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'NVIDIA'){throw ('Conflicting dependency is not verified NVIDIA software; refusing replacement: '+$file.path)}
    }
    $plan+=@{file=$file;destination=$destination;existed=$exists;before=$before}
  }
  if($plan.Count -eq 0 -and $newText -eq $text){Write-Host 'the rabbit has already been fed. no changes required.';exit 0}
  Write-Host 'CHUNGUSWARE PREREQUISITE SETUP'
  Write-Host 'Official NVIDIA Streamline 2.14.1 (~276 MB) and ReShade 6.8.0 full-addon (~4.3 MB).'
  Write-Host 'Source: https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1'
  Write-Host 'Review NVIDIA terms in the release and bundled notices/NVIDIA-RTX-SDK.txt and notices/Streamline.txt.'
  Write-Host 'ReShade source: https://reshade.me/ (BSD-3-Clause; bundled notices/ReShade.txt).'
  Write-Host 'Continuing means you have reviewed and agree to applicable upstream terms. No license is granted by ChungusWare.'
  foreach($item in $plan){Write-Host ('  '+$(if($item.existed){'back up and replace: '}else{'add: '})+$item.file.path)}
  if($newText -ne $text){Write-Host '  back up ReShade.ini; configure only its ChungusWare proxy entry'}
  Write-Host 'No elevation, registry edits, services, telemetry or updates. ReShade and BetterRTX stay installed.'
  if((Read-Host 'Install ChungusWare? [y/n]').Trim().ToLowerInvariant() -ne 'y'){Write-Host 'Cancelled. Nothing changed.';exit 0}
  $sources=@{}
  if($needReShade){
      $setup=Join-Path ([IO.Path]::GetTempPath()) ('cw-reshade-'+[Guid]::NewGuid().ToString('N')+'.exe');$tempFiles.Add($setup)
      [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
      $client=New-Object Net.WebClient
      try {$client.Headers['User-Agent']='ChungusWare-prerequisite-setup';$client.DownloadFile('https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe',$setup)} finally {$client.Dispose()}
    if((FileHash $setup) -ne 'afe4c8f13048306307983b8b3d41d5bf00a86820440b0e57dea10950e1176445'){throw 'Official ReShade setup hash mismatch; nothing installed'}
    # The verified official setup contains a ZIP payload at byte 153088.
    # Extract only its x64 DLL; do not execute its installer or install effects.
    $bytes=[IO.File]::ReadAllBytes($setup);$payload=New-Object IO.MemoryStream
    $payload.Write($bytes,153088,$bytes.Length-153088);$payload.Position=0
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $rsArchive=New-Object IO.Compression.ZipArchive($payload,[IO.Compression.ZipArchiveMode]::Read)
    try {
      $entry=$rsArchive.GetEntry('ReShade64.dll');if(!$entry -or $entry.Length -ne 5592064){throw 'ReShade DLL payload invalid'}
      $scratch=Join-Path ([IO.Path]::GetTempPath()) ('cw-reshade-'+[Guid]::NewGuid().ToString('N')+'.dll');$tempFiles.Add($scratch)
      $input=$entry.Open();$output=[IO.File]::Create($scratch);try {$input.CopyTo($output)} finally {$input.Dispose();$output.Dispose()}
      if((FileHash $scratch) -ne '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'){throw 'ReShade DLL hash mismatch'}
      $sources['dxgi.dll']=$scratch
    } finally {$rsArchive.Dispose();$payload.Dispose()}
  }
  $nvidiaPlan=@($plan | Where-Object {$_.file.path -ne 'dxgi.dll'})
  if($nvidiaPlan.Count){
      $zip=Join-Path ([IO.Path]::GetTempPath()) ('cw-sdk-'+[Guid]::NewGuid().ToString('N')+'.zip');$tempFiles.Add($zip)
      [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
      Write-Host 'Downloading directly from NVIDIA-RTX/Streamline on GitHub...'
      $client=New-Object Net.WebClient
      try {$client.Headers['User-Agent']='ChungusWare-prerequisite-setup';$client.DownloadFile('https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip',$zip)} finally {$client.Dispose()}
    if((FileHash $zip) -ne '92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b'){throw 'SDK ZIP hash mismatch; nothing installed'}
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {foreach($item in $nvidiaPlan){
      $name=[IO.Path]::GetFileName($item.file.path);$entry=$archive.GetEntry('bin/x64/'+$name)
      if(!$entry -or $entry.Length -gt 100MB){throw ('Required release file missing or oversized: '+$name)}
      $scratch=Join-Path ([IO.Path]::GetTempPath()) ('cw-file-'+[Guid]::NewGuid().ToString('N')+'.dll');$tempFiles.Add($scratch)
      $input=$entry.Open();$output=[IO.File]::Create($scratch);try {$input.CopyTo($output)} finally {$input.Dispose();$output.Dispose()}
      if((FileHash $scratch) -ne $item.file.sha256){throw ('Release file hash mismatch: '+$name)}
      $sources[$item.file.path]=$scratch
    }} finally {$archive.Dispose()}
  }
  # All conflicts, downloads and hashes are checked before the first runtime change.
  if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Minecraft started during setup; nothing installed'}
  if($iniExists){if((FileHash $ini) -ne $iniBefore){throw 'ReShade.ini changed during setup; nothing installed'}}elseif(Test-Path -LiteralPath $ini){throw 'ReShade.ini appeared during setup; nothing installed'}
  foreach($item in $plan){$null=SafePath $item.destination;if($item.existed){if((FileHash $item.destination) -ne $item.before){throw 'A dependency changed during setup'}}elseif(Test-Path -LiteralPath $item.destination){throw 'A dependency appeared during setup'}}
  $backup=SafePath (Join-Path $root ('ChungusWare/prerequisite-backups/'+[Guid]::NewGuid().ToString('N')))
  New-Item -ItemType Directory -Path $backup | Out-Null
  foreach($item in $plan){
    $saved=$null;if($item.existed){$saved=Join-Path $backup ([IO.Path]::GetFileName($item.file.path));[IO.File]::Copy($item.destination,$saved,$false)}
    $parent=SafePath ([IO.Path]::GetDirectoryName($item.destination));New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $changed.Add(@{path=$item.file.path;destination=$item.destination;backup=$saved;existed=$item.existed;sha256=$item.file.sha256})
    [IO.File]::Copy($sources[$item.file.path],$item.destination,$true)
    if((FileHash $item.destination) -ne $item.file.sha256){throw 'Installed hash mismatch'}
  }
  if($newText -ne $text){
    $saved=$null;if($iniExists){$saved=Join-Path $backup 'ReShade.ini';[IO.File]::WriteAllBytes($saved,$iniBytes)}
    $changed.Add(@{path='ReShade.ini';destination=$ini;backup=$saved;existed=$iniExists})
    [IO.File]::WriteAllText($ini,$newText,(New-Object Text.UTF8Encoding($false)))
  }
  $receipt=@{sdk='2.14.1';source='https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1';files=$changed.ToArray();restore='Close Minecraft. Restore backed-up files manually; remove newly added prerequisites only after confirming no other mod uses them.'}
  [IO.File]::WriteAllText((Join-Path $backup 'receipt.json'),($receipt | ConvertTo-Json -Depth 6))
  $completed=$true
  Write-Host ('Backups/receipt: '+$backup)
  Write-Host 'the rabbit has been fed. launch Minecraft and press F8.'
} catch {
  if(!$completed){for($i=$changed.Count-1;$i -ge 0;$i--){$item=$changed[$i];try {if($item.existed){[IO.File]::Copy($item.backup,$item.destination,$true)}elseif(Test-Path -LiteralPath $item.destination){Remove-Item -LiteralPath $item.destination}}catch{Write-Host ('Rollback needs attention: '+$item.path)}}}
  Write-Host ('ABORT: '+$_.Exception.Message)
  exit 1
} finally {
  foreach($file in $tempFiles){if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file}}
}
