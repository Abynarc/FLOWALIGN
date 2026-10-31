param(
    [string]$BuildDir = "build-windows",
    [string]$JucePath = "../JUCE",
    [switch]$SkipBuild,
    [switch]$WithChecks
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
Push-Location $Root
try {
    $Version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(FLOWALIGN VERSION (\d+\.\d+\.\d+)').Groups[1].Value
    if (!$Version) { throw "Missing project version" }
    if (!$SkipBuild) {
        $Checks = if ($WithChecks) { "ON" } else { "OFF" }
        & cmake -S . -B $BuildDir -A x64 "-DJUCE_PATH=$JucePath" "-DFLOWALIGN_BUILD_VERIFICATION=$Checks"
        if ($LASTEXITCODE) { throw "CMake configuration failed" }
        & cmake --build $BuildDir --config Release --target FLOWALIGN_VST3 --parallel 4
        if ($LASTEXITCODE) { throw "VST3 build failed" }
        if ($WithChecks) {
            & cmake --build $BuildDir --config Release --target FLOWALIGN_verify --parallel 4
            if ($LASTEXITCODE) { throw "Regression driver build failed" }
        }
    }
    $Plugin = (Resolve-Path "$BuildDir/FLOWALIGN_artefacts/Release/VST3/FLOWALIGN.vst3").Path
    $Licenses = New-Item -ItemType Directory -Force (Join-Path $Plugin "Contents/Resources/Licenses")
    Copy-Item LICENSE (Join-Path $Licenses.FullName "FLOWMINNERS-MIT.txt") -Force
    Copy-Item third_party/vst3/LICENSE.txt (Join-Path $Licenses.FullName "Steinberg-VST3-MIT.txt") -Force
    Copy-Item THIRD_PARTY.md (Join-Path $Licenses.FullName "THIRD_PARTY.md") -Force
    $Binary = Join-Path $Plugin "Contents/x86_64-win/FLOWALIGN.vst3"
    if (!(Test-Path $Binary -PathType Leaf)) { throw "Missing x64 VST3 binary" }
    $Bytes = [IO.File]::ReadAllBytes($Binary)
    $PeOffset = [BitConverter]::ToInt32($Bytes, 0x3c)
    if ([BitConverter]::ToUInt32($Bytes, $PeOffset) -ne 0x00004550 -or
        [BitConverter]::ToUInt16($Bytes, $PeOffset + 4) -ne 0x8664) { throw "VST3 binary is not Windows x64 PE" }
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $VisualStudio = & $VsWhere -latest -products '*' -property installationPath
    $Dumpbin = Get-ChildItem "$VisualStudio/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe" | Sort-Object FullName -Descending | Select-Object -First 1
    if (!$Dumpbin) { throw "Missing dependency inspection tool" }
    $Imports = & $Dumpbin.FullName /dependents $Binary | Out-String
    if ($LASTEXITCODE) { throw "Dependency inspection failed" }
    Write-Output $Imports
    if ($Imports -match '(?i)(vcruntime|msvcp|concrt)\d+.*\.dll') { throw "Unexpected Visual C++ runtime dependency; build with static runtime" }
    $Dist = (New-Item -ItemType Directory -Force dist).FullName
    $Doc = Join-Path $Dist "FLOWALIGN-$Version-Windows-Install-RU.txt"
    Copy-Item docs/INSTALL-WINDOWS.md $Doc -Force
    $Manifest = Join-Path (Resolve-Path $BuildDir) "installer-manifest.iss"
    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.Add('procedure VerifyPayload;')
    $Lines.Add('begin')
    $Payload = Get-ChildItem $Plugin -Recurse -File
    foreach ($File in $Payload) {
        $Relative = $File.FullName.Substring($Plugin.Length + 1).Replace("'", "''")
        $Hash = (Get-FileHash $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $Lines.Add("  if CompareText(GetSHA256OfFile(ExpandConstant('{commoncf64}\VST3\FLOWALIGN.vst3\$Relative')), '$Hash') <> 0 then")
        $Lines.Add("    RaiseException('FLOWALIGN: installed file verification failed: $Relative');")
    }
    $Lines.Add('end;')
    [IO.File]::WriteAllLines($Manifest, $Lines, [Text.UTF8Encoding]::new($true))
    $Compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $CompilerPath = if ($Compiler) { $Compiler.Source } else { "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
    if (!(Test-Path $CompilerPath)) { throw "Install Inno Setup 6.3+ to build the installer" }
    & $CompilerPath "/DPluginVersion=$Version" "/DPluginDir=$Plugin" "/DDistDir=$Dist" "/DDocPath=$Doc" "/DManifestInclude=$Manifest" packaging/windows/FLOWALIGN.iss
    if ($LASTEXITCODE) { throw "Installer compilation failed" }
    $Setup = Join-Path $Dist "FLOWALIGN-$Version-Windows-x64-Setup.exe"
    $Checksums = @($Setup, $Doc) | ForEach-Object {
        "{0}  {1}" -f (Get-FileHash $_ -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $_ -Leaf)
    }
    [IO.File]::WriteAllLines((Join-Path $Dist "SHA256SUMS-Windows.txt"), $Checksums, [Text.UTF8Encoding]::new($false))
    Write-Output "Built: $Setup"
} finally { Pop-Location }
