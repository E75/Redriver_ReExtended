@echo off
rem Prepares the Driver 1 cities from your own Driver 1 files (nothing of Driver 1 or Driver 2 is shipped with this game).
rem Put this file next to the game exe (REDRIVER2.exe) with the folders DRIVER1 and DRIVER2, then run it once.
rem It looks for DRIVER1\LEVELS, converts every city it finds into DRIVER1\CONVERTED, switches the Driver 1 cities on in
rem config.ini ([d1dlc]) and writes REDRIVER2.log. The same can be done in the game: Options - Gameplay - Secret -
rem Extended Settings - Driver 1 DLC - Check Driver 1 Data.
setlocal
cd /d "%~dp0"

if not exist "DRIVER2\LEVELS\CHICAGO.LEV" (
    echo DRIVER2\LEVELS\CHICAGO.LEV not found: the Driver 2 folder must be next to this file.
    goto failed
)

if not exist "DRIVER1\LEVELS" (
    echo DRIVER1\LEVELS not found: copy the LEVELS and DATA folders of your Driver 1 disc into a DRIVER1 folder next to this file.
    goto failed
)

set GAME=REDRIVER2.exe
if not exist "%GAME%" set GAME=REDRIVER2_dev.exe
if not exist "%GAME%" (
    echo REDRIVER2.exe not found next to this file.
    goto failed
)

echo Converting the Driver 1 cities, this takes about a minute...
"%~dp0%GAME%" -nofmv -d1prepare
findstr /C:"Driver 1 prepare" REDRIVER2.log
echo.
echo Done. Start the game and open Options - Gameplay - Secret - Extended Settings - Driver 1 DLC if the cities are not on yet.
endlocal
pause
exit /b 0

:failed
endlocal
pause
exit /b 1
