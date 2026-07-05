#define AppName "OmniStem Studio"
#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#ifndef StageDir
  #define StageDir "stage"
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif

[Setup]
AppId={{B4D44758-9017-4BF4-9E2C-8F0F962624D1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=OmniStem
DefaultDirName={autopf}\OmniStem Studio
DefaultGroupName=OmniStem Studio
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=OmniStem-Studio-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\OmniStem Studio.exe
SetupLogging=yes
WizardStyle=modern

[Files]
Source: "{#StageDir}\OmniStem Studio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\OmniStemWorker.exe"; DestDir: "{app}\worker"; Flags: ignoreversion
Source: "{#StageDir}\release-manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\OmniStem Studio"; Filename: "{app}\OmniStem Studio.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\OmniStem Studio"; Filename: "{app}\OmniStem Studio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "startworker"; Description: "Start the local audio worker when Windows starts"; GroupDescription: "Background services:"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "OmniStemWorker"; ValueData: """{app}\worker\OmniStemWorker.exe"""; Tasks: startworker; Flags: uninsdeletevalue

[Run]
Filename: "{app}\worker\OmniStemWorker.exe"; Description: "Start OmniStem local worker"; Flags: postinstall nowait skipifsilent
Filename: "{app}\OmniStem Studio.exe"; Description: "Launch OmniStem Studio"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM OmniStemWorker.exe /F"; Flags: runhidden waituntilterminated; RunOnceId: "StopWorker"
