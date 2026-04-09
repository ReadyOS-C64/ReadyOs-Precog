Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-BuildSupportDir {
    if ($env:BUILD_SUPPORT_DIR -and (Test-Path -LiteralPath $env:BUILD_SUPPORT_DIR -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:BUILD_SUPPORT_DIR).Path
    }
    if (Test-Path -LiteralPath 'build_support' -PathType Container) {
        return (Resolve-Path -LiteralPath 'build_support').Path
    }
    if ($env:TOOLS_DIR -and (Test-Path -LiteralPath $env:TOOLS_DIR -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:TOOLS_DIR).Path
    }
    if (Test-Path -LiteralPath 'tools' -PathType Container) {
        return (Resolve-Path -LiteralPath 'tools').Path
    }
    if (Test-Path -LiteralPath '../agenticdevharness/tools' -PathType Container) {
        return (Resolve-Path -LiteralPath '../agenticdevharness/tools').Path
    }
    throw 'Error: ReadyOS build support directory not found.'
}

function Configure-ViceEnv {
    if (-not $IsMacOS) {
        return
    }

    $schemaDir = '/opt/homebrew/share/glib-2.0/schemas'
    if (-not $env:GSETTINGS_SCHEMA_DIR -and (Test-Path -LiteralPath (Join-Path $schemaDir 'gschemas.compiled'))) {
        $env:GSETTINGS_SCHEMA_DIR = $schemaDir
    }

    $shareDir = '/opt/homebrew/share'
    if (-not (Test-Path -LiteralPath $shareDir -PathType Container)) {
        return
    }

    if ($env:XDG_DATA_DIRS) {
        if (-not (":{0}:" -f $env:XDG_DATA_DIRS).Contains((":{0}:" -f $shareDir))) {
            $env:XDG_DATA_DIRS = '{0}:{1}' -f $shareDir, $env:XDG_DATA_DIRS
        }
    }
    else {
        $env:XDG_DATA_DIRS = $shareDir
    }
}

function Resolve-CommandPath {
    param(
        [string[]]$Candidates,
        [string]$ErrorMessage
    )

    foreach ($candidate in $Candidates) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    throw $ErrorMessage
}

function Invoke-PythonText {
    param([string[]]$Args)
    $result = & $script:PythonExe @Args
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed: python3 {0}" -f ($Args -join ' '))
    }
    return ($result | Out-String).Trim()
}

function Get-ProfileManifest {
    param(
        [string]$ProfileId,
        [string]$VersionText,
        [switch]$Latest
    )

    $args = @($script:ProfileTool, 'resolve', '--profile', $ProfileId)
    if ($Latest) {
        $args += '--latest'
    }
    else {
        $args += @('--version', $VersionText)
    }
    return (Invoke-PythonText -Args $args | ConvertFrom-Json)
}

function Get-ViceAttachArgs {
    param($Manifest)

    $args = @()
    foreach ($disk in $Manifest.disks) {
        $drive = [string]$disk.drive
        $args += @("-drive${drive}type", [string]$disk.vice_drive_type)
        if ($disk.true_drive) {
            $args += "-drive${drive}truedrive"
        }
        $args += @("-devicebackend${drive}", '0', "+busdevice${drive}", "-${drive}", [string]$disk.path)
    }
    return $args
}

function Show-Help {
    @"
ReadyOS Run Script (PowerShell)

Usage: ./run.ps1 [flags] [option]

Modes:
  (none)         Run ReadyOS normally
  test           Run REU test program standalone
  debug          Run with VICE monitor breakpoints
  warp           Run in warp mode
  launcher       Run launcher.prg directly
  editor         Run editor.prg directly
  calcplus       Run calcplus.prg directly
  hexview        Run hexview.prg directly
  2048           Run game2048.prg directly
  deminer        Run deminer.prg directly
  cal26          Run cal26.prg directly
  dizzy          Run dizzy.prg directly
  readme         Run readme.prg directly
  showcfg        Run BASIC APPS.CFG inspector
  xfilechk       Run standalone IEC file-operation harness
  monitor        Start with VICE monitor open
  readyshell-mon Normal boot with remote monitor endpoints
  noreu          Run boot without REU

Flags:
  --profile ID
  --list-profiles
  --build-all
  --skipbuild
  --config PATH
  --load-all 0|1
  --run-first APP
  --parse-trace-debug 0|1
  --interactive
"@ | Write-Host
}

