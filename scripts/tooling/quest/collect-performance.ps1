# SPDX-License-Identifier: GPL-3.0-or-later
# User-operated read-only retrieval. Never stops/installs/launches/clears logs.
[CmdletBinding()]
param(
    [ValidatePattern('^[a-zA-Z0-9-]{1,48}$')][string]$Label = 'vergleich',
    [string]$Adb = 'adb',
    [string]$DeviceSerial = '',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'Performance-Logs')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ($DeviceSerial -and $DeviceSerial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Ungueltige Geraetekennung.' }
$deviceArgs = @()
if ($DeviceSerial) { $deviceArgs = @('-s', $DeviceSerial) }
function Read-Adb {
    param([string[]]$Command)
    $ErrorActionPreference = 'Continue'
    $lines = & $Adb @deviceArgs @Command 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "ADB-Abfrage fehlgeschlagen ($code). Quest-Verbindung pruefen." }
    return (($lines | Out-String).Trim())
}
$appProcess = Read-Adb @('shell', 'pidof', 'com.generalsx.questvr')
if ($appProcess -notmatch '^([0-9]+)(?:\s+[0-9]+)*$') {
    throw 'Quest-Spiel samt laufendem Skirmish offen lassen und erneut ausfuehren.'
}
$gameProcessId = $Matches[1]
$raw = Read-Adb @('logcat', '-d', ('--pid=' + $gameProcessId), '-b', 'main', '-b', 'system', '-v', 'threadtime')
$selected = @($raw -split '\r?\n' | Where-Object {
    $_ -match '\bGeneralsXR\s*:.*(?:Quest perf (?:choices|interval|stage|reuse|audio):|SMAA edge scratch:)'
})
$choiceLines = @($selected | Where-Object { $_ -match 'Quest perf choices:' })
$windows = @($selected | Where-Object { $_ -match 'Quest perf interval: n=120' }).Count
$complete = $choiceLines.Count -gt 0 -and $windows -ge 3
if (-not (Test-Path -LiteralPath $OutputDirectory)) { New-Item -ItemType Directory -Path $OutputDirectory | Out-Null }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$base = Join-Path $OutputDirectory ('quest-perf-' + $Label + '-' + $stamp)
if (Test-Path -LiteralPath ($base + '.log')) { throw 'Ausgabedatei existiert bereits; nichts ueberschreiben.' }
# Store only whitelisted performance lines, never account/network messages.
$selected | Set-Content -LiteralPath ($base + '.log') -Encoding UTF8
$status = if ($complete) { 'PERFORMANCE_SAMPLES_COLLECTED' } else { 'INSUFFICIENT_SAMPLES' }
[ordered]@{
    status = $status
    label = $Label
    timestamp = (Get-Date).ToUniversalTime().ToString('o')
    processId = $gameProcessId
    windowsOf120Frames = $windows
    choices = $choiceLines
    interpretation = 'Wall intervals include pacing and possible pause/load gaps; not full-world GPU timings or CRC evidence.'
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath ($base + '.json') -Encoding UTF8
Write-Host ('Gesichert: ' + $base + '.log')
if (-not $complete) {
    Write-Host 'Noch keine vollstaendige Vergleichsmessung. Performance-Messwerte im Launcher aktivieren, neu starten und im gleichen Skirmish etwa 60 Sekunden spielen.' -ForegroundColor Yellow
    exit 2
}
Write-Host ('FERTIG: ' + $windows + ' Messfenster gesichert. Jetzt die App schliessen und den naechsten Schalter testen.') -ForegroundColor Green
