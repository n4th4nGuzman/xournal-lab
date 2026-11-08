# Interchange check between the C++ app and the Electron app (read-only on both; works in a temp folder).
#   1. C++ -> Electron: a document with every generated kind (xl_qa_gen) and the C++ re-save of every sample are parsed by the Electron app's own
#      parseXopp and must give the same canonical dump as the C++ loader.
#   2. Electron -> C++: the samples and the Electron re-serialization of every file are loaded by C++ and must dump identically.
# Usage: powershell -File tools\interchange\run_interchange.ps1 [-Build build-qa] [-Extra file.xopp ...]
param([string]$Build = "build-qa", [string[]]$Extra = @())
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
Remove-Item Env:ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue
$t = Join-Path $env:TEMP "xl_interchange"
Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$t\in", "$t\cpp", "$t\out" | Out-Null
$samples = Get-ChildItem "$root\..\samples\*.xopp" | ForEach-Object FullName
$shots = Get-ChildItem "$root\..\app\shots\e2e-out.xopp", "$root\..\app\e2e_out.xopp" -ErrorAction SilentlyContinue | ForEach-Object FullName
& "$root\$Build\xl_qa_gen.exe" "$t\in\qa_gen.xopp" | Out-Null
$inputs = @("$t\in\qa_gen.xopp") + $samples + $shots + $Extra
node "$PSScriptRoot\build_bundle.mjs" | Out-Null
$fail = 0
# C++ re-saves of every input
foreach ($f in $inputs) {
  $n = [IO.Path]::GetFileNameWithoutExtension($f)
  & $env:ComSpec /c """$root\$Build\xl_dump.exe"" --resave ""$t\cpp\$n.resaved.xopp"" ""$f"" > ""$t\cpp\$n.cpp.json"""
  if ($LASTEXITCODE) { Write-Host "FAIL C++ cannot load $f"; $fail++ }
}
$all = $inputs + (Get-ChildItem "$t\cpp\*.resaved.xopp" | ForEach-Object FullName)
$ErrorActionPreference = "Continue"
Push-Location "$root\..\app"
& .\node_modules\electron\dist\electron.exe "$PSScriptRoot\electron_main.cjs" "$t\out" @all 2>&1 | Where-Object { $_ -match "^(OK|FAIL)" }
Pop-Location
foreach ($f in $inputs) {
  $n = [IO.Path]::GetFileNameWithoutExtension($f)
  # a) Electron's parse of the original == C++ parse of the original
  $r1 = node "$PSScriptRoot\compare.mjs" "$t\cpp\$n.cpp.json" "$t\out\$n.electron.json"
  # b) Electron's parse of the C++ re-save == Electron's parse of the original (C++ writes nothing the Electron app reads differently)
  $r2 = node "$PSScriptRoot\compare.mjs" "$t\out\$n.electron.json" "$t\out\$n.resaved.electron.json"
  # c) C++ loading Electron's own re-serialization == C++ loading the original
  & $env:ComSpec /c """$root\$Build\xl_dump.exe"" ""$t\out\$n.electron.xopp"" > ""$t\cpp\$n.fromElectron.json"""
  $r3 = node "$PSScriptRoot\compare.mjs" "$t\cpp\$n.cpp.json" "$t\cpp\$n.fromElectron.json"
  "{0,-34} cpp==electron: {1,-12} electron(resaved)==orig: {2,-12} cpp(electron-saved)==orig: {3}" -f $n, ($r1 | Select-Object -Last 1), ($r2 | Select-Object -Last 1), ($r3 | Select-Object -Last 1)
  if (@($r1)[-1] -ne "IDENTICAL" -or @($r2)[-1] -ne "IDENTICAL" -or @($r3)[-1] -ne "IDENTICAL") { $fail++; $r1 + $r2 + $r3 | Where-Object { $_ -like "DIFF*" } | Select-Object -First 6 }
}
"work folder: $t"
exit $fail
