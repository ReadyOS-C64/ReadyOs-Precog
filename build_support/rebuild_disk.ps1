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
                @((Assert-RepoFile 'preboot.prg'), 'preboot'),
                @((Assert-RepoFile 'setd71.prg'), 'setd71'),
                @((Assert-RepoFile 'showcfg.prg'), 'showcfg'),
                @((Assert-RepoFile 'boot.prg'), 'boot'),
                @((Assert-RepoFile 'launcher.prg'), 'launcher'),
                @((Assert-RepoFile 'deminer.prg'), 'deminer'),
                @((Assert-RepoFile 'cal26.prg'), 'cal26'),
                @((Assert-RepoFile 'dizzy.prg'), 'dizzy'),
                @((Assert-RepoFile 'readyshell.prg'), 'readyshell'),
                @((Assert-RepoFile 'obj/readyshell_ovl1.prg'), 'rsovl1'),
                @((Assert-RepoFile 'obj/readyshell_ovl2.prg'), 'rsovl2'),
                @((Assert-RepoFile 'obj/readyshell_ovl3.prg'), 'rsovl3'),
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
                @((Assert-RepoFile 'editor.prg'), 'editor'),
                @((Assert-RepoFile 'calcplus.prg'), 'calcplus'),
                @((Assert-RepoFile 'hexview.prg'), 'hexview'),
                @((Assert-RepoFile 'clipmgr.prg'), 'clipmgr'),
                @((Assert-RepoFile 'reuviewer.prg'), 'reuviewer'),
                @((Assert-RepoFile 'tasklist.prg'), 'tasklist'),
                @((Assert-RepoFile 'simplefiles.prg'), 'simplefiles'),
                @((Assert-RepoFile 'simplecells.prg'), 'simplecells'),
                @((Assert-RepoFile 'game2048.prg'), 'game2048'),
                @((Assert-RepoFile 'readme.prg'), 'readme')
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
