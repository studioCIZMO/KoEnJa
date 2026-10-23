@echo off
rem Builds the TIP (KoEnJa.dll, with the engine linked in, its icons, and its data next to it),
rem KoEnJaSettings.exe (keyboard, languages, switch key) and tip_test.exe into .build\windows-tip, x64. Static CRT (/MT): the DLL loads into any app, which may not ship one.
rem "build.cmd x86": the DLL and tip_test for 32-bit apps into .build\windows-tip-x86 (no data: the installer puts both DLLs by one data folder).
rem Then, as admin: regsvr32 KoEnJa.dll, and tip_test.exe types into it.
setlocal
cd /d "%~dp0"
set ARCH=x64
set OUT=..\..\.build\windows-tip
if /i "%~1"=="x86" (set ARCH=x86& set OUT=..\..\.build\windows-tip-x86)
rem Any Visual Studio with C++ (Build Tools here, Enterprise on GitHub's runners).
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if /i not "%VSCMD_ARG_TGT_ARCH%"=="%ARCH%" call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1
set FLAGS=/nologo /std:c++20 /utf-8 /fp:precise /EHsc /O2 /W3 /MT /MP /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS
if not exist %OUT%\obj mkdir %OUT%\obj
cl %FLAGS% /c ..\engine\src\*.cpp ..\japanese\MozcConverter.cpp Tip.cpp /Fo%OUT%\obj\ || exit /b 1
rc /nologo /fo %OUT%\Tip.res Tip.rc || exit /b 1
link /nologo /DLL /DEF:KoEnJa.def /OUT:%OUT%\KoEnJa.dll %OUT%\obj\*.obj %OUT%\Tip.res ole32.lib oleaut32.lib advapi32.lib user32.lib gdi32.lib dwmapi.lib shcore.lib || exit /b 1
cl %FLAGS% tip_test.cpp /Fo%OUT%\ /Fe%OUT%\tip_test.exe ole32.lib oleaut32.lib user32.lib advapi32.lib || exit /b 1
if %ARCH%==x86 exit /b 0
rc /nologo /fo %OUT%\Settings.res Version.rc || exit /b 1
cl %FLAGS% Settings.cpp %OUT%\Settings.res /Fo%OUT%\ /Fe%OUT%\KoEnJaSettings.exe user32.lib gdi32.lib advapi32.lib shell32.lib version.lib /link /MANIFEST:EMBED || exit /b 1
xcopy /y /q /i ..\..\Packages\SeamtypeCore\Sources\SeamtypeCore\Data %OUT%\data >nul || exit /b 1
rem The English spelling list: Webster's 2nd International (web2, macOS's /usr/share/dict/words), public domain.
copy /y ..\engine\data\words.txt %OUT%\data\ >nul || exit /b 1
rem Mozc's dictionary, unpacked once: the converter maps it, shared by every app the TIP loads into.
if not exist %OUT%\data\mozc.dic powershell -NoProfile -Command "$i=[IO.File]::OpenRead('..\..\Android\japanese\src\main\resources\mozc.dic.gz'); $o=[IO.File]::Create('%OUT%\data\mozc.dic'); (New-Object IO.Compression.GZipStream($i,[IO.Compression.CompressionMode]::Decompress)).CopyTo($o); $o.Close(); $i.Close()" || exit /b 1
