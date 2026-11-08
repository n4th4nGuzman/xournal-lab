@echo off
rem Opens the C++ application (Qt lives in the MSYS2 UCRT64 folder, so it has to be on the PATH).
set PATH=C:\msys64\ucrt64\bin;%PATH%
"%~dp0build\xl_app.exe" %*
