param(
    [string]$BuildDir = "build-windows",
    [string]$JucePath = "../JUCE",
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
Push-Location $Root
try {
    $Version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(FLOWALIGN VERSION (\d+\.\d+\.\d+)').Groups[1].Value
    if (!$Version) { throw "Missing project version" }
    if (!$SkipBuild) {
        & cmake -S . -B $BuildDir -A x64 "-DJUCE_PATH=$JucePath"
        if ($LASTEXITCODE) { throw "CMake configuration failed" }
        & cmake --build $BuildDir --config Release --target FLOWALIGN_VST3 --parallel 4
        if ($LASTEXITCODE) { throw "VST3 build failed" }
    }
    $Plugin = (Resolve-Path "$BuildDir/FLOWALIGN_artefacts/Release/VST3/FLOWALIGN.vst3").Path
    $Binary = Join-Path $Plugin "Contents/x86_64-win/FLOWALIGN.vst3"
    if (!(Test-Path $Binary -PathType Leaf)) { throw "Missing x64 VST3 binary" }
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
