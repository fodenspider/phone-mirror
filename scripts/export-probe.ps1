param(
    [Parameter(Mandatory=$true)][string]$Serial,
    [string]$Adb = 'adb',
    [string]$OutputPath = 'capture.pmstream'
)
$ErrorActionPreference = 'Stop'
$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $resolvedOutput) { throw "Output already exists: $resolvedOutput. Choose another -OutputPath." }
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $Adb
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
foreach ($argument in @('-s', $Serial, 'exec-out', 'run-as', 'io.github.fodenspider.phonemirror', 'cat', 'files/capture.pmstream')) {
    $info.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::new()
$process.StartInfo = $info
$stream = $null
try {
    $stream = [IO.File]::Open($resolvedOutput, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    if (-not $process.Start()) { throw 'Could not start ADB.' }
    $errorTask = $process.StandardError.ReadToEndAsync()
    $process.StandardOutput.BaseStream.CopyTo($stream)
    $process.WaitForExit()
    $errorText = $errorTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "ADB failed: $errorText. Partial output may exist at $resolvedOutput." }
    Write-Output "Exported: $resolvedOutput"
} finally {
    if ($null -ne $stream) { $stream.Dispose() }
    $process.Dispose()
}
