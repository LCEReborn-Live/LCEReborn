#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{B69A9C88-3B13-43C7-AFEA-9B83D1F13E27}
AppName=LCEReborn Live
AppVersion={#AppVersion}
AppPublisher=LCEReborn Live
AppPublisherURL=https://github.com/LCEReborn-Live/LCEReborn
DefaultDirName={localappdata}\Programs\LCEReborn Live
DefaultGroupName=LCEReborn Live
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\build\release
OutputBaseFilename=LCEReborn-Live-Setup-{#AppVersion}
SetupIconFile=..\resources\launcher\icon.ico
LicenseFile=..\LICENSE
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\LCERebornLauncher.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "..\build\installer-files\LCEReborn\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LCEReborn Live"; Filename: "{app}\LCERebornLauncher.exe"
Name: "{autodesktop}\LCEReborn Live"; Filename: "{app}\LCERebornLauncher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LCERebornLauncher.exe"; Description: "Launch LCEReborn Live"; Flags: postinstall nowait skipifsilent
