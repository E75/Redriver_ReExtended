param([string]$GameDir = (Get-Location).Path, [int]$City = 4, [int]$Mission = 50, [string]$Prefix = "sweep", [int]$CX = 0, [int]$CZ = 0, [int]$Off = 45000)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Checks the ground of a city from above: starts the editor build, flies a top-down view over the centre ($CX, $CZ; the player
# start when both are 0) and four spots $Off units away and saves one small png per spot (<Prefix>_0..4.png in the current
# folder). Put them in one picture with: python montage.py <Prefix>
# Usage: sweep.ps1 -GameDir <install folder> -City 4 -Prefix sw4   (D2 cities other than Chicago need -Mission 52/54/56)
$dir = $GameDir
$out = (Get-Location).Path
Remove-Item "$dir\EDITCMD.txt", "$dir\EDITRES.txt", "$dir\SCREENSHOT.BMP" -ErrorAction SilentlyContinue
$p = Start-Process "$dir\REDRIVER2_dev.exe" -ArgumentList @("-nofmv", "-d1detect", "-city", "$City", "-mission", "$Mission", "-editor") -WorkingDirectory $dir -WindowStyle Normal -PassThru
Start-Sleep -Seconds 35
Set-Content "$dir\EDITCMD.txt" "info"
Start-Sleep -Seconds 3
$res = Get-Content "$dir\EDITRES.txt" -Raw
$pc = [regex]::Match($res, "player car at (-?\d+) -?\d+ (-?\d+)")
if ($CX -eq 0 -and $CZ -eq 0 -and $pc.Success) { $CX = [int]$pc.Groups[1].Value; $CZ = [int]$pc.Groups[2].Value }
Write-Output "centre $CX $CZ"
$spots = @(@(0, 0), @(-1, -1), @(1, -1), @(-1, 1), @(1, 1))
$i = 0
foreach ($s in $spots) {
    $x = $CX + $s[0] * $Off; $z = $CZ + $s[1] * $Off
    Set-Content "$dir\EDITCMD.txt" "goto $x $z 30000"
    Start-Sleep -Seconds 9
    Set-Content "$dir\EDITCMD.txt" "shot"
    Start-Sleep -Seconds 3
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap("$dir\SCREENSHOT.BMP")
    $bmp.RotateFlip([System.Drawing.RotateFlipType]::RotateNoneFlipY)
    $small = New-Object System.Drawing.Bitmap($bmp, 400, 250)
    $small.Save("$out\${Prefix}_$i.png")
    $bmp.Dispose(); $small.Dispose()
    Write-Output "spot $i at $x $z"
    $i++
}
[void]$p.CloseMainWindow(); Start-Sleep -Seconds 4; if (-not $p.HasExited) { $p.Kill() }
