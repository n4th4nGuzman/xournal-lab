@echo off
rem Rebuilds the C++ app and repackages dist\XournalLab (stand-alone, no MSYS2 needed to run it). Extra arguments go to package.ps1 (-NoBuild, -Zip, -Build dir).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
if errorlevel 1 (echo Packaging FAILED & exit /b 1)
