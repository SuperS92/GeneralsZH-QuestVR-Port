# SPDX-License-Identifier: GPL-3.0-or-later
# User-operated recovery of a missing ZH selection from its retained nested base path.
# Stops only the derivative. No install, launch, data clear, asset or graphics edit.
[CmdletBinding()]
param(
    [string]$Adb = 'adb',
    [string]$DeviceSerial = '',
    [string]$BackupDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ([string]::IsNullOrWhiteSpace($BackupDirectory)) {
    # Windows PowerShell -File can bind defaults before PSScriptRoot is populated.
    $recoveryScriptDirectory = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($recoveryScriptDirectory)) {
        $recoveryScriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    }
    if ([string]::IsNullOrWhiteSpace($recoveryScriptDirectory)) { throw 'Could not locate recovery script directory.' }
    $BackupDirectory = Join-Path $recoveryScriptDirectory 'Quest-Start-Sicherung'
}
if ($DeviceSerial -and $DeviceSerial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Invalid device serial.' }
$deviceArgs = @()
if ($DeviceSerial) { $deviceArgs = @('-s', $DeviceSerial) }
$package = 'com.generalsx.questvr'
$prefsPath = 'shared_prefs/generalszh_setup.xml'
$markerPath = 'files/gamedata_path.txt'
$utf8 = New-Object System.Text.UTF8Encoding($false, $true)
function Invoke-Adb {
    param([string[]]$Command)
    $result = & $Adb @deviceArgs @Command
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $($Command[0])" }
    return (($result | Out-String).Trim())
}
function Read-Bytes {
    param([string]$Path)
    return ,([Convert]::FromBase64String((Invoke-Adb @('exec-out','run-as',$package,'base64',$Path))))
}
function Hash-Bytes {
    param([byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Remote-Hash {
    param([string]$Path)
    $value = Invoke-Adb @('exec-out','run-as',$package,'sha256sum',$Path)
    if ($value -notmatch '^([a-f0-9]{64})\s') { throw 'Device hash was unavailable.' }
    return $Matches[1]
}
function Remote-Exists {
    param([string]$Path)
    & $Adb @deviceArgs shell run-as $package test -e $Path | Out-Null
    if ($LASTEXITCODE -eq 0) { return $true }
    if ($LASTEXITCODE -eq 1) { return $false }
    throw 'Could not check device file.'
}
function Assert-Closed {
    $running = & $Adb @deviceArgs shell pidof $package 2>$null
    $code = $LASTEXITCODE
    if ($code -notin @(0,1)) { throw 'Could not check app process.' }
    if (($running | Out-String).Trim()) { throw 'Quest-App wurde wieder geoeffnet. Bitte erneut ausfuehren und bis FERTIG geschlossen lassen.' }
}
function Stage-Bytes {
    param([byte[]]$Bytes, [string]$PrivateStage)
    $token = [Guid]::NewGuid().ToString('N')
    $local = Join-Path $backupRoot ('stage-' + $token)
    $remote = '/data/local/tmp/quest-selection-' + $token
    [IO.File]::WriteAllBytes($local, $Bytes)
    Invoke-Adb @('push',$local,$remote) | Out-Null
    Invoke-Adb @('shell','chmod','644',$remote) | Out-Null
    Invoke-Adb @('shell','run-as',$package,'cp',$remote,$PrivateStage) | Out-Null
    if ((Remote-Hash $PrivateStage) -ne (Hash-Bytes $Bytes)) { throw 'Staged file hash mismatch; selection unchanged.' }
    Invoke-Adb @('shell','rm',$remote) | Out-Null
    Remove-Item -LiteralPath $local
}
if ((Invoke-Adb @('get-state')) -ne 'device') { throw 'Quest nicht verbunden oder USB-Debugging nicht erlaubt.' }
Invoke-Adb @('shell','am','force-stop',$package) | Out-Null
Assert-Closed
[byte[]]$original = Read-Bytes $prefsPath
$oldHash = Hash-Bytes $original
$doc = New-Object System.Xml.XmlDocument
$doc.PreserveWhitespace = $true
$doc.LoadXml($utf8.GetString($original))
if ($doc.DocumentElement.Name -ne 'map') { throw 'Unexpected preferences format; nothing changed.' }
$existing = @($doc.SelectNodes('/map/*[@name="game_path"]'))
if ($existing.Count -ne 0) { throw 'Die Zero-Hour-Auswahl ist bereits vorhanden. Nichts geaendert; bitte im Chat melden.' }
$baseNodes = @($doc.SelectNodes('/map/string[@name="base_generals_path"]'))
if ($baseNodes.Count -ne 1) { throw 'Keine eindeutige gespeicherte Base-Generals-Auswahl. Nichts geaendert.' }
$basePath = $baseNodes[0].InnerText
$rootPattern = '^(/data/(?:user/0|data)/com\.generalsx\.questvr/files/Imported/[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12})/ZH_Generals$'
if ($basePath -notmatch $rootPattern) { throw 'Base-Generals-Pfad erlaubt keine eindeutige Wiederherstellung. Nichts geaendert.' }
$gamePath = $Matches[1]
$canonical = Invoke-Adb @('exec-out','run-as',$package,'readlink','-f',$gamePath)
$expectedCanonical = $gamePath -replace '^/data/data/', '/data/user/0/'
if ($canonical -cne $expectedCanonical) { throw 'Imported root is outside the expected private directory; nothing changed.' }
foreach ($archive in @('INIZH.big','TerrainZH.big','TexturesZH.big','W3DZH.big','ZH_Generals/INI.big')) {
    $magic = Invoke-Adb @('exec-out','run-as',$package,'head','-c','4',($gamePath + '/' + $archive))
    if ($magic -cne 'BIGF') { throw "Archive missing or unexpected: $archive. Nothing changed." }
}
if ((Remote-Exists $markerPath) -or (Remote-Exists ($prefsPath + '.bak'))) {
    throw 'Ein Marker oder eine Android-Einstellungssicherung ist noch vorhanden. Nichts geaendert; bitte im Chat melden.'
}
$backupRoot = Join-Path $BackupDirectory ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($backupRoot) | Out-Null
[IO.File]::WriteAllBytes((Join-Path $backupRoot 'generalszh_setup-before.xml'), $original)
$node = $doc.CreateElement('string')
$node.SetAttribute('name', 'game_path')
$node.InnerText = $gamePath
$doc.DocumentElement.AppendChild($node) | Out-Null
$memory = New-Object IO.MemoryStream
$settings = New-Object Xml.XmlWriterSettings
$settings.Encoding = $utf8
$writer = [Xml.XmlWriter]::Create($memory, $settings)
try { $doc.Save($writer); $writer.Flush(); [byte[]]$desired = $memory.ToArray() }
finally { $writer.Dispose(); $memory.Dispose() }
[byte[]]$marker = $utf8.GetBytes($gamePath + "`n")
$token = [Guid]::NewGuid().ToString('N')
$prefsStage = $prefsPath + '.recover-' + $token
$markerStage = $markerPath + '.recover-' + $token
Stage-Bytes $desired $prefsStage
Stage-Bytes $marker $markerStage
Assert-Closed
if ((Remote-Hash $prefsPath) -ne $oldHash -or (Remote-Exists $markerPath) -or (Remote-Exists ($prefsPath + '.bak'))) {
    throw 'Selection changed during preparation. Nothing replaced; retry after closing the app.'
}
Invoke-Adb @('shell','run-as',$package,'mv',$markerStage,$markerPath) | Out-Null
try { Invoke-Adb @('shell','run-as',$package,'mv',$prefsStage,$prefsPath) | Out-Null }
catch {
    if ((Remote-Hash $markerPath) -eq (Hash-Bytes $marker)) {
        Invoke-Adb @('shell','run-as',$package,'rm',$markerPath) | Out-Null
    }
    throw
}
if ((Remote-Hash $prefsPath) -ne (Hash-Bytes $desired) -or (Remote-Hash $markerPath) -ne (Hash-Bytes $marker)) {
    throw 'Restored file did not verify. Retain the backup and report in chat.'
}
[IO.File]::WriteAllText((Join-Path $backupRoot 'result.json'), (@{
    status='SELECTION_RESTORED'; device=$DeviceSerial; oldPrefsSHA256=$oldHash;
    newPrefsSHA256=(Hash-Bytes $desired); markerSHA256=(Hash-Bytes $marker);
    runtime='NOT_VERIFIED'; changed='game_path preference and gamedata_path.txt only'
} | ConvertTo-Json), $utf8)
Write-Host ('Sicherung auf dem PC: ' + $backupRoot)
Write-Host 'FERTIG: Vorhandene Zero-Hour-Spieldaten wieder verknuepft. Alle anderen Einstellungen erhalten. Jetzt App selbst oeffnen und Spiel starten.' -ForegroundColor Green
