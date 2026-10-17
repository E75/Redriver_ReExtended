param([string]$GameDir = (Get-Location).Path, [int]$City = 4, [string]$StartX = "117000", [string]$StartZ = "-21000", [string]$CmdFile = "", [int]$WaitSeconds = 25, [string]$Out = "editor_session.png", [string]$Extra = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Map editor test driver (see CLAUDE.md, "Map editor").
# Usage: editor_session.ps1 -GameDir <folder with REDRIVER2_dev.exe, DRIVER1 and DRIVER2> -City 4 -CmdFile cmds.txt
# Starts the editor build, waits for the level, sends the commands of $CmdFile through EDITCMD.txt, prints EDITRES.txt,
# saves a screenshot when the commands made one (shot) and closes the game.
$dir = $GameDir
Remove-Item "$dir\EDITCMD.txt", "$dir\EDITRES.txt", "$dir\SCREENSHOT.BMP", "$dir\REDRIVER2.log" -ErrorAction SilentlyContinue

# -d1detect looks for the Driver 1 data at every start (the normal game does it once from the menu)
$gameArgs = @("-d1detect", "-nofmv", "-city", "$City", "-mission", "50", "-editor")
if ($StartX -ne "") { $gameArgs += @("-startpos", $StartX, $StartZ) }
if ($Extra -ne "") { $gameArgs += $Extra.Split(" ") }
$p = Start-Process "$dir\REDRIVER2_dev.exe" -ArgumentList $gameArgs -WorkingDirectory $dir -WindowStyle Normal -PassThru
Start-Sleep -Seconds $WaitSeconds

Copy-Item $CmdFile "$dir\EDITCMD.txt"
$deadline = (Get-Date).AddSeconds(30)
while (-not (Test-Path "$dir\EDITRES.txt") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
Start-Sleep -Milliseconds 500
if (Test-Path "$dir\EDITRES.txt") { Get-Content "$dir\EDITRES.txt" } else { "no answer from the editor" }

if (Test-Path "$dir\SCREENSHOT.BMP") {
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap("$dir\SCREENSHOT.BMP")
    $bmp.RotateFlip([System.Drawing.RotateFlipType]::RotateNoneFlipY)
    $small = New-Object System.Drawing.Bitmap($bmp, 800, [int]($bmp.Height * 800 / $bmp.Width))
    $small.Save((Join-Path (Get-Location).Path $Out), [System.Drawing.Imaging.ImageFormat]::Png)
    "saved $Out"
}
if (-not $p.HasExited) { [void]$p.CloseMainWindow(); Start-Sleep -Seconds 4 }
if (-not $p.HasExited) { $p.Kill() }
