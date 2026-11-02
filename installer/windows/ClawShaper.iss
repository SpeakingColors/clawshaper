; Inno Setup script. Build: iscc /DMyVersion=1.2.3 /DSourceVst3=path\to\"ClawShaper.vst3" /DManualPdf=path\to\manual.pdf ClawShaper.iss
#ifndef MyVersion
  #define MyVersion "0.0.0"
#endif
#ifndef SourceVst3
  #define SourceVst3 "..\..\build\ClawShaper_artefacts\Release\VST3\ClawShaper.vst3"
#endif
[Setup]
AppId={{D2F61A7E-93B4-4C58-A0E1-7B3C95D8F246}
AppName=ClawShaper
AppVersion={#MyVersion}
AppPublisher=Studio 87
AppPublisherURL=https://github.com/SpeakingColors/clawshaper
DefaultDirName={autopf}\Studio 87\ClawShaper
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=ClawShaper-Setup
Compression=lzma2
SolidCompression=yes
LicenseFile=..\..\LICENSE
UninstallDisplayName=ClawShaper
CloseApplications=yes
[Messages]
SelectDirDesc=Where should the manual and licence go?
SelectDirLabel3=The plugin itself is installed to the common VST3 folder (C:\Program Files\Common Files\VST3). This folder only holds the manual, the licence and the uninstaller.
[Files]
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\ClawShaper.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
#ifdef ManualPdf
Source: "{#ManualPdf}"; DestDir: "{app}"; DestName: "ClawShaper-manual.pdf"; Flags: ignoreversion
#endif
