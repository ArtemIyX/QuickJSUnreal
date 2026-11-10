param(
    [Parameter(Mandatory = $true)] [string] $SourceRoot,
    [string] $Generator = "Visual Studio 18 2026",
    [string] $Toolset = ""
)

$ErrorActionPreference = "Stop"
$pluginRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$thirdParty = Join-Path $pluginRoot "Source/ThirdParty/QuickJS"
$pinnedRevision = (Get-Content -LiteralPath (Join-Path $thirdParty "REVISION.txt") -Raw).Trim()
$actualRevisionOutput = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or -not $actualRevisionOutput) {
    throw "Could not read the QuickJS-NG checkout revision."
}
$actualRevision = $actualRevisionOutput.Trim()
if ($actualRevision -ne $pinnedRevision) {
    throw "QuickJS-NG checkout must be at $pinnedRevision; found $actualRevision."
}
$trackedChanges = & git -C $source status --porcelain --untracked-files=no --ignore-submodules=all
if ($LASTEXITCODE -ne 0 -or $trackedChanges) {
    throw "QuickJS-NG checkout has tracked changes. Build from a clean pinned checkout."
}
$sourceHeader = Join-Path $source "quickjs.h"
$bundledHeader = Join-Path $thirdParty "include/quickjs.h"
if ((Get-FileHash -LiteralPath $sourceHeader -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $bundledHeader -Algorithm SHA256).Hash) {
    throw "The bundled quickjs.h does not match the pinned source checkout."
}
if ((Get-FileHash -LiteralPath (Join-Path $source "LICENSE") -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $thirdParty "LICENSE") -Algorithm SHA256).Hash) {
    throw "The bundled license does not match the pinned source checkout."
}

$buildDirectory = Join-Path $source "build-ue-shared-win64"
$configureArgs = @(
    "-S", $source, "-B", $buildDirectory, "-G", $Generator, "-A", "x64",
    "-DBUILD_SHARED_LIBS=ON", "-DQJS_BUILD_LIBC=OFF", "-DQJS_BUILD_EXAMPLES=OFF",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
)
if ($Toolset) { $configureArgs += @("-T", $Toolset) }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "QuickJS-NG CMake configuration failed." }
& cmake --build $buildDirectory --config Release --target qjs
if ($LASTEXITCODE -ne 0) { throw "QuickJS-NG shared-library build failed." }

$dll = Join-Path $buildDirectory "Release/qjs.dll"
$importLibrary = Join-Path $buildDirectory "Release/qjs.lib"
if (-not (Test-Path -LiteralPath $dll -PathType Leaf) -or -not (Test-Path -LiteralPath $importLibrary -PathType Leaf)) {
    throw "The build did not produce both Release/qjs.dll and Release/qjs.lib."
}
$dllDirectory = Join-Path $pluginRoot "Binaries/ThirdParty/QuickJS/Win64"
$libraryDirectory = Join-Path $thirdParty "lib/Win64"
New-Item -ItemType Directory -Force -Path $dllDirectory, $libraryDirectory | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $dllDirectory "qjs.dll") -Force
Copy-Item -LiteralPath $importLibrary -Destination (Join-Path $libraryDirectory "qjs-import.lib") -Force
Write-Host "Installed shared QuickJS-NG DLL and import library from $pinnedRevision."
