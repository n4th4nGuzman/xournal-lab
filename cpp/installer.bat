@echo off
rem Builds dist\XournalLab-Setup-<version>.exe: packages the app (package.bat) and runs Inno Setup. Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
rem Usage: installer.bat [version] [-NoBuild]   e.g. installer.bat 0.2.0 -NoBuild
setlocal
set "VER=%~1"
if "%VER%"=="" set "VER=0.1.0"
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (echo Inno Setup 6 not found. Install it: winget install JRSoftware.InnoSetup & exit /b 1)
cd /d "%~dp0"
call "%~dp0package.bat" %2
if errorlevel 1 exit /b 1
"%ISCC%" /DAppVersion=%VER% "%~dp0installer.iss"
if errorlevel 1 (echo Installer FAILED & exit /b 1)
echo Installer ready: %~dp0dist\XournalLab-Setup-%VER%.exe
