# Opens every hostile document (gen_hostile.mjs) in the real application (offscreen, screenshot mode), with a time limit.
# bad_* must print "open failed: <Spanish message>" and not crash; ok_* must open and render. Reports exit code, time and peak memory of each.
param([string]$Build = "build-qa", [int]$TimeoutSec = 90)
$root = Resolve-Path "$PSScriptRoot\..\.."
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
$dir = Join-Path $env:TEMP "xl_hostile"
& node "$PSScriptRoot\gen_hostile.mjs" $dir | Out-Null
$fails = 0
foreach ($f in Get-ChildItem "$dir\*.xopp" | Sort-Object Name) {
  $png = Join-Path $dir ($f.BaseName + ".png"); $err = Join-Path $dir ($f.BaseName + ".err")
  Remove-Item $png, $err -ErrorAction SilentlyContinue
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath "$root\$Build\xl_app.exe" -ArgumentList "--screenshot", "`"$png`"", "`"$($f.FullName)`"" -PassThru -RedirectStandardError $err -WindowStyle Hidden
  $null = $p.Handle   # keeps the exit code readable after the process ends
  $peak = 0
  while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $TimeoutSec) { try { $p.Refresh(); $peak = [Math]::Max($peak, $p.PeakWorkingSet64) } catch {}; Start-Sleep -Milliseconds 100 }
  $hung = -not $p.HasExited
  if ($hung) { $p.Kill() }
  $p.WaitForExit()
  $msg = if (Test-Path $err) { (Get-Content $err -Raw -ErrorAction SilentlyContinue) } else { "" }
  $msg = ("" + $msg).Trim().Split("`n")[0]
  $isBad = $f.Name -like "bad_*"
  $ok = (-not $hung) -and ($p.ExitCode -eq 0) -and ($(if ($isBad) { $msg -like "open failed:*" } else { (Test-Path $png) -and ($msg -notlike "open failed:*") }))
  if (-not $ok) { $fails++ }
  "{0,-5} {1,-24} exit={2,-11} {3,6:N1}s peak={4,5:N0} MB  {5}" -f $(if ($ok) { "OK" } else { "FAIL" }), $f.Name, $(if ($hung) { "HUNG" } else { $p.ExitCode }), $sw.Elapsed.TotalSeconds, ($peak / 1MB), $msg
}
exit $fails
