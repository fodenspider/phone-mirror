#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Archive,
      [string]$DestinationRoot = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'PhoneMirror/components'))
$ErrorActionPreference = 'Stop'
# Official Google Windows Platform-Tools 37.0.1 ZIP; fail closed if the download changes.
$expected = '45F4D63113E895EBDE0C90F194099A4676B6AC653BD28D54314A9E022BBC1A99'
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $expected) { throw 'Unsupported archive digest.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath($DestinationRoot)
$target = Join-Path $root 'platform-tools-37.0.1'
if (Test-Path -LiteralPath $target) { throw 'Components already exist; existing installation was preserved.' }
$stage = Join-Path $root ('import-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($stage)
$zip = $null
try {
    $zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
    # Fixed entry allowlist: never extract archive-controlled paths.
    foreach ($name in @('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','libwinpthread-1.dll','NOTICE.txt','source.properties')) {
        $entry = $zip.GetEntry('platform-tools/' + $name)
        if ($null -eq $entry -or $entry.Length -gt 32MB) { throw 'Missing or oversized component.' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(Join-Path $stage $name),$false)
    }
    $zip.Dispose(); $zip = $null
    [IO.Directory]::Move($stage,$target)
} finally {
    if ($null -ne $zip) { $zip.Dispose() }
    # Only this freshly generated staging directory may be removed.
    $resolvedStage=[IO.Path]::GetFullPath($stage)
    $resolvedRoot=[IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    if ($resolvedStage.StartsWith($resolvedRoot,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
