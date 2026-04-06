param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('backup', 'restore')]
    [string]$Mode,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Disk,

    [Parameter(Mandatory = $true, Position = 2)]
    [string]$Target
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$managedPrgs = @(
    'preboot',
    'prebootraw',
    'setd71',
    'showcfg',
    'boot',
    'launcher',
    'editor',
    'calcplus',
    'hexview',
    'clipmgr',
    'reuviewer',
    'tasklist',
    'game2048',
    'deminer',
    'cal26',
    'dizzy',
    'readme',
    'readyshell',
    'rsovl1',
    'rsovl2',
    'rsovl3',
    'ovl1',
    'ovl2',
    'ovl3',
    'test reu'
)

$managedSeqs = @(
    'apps.cfg',
    'editor help',
    'example tasks',
    'c',
    'b',
    'test'
)

function Test-ManagedBuildFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Type
    )

    switch ($Type) {
        'prg' { return $managedPrgs -contains $Name }
        'seq' { return $managedSeqs -contains $Name }
        default { return $false }
    }
}

function Get-FileLength {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return 0
    }
    return (Get-Item -LiteralPath $Path).Length
}

function Invoke-BackupMode {
    param([string]$OutDir)

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

    $listing = Join-Path $OutDir 'listing.txt'
    $manifest = Join-Path $OutDir 'manifest.tsv'
    Set-Content -Path $manifest -Value $null -Encoding ascii

    & c1541 $Disk '-list' 2>$null | Set-Content -Path $listing -Encoding ascii

    $idx = 0
    foreach ($line in Get-Content -Path $listing) {
        if ($line -notmatch '"') {
            continue
        }

        $nameMatch = [regex]::Match($line, '"([^"]+)"')
        if (-not $nameMatch.Success) {
            continue
        }

        $name = $nameMatch.Groups[1].Value
        $typeMatch = [regex]::Match($line, '\s([A-Za-z]+)\s*$')
        if (-not $typeMatch.Success) {
            continue
        }

        $type = $typeMatch.Groups[1].Value.ToLowerInvariant()
        if ($type -notin @('prg', 'seq', 'rel', 'usr')) {
            continue
        }

        $nameLc = $name.ToLowerInvariant()
        if (Test-ManagedBuildFile -Name $nameLc -Type $type) {
            continue
        }

        $idx += 1
        $hostFile = Join-Path $OutDir ("file_{0}.bin" -f $idx)

        if ($type -eq 'rel') {
            $relOut = (& c1541 $Disk '-read' ("{0},l" -f $name) $hostFile 2>&1 | Out-String)
            if ((Get-FileLength -Path $hostFile) -le 0) {
                Remove-Item -LiteralPath $hostFile -ErrorAction SilentlyContinue
                continue
            }

            $recLenMatch = [regex]::Match($relOut, 'record length ([0-9]+)')
            if (-not $recLenMatch.Success) {
                Remove-Item -LiteralPath $hostFile -ErrorAction SilentlyContinue
                continue
            }

            Add-Content -Path $manifest -Value ("{0}`t{1}`t{2}`t{3}" -f $name, $type, $recLenMatch.Groups[1].Value, $hostFile) -Encoding ascii
            continue
        }

        $readSpec = $name
        switch ($type) {
            'seq' { $readSpec = "{0},s" -f $name }
            'usr' { $readSpec = "{0},u" -f $name }
            'prg' { $readSpec = "{0},p" -f $name }
        }

        & c1541 $Disk '-read' $readSpec $hostFile *> $null
        if ($LASTEXITCODE -ne 0) {
            Remove-Item -LiteralPath $hostFile -ErrorAction SilentlyContinue
            continue
        }

        Add-Content -Path $manifest -Value ("{0}`t{1}`t0`t{2}" -f $name, $type, $hostFile) -Encoding ascii
    }
}

function Invoke-RestoreMode {
    param([string]$Manifest)

    if (-not (Test-Path -LiteralPath $Manifest)) {
        return
    }

    foreach ($line in Get-Content -Path $Manifest) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line -split "`t", 4
        if ($parts.Count -lt 4) {
            continue
        }

        $name = $parts[0]
        $type = $parts[1]
        $recLen = $parts[2]
        $hostFile = $parts[3]

        if (-not (Test-Path -LiteralPath $hostFile)) {
            continue
        }

        & c1541 $Disk '-delete' $name *> $null

        switch ($type) {
            'rel' { $writeSpec = "{0},l,{1}" -f $name, $recLen }
            'seq' { $writeSpec = "{0},s" -f $name }
            'usr' { $writeSpec = "{0},u" -f $name }
            default { $writeSpec = $name }
        }

        & c1541 $Disk '-write' $hostFile $writeSpec *> $null
    }
}

switch ($Mode) {
    'backup' { Invoke-BackupMode -OutDir $Target }
    'restore' { Invoke-RestoreMode -Manifest $Target }
}
