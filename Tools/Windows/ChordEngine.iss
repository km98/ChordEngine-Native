; ChordEngine - Windows x64 VST3 installer (Inno Setup 6)
;
; Build (what .github/workflows/windows-vst3.yml does):
;   ISCC.exe /DAppVersion=0.6.0 /DStagedBundle=<path to ChordEngine.vst3> /O<out dir> Tools\Windows\ChordEngine.iss
;
; What it does
;   * installs ChordEngine.vst3 into the shared VST3 folder
;     (C:\Program Files\Common Files\VST3) and nothing else. The uninstaller is
;     kept outside that folder so hosts never scan it.
;   * an upgrade replaces the bundle in place (same AppId); uninstall removes only
;     the files this installer wrote.
;
; Status: UNSIGNED. Code signing needs a Windows code signing certificate
; (SignTool / Azure Trusted Signing), which the project does not have yet; until
; then SmartScreen warns on first run. Pattern follows PumpCurve's installer
; (km98/SideChainer, SideChain/Builds/Windows/installer/PumpCurve.iss).

#define AppName "ChordEngine"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StagedBundle
  #define StagedBundle "..\..\Build\Windows\Bundles\ChordEngine.vst3"
#endif

[Setup]
; Never change this id: it is how an upgrade finds the previous install.
AppId={{32A9BE33-C6E8-4A03-A598-2D1A4BF95CC1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Music-Prod
AppPublisherURL=https://music-prod.com
AppSupportURL=https://music-prod.com/support
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Music-Prod
VersionInfoProductName={#AppName}
DefaultDirName={commoncf}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={commoncf}\Music-Prod\{#AppName}
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName={#AppName} {#AppVersion} (VST3)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StagedBundle}\Contents\*"; DestDir: "{commoncf}\VST3\{#AppName}.vst3\Contents"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Only the bundle directory this installer created; the shared VST3 folder stays.
Type: dirifempty; Name: "{commoncf}\VST3\{#AppName}.vst3\Contents\x86_64-win"
Type: dirifempty; Name: "{commoncf}\VST3\{#AppName}.vst3\Contents\Resources"
Type: dirifempty; Name: "{commoncf}\VST3\{#AppName}.vst3\Contents"
Type: dirifempty; Name: "{commoncf}\VST3\{#AppName}.vst3"
