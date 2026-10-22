#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #define OutputDir "output"
#endif
[Setup]
AppId={{46F6FF42-5D82-49AE-8C20-CE112713AD74}
AppName=Phone Mirror
AppVersion=0.4.0-preview
AppPublisher=Phone Mirror Project
AppPublisherURL=https://github.com/fodenspider/phone-mirror
DefaultDirName={localappdata}\Programs\PhoneMirror
DefaultGroupName=Phone Mirror
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=PhoneMirror-0.4.0-preview-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\PhoneMirror.exe
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Phone Mirror"; Filename: "{app}\PhoneMirror.exe"
Name: "{autodesktop}\Phone Mirror"; Filename: "{app}\PhoneMirror.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\PhoneMirror.exe"; Description: "Open Phone Mirror"; Flags: nowait postinstall skipifsilent