function Validate-ParseTraceDebug {
    param([string]$Value)
    if ($Value -notin @('0', '1')) {
        throw ("Error: --parse-trace-debug must be 0 or 1 (got '{0}')." -f $Value)
    }
}

function Validate-LoadAll {
    param([string]$Value)
    if ($Value -notin @('0', '1')) {
        throw ("Error: --load-all must be 0 or 1 (got '{0}')." -f $Value)
    }
}

function Validate-RunFirst {
    param([string]$Value)
    if ($Value -notmatch '^[a-z0-9_.-]+$') {
        throw ("Error: --run-first must be a lowercase prg token (got '{0}')." -f $Value)
    }
    if ($Value.EndsWith('.prg')) {
        throw ("Error: --run-first must not include .prg (got '{0}')." -f $Value)
    }
    if ($Value.Length -gt 12) {
        throw ("Error: --run-first must be 12 characters or fewer (got '{0}')." -f $Value)
    }
}

function Current-ParseTraceLabel {
    switch ($script:ParseTraceDebug) {
        '1' { return 'debug-trace (READYSHELL_OVERLAYSIZE=0x2480, __OVERLAYSTART__=0xA180)' }
        default { return 'release/default (READYSHELL_OVERLAYSIZE=0x2440, __OVERLAYSTART__=0xA1C0)' }
    }
}

function Assert-FileExists {
    param([string]$Path, [string]$Kind)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw ("Error: {0} not found: {1}" -f $Kind, $Path)
    }
}

function Print-Info {
    param([string]$RunMode, [string]$Target)
    Write-Host '=== Ready OS ==='
    Write-Host ''
    Write-Host ("Mode: {0}" -f $RunMode)
    Write-Host ("VICE: {0}" -f $script:ViceName)
    Write-Host ("Profile: {0}" -f $script:ProfileManifest.display_name)
    Write-Host ("Target: {0}" -f $Target)
    foreach ($disk in $script:ProfileManifest.disks) {
        Write-Host ("Drive {0}: {1}" -f $disk.drive, $disk.path)
    }
    Write-Host ("Build Support: {0}" -f $script:BuildSupportDir)
    Write-Host ("ReadyShell parse trace: {0}" -f (Current-ParseTraceLabel))
    Write-Host ''
}

function Start-ViceProcess {
    param(
        [string[]]$Arguments,
        [string]$RedirectPath = ''
    )

    if ($IsMacOS) {
        $quoteArg = {
            param([string]$Value)
            return "'" + ($Value -replace "'", "'""'""'") + "'"
        }

        $commandParts = @('exec', (& $quoteArg $script:ViceExe))
        foreach ($arg in $Arguments) {
            $commandParts += (& $quoteArg $arg)
        }
        if ($RedirectPath) {
            $commandParts += @('>', (& $quoteArg $RedirectPath), '2>&1')
        }
        Start-Process -FilePath '/bin/sh' -ArgumentList @('-lc', ($commandParts -join ' ')) | Out-Null
        return
    }

    $startArgs = @{
        FilePath = $script:ViceExe
        ArgumentList = $Arguments
    }
    if ($RedirectPath) {
        $startArgs.RedirectStandardOutput = $RedirectPath
        $startArgs.RedirectStandardError = $RedirectPath
    }
    Start-Process @startArgs | Out-Null
}

