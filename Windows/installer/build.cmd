@echo off
rem Builds .build\installer\KoEnJa-Setup-<version>.exe (KoEnJa.iss) with README-test.txt next to it:
rem the TIP for x64 and x86 (tip\build.cmd), LICENSES.txt, then Inno Setup 6 (installed with winget if missing).
setlocal
cd /d "%~dp0"
call ..\tip\build.cmd || exit /b 1
call ..\tip\build.cmd x86 || exit /b 1
cd /d "%~dp0"
copy /y /b LICENSES.txt + ..\..\Android\japanese\src\main\resources\mozc.dic.LICENSE.txt ..\..\.build\windows-tip\LICENSES.txt >nul || exit /b 1
set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" winget install --id JRSoftware.InnoSetup -e --accept-source-agreements --accept-package-agreements
if not exist "%ISCC%" set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
"%ISCC%" /Q KoEnJa.iss || exit /b 1
copy /y README-test.txt ..\..\.build\installer\ >nul
