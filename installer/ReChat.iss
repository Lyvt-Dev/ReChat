#define MyAppName "ReChat"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
[Setup]
AppId={{1F810F9B-18CC-4CFB-91A6-12CC6065DB2A}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppPublisher=Lyvt-Dev
DefaultDirName={autopf}\ReChat
DefaultGroupName=ReChat
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=ReChat-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\ReChat.exe
PrivilegesRequired=lowest
[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{autoprograms}\ReChat"; Filename: "{app}\ReChat.exe"
Name: "{autodesktop}\ReChat"; Filename: "{app}\ReChat.exe"; Tasks: desktopicon
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"
[Run]
Filename: "{app}\ReChat.exe"; Description: "Launch ReChat"; Flags: nowait postinstall skipifsilent