function Run-InteractiveMenu {
    @'
ReadyOS interactive launcher

  [1] Normal run (release profile build)
  [2] Normal run (debug-trace profile build)
  [3] Debug monitor mode (release profile build)
  [4] Debug monitor mode (debug-trace profile build)
  [5] Readyshell remote monitor (debug-trace profile build)
  [6] Normal run (skip build, keep current binaries)
  [q] Quit
'@ | Write-Host

    $choice = Read-Host 'Choice'
    switch ($choice) {
        '1' { $script:Mode = ''; $script:ParseTraceDebug = '0'; $script:SkipBuild = $false }
        '2' { $script:Mode = ''; $script:ParseTraceDebug = '1'; $script:SkipBuild = $false }
        '3' { $script:Mode = 'debug'; $script:ParseTraceDebug = '0'; $script:SkipBuild = $false }
        '4' { $script:Mode = 'debug'; $script:ParseTraceDebug = '1'; $script:SkipBuild = $false }
        '5' { $script:Mode = 'readyshell-mon'; $script:ParseTraceDebug = '1'; $script:SkipBuild = $false }
        '6' { $script:Mode = ''; $script:SkipBuild = $true }
        'q' { exit 0 }
        'Q' { exit 0 }
        default { throw ("Invalid selection: '{0}'" -f $choice) }
    }
}

function Maybe-Build {
    if ($script:BuildAll) {
        $script:RunVersionText = Invoke-PythonText -Args @($script:VersionTool, '--next')
        Write-Host ("Build version: {0}" -f $script:RunVersionText)
        Write-Host 'Building all release profiles'
        Write-Host ("ReadyShell parse trace profile: {0}" -f (Current-ParseTraceLabel))
        & $script:MakeExe '-B' "BUILD_SUPPORT_DIR=$($script:BuildSupportDir)" "READYOS_VERSION_TEXT=$($script:RunVersionText)" 'release-all'
        if ($LASTEXITCODE -ne 0) {
            throw 'Build failed.'
        }
        return
    }

    if ($script:SkipBuild) {
        $script:ProfileManifest = Get-ProfileManifest -ProfileId $script:ProfileId -Latest
        return
    }

    $script:RunVersionText = Invoke-PythonText -Args @($script:VersionTool, '--next')
    Write-Host ("Build version: {0}" -f $script:RunVersionText)
    Write-Host ("Profile: {0}" -f $script:ProfileId)
    Write-Host ("ReadyShell parse trace profile: {0}" -f (Current-ParseTraceLabel))

    $makeArgs = @('-B', "BUILD_SUPPORT_DIR=$($script:BuildSupportDir)", "PROFILE=$($script:ProfileId)", "READYOS_VERSION_TEXT=$($script:RunVersionText)")
    if ($script:ConfigSource) {
        $makeArgs += "READYOS_CONFIG_SRC=$($script:ConfigSource)"
    }
    if ($script:ConfigLoadAll) {
        $makeArgs += "READYOS_CONFIG_LOAD_ALL=$($script:ConfigLoadAll)"
    }
    if ($script:ConfigRunFirst) {
        $makeArgs += "READYOS_CONFIG_RUN_FIRST=$($script:ConfigRunFirst)"
    }
    $makeArgs += 'profile'

    & $script:MakeExe @makeArgs
    if ($LASTEXITCODE -ne 0) {
        throw 'Build failed.'
    }

    $script:ProfileManifest = Get-ProfileManifest -ProfileId $script:ProfileId -VersionText $script:RunVersionText
}

$script:RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $script:RepoRoot

$script:BuildSupportDir = Resolve-BuildSupportDir
Configure-ViceEnv

$script:PythonExe = Resolve-CommandPath -Candidates @('python3', 'python') -ErrorMessage 'Error: python3/python not found in PATH'
$script:ProfileTool = Join-Path $script:BuildSupportDir 'readyos_profiles.py'
$script:VersionTool = Join-Path $script:BuildSupportDir 'update_build_version.py'
$script:DefaultProfile = Invoke-PythonText -Args @($script:ProfileTool, 'default-id')
$script:ProfileId = $script:DefaultProfile

