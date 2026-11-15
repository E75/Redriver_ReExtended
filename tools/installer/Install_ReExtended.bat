@echo off
rem Starts the installer window (PowerShell is part of Windows, nothing to install).
start "" powershell -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0Install_ReExtended.ps1"
