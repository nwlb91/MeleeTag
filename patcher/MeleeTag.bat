@echo off
rem Drag a vanilla Super Smash Bros. Melee NTSC 1.02 ISO onto this file.
rem The patched copy is written next to it as "<name>.MeleeTag.iso";
rem the original ISO is not modified.
rem
rem Optional settings, passed straight to the patcher, for example:
rem   set MELEETAG_OPTIONS=--cooldown 45 --no-hud
setlocal
if "%~1"=="" (
    echo Drag your Melee ISO onto MeleeTag.bat to create a MeleeTag ISO.
    echo.
    pause
    exit /b 1
)
"%~dp0meleetag-patch.exe" %MELEETAG_OPTIONS% "%~1"
set RESULT=%ERRORLEVEL%
echo.
if %RESULT%==0 (
    echo Finished. Load the .MeleeTag.iso in Dolphin or Slippi.
) else (
    echo Patching failed. See the message above.
)
pause
exit /b %RESULT%
