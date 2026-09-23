; Inno Setup script for QShot.
;
;   Build the installer:      iscc tools\installer\qshot.iss
;   Stage the payload first:  bash tools/deploy.sh --build
;
; !! THIS SCRIPT HAS NEVER BEEN COMPILED.
;    Inno Setup is not installed on the machine this was written on -- no ISCC.exe under
;    Program Files, nothing named iscc on PATH -- so it has been written and read carefully
;    but never executed. Treat the first compile as the real test and expect to correct a
;    path or a flag. Everything it consumes has been run: tools/deploy.sh assembles and
;    verifies the staging folder, and tools/smoke_deploy.sh starts it from a PATH with no Qt
;    on it. This file is the only unverified link in the chain.
;
; Three choices below are deliberate and are the ones most likely to be "fixed" wrongly:
;
;   * Per-user install. PrivilegesRequired=lowest with {autopf} resolving to
;     %LOCALAPPDATA%\Programs. QShot is inherently per-user: settings go to
;     HKCU\Software\QShot, autostart to the HKCU Run key, and capture history to
;     %APPDATA%\QShot. An all-users install would keep every byte of state per-user anyway,
;     while adding a UAC prompt and putting the executable somewhere the user cannot update.
;     So: no admin rights, no UAC, not Program Files.
;
;   * This file must stay UTF-8 *with a byte order mark*. Inno Setup 6 reads a script without
;     one as ANSI in the system code page. Measured on the development machine: ACP 936 (GBK).
;     Saving this file as UTF-8 without a BOM would therefore turn every Chinese string below
;     into mojibake here, and into different mojibake on a machine with another code page.
;     If you edit this file, check that your editor preserved the BOM.
;
;   * Uninstall leaves the user's settings and history alone. They are small, they are the
;     user's own data, and keeping them means a reinstall comes back with the same hotkey,
;     save folder and history instead of starting from scratch.

#define AppName "QShot"
#define AppDisplayName "QShot (快截)"
#define AppVersion "0.1.0"
#define AppPublisher "QShot"
#define AppExeName "qshot.exe"

; Where tools/deploy.sh left the staged folder. Overridable from the command line, which is
; how a CI job would point it at its own build output:
;   iscc /DStageDir=C:\build\QShot tools\installer\qshot.iss
#ifndef StageDir
  #define StageDir SourcePath + "..\..\dist\QShot"
#endif

; Fail at compile time rather than producing an installer that ships an empty folder. An
; installer that installs nothing is worse than no installer, because it looks like it worked.
#if !FileExists(StageDir + "\" + AppExeName)
  #error The staged folder has no qshot.exe. Run: bash tools/deploy.sh --build
#endif

[Setup]
; The doubled brace is the escape for a literal one: this must end up as {9004982A-...}.
; Do not change it after a release -- it is the identity Windows uses to recognise an
; existing installation, and a different value installs a second copy side by side.
AppId={{9004982A-6147-4006-9F09-C71C67FF2696}
AppName={#AppName}
AppVerName={#AppDisplayName} {#AppVersion}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppDisplayName}
VersionInfoDescription={#AppDisplayName} Setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#AppDisplayName}
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=..\..\resources\qshot.ico
OutputDir=..\..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 64-bit only, because the payload is a 64-bit MinGW build. Inno 6.3 renamed these from
; "x64os"/"x64" to the *compatible spellings below; on 6.0-6.2 use ArchitecturesAllowed=x64.
ArchitecturesAllowed=x64compatible
PrivilegesRequired=lowest
; A running instance holds qshot.exe open, so an upgrade cannot overwrite it. QShot has no
; single-instance guard, so an AppMutex would never be created and Inno's own running-app
; detection would silently do nothing -- CloseApplications is the mechanism that works here.
CloseApplications=yes
; QShot is a tray application with no main window and no reason to hold a file open across a
; restart, so the wizard never asks for one.
RestartIfNeededByRun=no

[Languages]
; The custom strings in this script are Chinese, but the built-in wizard pages come from
; Inno's own message files, and Inno Setup does not ship a Chinese one. The unofficial
; translation is listed on https://jrsoftware.org/files/istrans/ (Chinese, Simplified);
; download it into Inno Setup's Languages\ folder and uncomment the second line.
Name: "english"; MessagesFile: "compiler:Default.isl"
; Name: "chinese"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
; Both unchecked: an installer does not get to put an icon on the desktop or add itself to
; the user's logon sequence without being asked.
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："; Flags: unchecked
Name: "autostart";   Description: "开机时自动启动 {#AppDisplayName}"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
; recursesubdirs is what pulls in platforms\, imageformats\, iconengines\, styles\ and
; translations\. The folder was assembled and its import closure verified by tools/deploy.sh;
; this script ships it as-is rather than second-guessing which DLLs belong there.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppDisplayName}"; Filename: "{app}\{#AppExeName}"; Comment: "屏幕截图（启动后驻留在系统托盘）"
Name: "{group}\卸载 {#AppDisplayName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppDisplayName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Autostart, written to the same place and under the same value name the application itself
; uses (see WinAutoStart.cpp: HKCU\...\Run, value "QShot"). The data is a *quoted* path,
; matching commandLine() there, because Windows parses the value as a command line and an
; unquoted path containing a space would be split at the first space.
;
; uninsdeletevalue is what stops an uninstall from leaving a Run entry pointing at a deleted
; executable -- which Windows then reports at every single logon.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "QShot"; ValueData: """{app}\{#AppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "立即启动 {#AppDisplayName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Nothing. The settings in HKCU\Software\QShot and the capture history in %APPDATA%\QShot are
; left for the user; see the note at the top of this file.

[Code]
// The [Registry] entry above only covers the case where the autostart task was ticked during
// installation. The application can also switch autostart on later from its own settings
// dialog, and the installer has no record of that. So the value is removed unconditionally.
//
// RegDeleteValue returns non-zero when the value is absent, which is the ordinary case (most
// users never enable autostart). That is not a failure and there is nothing to report.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RegDeleteValue(HKEY_CURRENT_USER,
                   'Software\Microsoft\Windows\CurrentVersion\Run', 'QShot');
end;
