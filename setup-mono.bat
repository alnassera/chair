@echo off
:: CHAIR - Equalizer APO Mono Setup
:: Writes a mono downmix config for Equalizer APO.
:: Run this AFTER installing Equalizer APO and selecting your headphones in DeviceSelector.

set "APO_CONFIG=C:\Program Files\EqualizerAPO\config\config.txt"

if not exist "C:\Program Files\EqualizerAPO" (
    echo.
    echo  Equalizer APO is not installed.
    echo  Download it from: https://sourceforge.net/projects/equalizerapo/
    echo.
    echo  During install, check ONLY your headphones in the Configurator.
    echo  Then reboot and run this script again.
    echo.
    pause
    exit /b 1
)

echo.
echo  Writing mono downmix config to:
echo  %APO_CONFIG%
echo.

:: Write the mono config
(
echo Copy: L=0.5*L+0.5*R
echo Copy: R=L
) > "%APO_CONFIG%"

echo  Done! Your headphones will now output mono.
echo.
echo  If you haven't already, run DeviceSelector to pick your headphones:
echo  "C:\Program Files\EqualizerAPO\DeviceSelector.exe"
echo.
echo  You may need to reboot for changes to take effect.
echo.
pause
