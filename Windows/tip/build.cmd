@echo off
rem Builds the TIP (KoEnJa.dll, with the engine linked in, its icons, and its data next to it) and
rem tip_test.exe into .build\windows-tip, x64. Static CRT (/MT): the DLL loads into any app, which may not ship one.
rem Then, as admin: regsvr32 KoEnJa.dll, and tip_test.exe types into it.
setlocal
cd /d "%~dp0"
set OUT=..\..\.build\windows-tip
if not defined VCINSTALLDIR call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set FLAGS=/nologo /std:c++20 /utf-8 /fp:precise /EHsc /O2 /W3 /MT /MP /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS
if not exist %OUT%\obj mkdir %OUT%\obj
cl %FLAGS% /c ..\engine\src\*.cpp Tip.cpp /Fo%OUT%\obj\ || exit /b 1
rc /nologo /fo %OUT%\Tip.res Tip.rc || exit /b 1
link /nologo /DLL /OUT:%OUT%\KoEnJa.dll %OUT%\obj\*.obj %OUT%\Tip.res ole32.lib oleaut32.lib advapi32.lib user32.lib gdi32.lib dwmapi.lib shcore.lib || exit /b 1
cl %FLAGS% tip_test.cpp /Fo%OUT%\ /Fe%OUT%\tip_test.exe ole32.lib oleaut32.lib user32.lib || exit /b 1
xcopy /y /q /i ..\..\Packages\SeamtypeCore\Sources\SeamtypeCore\Data %OUT%\data >nul || exit /b 1
rem The English spelling list: Webster's 2nd International (web2, macOS's /usr/share/dict/words), public domain.
copy /y ..\engine\data\words.txt %OUT%\data\ >nul || exit /b 1
