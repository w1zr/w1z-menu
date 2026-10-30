@echo off
rem Builds release\w1zsMenu-v<version>.zip (Fluffy Mod Manager or manual install)
rem from build\winmm.dll and the files in packageluffy. Settings are not shipped:
rem the menu keeps them in Documents\w1z's menu, so updates never reset them.
rem Run build.bat first.
setlocal
set VERSION=1.2.0
cd /d "%~dp0"
if not exist build\winmm.dll (
  echo build\winmm.dll not found. Run build.bat first.
  exit /b 1
)
copy /y build\winmm.dll package\fluffy\ >nul || exit /b 1
if exist package\fluffy\w1zsMenu.ini del package\fluffy\w1zsMenu.ini
rem Empty log so Fluffy records it and removes it on uninstall.
type nul > package\fluffy\w1zsMenu.log
if not exist release mkdir release
set ZIP=release\w1zsMenu-v%VERSION%.zip
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path package\fluffy\modinfo.ini,package\fluffy\screenshot.png,package\fluffy\winmm.dll,package\fluffy\w1zsMenu.log,package\fluffy\w1zsMenu-README.txt -DestinationPath '%ZIP%'" || exit /b 1
echo Built %ZIP%
