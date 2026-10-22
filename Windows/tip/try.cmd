@echo off
rem Run as administrator after build.cmd: registers KoEnJa, adds it to your Japanese (and Korean, if
rem you have it) keyboards, and opens Notepad. Pick KoEnJa with Win+Space, type dkssudgktpdy and
rem Space for 안녕하세요. "try.cmd /u" removes it from the keyboards and unregisters it.
setlocal
cd /d "%~dp0..\..\.build\windows-tip" || exit /b 1
net session >nul 2>&1 || (echo Run this as administrator. & exit /b 1)
set TIP={113C852D-C4E7-4C6E-B713-3F79874DE867}{56DCC211-5E23-42CE-9CF1-20EBA011028E}
set PS=$l = Get-WinUserLanguageList; foreach ($x in $l) { $t = $null; if ($x.LanguageTag -like 'ja*') { $t = '0411:%TIP%' } elseif ($x.LanguageTag -like 'ko*') { $t = '0412:%TIP%' }; if ($t)
rem One line each: a ( ) block would end at the first ) in %PS%.
if /i "%~1"=="/u" powershell -NoProfile -Command "%PS% { [void]$x.InputMethodTips.Remove($t) } }; Set-WinUserLanguageList $l -Force"
if /i "%~1"=="/u" regsvr32 /s /u KoEnJa.dll & echo KoEnJa removed. & exit /b
rem Apps in an AppContainer (Start search, Store apps) load the DLL and its data too.
icacls . /grant "*S-1-15-2-1:(OI)(CI)RX" /t /q >nul
regsvr32 /s KoEnJa.dll || (echo regsvr32 failed. & exit /b 1)
powershell -NoProfile -Command "%PS% { if (-not $x.InputMethodTips.Contains($t)) { $x.InputMethodTips.Add($t) } } }; Set-WinUserLanguageList $l -Force"
start notepad
echo KoEnJa registered: pick it with Win+Space in Notepad.
