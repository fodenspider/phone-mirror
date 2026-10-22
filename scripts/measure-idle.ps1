param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference='Stop'
# Hidden preview only: no device commands and no user preferences are written.
$application=Start-Process -FilePath (Resolve-Path $Executable).Path -ArgumentList '--preview' -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 1
    $application.Refresh()
    if($application.HasExited) { throw 'Preview exited early' }
    $initialCpu=$application.TotalProcessorTime.TotalMilliseconds
    $watch=[Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 10
    $application.Refresh()
    $cpu=$application.TotalProcessorTime.TotalMilliseconds-$initialCpu
    [pscustomobject]@{
        scope='Hidden idle window; no phone, not live mirroring'
        sampleSeconds=[Math]::Round($watch.Elapsed.TotalSeconds,2)
        cpuCorePercent=[Math]::Round($cpu/$watch.Elapsed.TotalMilliseconds*100,3)
        workingSetMiB=[Math]::Round($application.WorkingSet64/1MB,2)
        privateMiB=[Math]::Round($application.PrivateMemorySize64/1MB,2)
    } | ConvertTo-Json
} finally {
    # This process is our isolated idle probe and holds no capture or saved state.
    if(!$application.HasExited) { $application.Kill(); $application.WaitForExit() }
    $application.Dispose()
}
