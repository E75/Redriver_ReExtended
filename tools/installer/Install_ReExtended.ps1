# Redriver ReExtended installer: a small window for players (PowerShell + WinForms, both ship with Windows).
# Pick your own Driver 2 and Driver 1 (folder, disc drive or .iso); it copies the files into DRIVER2\ and DRIVER1\
# next to the game, converts the Driver 1 cities and says when the game can be started.
# Nothing of Driver 1 or Driver 2 is shipped: the installer only reads your files and writes into the game folder.
# Layout: strings (Dutch/English) -> helpers (find/copy/convert) -> Run-Install -> window.
# Test without a window: -Auto -Driver2 <path> -Driver1 <path> [-GameDir <path>].
[CmdletBinding()]
param(
    [string]$GameDir = $PSScriptRoot,
    [string]$Driver2 = '',
    [string]$Driver1 = '',
    [switch]$Auto
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$nl = (Get-UICulture).TwoLetterISOLanguageName -eq 'nl'
function T($nlText, $enText) { if ($nl) { $nlText } else { $enText } }

$D2_MARKER = 'LEVELS\CHICAGO.LEV'   # proves a Driver 2 folder
$D1_MARKER = 'LEVELS\MIAMI.LEV'     # proves a Driver 1 folder
$D1_COPY_DIRS = 'LEVELS', 'DATA', 'MISSIONS'
$D1_COPY_FILES = 'IVIEW.BIN', 'SOUND\VOICES.BLK'   # car park test rules and voice

$script:mountedImages = @()

# Folder (or disc drive / .iso) -> folder that holds the marker file, looking in the folder, DRIVER1/DRIVER2 and one level deeper.
function Resolve-Source($path, $marker, $subName) {
    if (-not $path) { return $null }
    if ($path -match '\.iso$' -and (Test-Path -LiteralPath $path -PathType Leaf)) {
        $img = Mount-DiskImage -ImagePath $path -PassThru
        $script:mountedImages += $path
        $path = ($img | Get-Volume).DriveLetter + ':\'
    }
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { return $null }
    $cands = @($path, (Join-Path $path $subName))
    $cands += Get-ChildItem -LiteralPath $path -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName; Join-Path $_.FullName $subName }
    foreach ($c in $cands) { if (Test-Path -LiteralPath (Join-Path $c $marker)) { return $c } }
    return $null
}

