$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$global:FixturePath = '/data/user/0/com.generalsx.questvr/files/Imported/1234-abcd/dxvk.conf'
$global:FixtureOriginal = $utf8.GetBytes("# preserve comment`r`nd3d9.samplerAnisotropy = 16`r`n")
$global:MockFiles = @{}
$global:MockRunning = $false
$global:MockWriteFailure = $false
$global:MockWrites = 0
$global:MockStops = 0
$global:MockStopFailure = $false
function Hash-Bytes([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Mock-Adb {
    [CmdletBinding(PositionalBinding = $false)]
    param([Parameter(ValueFromRemainingArguments = $true)][object[]]$Options)
    $global:LASTEXITCODE = 0
    $command = @($Options[2..($Options.Count-1)])
    if ($command[0] -eq 'get-state') { 'device'; return }
    if ($command -contains 'force-stop') {
        if ($command.Count -ne 4 -or $command[-1] -ne 'com.generalsx.questvr') { throw 'Wrong app stop target.' }
        $global:MockStops++
        if ($global:MockStopFailure) { $global:LASTEXITCODE = 1; return }
        $global:MockRunning = $false
        return
    }
    if ($command -contains 'pidof') {
        if ($global:MockRunning) { '1234' } else { $global:LASTEXITCODE = 1 }
        return
    }
    if ($command -contains 'base64') {
        $path = $command[-1]
        if ($path -eq 'shared_prefs/generalszh_setup.xml') {
            [Convert]::ToBase64String($utf8.GetBytes('<map><string name="game_path">/data/user/0/com.generalsx.questvr/files/Imported/1234-abcd</string></map>'))
        } else { [Convert]::ToBase64String($global:MockFiles[$path]) }
        return
    }
    if ($command -contains 'sha256sum') { (Hash-Bytes $global:MockFiles[$command[-1]]) + '  ' + $command[-1]; return }
    if ($command[0] -eq 'push') { $global:MockFiles[$command[2]] = [IO.File]::ReadAllBytes($command[1]); return }
    if ($command -contains 'chmod') { return }
    if ($command -contains 'cp') { $global:MockFiles[$command[-1]] = $global:MockFiles[$command[-2]]; return }
    if ($command -contains 'mv') {
        if ($global:MockWriteFailure) { $global:LASTEXITCODE = 1; return }
        $global:MockFiles[$command[-1]] = $global:MockFiles[$command[-2]]
        $global:MockWrites++
        return
    }
    if ($command -contains 'rm') { $global:MockFiles.Remove($command[-1]); return }
    throw ('Unexpected device operation: ' + ($command -join ' '))
}
$source = Join-Path $PSScriptRoot '../../tooling/quest/set-texture-test.ps1'
$tokens = $null
$parseErrors = $null
[Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$evidence = Join-Path $PSScriptRoot ('../../../work/evidence/texture-helper-mocks/' + [Guid]::NewGuid().ToString('N'))
$global:MockFiles[$global:FixturePath] = $global:FixtureOriginal
& $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Test
$expected = $utf8.GetBytes($utf8.GetString($global:FixtureOriginal) + "d3d9.samplerLodBias = 0.5`n")
if ((Hash-Bytes $global:MockFiles[$global:FixturePath]) -ne (Hash-Bytes $expected)) { throw 'Test payload changed unrelated bytes.' }
& $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Test
if ($global:MockWrites -ne 1) { throw 'Repeated Test wrote again or lost baseline.' }
& $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Restore
if ((Hash-Bytes $global:MockFiles[$global:FixturePath]) -ne (Hash-Bytes $global:FixtureOriginal)) { throw 'Restore was not byte-exact.' }
$global:MockRunning = $true
$blocked = $false
try { & $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'running') } catch { $blocked = $true }
if (-not $blocked -or $global:MockWrites -ne 2) { throw 'Running app was not blocked.' }
if ($global:MockStops -ne 0) { throw 'App was stopped without explicit switch.' }
& $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Test -StopApp
if ($global:MockStops -ne 1 -or $global:MockRunning -or $global:MockWrites -ne 3) { throw 'Explicit app stop failed.' }
& $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Restore
$global:MockRunning = $true
$global:MockStopFailure = $true
$blocked = $false
try { & $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'stopfail') -Mode Test -StopApp } catch { $blocked = $true }
if (-not $blocked -or $global:MockWrites -ne 4) { throw 'Failed app stop allowed config write.' }
$global:MockStopFailure = $false
$global:MockRunning = $false
$global:MockFiles[$global:FixturePath] = $utf8.GetBytes('independent user edit')
$blocked = $false
try { & $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'roundtrip') -Mode Restore } catch { $blocked = $true }
if (-not $blocked -or $utf8.GetString($global:MockFiles[$global:FixturePath]) -ne 'independent user edit') { throw 'User edit was overwritten.' }
$global:MockFiles[$global:FixturePath] = $global:FixtureOriginal
$global:MockWriteFailure = $true
$blocked = $false
try { & $source -Adb Mock-Adb -DeviceSerial MOCK -BackupDirectory (Join-Path $evidence 'writefail') -Mode Test } catch { $blocked = $true }
if (-not $blocked -or (Hash-Bytes $global:MockFiles[$global:FixturePath]) -ne (Hash-Bytes $global:FixtureOriginal)) { throw 'Failed commit changed original or reported success.' }
'PASS: PS parser, exact Test/Restore, repeated Test preserves backup, cached/running app blocked by default, explicit stop targets Quest only, failed stop prevents writes, user edit protected, failed commit rejected. Mock only; no real device operations.'
