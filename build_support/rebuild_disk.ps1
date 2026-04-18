param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('disk1', 'disk2')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$DiskPath,

    [Parameter(Mandatory = $true)]
    [string]$BuildSupportDir,

    [string]$RelSeedD71 = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildSupportPath = if ([System.IO.Path]::IsPathRooted($BuildSupportDir)) { $BuildSupportDir } else { Join-Path $repoRoot $BuildSupportDir }
$preserveScript = Join-Path $buildSupportPath 'preserve_d71_user_data.ps1'
$recoverScript = Join-Path $buildSupportPath 'recover_cal26_rel_from_d71.ps1'
$seedScript = Join-Path $buildSupportPath 'seed_cal26_rel.py'

function New-TempDir {
    param([string]$Prefix)
    $path = Join-Path ([System.IO.Path]::GetTempPath()) ("{0}{1}" -f $Prefix, [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

function Assert-RepoFile {
    param([string]$RelativePath)
    $fullPath = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw ("required file not found: {0}" -f $fullPath)
    }
    return $fullPath
}

function Invoke-C1541WriteBatch {
    param(
        [string]$DiskLabel,
        [string[][]]$Mappings
    )

    $args = @('-format', $DiskLabel, 'd71', $DiskPath)
    foreach ($mapping in $Mappings) {
        $args += @('-write', $mapping[0], $mapping[1])
    }
    & c1541 @args
}

$preserveDir = New-TempDir -Prefix ("readyos_{0}_" -f $Mode)

try {
    if (Test-Path -LiteralPath $DiskPath) {
        & pwsh -NoLogo -NoProfile -File $preserveScript backup $DiskPath $preserveDir
    }

    switch ($Mode) {
        'disk1' {
            $mappings = @(
                @((Assert-RepoFile 'bin/preboot.prg'), 'preboot'),
                @((Assert-RepoFile 'bin/setd71.prg'), 'setd71'),
                @((Assert-RepoFile 'bin/showcfg.prg'), 'showcfg'),
                @((Assert-RepoFile 'bin/boot.prg'), 'boot'),
                @((Assert-RepoFile 'bin/launcher.prg'), 'launcher'),
                @((Assert-RepoFile 'bin/quicknotes.prg'), 'quicknotes'),
                @((Assert-RepoFile 'bin/deminer.prg'), 'deminer'),
                @((Assert-RepoFile 'bin/cal26.prg'), 'cal26'),
                @((Assert-RepoFile 'bin/dizzy.prg'), 'dizzy'),
                @((Assert-RepoFile 'bin/readyshell.prg'), 'readyshell'),
                @((Assert-RepoFile 'obj/rsparser.prg'), 'rsparser'),
                @((Assert-RepoFile 'obj/rsvm.prg'), 'rsvm'),
                @((Assert-RepoFile 'obj/rsdrvilst.prg'), 'rsdrvilst'),
                @((Assert-RepoFile 'obj/rsldv.prg'), 'rsldv'),
                @((Assert-RepoFile 'obj/rsstv.prg'), 'rsstv'),
                @((Assert-RepoFile 'obj/rsfops.prg'), 'rsfops'),
                @((Assert-RepoFile 'obj/rscat.prg'), 'rscat'),
                @((Assert-RepoFile 'obj/rscopy.prg'), 'rscopy'),
                @((Assert-RepoFile 'obj/apps_cfg_petscii.seq'), 'apps.cfg,s'),
                @((Assert-RepoFile 'obj/editor_help.seq'), 'editor help,s')
            )

            Invoke-C1541WriteBatch -DiskLabel 'readyos,ro' -Mappings $mappings
            Write-Host ''
            Write-Host 'Seeding CAL26 REL files:'
            & python3 $seedScript '--disk' $DiskPath

            $manifest = Join-Path $preserveDir 'manifest.tsv'
            if ((Test-Path -LiteralPath $manifest -PathType Leaf) -and ((Get-Item -LiteralPath $manifest).Length -gt 0)) {
                & pwsh -NoLogo -NoProfile -File $preserveScript restore $DiskPath $manifest
            }

            if ($RelSeedD71 -and (Test-Path -LiteralPath $RelSeedD71)) {
                Write-Host ("Restoring REL files from {0} ..." -f $RelSeedD71)
                & pwsh -NoLogo -NoProfile -File $recoverScript $RelSeedD71 $DiskPath
            }
            elseif ($RelSeedD71) {
                Write-Host ("warning: REL donor disk not found: {0}" -f $RelSeedD71)
            }
        }

        'disk2' {
            $mappings = @(
                @((Assert-RepoFile 'bin/editor.prg'), 'editor'),
                @((Assert-RepoFile 'bin/calcplus.prg'), 'calcplus'),
                @((Assert-RepoFile 'bin/hexview.prg'), 'hexview'),
                @((Assert-RepoFile 'bin/clipmgr.prg'), 'clipmgr'),
                @((Assert-RepoFile 'bin/reuviewer.prg'), 'reuviewer'),
                @((Assert-RepoFile 'bin/tasklist.prg'), 'tasklist'),
                @((Assert-RepoFile 'bin/simplefiles.prg'), 'simplefiles'),
                @((Assert-RepoFile 'bin/simplecells.prg'), 'simplecells'),
                @((Assert-RepoFile 'bin/game2048.prg'), 'game2048'),
                @((Assert-RepoFile 'bin/readme.prg'), 'readme')
            )

            Invoke-C1541WriteBatch -DiskLabel 'readyos2,ro' -Mappings $mappings

            $manifest = Join-Path $preserveDir 'manifest.tsv'
            if ((Test-Path -LiteralPath $manifest -PathType Leaf) -and ((Get-Item -LiteralPath $manifest).Length -gt 0)) {
                & pwsh -NoLogo -NoProfile -File $preserveScript restore $DiskPath $manifest
            }
        }
    }

    Write-Host ''
    Write-Host 'Disk contents:'
    & c1541 $DiskPath '-list'
}
finally {
    Remove-Item -LiteralPath $preserveDir -Recurse -Force -ErrorAction SilentlyContinue
}