# robocopy ships with Windows; /XC /XN /XO = never overwrite what is already there (keeps the game's own DATA, GFX, LANG).
function Copy-Tree($src, $dst, $step) {
    if ([IO.Path]::GetFullPath($src).TrimEnd('\') -ieq [IO.Path]::GetFullPath($dst).TrimEnd('\')) { return }
    $p = Start-Process robocopy -ArgumentList "`"$src`" `"$dst`" /E /XC /XN /XO /A-:R /R:1 /W:1 /NFL /NDL /NJH /NJS /NP" -WindowStyle Hidden -PassThru
    while (-not $p.HasExited) { $step.Invoke(); Start-Sleep -Milliseconds 100 }
    if ($p.ExitCode -ge 8) { throw (T "Kopieren mislukt ($src). Genoeg schijfruimte en schrijfrechten?" "Copying failed ($src). Enough disk space and write access?") }
}

function Invoke-Install($gameDir, $d2In, $d1In, $setStatus, $setProgress, $pump) {
    $r = [ordered]@{ ok = $false; msg = ''; cities = 0; exe = '' }
    try {
        $exe = @('REDRIVER2.exe', 'REDRIVER2_dev.exe') | Where-Object { Test-Path -LiteralPath (Join-Path $gameDir $_) } | Select-Object -First 1
        if (-not $exe) { throw (T "Het spel (REDRIVER2_dev.exe) staat niet in '$gameDir'. Pak eerst het hele zip-bestand uit en start de installer vanuit die map." "The game (REDRIVER2_dev.exe) is not in '$gameDir'. Unzip the whole download first and start the installer from that folder.") }

        & $setStatus (T 'Driver 2 zoeken...' 'Looking for Driver 2...'); & $setProgress 5
        $d2 = Resolve-Source $d2In $D2_MARKER 'DRIVER2'
        if (-not $d2) { throw (T "Driver 2 niet gevonden in '$d2In'. Kies de map (of schijf) van Driver 2 schijf 1: daarin moet LEVELS\CHICAGO.LEV staan." "Driver 2 not found in '$d2In'. Choose the folder (or drive) of Driver 2 disc 1: it must contain LEVELS\CHICAGO.LEV.") }

        & $setStatus (T 'Driver 1 zoeken...' 'Looking for Driver 1...'); & $setProgress 10
        $d1 = $null
        if ($d1In) {
            $d1 = Resolve-Source $d1In $D1_MARKER 'DRIVER1'
            if (-not $d1) { throw (T "Driver 1 niet gevonden in '$d1In'. Kies de map (of schijf) van de Driver 1 schijf: daarin moet LEVELS\MIAMI.LEV staan. Of laat dit veld leeg om zonder Driver 1 te installeren." "Driver 1 not found in '$d1In'. Choose the folder (or drive) of the Driver 1 disc: it must contain LEVELS\MIAMI.LEV. Or leave the field empty to install without Driver 1.") }
        }

        & $setStatus (T 'Driver 2 bestanden kopieren (dit duurt even)...' 'Copying Driver 2 files (this takes a while)...'); & $setProgress 15
        Copy-Tree $d2 (Join-Path $gameDir 'DRIVER2') $pump
        if (-not (Test-Path -LiteralPath (Join-Path $gameDir "DRIVER2\$D2_MARKER"))) { throw (T 'Na het kopieren ontbreekt DRIVER2\LEVELS\CHICAGO.LEV.' 'DRIVER2\LEVELS\CHICAGO.LEV is missing after copying.') }

        if ($d1) {
            & $setStatus (T 'Driver 1 bestanden kopieren...' 'Copying Driver 1 files...'); & $setProgress 45
            $d1Dst = Join-Path $gameDir 'DRIVER1'
            New-Item -ItemType Directory -Force -Path $d1Dst | Out-Null
            foreach ($d in $D1_COPY_DIRS) {
                if (Test-Path -LiteralPath (Join-Path $d1 $d)) { Copy-Tree (Join-Path $d1 $d) (Join-Path $d1Dst $d) $pump }
            }
            foreach ($f in $D1_COPY_FILES) {
                $s = Join-Path $d1 $f
                if (Test-Path -LiteralPath $s) {
                    $t = Join-Path $d1Dst $f
                    New-Item -ItemType Directory -Force -Path (Split-Path $t) | Out-Null
                    if (-not (Test-Path -LiteralPath $t)) { Copy-Item -LiteralPath $s -Destination $t; (Get-Item -LiteralPath $t).IsReadOnly = $false }
                }
            }
            if (-not (Test-Path -LiteralPath (Join-Path $d1Dst $D1_MARKER))) { throw (T 'Na het kopieren ontbreekt DRIVER1\LEVELS\MIAMI.LEV.' 'DRIVER1\LEVELS\MIAMI.LEV is missing after copying.') }

            & $setStatus (T 'Driver 1 steden omzetten (ongeveer een minuut)...' 'Converting the Driver 1 cities (about a minute)...'); & $setProgress 70
            $log = Join-Path $gameDir 'REDRIVER2.log'
            Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
            $p = Start-Process (Join-Path $gameDir $exe) -ArgumentList '-nofmv -d1prepare' -WorkingDirectory $gameDir -WindowStyle Minimized -PassThru
            $t0 = Get-Date
            while (-not $p.HasExited) {
                $pump.Invoke(); Start-Sleep -Milliseconds 100
                if (((Get-Date) - $t0).TotalMinutes -gt 15) { $p.Kill(); throw (T 'Het omzetten duurde te lang en is gestopt.' 'The conversion took too long and was stopped.') }
            }
            $done = if (Test-Path -LiteralPath $log) { Select-String -LiteralPath $log -Pattern 'Driver 1 prepare: ' | ForEach-Object { $_.Line } } else { @() }
            $ready = @($done | Where-Object { $_ -match ' ready' }).Count
            if ($ready -lt 1) { throw (T 'Geen enkele Driver 1 stad kon worden omgezet. Controleer of je de LEVELS- en DATA-mappen van Driver 1 hebt gekozen (details: REDRIVER2.log).' 'No Driver 1 city could be converted. Check that you chose the LEVELS and DATA folders of Driver 1 (details: REDRIVER2.log).') }
            $r.cities = $ready
        }

        & $setProgress 100
        $r.ok = $true
        $r.msg = if ($d1) { T "Klaar! Het spel is geinstalleerd ($($r.cities) Driver 1 steden omgezet). Je kunt het spel nu starten." "Done! The game is installed ($($r.cities) Driver 1 cities converted). You can start the game now." }
                 else { T 'Klaar! Het spel is geinstalleerd (zonder Driver 1). Je kunt het spel nu starten.' 'Done! The game is installed (without Driver 1). You can start the game now.' }
        $r.exe = Join-Path $gameDir $exe
    }
    catch { $r.msg = $_.Exception.Message }
    finally { foreach ($i in $script:mountedImages) { Dismount-DiskImage -ImagePath $i -ErrorAction SilentlyContinue | Out-Null } $script:mountedImages = @() }
    return $r
}

if ($Auto) {
    $noop = { param($x) }
    $res = Invoke-Install $GameDir $Driver2 $Driver1 { param($s) Write-Host $s } $noop {}
    Write-Host $(if ($res.ok) { 'OK: ' } else { 'FAIL: ' }) $res.msg
    exit $(if ($res.ok) { 0 } else { 1 })
}

# ---- window ----
$form = New-Object Windows.Forms.Form
$form.Text = 'Redriver ReExtended - ' + (T 'Installatie' 'Setup')
$form.ClientSize = New-Object Drawing.Size(560, 380)
$form.StartPosition = 'CenterScreen'; $form.FormBorderStyle = 'FixedDialog'; $form.MaximizeBox = $false
$form.Font = New-Object Drawing.Font('Segoe UI', 9)

function Add-Label($text, $y, $h = 20) { $l = New-Object Windows.Forms.Label; $l.Text = $text; $l.SetBounds(15, $y, 530, $h); $form.Controls.Add($l); $l }
function Add-PathRow($y, $isFile) {
    $tb = New-Object Windows.Forms.TextBox; $tb.SetBounds(15, $y, 360, 24); $form.Controls.Add($tb)
    $b = New-Object Windows.Forms.Button; $b.Text = T 'Map...' 'Folder...'; $b.SetBounds(383, ($y - 1), 75, 26); $form.Controls.Add($b)
    $b.Tag = $tb
    $b.Add_Click({ $d = New-Object Windows.Forms.FolderBrowserDialog; $d.Description = T 'Kies de map of schijf' 'Choose the folder or drive'
        if ($d.ShowDialog() -eq 'OK') { $this.Tag.Text = $d.SelectedPath } })
    $i = New-Object Windows.Forms.Button; $i.Text = 'ISO...'; $i.SetBounds(463, ($y - 1), 75, 26); $form.Controls.Add($i)
    $i.Tag = $tb
    $i.Add_Click({ $d = New-Object Windows.Forms.OpenFileDialog; $d.Filter = 'ISO (*.iso)|*.iso'
        if ($d.ShowDialog() -eq 'OK') { $this.Tag.Text = $d.FileName } })
    $tb
}

$title = Add-Label (T 'Redriver ReExtended installeren' 'Install Redriver ReExtended') 12 24
$title.Font = New-Object Drawing.Font('Segoe UI', 13, [Drawing.FontStyle]::Bold)
Add-Label (T 'Kies waar jouw eigen Driver 2 en Driver 1 staan (map, schijf met disc, of ISO-bestand). Er wordt niets van deze spellen meegeleverd.' 'Choose where your own Driver 2 and Driver 1 are (folder, disc drive or ISO file). Nothing of these games is shipped.') 42 36 | Out-Null
Add-Label (T 'Driver 2 (verplicht), schijf 1:' 'Driver 2 (required), disc 1:') 90 | Out-Null
$tb2 = Add-PathRow 112
Add-Label (T 'Driver 1 (optioneel, voor de Driver 1 steden en missies):' 'Driver 1 (optional, for the Driver 1 cities and missions):') 148 | Out-Null
$tb1 = Add-PathRow 170
Add-Label (T 'Spelmap (hier komt alles in):' 'Game folder (everything goes here):') 206 | Out-Null
$tbG = New-Object Windows.Forms.TextBox; $tbG.SetBounds(15, 228, 360, 24); $tbG.Text = $GameDir; $form.Controls.Add($tbG)
$bg = New-Object Windows.Forms.Button; $bg.Text = T 'Map...' 'Folder...'; $bg.SetBounds(383, 227, 75, 26); $form.Controls.Add($bg)
$bg.Add_Click({ $d = New-Object Windows.Forms.FolderBrowserDialog; if ($d.ShowDialog() -eq 'OK') { $tbG.Text = $d.SelectedPath } })

$bar = New-Object Windows.Forms.ProgressBar; $bar.SetBounds(15, 275, 530, 22); $form.Controls.Add($bar)
$status = Add-Label '' 303 40
$install = New-Object Windows.Forms.Button; $install.Text = T 'Installeren' 'Install'; $install.SetBounds(15, 340, 120, 30); $form.Controls.Add($install)
$start = New-Object Windows.Forms.Button; $start.Text = T 'Start spel' 'Start game'; $start.SetBounds(145, 340, 120, 30); $start.Enabled = $false; $form.Controls.Add($start)
$close = New-Object Windows.Forms.Button; $close.Text = T 'Sluiten' 'Close'; $close.SetBounds(425, 340, 120, 30); $form.Controls.Add($close)
$close.Add_Click({ $form.Close() })

$script:exePath = ''
$start.Add_Click({ Start-Process $script:exePath -WorkingDirectory (Split-Path $script:exePath); $form.Close() })
$pump = { [Windows.Forms.Application]::DoEvents() }
$install.Add_Click({
    $install.Enabled = $false; $start.Enabled = $false; $bar.Value = 0; $status.ForeColor = 'Black'
    if (-not $tb2.Text.Trim()) { $status.ForeColor = 'Firebrick'; $status.Text = T 'Kies eerst de map van Driver 2.' 'Choose the Driver 2 folder first.'; $install.Enabled = $true; return }
    $res = Invoke-Install $tbG.Text.Trim() $tb2.Text.Trim() $tb1.Text.Trim() { param($s) $status.Text = $s; [Windows.Forms.Application]::DoEvents() } { param($v) $bar.Value = $v } $pump
    $status.Text = $res.msg
    if ($res.ok) { $status.ForeColor = 'DarkGreen'; $script:exePath = $res.exe; $start.Enabled = $true } else { $status.ForeColor = 'Firebrick' }
    $install.Enabled = $true
})
[void]$form.ShowDialog()