$script:ViceExe = Resolve-CommandPath -Candidates @('x64sc', 'x64') -ErrorMessage 'Error: VICE emulator not found (tried x64sc, x64)'
$script:ViceName = Split-Path -Leaf $script:ViceExe
$script:MakeExe = Resolve-CommandPath -Candidates @('make', 'gmake') -ErrorMessage 'Error: make/gmake not found in PATH'

$script:PrebootPrg = 'preboot.prg'
$script:TestPrg = 'test_reu.prg'
$script:LauncherPrg = 'launcher.prg'
$script:EditorPrg = 'editor.prg'
$script:CalcplusPrg = 'calcplus.prg'
$script:HexviewPrg = 'hexview.prg'
$script:Game2048Prg = 'game2048.prg'
$script:DeminerPrg = 'deminer.prg'
$script:Cal26Prg = 'cal26.prg'
$script:DizzyPrg = 'dizzy.prg'
$script:ReadmePrg = 'readme.prg'
$script:ShowcfgPrg = 'showcfg.prg'
$script:XFilechkBootPrg = 'xfilechk_boot.prg'
$script:XFilechkPrg = 'xfilechk.prg'
$script:XFilechkDisk1 = 'xfilechk.d71'
$script:XFilechkDisk2 = 'xfilechk_2.d71'
$script:RemoteMonAddr = '127.0.0.1:6510'
$script:BinaryMonAddr = '127.0.0.1:6502'
$script:RemoteMonLog = 'logs/vice_remote_monitor.log'
$script:ViceStdioLog = 'logs/vice_readyshell_mon.out'
$script:ViceLogFile = 'logs/vice.log'

$script:SkipBuild = $false
$script:BuildAll = $false
$script:Mode = ''
$script:ParseTraceDebug = '0'
$script:ConfigSource = ''
$script:ConfigLoadAll = ''
$script:ConfigRunFirst = ''
$interactive = $false
$listProfiles = $false

for ($i = 0; $i -lt $args.Count; ) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^-h$|^--help$' {
            Show-Help
            exit 0
        }
        '^--profile$' {
            $script:ProfileId = $args[$i + 1]
            $i += 2
        }
        '^--profile=(.+)$' {
            $script:ProfileId = $Matches[1]
            $i += 1
        }
        '^--list-profiles$' {
            $listProfiles = $true
            $i += 1
        }
        '^--build-all$' {
            $script:BuildAll = $true
            $i += 1
        }
        '^--skipbuild$' {
            $script:SkipBuild = $true
            $i += 1
        }
        '^--config$' {
            $script:ConfigSource = $args[$i + 1]
            $i += 2
        }
        '^--config=(.+)$' {
            $script:ConfigSource = $Matches[1]
            $i += 1
        }
        '^--load-all$' {
            $script:ConfigLoadAll = $args[$i + 1]
            Validate-LoadAll $script:ConfigLoadAll
            $i += 2
        }
        '^--load-all=(.+)$' {
            $script:ConfigLoadAll = $Matches[1]
            Validate-LoadAll $script:ConfigLoadAll
            $i += 1
        }
        '^--run-first$' {
            $script:ConfigRunFirst = $args[$i + 1]
            Validate-RunFirst $script:ConfigRunFirst
            $i += 2
        }
        '^--run-first=(.+)$' {
            $script:ConfigRunFirst = $Matches[1]
            Validate-RunFirst $script:ConfigRunFirst
            $i += 1
        }
        '^--parse-trace-debug$' {
            $script:ParseTraceDebug = $args[$i + 1]
            Validate-ParseTraceDebug $script:ParseTraceDebug
            $i += 2
        }
        '^--parse-trace-debug=(.+)$' {
            $script:ParseTraceDebug = $Matches[1]
            Validate-ParseTraceDebug $script:ParseTraceDebug
            $i += 1
        }
        '^--interactive$' {
            $interactive = $true
            $i += 1
        }
        default {
            if ($script:Mode) {
                throw ("Unknown option: {0}" -f $arg)
            }
            $script:Mode = $arg
            $i += 1
        }
    }
}

