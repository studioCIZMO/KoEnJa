; KoEnJa's installer (Inno Setup 6, built by build.cmd): the TIP for 64-bit and 32-bit apps with one data
; folder, registered and added to the user's Korean/Japanese keyboards. Each version goes in its own
; folder (apps keep the DLLs loaded); an upgrade deletes the older folders, at the next restart if in use.
; The user's settings and learned words (HKCU\Software\KoEnJa, %LOCALAPPDATA%\KoEnJa) stay on uninstall.

#define Tip "..\..\.build\windows-tip"
#define AppVersion GetStringFileInfo(Tip + "\KoEnJa.dll", "ProductVersion")

[Setup]
AppId={{6F2B7C1E-3D4A-4E8B-9C21-5A7D0E4F8B36}
AppName=KoEnJa
AppVersion={#AppVersion}
AppPublisher=CIZMO, K.K.
DefaultDirName={autopf}\KoEnJa\{#AppVersion}
; Not the folder of the version installed before (Inno Setup's default): 0.2.0 went into 0.1.0's.
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Apps that have the TIP loaded (Explorer among them) keep running; files in use are replaced at restart.
CloseApplications=no
ShowLanguageDialog=auto
WizardStyle=modern
SetupIconFile=..\tip\KoEnJa.ico
UninstallDisplayIcon={app}\KoEnJaSettings.exe
OutputDir=..\..\.build\installer
OutputBaseFilename=KoEnJa-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
VersionInfoCompany=CIZMO, K.K.

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
en.Arm64=KoEnJa does not support ARM64 Windows yet.
ko.Arm64=KoEnJa는 아직 ARM64 Windows를 지원하지 않습니다.
ja.Arm64=KoEnJa はまだ ARM64 版 Windows に対応していません。
en.KoreanAdded=Korean was added to your languages, with KoEnJa as its keyboard.
ko.KoreanAdded=언어 목록에 한국어를 추가하고 KoEnJa를 그 키보드로 넣었습니다.
ja.KoreanAdded=言語の一覧に韓国語を追加し、KoEnJa をそのキーボードにしました。
en.OpenSettings=Open KoEnJa settings (keyboard, languages, switch key)
ko.OpenSettings=KoEnJa 설정 열기 (키보드, 언어, 전환 키)
ja.OpenSettings=KoEnJa の設定を開く (キーボード、言語、切り替えキー)

[Files]
; regserver: Setup registers (and the uninstaller unregisters) each with the regsvr32 of its bitness.
Source: "{#Tip}\KoEnJa.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 64bit restartreplace uninsrestartdelete
Source: "{#Tip}-x86\KoEnJa.dll"; DestDir: "{app}"; DestName: "KoEnJa32.dll"; Flags: ignoreversion regserver 32bit restartreplace uninsrestartdelete
Source: "{#Tip}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#Tip}\KoEnJaSettings.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Tip}\LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "languages.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\KoEnJa 설정"; Filename: "{app}\KoEnJaSettings.exe"

[Run]
; Apps in an AppContainer (Start search, Store apps) load the DLLs and the data too.
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /grant *S-1-15-2-1:(OI)(CI)RX *S-1-15-2-2:(OI)(CI)RX /t /q"; Flags: runhidden
Filename: "{app}\KoEnJaSettings.exe"; Description: "{cm:OpenSettings}"; Flags: postinstall nowait runasoriginaluser skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\languages.ps1"" -Remove"; Flags: runhidden; RunOnceId: "Languages"

[Code]
var KoreanAdded: Boolean;

function InitializeSetup(): Boolean;
begin
  // x64 DLLs don't load into ARM64 apps.
  Result := not IsArm64;
  if not Result then SuppressibleMsgBox(CustomMessage('Arm64'), mbError, MB_OK, IDOK);
end;

{ Deletes Dir, and what is in use (a loaded DLL, the mapped dictionary) at the next restart. }
procedure DeleteNowOrAtRestart(Dir: String);
var F: TFindRec;
begin
  if FindFirst(Dir + '\*', F) then
    try
      repeat
        if (F.Name <> '.') and (F.Name <> '..') then
          if F.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then DeleteNowOrAtRestart(Dir + '\' + F.Name)
          else if not DeleteFile(Dir + '\' + F.Name) then RestartReplace(Dir + '\' + F.Name, '');
      until not FindNext(F);
    finally
      FindClose(F);
    end;
  if not RemoveDir(Dir) then RestartReplace(Dir, '');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var F: TFindRec; Root: String; Code: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  // The user's keyboards are theirs: as the user who started Setup, not the elevated admin.
  ExecAsOriginalUser('powershell.exe', '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\languages.ps1') + '"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  KoreanAdded := Code = 2;
  Root := ExpandConstant('{autopf}\KoEnJa');
  if FindFirst(Root + '\*', F) then
    try
      repeat
        if (F.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (F.Name <> '.') and (F.Name <> '..') and (F.Name <> '{#AppVersion}') then
          DeleteNowOrAtRestart(Root + '\' + F.Name);
      until not FindNext(F);
    finally
      FindClose(F);
    end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and KoreanAdded then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('KoreanAdded');
end;
