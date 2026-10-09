# User-operated Quest-only capture. Does not install, launch or change AA settings.
param(
    [string]$Adb = 'adb',
    [string]$DeviceSerial = '',
    [ValidateRange(0,60)][int]$Countdown = 15
)
$ErrorActionPreference = 'Stop'
$deviceArgs = @()
if ($DeviceSerial) { $deviceArgs = @('-s', $DeviceSerial) }
function Read-CaptureStatus {
    try {
        $value = (& $Adb @deviceArgs exec-out run-as com.generalsx.questvr cat files/quest-eye-capture.status 2>$null | Out-String).Trim()
        if ($value) { return ($value | ConvertFrom-Json) }
    } catch { return $null } # A first-ever capture has no status file yet.
    return $null
}
$appPid = (& $Adb @deviceArgs shell pidof com.generalsx.questvr | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $appPid) { throw 'Quest-Spiel starten und einen Skirmish laden.' }
$previous = Read-CaptureStatus
Write-Host "In $Countdown Sekunden beginnt die Bilddiagnose. Headset aufsetzen, die flimmernde Stelle ansehen und den Stick loslassen. Danach etwa 10 Sekunden ruhig schauen."
Start-Sleep -Seconds $Countdown
'capture' | & $Adb @deviceArgs shell run-as com.generalsx.questvr tee files/quest-eye-capture.request | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Diagnoseanforderung konnte nicht geschrieben werden.' }
for ($attempt = 0; $attempt -lt 60; $attempt++) {
    Start-Sleep -Seconds 1
    $result = Read-CaptureStatus
    if (-not $result -or ($previous -and $result.session -eq $previous.session)) { continue }
    if ($result.state -eq 'CAPTURING') { continue }
    if ($result.state -eq 'COMPLETE' -and $result.writtenFrames -eq 8) {
        Write-Host "Fertig: $($result.writtenFrames)/8 Bildpaare auf der Quest gespeichert. Sitzung: $($result.session)"
        Write-Host 'Bitte im Chat fertig melden. Die Daten werden danach gesichert.'
        exit 0
    }
    throw "Diagnose nicht abgeschlossen: $($result.state), $($result.writtenFrames)/8. Bitte den Wortlaut im Chat melden."
}
throw 'Noch kein fertiges Diagnoseergebnis. Bitte den Skirmish geöffnet lassen und im Chat melden; nicht mehrfach starten.'
