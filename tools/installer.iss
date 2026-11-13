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
OutputBaseFilename=LCEReborn-Live-Installer-{#AppVersion}
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
Source: "..\build\installer-files\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LCEReborn Live"; Filename: "{app}\LCERebornLauncher.exe"
Name: "{autodesktop}\LCEReborn Live"; Filename: "{app}\LCERebornLauncher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LCERebornLauncher.exe"; Description: "Launch LCEReborn Live"; Flags: postinstall nowait skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Classes\lcereborn"; ValueType: string; ValueName: ""; ValueData: "URL:LCEReborn Live"
Root: HKCU; Subkey: "Software\Classes\lcereborn"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\lcereborn\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\LCERebornLauncher.exe"",0"
Root: HKCU; Subkey: "Software\Classes\lcereborn\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\LCERebornLauncher.exe"" --live-join ""%1"""

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  CurrentCommand: String;
begin
  if CurUninstallStep = usPostUninstall then
    if RegQueryStringValue(HKCU, 'Software\Classes\lcereborn\shell\open\command', '', CurrentCommand) then
      if CompareText(CurrentCommand, ExpandConstant('"{app}\LCERebornLauncher.exe" --live-join "%1"')) = 0 then
        RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\lcereborn');
end;
