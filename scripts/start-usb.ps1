#requires -Version 7.0
param(
    [string]$Serial,
    [string]$Adb,
    [string]$Executable,
    [ValidateRange(0,120)][int]$CheckSeconds = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Adb) {
    $bundled = Join-Path $root '.tools/platform-tools/adb.exe'
    $Adb = if (Test-Path -LiteralPath $bundled) { $bundled } else { 'adb' }
}
if (-not $Executable) {
    $name = if ($CheckSeconds) { 'pm-live-check.exe' } else { 'PhoneMirror.exe' }
    $Executable = Join-Path $root "build/$name"
    if (-not (Test-Path -LiteralPath $Executable)) { $Executable = Join-Path $root "build/Release/$name" }
}
if (-not (Test-Path -LiteralPath $Executable)) { throw 'Build the Windows application first, or specify -Executable.' }
function Invoke-AdbText([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Adb; $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($item in $Arguments) { $info.ArgumentList.Add($item) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Could not start ADB.' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) { $process.Kill(); throw 'ADB operation timed out.' }
        if ($process.ExitCode -ne 0) { throw 'ADB operation failed. Check USB authorization and start USB sharing on the phone.' }
        $null = $errorTask.GetAwaiter().GetResult()
        return $outputTask.GetAwaiter().GetResult().Trim()
    } finally { $process.Dispose() }
}
if (-not $Serial) {
    $devices = @( (Invoke-AdbText @('devices')) -split '\r?\n' | ForEach-Object {
        if ($_ -match '^(\S+)\s+device$') { $Matches[1] }
    })
    if ($devices.Count -ne 1) { throw 'Connect exactly one authorized phone, or specify -Serial.' }
    $Serial = $devices[0]
}
$localPort = $null; $process = $null; $session = $null
try {
    # Keep the one-time credential in memory and an inherited pipe, never argv or disk.
    $session = (Invoke-AdbText @('-s',$Serial,'exec-out','run-as','io.github.fodenspider.phonemirror','cat','files/usb-session.json')) | ConvertFrom-Json
    if ($session.version -ne 1 -or $session.port -lt 1 -or $session.port -gt 65535 -or $session.token -cnotmatch '^[0-9a-f]{32}$') {
        throw 'Invalid USB session. Stop sharing on the phone and authorize a new session.'
    }
    $forward = Invoke-AdbText @('-s',$Serial,'forward','tcp:0',"tcp:$($session.port)")
    if ($forward -notmatch '^\d{1,5}$' -or [int]$forward -lt 1 -or [int]$forward -gt 65535) { throw 'Invalid ADB forward port.' }
    $localPort = [int]$forward
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = [IO.Path]::GetFullPath($Executable)
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true; $info.RedirectStandardInput = $true
    if ($CheckSeconds) { $arguments = @('--port',"$localPort",'--seconds',"$CheckSeconds") }
    else { $arguments = @('--usb',"$localPort") }
    foreach ($item in $arguments) { $info.ArgumentList.Add($item) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    if (-not $process.Start()) { throw 'Could not start Windows receiver.' }
    $process.StandardInput.Write($session.token); $process.StandardInput.Close()
    $session = $null
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'Windows receiver failed. Authorize a new session on the phone to retry.' }
} finally {
    $session = $null
    if ($null -ne $localPort) {
        try { $null = Invoke-AdbText @('-s',$Serial,'forward','--remove',"tcp:$localPort") }
        catch { Write-Warning 'Could not remove this USB forward; disconnect the phone to clear it.' }
    }
    if ($null -ne $process) { $process.Dispose() }
}
