; Inno Setup script of the Xournal Lab installer. Build it with installer.bat (which first packages dist\XournalLab).
; Per-user install by default (no administrator needed); the user can choose "for all users" in the first page.
#ifndef AppVersion
  #define AppVersion "0.1.0-alpha.1"
#endif
#ifndef FileVersion
  #define FileVersion "0.1.0.1"   ; numeric form for the version resource of the setup program
#endif

[Setup]
AppId={{6F1D3C52-8E0B-4D7A-9B7E-5A2C1F0E4D31}
AppName=Xournal Lab
AppVersion={#AppVersion}
AppPublisher=Xournal Lab
DefaultDirName={autopf}\Xournal Lab
DefaultGroupName=Xournal Lab
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=XournalLab-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=app\icon\xournal-lab.ico
VersionInfoVersion={#FileVersion}
InfoBeforeFile=installer-notice.txt
UninstallDisplayIcon={app}\xl_app.exe
UninstallDisplayName=Xournal Lab
CloseApplications=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"
; not ticked by default: Xournal++ may already own .xopp files
Name: "assoc"; Description: "Abrir los archivos .xopp con Xournal Lab"; GroupDescription: "Archivos:"; Flags: unchecked

[Files]
Source: "dist\XournalLab\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Xournal Lab"; Filename: "{app}\xl_app.exe"
Name: "{group}\Desinstalar Xournal Lab"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Xournal Lab"; Filename: "{app}\xl_app.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.xopp\OpenWithProgids"; ValueType: string; ValueName: "XournalLab.xopp"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\XournalLab.xopp"; ValueType: string; ValueData: "Documento de Xournal Lab"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\XournalLab.xopp\DefaultIcon"; ValueType: string; ValueData: "{app}\xl_app.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\XournalLab.xopp\shell\open\command"; ValueType: string; ValueData: """{app}\xl_app.exe"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\xl_app.exe"; Description: "Abrir Xournal Lab"; Flags: nowait postinstall skipifsilent