if ($listProfiles) {
    Invoke-PythonText -Args @($script:ProfileTool, 'list-ids') | Write-Host
    exit 0
}

if ($script:BuildAll -and $script:SkipBuild) {
    throw 'Error: --build-all cannot be combined with --skipbuild.'
}

if ($script:BuildAll -and $script:Mode) {
    throw ("Error: --build-all cannot be combined with an explicit mode ('{0}')." -f $script:Mode)
}

if ($script:SkipBuild -and ($script:ConfigSource -or $script:ConfigLoadAll -or $script:ConfigRunFirst)) {
    throw 'Error: --skipbuild cannot be combined with --config, --load-all, or --run-first.'
}

if ($script:ConfigSource -and -not (Test-Path -LiteralPath $script:ConfigSource)) {
    throw ("Error: config source not found: {0}" -f $script:ConfigSource)
}

if ($interactive) {
    if ($script:Mode) {
        throw ("Error: --interactive cannot be combined with an explicit mode ('{0}')." -f $script:Mode)
    }
    Run-InteractiveMenu
}

if ($script:Mode -ne 'xfilechk' -and $script:Mode -notin @('help', '-h', '--help')) {
    Maybe-Build
}

if ($script:BuildAll) {
    exit 0
}

$script:ViceAttachArgs = if ($script:ProfileManifest) { Get-ViceAttachArgs -Manifest $script:ProfileManifest } else { @() }

