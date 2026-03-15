@echo off
setlocal enabledelayedexpansion
title CHAIR Setup

echo.
echo  ============================================
echo   CHAIR v1.0 - First Time Setup
echo  ============================================
echo.

:: -----------------------------------------------------------
:: Step 1: Check for VB-CABLE
:: -----------------------------------------------------------
echo  [1/5] Checking for VB-CABLE...

if exist "C:\Windows\System32\drivers\vbaudio_cable64_win10.sys" (
    echo         VB-CABLE found.
) else if exist "C:\Windows\System32\drivers\vbaudio_cable64.sys" (
    echo         VB-CABLE found.
) else (
    echo.
    echo         VB-CABLE not found.
    echo         Opening download page -- install it and REBOOT, then run this again.
    echo.
    start https://vb-audio.com/Cable/
    echo  Press any key after installing VB-CABLE and rebooting...
    pause >nul
)

:: -----------------------------------------------------------
:: Step 2: Check for Equalizer APO
:: -----------------------------------------------------------
echo  [2/5] Checking for Equalizer APO...

if exist "C:\Program Files\EqualizerAPO\config\config.txt" (
    echo         Equalizer APO found.
) else (
    echo.
    echo         Equalizer APO not found.
    echo         Opening download page -- during install, select ONLY your headphones.
    echo         Then REBOOT and run this again.
    echo.
    start https://sourceforge.net/projects/equalizerapo/
    echo  Press any key after installing Equalizer APO and rebooting...
    pause >nul
)

:: -----------------------------------------------------------
:: Step 3: Configure Equalizer APO for mono
:: -----------------------------------------------------------
echo  [3/5] Configuring mono output...

if exist "C:\Program Files\EqualizerAPO\config\config.txt" (
    findstr /C:"0.5*L+0.5*R" "C:\Program Files\EqualizerAPO\config\config.txt" >nul 2>&1
    if !errorlevel! equ 0 (
        echo         Mono config already set.
    ) else (
        (
            echo Copy: L=0.5*L+0.5*R
            echo Copy: R=L
        ) > "C:\Program Files\EqualizerAPO\config\config.txt"
        echo         Mono config written.
    )
) else (
    echo         Skipped -- Equalizer APO not installed.
)

:: -----------------------------------------------------------
:: Step 4: Configure Listen loopback on CABLE Output
:: -----------------------------------------------------------
echo  [4/5] Configuring CABLE listen loopback...

:: Use PowerShell to find CABLE Output endpoint and enable Listen
powershell -ExecutionPolicy Bypass -Command ^
  "$found = $false; ^
   $regPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'; ^
   Get-ChildItem $regPath 2>$null | ForEach-Object { ^
     $props = Join-Path $_.PSPath 'Properties'; ^
     if (Test-Path $props) { ^
       $vals = Get-ItemProperty $props -ErrorAction SilentlyContinue; ^
       $name = $vals.'{a45c254e-df1c-4efd-8020-67d146a850e0},2' ; ^
       if ($name -like '*CABLE Output*') { ^
         Write-Host ('        Found: ' + $name); ^
         $found = $true ^
       } ^
     } ^
   }; ^
   if (-not $found) { Write-Host '        CABLE Output not found in recording devices.' }" 2>nul

echo.
echo         The Listen loopback must be set manually (Windows blocks scripting it):
echo.
echo         1. Press Win+R, type mmsys.cpl, press Enter
echo         2. Go to the Recording tab
echo         3. Right-click "CABLE Output" ^> Properties
echo         4. Listen tab ^> check "Listen to this device"
echo         5. Select your headphones from the dropdown ^> OK
echo.
start mmsys.cpl
echo  Press any key when done...
pause >nul

:: -----------------------------------------------------------
:: Step 5: Route VALORANT to CABLE in Volume Mixer
:: -----------------------------------------------------------
echo  [5/5] Route VALORANT audio to CABLE...
echo.
echo         1. Launch VALORANT if it's not running
echo         2. Open Windows Settings ^> System ^> Sound ^> Volume Mixer
echo            (or click the link that opens now)
echo         3. Find VALORANT and set its output to "CABLE Input"
echo.
start ms-settings:apps-volume
echo  Press any key when done...
pause >nul

:: -----------------------------------------------------------
:: Done
:: -----------------------------------------------------------
echo.
echo  ============================================
echo   Setup complete!
echo  ============================================
echo.
echo   From now on, just double-click chair.exe to run CHAIR.
echo   (It automatically uses VB-CABLE)
echo.
echo   Controls:
echo     Ctrl+Shift+Q  - close overlay
echo     Ctrl+C        - stop engine
echo.
echo   You can minimize the CHAIR window while playing.
echo.

set /p LAUNCH="  Launch CHAIR now? (Y/N): "
if /i "%LAUNCH%"=="Y" (
    start "" "%~dp0chair.exe"
)
