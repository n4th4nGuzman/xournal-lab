@echo off
rem Starts the app writing a diagnostic trace of the pen events and decisions to %TEMP%\xl_penlog.txt
set PATH=C:\msys64\ucrt64\bin;%PATH%
set XL_PENLOG=%TEMP%\xl_penlog.txt
if exist "%XL_PENLOG%" del "%XL_PENLOG%"
"%~dp0build\xl_app.exe" %*
