# ChordEngine - installer smoke test for a throwaway Windows machine (CI runner).
#
# Installs the Setup.exe silently, checks the installed module is byte-identical
# to the built one, checks that a foreign plug-in in the shared VST3 folder
# survives, runs the installer again (upgrade path), uninstalls, and checks that
# only ChordEngine's files are gone. Exits non-zero on the first failure.
param(
    [Parameter(Mandatory)] [string] $Setup,
    [Parameter(Mandatory)] [string] $BuiltModule,
    [string] $LogDir = "$env:TEMP\chordengine-installer-test"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$vst3 = Join-Path $env:CommonProgramFiles 'VST3'
$bundle = Join-Path $vst3 'ChordEngine.vst3'
$module = Join-Path $bundle 'Contents\x86_64-win\ChordEngine.vst3'
$foreign = Join-Path $vst3 'SomeoneElse.vst3\Contents\x86_64-win\SomeoneElse.vst3'

function Fail([string] $why) { Write-Host "FAIL: $why"; exit 1 }
function RunSetup([string] $exe, [string] $log) {
    $p = Start-Process -FilePath $exe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log" -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "$exe exited with $($p.ExitCode) (log: $log)" }
}

if (-not (Test-Path $Setup)) { Fail "installer not found: $Setup" }
$expected = (Get-FileHash -Algorithm SHA256 -Path $BuiltModule).Hash

# A foreign plug-in that must survive install and uninstall.
New-Item -ItemType Directory -Force -Path (Split-Path $foreign) | Out-Null
Set-Content -Path $foreign -Value 'not ours'

Write-Host '== install =='
RunSetup $Setup (Join-Path $LogDir 'install.log')
if (-not (Test-Path $module)) { Fail "module not installed at $module" }
$got = (Get-FileHash -Algorithm SHA256 -Path $module).Hash
if ($got -ne $expected) { Fail "installed module hash $got != built $expected" }
if (-not (Test-Path (Join-Path $bundle 'Contents\Resources\moduleinfo.json'))) { Fail 'moduleinfo.json missing' }
Write-Host "OK installed, sha256 $got"

Write-Host '== install again (upgrade path) =='
RunSetup $Setup (Join-Path $LogDir 'reinstall.log')
if ((Get-FileHash -Algorithm SHA256 -Path $module).Hash -ne $expected) { Fail 'module changed after reinstall' }
Write-Host 'OK reinstalled'

Write-Host '== uninstall =='
$uninstaller = Get-ChildItem -Path (Join-Path $env:CommonProgramFiles 'Music-Prod\ChordEngine') -Filter 'unins*.exe' |
    Select-Object -First 1
if (-not $uninstaller) { Fail 'uninstaller not found' }
RunSetup $uninstaller.FullName (Join-Path $LogDir 'uninstall.log')
# The Inno uninstaller re-launches itself from %TEMP%; give that copy time to finish.
for ($i = 0; $i -lt 60 -and (Test-Path $bundle); $i++) { Start-Sleep -Milliseconds 500 }
if (Test-Path $bundle) { Fail "bundle still present after uninstall: $bundle" }
if (-not (Test-Path $foreign)) { Fail 'uninstall removed a foreign plug-in' }
if (-not (Test-Path $vst3)) { Fail 'uninstall removed the shared VST3 folder' }
Write-Host 'OK uninstalled; foreign plug-in and shared folder untouched'

Remove-Item -Recurse -Force (Join-Path $vst3 'SomeoneElse.vst3')
Write-Host 'PASS'
exit 0
