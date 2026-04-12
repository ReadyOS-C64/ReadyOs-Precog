param(
    [Parameter(Position = 0)]
    [string]$SourceD71,

    [Parameter(Position = 1)]
    [string]$DestD71
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$relFiles = @(
    'cal26.rel',
    'cal26cfg.rel',
    'dizzy.rel',
    'dizzycfg.rel'
)

function Find-DefaultSourceD71 {
    Get-ChildItem -Path '/dev' -Filter '*.d71' -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $SourceD71) {
    $SourceD71 = Find-DefaultSourceD71
    if (-not $SourceD71) {
        throw 'error: no source d71 provided and no /dev/*.d71 found'
    }
}

if (-not $DestD71) {
    $profileTool = Join-Path $PSScriptRoot 'readyos_profiles.py'
    $DestD71 = (& python3 $profileTool 'latest-disk' '--profile' 'precog-dual-d71' '--drive' '8' | Out-String).Trim()
}

if (-not (Test-Path -LiteralPath $SourceD71)) {
    throw ("error: source disk not found: {0}" -f $SourceD71)
}

if (-not (Test-Path -LiteralPath $DestD71)) {
    throw ("error: destination disk not found: {0}" -f $DestD71)
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("readyos_rel_recover.{0}" -f [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

try {
    foreach ($relName in $relFiles) {
        $hostBin = Join-Path $tempDir ("{0}.bin" -f $relName)
        $readOut = (& c1541 $SourceD71 '-read' ("{0},l" -f $relName) $hostBin 2>&1 | Out-String)

        if (-not (Test-Path -LiteralPath $hostBin) -or (Get-Item -LiteralPath $hostBin).Length -le 0) {
            throw ("error: could not read {0} as REL from {1}" -f $relName, $SourceD71)
        }

        $recLenMatch = [regex]::Match($readOut, 'record length ([0-9]+)')
        if (-not $recLenMatch.Success) {
            throw ("error: could not determine REL record length for {0}" -f $relName)
        }

        $recLen = $recLenMatch.Groups[1].Value
        & c1541 $DestD71 '-delete' $relName *> $null
        & c1541 $DestD71 '-write' $hostBin ("{0},l,{1}" -f $relName, $recLen) | Out-Null
        Write-Host ("restored {0} (record length {1})" -f $relName, $recLen)
    }

    Write-Host 'REL recovery complete:'
    Write-Host ("  source: {0}" -f $SourceD71)
    Write-Host ("  dest:   {0}" -f $DestD71)
}
finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
