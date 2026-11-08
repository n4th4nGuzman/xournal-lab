# One-screen health check after each change:  powershell -ExecutionPolicy Bypass -File smoke.ps1 [-Build build-smoke] [-NoBuild] [-Interchange] [-SkipSelftest]
# build (Release, all targets) -> ctest -> selftest offscreen (reads its log) -> optional Electron interchange. Exit code 0 only when everything passed.
# Uses its own build folder so it never fights with another build: pass -Build build to check the main folder.
param([string]$Build = "build-smoke", [switch]$NoBuild, [switch]$Interchange, [switch]$SkipSelftest)
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$bd = Join-Path $root $Build
$sw = [Diagnostics.Stopwatch]::StartNew()
$rows = @()
$bad = 0
function Row($name, $ok, $detail) { $script:rows += ("{0,-12} {1,-5} {2}" -f $name, $(if ($ok) { "OK" } else { "FAIL" }), $detail); if (-not $ok) { $script:bad++ } }

# 1. build
if (-not $NoBuild) {
  $log = Join-Path $bd "smoke_build.log"
  & cmake -S $root -B $bd -G Ninja -DCMAKE_BUILD_TYPE=Release > $null 2>&1
  New-Item -ItemType Directory -Force $bd | Out-Null
  & cmake --build $bd *> $log
  $ok = ($LASTEXITCODE -eq 0)
  $warn = (Select-String -Path $log -Pattern "warning:" -SimpleMatch | Measure-Object).Count
  Row "build" $ok ("{0} compiler warning(s){1}" -f $warn, $(if (-not $ok) { "; first error: " + ((Select-String -Path $log -Pattern "error" | Select-Object -First 1).Line) } else { "" }))
  if (-not $ok) { $rows; "log: $log"; exit 1 }
}

# 2. ctest (never with QT_QPA_PLATFORM set: some tests assume the real platform)
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
$ct = & ctest --test-dir $bd --output-on-failure 2>&1 | Out-String
$total = [regex]::Match($ct, 'out of (\d+)').Groups[1].Value
$nfail = [regex]::Match($ct, '(\d+) tests failed').Groups[1].Value
if (-not $nfail) { $nfail = 0 }
$names = ([regex]::Matches($ct, '\d+ - (\S+) \(Failed\)') | ForEach-Object { $_.Groups[1].Value }) -join ", "
Row "ctest" ($total -and [int]$nfail -eq 0) ("{0} of {1} passed{2}" -f ([int]$total - [int]$nfail), $total, $(if ($names) { "; failed: $names" } else { "" }))

# 3. selftest
if (-not $SkipSelftest) {
  $slog = Join-Path $bd "smoke_selftest.log"
  Remove-Item $slog -ErrorAction SilentlyContinue
  $env:QT_QPA_PLATFORM = "offscreen"
  $t0 = $sw.Elapsed.TotalSeconds
  $p = Start-Process -FilePath (Join-Path $bd "xl_app.exe") -ArgumentList "--selftest", "--log", "`"$slog`"" -PassThru -WindowStyle Hidden
  $peak = 0
  while (-not $p.HasExited) { try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}; Start-Sleep -Milliseconds 200 }
  Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
  if (Test-Path $slog) {
    $txt = Get-Content $slog -Raw
    $pass = ([regex]::Matches($txt, "(?m)^\s*PASS\b")).Count
    $fail = ([regex]::Matches($txt, "(?m)^\s*FAIL\b")).Count
    $mem = [regex]::Match($txt, "peak working set (\d+) MB").Groups[1].Value
    if (-not $mem) { $mem = [int]($peak / 1MB) }   # the run died before it printed its own figure
    $first = if ($fail) { " first: " + (([regex]::Matches($txt, "(?m)^\s*FAIL.*$") | Select-Object -First 1).Value.Trim()) } else { "" }
    Row "selftest" ($fail -eq 0 -and $p.ExitCode -eq 0) ("{0} PASS / {1} FAIL, exit {2}, peak {3} MB, {4:N0} s{5}" -f $pass, $fail, $p.ExitCode, $mem, ($sw.Elapsed.TotalSeconds - $t0), $first)
  } else { Row "selftest" $false "no log written (crash?) exit $($p.ExitCode)" }
}

# 4. Electron interchange (needs the Electron app's node_modules; reads only)
if ($Interchange) {
  $out = & (Join-Path $root "tools\interchange\run_interchange.ps1") -Build $Build 2>&1 | Out-String
  $okI = ($LASTEXITCODE -eq 0)
  $n = ([regex]::Matches($out, "cpp==electron: IDENTICAL")).Count
  Row "interchange" $okI ("{0} document(s) identical both ways" -f $n)
  $pr = & node (Join-Path $root "tools\interchange\prefs_check.mjs") (Join-Path $bd "xl_prefsio.exe") 2>&1 | Out-String
  Row "prefs" ($pr -notmatch "FAIL") ("{0} compatibility check(s)" -f ([regex]::Matches($pr, "(?m)^OK ")).Count)
}

""
"=== smoke ($Build) ==="
$rows
"total {0:N0} s -> {1}" -f $sw.Elapsed.TotalSeconds, $(if ($bad) { "$bad FAILED" } else { "ALL OK" })
exit $bad
