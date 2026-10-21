<#
.SYNOPSIS
  Compilation, téléversement et démonstration de la station 3W (Windows).

.DESCRIPTION
  Enchaîne les commandes PlatformIO. Fonctionne avec l'extension PlatformIO
  de VS Code, même si "pio" n'est pas dans le PATH.

.EXAMPLE
  .\scripts\build.ps1 build            # compile la version client
  .\scripts\build.ps1 upload           # compile et téléverse la version client
  .\scripts\build.ps1 demo             # compile et téléverse la version démo, puis ouvre la console
  .\scripts\build.ps1 monitor          # console série (115 200 bauds, journal dans logs\)
  .\scripts\build.ps1 test             # tests sur PC (gcc requis)
  .\scripts\build.ps1 verify -Drive E: # contrôle les fichiers LOG de la carte SD
  .\scripts\build.ps1 sd-fill -Drive E:   # prépare la démo "carte pleine"
  .\scripts\build.ps1 sd-unfill -Drive E:
  .\scripts\build.ps1 upload -Copy     # téléversement par copie sur le lecteur NOD_L476RG
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'upload', 'demo', 'monitor', 'test', 'clean', 'size', 'verify', 'sd-fill', 'sd-unfill', 'help')]
    [string]$Action = 'help',
    [string]$Env = '',
    [string]$Drive = '',
    [int]$LeaveKB = 0,
    [int]$FileMaxSize = 2048,
    [switch]$Copy
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Find-Pio {
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $local = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
    if (Test-Path $local) { return $local }
    throw "PlatformIO introuvable. Installer l'extension PlatformIO de VS Code (ou 'pip install platformio')."
}

function Find-Python {
    $local = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
    if (Test-Path $local) { return $local }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "Python introuvable (il est normalement fourni avec PlatformIO)."
}

function Invoke-Pio([string[]]$Arguments) {
    Write-Host "> pio $($Arguments -join ' ')" -ForegroundColor Cyan
    & $script:Pio @Arguments
    if ($LASTEXITCODE -ne 0) { throw "La commande PlatformIO a échoué (code $LASTEXITCODE)." }
}

function Assert-Drive {
    if (-not $Drive) { throw "Préciser la lettre de la carte SD, par exemple -Drive E:" }
    if (-not (Test-Path "$Drive\")) { throw "Lecteur $Drive introuvable." }
}

function Copy-Firmware([string]$e) {
    $bin = Join-Path $Root ".pio\build\$e\firmware.bin"
    $vol = Get-Volume | Where-Object { $_.FileSystemLabel -like 'NOD_L476*' } | Select-Object -First 1
    if (-not $vol) { throw "Lecteur NOD_L476RG introuvable : la carte est-elle branchée en USB ?" }
    Write-Host "Copie de $bin vers $($vol.DriveLetter):\ (les paramètres en Flash peuvent revenir aux valeurs par défaut)" -ForegroundColor Yellow
    Copy-Item $bin "$($vol.DriveLetter):\" -Force
}

function Upload([string]$e) {
    if ($Copy) {
        Invoke-Pio @('run', '-e', $e)
        Copy-Firmware $e
    } else {
        Invoke-Pio @('run', '-e', $e, '-t', 'upload')
    }
}

function Monitor {
    New-Item -ItemType Directory -Force -Path (Join-Path $Root 'logs') | Out-Null
    Push-Location (Join-Path $Root 'logs')   # log2file écrit le journal ici
    try { Invoke-Pio @('device', 'monitor', '-d', $Root) } finally { Pop-Location }
}

$Pio = $null
if ($Action -notin @('verify', 'sd-fill', 'sd-unfill', 'help')) { $Pio = Find-Pio }
if (-not $Env) { $Env = if ($Action -eq 'demo') { 'demo' } else { 'nucleo_l476rg' } }

switch ($Action) {
    'build'   { Invoke-Pio @('run', '-e', $Env) }
    'size'    { Invoke-Pio @('run', '-e', $Env, '-t', 'size') }
    'clean'   { Invoke-Pio @('run', '-t', 'clean') }
    'upload'  { Upload $Env }
    'demo'    { Upload 'demo'; Monitor }
    'monitor' { Monitor }
    'test'    { Invoke-Pio @('test', '-e', 'native') }
    'verify'  {
        Assert-Drive
        & (Find-Python) (Join-Path $PSScriptRoot 'verifier_logs.py') "$Drive\" --file-max-size $FileMaxSize
    }
    'sd-fill' {
        # Remplit la carte avec des fichiers REMPLIS_n.BIN (4 Go au plus chacun, limite FAT32).
        Assert-Drive
        $max = [int64]4GB - 1
        $n = 0
        while ($true) {
            $free = [int64](Get-PSDrive $Drive.TrimEnd(':')).Free - [int64]$LeaveKB * 1024
            if ($free -le 0) { break }
            $f = [System.IO.File]::Create("$Drive\REMPLIS_$n.BIN")
            $f.SetLength([Math]::Min($free, $max)); $f.Close()
            $n++
        }
        $left = [int64](Get-PSDrive $Drive.TrimEnd(':')).Free
        Write-Host ("$n fichier(s) REMPLIS_n.BIN créé(s) : il reste {0:N0} octets libres." -f $left)
        Write-Host "Supprimer les fichiers .LOG de la carte pour que la station ait besoin d'un nouveau cluster."
    }
    'sd-unfill' {
        Assert-Drive
        Remove-Item "$Drive\REMPLIS_*.BIN" -ErrorAction SilentlyContinue
        Write-Host "Fichiers REMPLIS_n.BIN supprimés."
    }
    default   { Get-Help $MyInvocation.MyCommand.Path -Detailed }
}
