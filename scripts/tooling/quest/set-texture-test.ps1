# User-operated sampling experiment. Optional explicit app stop; no install/launch/data edit.
param(
    [ValidateSet('Test','Restore')][string]$Mode = 'Test',
    [string]$Adb = 'C:\VCVRBuild\.android-sdk\platform-tools\adb.exe',
    [string]$DeviceSerial = '',
    [switch]$StopApp,
    [string]$BackupDirectory = (Join-Path $PSScriptRoot 'Quest-Texturtest-Sicherung')
)
$ErrorActionPreference = 'Stop'
# GeneralsX @tooling Codex 09/10/2026 Resolve the connected device without a private serial.
if (-not $DeviceSerial) {
    $devices = & $Adb devices
    if ($LASTEXITCODE -ne 0) { throw 'Could not list ADB devices.' }
    $connected = @($devices | ForEach-Object { if ($_ -match '^([^\s]+)\s+device$') { $Matches[1] } })
    if ($connected.Count -ne 1) { throw 'Connect exactly one device or pass -DeviceSerial explicitly.' }
    $DeviceSerial = $connected[0]
}
if ($DeviceSerial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Invalid device serial.' }
$deviceArgs = @('-s', $DeviceSerial)
function Invoke-CheckedAdb {
    param([string[]]$Command)
    $result = & $Adb @deviceArgs @Command
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $($Command[0])" }
    return (($result | Out-String).Trim())
}
function Read-RemoteBytes {
    param([string]$Path)
    $encoded = Invoke-CheckedAdb @('exec-out','run-as','com.generalsx.questvr','base64',$Path)
    return ,([Convert]::FromBase64String($encoded))
}
function Get-ByteHash {
    param([byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Assert-AppClosed {
    $running = & $Adb @deviceArgs shell pidof com.generalsx.questvr 2>$null
    $pidExit = $LASTEXITCODE
    if ($pidExit -notin @(0,1)) { throw 'Could not check Quest app state.' }
    if (($running | Out-String).Trim()) {
        throw 'Der Quest-App-Prozess existiert noch (auch bei geschlossenem Fenster moeglich). Befehl mit -StopApp ausfuehren; dadurch wird nur Generals ZH Quest VR beendet.'
    }
}
function Get-RemoteHash {
    param([string]$Path)
    $line = Invoke-CheckedAdb @('exec-out','run-as','com.generalsx.questvr','sha256sum',$Path)
    if ($line -notmatch '^([a-f0-9]{64})\s') { throw 'Could not verify device file hash.' }
    return $Matches[1]
}
if ((Invoke-CheckedAdb @('get-state')) -ne 'device') { throw 'Quest nicht verbunden oder USB-Zugriff nicht erlaubt.' }
if ($StopApp) {
    Invoke-CheckedAdb @('shell','am','force-stop','com.generalsx.questvr') | Out-Null
}
Assert-AppClosed
$utf8 = New-Object System.Text.UTF8Encoding($false, $true)
$prefs = [xml]$utf8.GetString((Read-RemoteBytes 'shared_prefs/generalszh_setup.xml'))
$nodes = @($prefs.map.string | Where-Object { $_.name -eq 'game_path' })
if ($nodes.Count -ne 1) { throw 'Could not identify the imported Quest game folder.' }
$gamePath = [string]$nodes[0].InnerText
if ($gamePath -notmatch '^/data/(?:user/0|data)/com\.generalsx\.questvr/files/Imported/[A-Fa-f0-9-]+$') {
    throw 'Unexpected game folder; no file changed.'
}
$target = $gamePath + '/dxvk.conf'
[byte[]]$current = Read-RemoteBytes $target
$currentHash = Get-ByteHash $current
if ((Get-RemoteHash $target) -ne $currentHash) { throw 'Config changed during read; no file changed.' }
[IO.Directory]::CreateDirectory($BackupDirectory) | Out-Null
$identity = (Get-ByteHash $utf8.GetBytes($gamePath)).Substring(0,12)
$backupPath = Join-Path $BackupDirectory ($DeviceSerial + '-' + $identity + '.json')
if (Test-Path -LiteralPath $backupPath) {
    $backup = $utf8.GetString([IO.File]::ReadAllBytes($backupPath)) | ConvertFrom-Json
    if ($backup.device -ne $DeviceSerial -or $backup.target -ne $target) { throw 'Backup identity mismatch.' }
    [byte[]]$original = [Convert]::FromBase64String($backup.bytesBase64)
    if ((Get-ByteHash $original) -ne $backup.sha256) { throw 'Backup hash mismatch.' }
} else {
    if ($Mode -eq 'Restore') { throw 'Keine Sicherung vorhanden; keine Datei geaendert.' }
    [byte[]]$original = $current
    $backup = @{device=$DeviceSerial;target=$target;sha256=$currentHash;bytesBase64=[Convert]::ToBase64String($original)}
    [IO.File]::WriteAllText($backupPath, ($backup | ConvertTo-Json), $utf8)
}
$text = $utf8.GetString($original)
$pattern = '(?m)^[\t ]*d3d9\.samplerLodBias[\t ]*=[^\r\n]*'
$keyCount = [regex]::Matches($text, $pattern).Count
if ($keyCount -gt 1) { throw 'Duplicate samplerLodBias keys; no file changed.' }
if ($keyCount -eq 1) {
    $trialText = [regex]::Replace($text, $pattern, 'd3d9.samplerLodBias = 0.5')
} else {
    $separator = if ($text.Length -eq 0 -or $text.EndsWith("`n")) { '' } else { "`n" }
    $trialText = $text + $separator + "d3d9.samplerLodBias = 0.5`n"
}
[byte[]]$trial = $utf8.GetBytes($trialText)
if ($currentHash -notin @((Get-ByteHash $original), (Get-ByteHash $trial))) {
    throw 'Die Konfiguration wurde inzwischen anderweitig geaendert. Nichts ueberschrieben; bitte im Chat melden.'
}
[byte[]]$desired = if ($Mode -eq 'Test') { $trial } else { $original }
$expectedHash = Get-ByteHash $desired
if ($currentHash -ne $expectedHash) {
    Assert-AppClosed
    $token = [Guid]::NewGuid().ToString('N')
    $localTemp = Join-Path $BackupDirectory ('write-' + $token + '.conf')
    $remoteTemp = '/data/local/tmp/quest-texture-' + $token + '.conf'
    $privateTemp = $target + '.quest-texture-test.tmp'
    [IO.File]::WriteAllBytes($localTemp, $desired)
    Invoke-CheckedAdb @('push',$localTemp,$remoteTemp) | Out-Null
    Invoke-CheckedAdb @('shell','chmod','644',$remoteTemp) | Out-Null
    Invoke-CheckedAdb @('shell','run-as','com.generalsx.questvr','cp',$remoteTemp,$privateTemp) | Out-Null
    if ((Get-RemoteHash $privateTemp) -ne $expectedHash) { throw 'Staged config did not verify; original unchanged.' }
    Assert-AppClosed
    if ((Get-RemoteHash $target) -ne $currentHash) { throw 'Config changed meanwhile; original not overwritten.' }
    Invoke-CheckedAdb @('shell','run-as','com.generalsx.questvr','mv',$privateTemp,$target) | Out-Null
    if ((Get-RemoteHash $target) -ne $expectedHash) { throw 'Written config did not verify; retain backup and report in chat.' }
    Invoke-CheckedAdb @('shell','rm',$remoteTemp) | Out-Null
    Remove-Item -LiteralPath $localTemp
}
Write-Host "Sicherung: $backupPath"
if ($Mode -eq 'Test') { Write-Host 'Fertig: Texturtest 0.5 gesetzt und geprueft. Jetzt Spiel mit SMAA starten und denselben Skirmish laden.' }
else { Write-Host 'Fertig: urspruengliche DXVK-Konfiguration exakt wiederhergestellt und geprueft. Jetzt Spiel neu starten.' }