switch ($script:Mode) {
    { $_ -in @('help', '-h', '--help') } {
        Show-Help
        exit 0
    }

    'test' {
        Assert-FileExists -Path $script:TestPrg -Kind 'Program file'
        Print-Info -RunMode 'REU Test' -Target $script:TestPrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:TestPrg)
    }

    'debug' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        Print-Info -RunMode 'Debug' -Target $script:ProfileManifest.autostart_prg
        $debugFile = Join-Path ([System.IO.Path]::GetTempPath()) ("vice_debug_{0}.cmd" -f [System.Guid]::NewGuid().ToString('N'))
        try {
            Set-Content -LiteralPath $debugFile -Value @('break C809', 'break C80c', 'break C80f') -Encoding ascii
            Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-moncommands', $debugFile, '-autostart', [string]$script:ProfileManifest.autostart_prg))
        }
        finally {
            Remove-Item -LiteralPath $debugFile -Force -ErrorAction SilentlyContinue
        }
    }

    'warp' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        Print-Info -RunMode 'Warp Mode' -Target $script:ProfileManifest.autostart_prg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-warp', '-autostart', [string]$script:ProfileManifest.autostart_prg))
    }

    'launcher' {
        Assert-FileExists -Path $script:LauncherPrg -Kind 'Program file'
        Print-Info -RunMode 'Direct Launch' -Target $script:LauncherPrg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-autostartprgmode', '1', $script:LauncherPrg))
    }

    'editor' {
        Assert-FileExists -Path $script:EditorPrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:EditorPrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:EditorPrg)
    }

    'calcplus' {
        Assert-FileExists -Path $script:CalcplusPrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:CalcplusPrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:CalcplusPrg)
    }

    'hexview' {
        Assert-FileExists -Path $script:HexviewPrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:HexviewPrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:HexviewPrg)
    }

    '2048' {
        Assert-FileExists -Path $script:Game2048Prg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:Game2048Prg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:Game2048Prg)
    }

    'deminer' {
        Assert-FileExists -Path $script:DeminerPrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:DeminerPrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:DeminerPrg)
    }

    'cal26' {
        Assert-FileExists -Path $script:Cal26Prg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:Cal26Prg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-autostartprgmode', '1', $script:Cal26Prg))
    }

    'dizzy' {
        Assert-FileExists -Path $script:DizzyPrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:DizzyPrg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-autostartprgmode', '1', $script:DizzyPrg))
    }

    'readme' {
        Assert-FileExists -Path $script:ReadmePrg -Kind 'Program file'
        Print-Info -RunMode 'Standalone' -Target $script:ReadmePrg
        Start-ViceProcess @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384', '-autostartprgmode', '1', $script:ReadmePrg)
    }

    'showcfg' {
        Assert-FileExists -Path $script:ShowcfgPrg -Kind 'Program file'
        Print-Info -RunMode 'Catalog Inspector' -Target $script:ShowcfgPrg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-autostartprgmode', '1', $script:ShowcfgPrg))
    }

    'xfilechk' {
        $version = Invoke-PythonText -Args @($script:VersionTool, '--next')
        Write-Host ("Build version: {0}" -f $version)
        if (-not $script:SkipBuild) {
            & $script:MakeExe '-B' "BUILD_SUPPORT_DIR=$($script:BuildSupportDir)" $script:XFilechkBootPrg $script:XFilechkPrg $script:XFilechkDisk1 $script:XFilechkDisk2
            if ($LASTEXITCODE -ne 0) {
                throw 'xfilechk build failed.'
            }
        }
        Start-ViceProcess @(
            '-logfile', $script:ViceLogFile,
            '-reu', '-reusize', '16384',
            '-drive8type', '1571', '-drive8truedrive', '-devicebackend8', '0', '+busdevice8', '-8', $script:XFilechkDisk1,
            '-drive9type', '1571', '-drive9truedrive', '-devicebackend9', '0', '+busdevice9', '-9', $script:XFilechkDisk2,
            '-autostart', $script:XFilechkBootPrg
        )
    }

    'monitor' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        Print-Info -RunMode 'Monitor' -Target $script:ProfileManifest.autostart_prg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-initbreak', '0xC80D', '-autostart', [string]$script:ProfileManifest.autostart_prg))
    }

    'readyshell-mon' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        New-Item -ItemType Directory -Force -Path 'logs' | Out-Null
        Set-Content -LiteralPath $script:RemoteMonLog -Value $null -Encoding ascii
        Set-Content -LiteralPath $script:ViceStdioLog -Value $null -Encoding ascii
        Print-Info -RunMode 'Readyshell Remote Monitor' -Target $script:ProfileManifest.autostart_prg
        Start-ViceProcess -Arguments (
            @('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') +
            $script:ViceAttachArgs +
            @(
                '-remotemonitor', '-remotemonitoraddress', $script:RemoteMonAddr,
                '-binarymonitor', '-binarymonitoraddress', $script:BinaryMonAddr,
                '-monlog', '-monlogname', $script:RemoteMonLog,
                '-autostart', [string]$script:ProfileManifest.autostart_prg
            )
        ) -RedirectPath $script:ViceStdioLog
    }

    'noreu' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        Print-Info -RunMode 'No REU' -Target $script:ProfileManifest.autostart_prg
        Start-ViceProcess ($script:ViceAttachArgs + @('-autostart', [string]$script:ProfileManifest.autostart_prg))
    }

    '' {
        Assert-FileExists -Path ([string]$script:ProfileManifest.autostart_prg) -Kind 'Program file'
        Print-Info -RunMode 'Normal' -Target $script:ProfileManifest.autostart_prg
        Start-ViceProcess (@('-logfile', $script:ViceLogFile, '-reu', '-reusize', '16384') + $script:ViceAttachArgs + @('-autostart', [string]$script:ProfileManifest.autostart_prg))
    }

    default {
        throw ("Unknown option: {0}" -f $script:Mode)
    }
}
