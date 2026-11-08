# Rebuilds the application and assembles the stand-alone folder dist\XournalLab (double-click xl_app.exe; no MSYS2 needed on the target PC).
#   powershell -ExecutionPolicy Bypass -File package.ps1 [-Build build] [-NoBuild] [-Zip]
# Steps: configure + build (Release) -> copy xl_app.exe -> windeployqt6 (Qt DLLs, platform, imageformats, styles) -> copy every other DLL the
# deployed files import from C:\msys64\ucrt64\bin (MinGW runtime, zlib, harfbuzz, freetype, libpng, icu, ...) -> drop plugins the app does not use ->
# check that every import resolves inside the folder or to Windows -> run it once with a PATH that excludes MSYS2 (screenshot of a sample).
param([string]$Build = "build", [switch]$NoBuild, [switch]$Zip)
$ErrorActionPreference = "Continue"   # native tools write warnings to stderr; failures are checked through $LASTEXITCODE
$root = $PSScriptRoot
$msys = "C:\msys64\ucrt64\bin"
$sys32 = Join-Path $env:SystemRoot "System32"
$dist = Join-Path $root "dist\XournalLab"
$env:PATH = "$msys;$env:PATH"

if (-not $NoBuild) {
  & cmake -S $root -B (Join-Path $root $Build) -G Ninja -DCMAKE_BUILD_TYPE=Release | Out-Null
  if ($LASTEXITCODE) { throw "cmake configure failed" }
  & cmake --build (Join-Path $root $Build) --target xl_app
  if ($LASTEXITCODE) { throw "build failed" }
}
$exe = Join-Path $root "$Build\xl_app.exe"
if (-not (Test-Path $exe)) { throw "missing $exe" }

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item $exe $dist
& "$msys\windeployqt6.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime (Join-Path $dist "xl_app.exe") 2>$null | Out-Null
if ($LASTEXITCODE) { throw "windeployqt6 failed" }

# Plugins this app never loads: touch (TUIO), network information and TLS (Qt6Network is only there because Qt6Pdf links it).
foreach ($d in "generic", "networkinformation", "tls") { Remove-Item (Join-Path $dist $d) -Recurse -Force -ErrorAction SilentlyContinue }

function Imports([string]$file) {
  & "$msys\objdump.exe" -p $file | Select-String "DLL Name:" | ForEach-Object { ($_ -split "DLL Name:\s*")[1].Trim() }
}
# Transitive closure of the DLLs that live in the MSYS2 folder (Windows' own ones are not copied).
$queue = New-Object System.Collections.Queue
Get-ChildItem $dist -Recurse -Include *.dll, *.exe | ForEach-Object { $queue.Enqueue($_.FullName) }
while ($queue.Count) {
  $f = $queue.Dequeue()
  foreach ($dll in Imports $f) {
    $target = Join-Path $dist $dll
    if (Test-Path $target) { continue }
    $src = Join-Path $msys $dll
    if (Test-Path $src) { Copy-Item $src $dist; $queue.Enqueue($target) }
  }
}

# Verify: every import is in the folder, in System32 or an API set.
$missing = @()
Get-ChildItem $dist -Recurse -Include *.dll, *.exe | ForEach-Object {
  foreach ($dll in Imports $_.FullName) {
    if ($dll -like "api-ms-win-*" -or $dll -like "ext-ms-win-*") { continue }
    if ((Test-Path (Join-Path $dist $dll)) -or (Test-Path (Join-Path $sys32 $dll))) { continue }
    $missing += "$($_.Name) -> $dll"
  }
}
if ($missing) { $missing | Sort-Object -Unique | ForEach-Object { Write-Host "MISSING $_" }; throw "unresolved DLL imports" }

Copy-Item (Join-Path $root "dist\README.txt") $dist -ErrorAction SilentlyContinue
$size = (Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum
"{0} files, {1:N1} MB in {2}" -f (Get-ChildItem $dist -Recurse -File).Count, ($size / 1MB), $dist

# Run it with a PATH that has no MSYS2 (and no Qt anywhere): a screenshot of a sample proves the platform plugin, styles, fonts and DLLs load.
$clean = ($env:PATH -split ";" | Where-Object { $_ -and $_ -notlike "C:\msys64*" -and $_ -notlike "*\Qt*" }) -join ";"
$shot = Join-Path $env:TEMP "xl_dist_check.png"
Remove-Item $shot -ErrorAction SilentlyContinue
$sample = Join-Path $root "..\samples\ejercicio_simulacro.xopp"
$sw = [Diagnostics.Stopwatch]::StartNew()
$saved = $env:PATH; $env:PATH = $clean   # Windows PowerShell 5.1 has no -Environment: change the PATH of this process around the call
$p = Start-Process -FilePath (Join-Path $dist "xl_app.exe") -ArgumentList "--screenshot", "`"$shot`"", "`"$sample`"" -PassThru -Wait
$env:PATH = $saved
$sw.Stop()
if ($p.ExitCode -eq 0 -and (Test-Path $shot)) { "clean-PATH run OK: screenshot {0} bytes, {1:N1} s (open + render + grab + exit)" -f (Get-Item $shot).Length, $sw.Elapsed.TotalSeconds }
else { throw "the packaged app failed on a PATH without MSYS2 (exit $($p.ExitCode))" }

if ($Zip) { $z = Join-Path $root "dist\XournalLab.zip"; Remove-Item $z -ErrorAction SilentlyContinue; Compress-Archive -Path $dist -DestinationPath $z; "zip: $z ({0:N1} MB)" -f ((Get-Item $z).Length / 1MB) }
