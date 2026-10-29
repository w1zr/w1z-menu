@echo off
rem Builds release\w1zsMenu-v<version>.zip (Fluffy Mod Manager or manual install)
rem from build\winmm.dll, dist\w1zsMenu.ini and the files in package\fluffy.
rem Run build.bat first.
setlocal
set VERSION=1.0.0
cd /d "%~dp0"
if not exist build\winmm.dll (
  echo build\winmm.dll not found. Run build.bat first.
  exit /b 1
)
copy /y build\winmm.dll package\fluffy\ >nul || exit /b 1
copy /y dist\w1zsMenu.ini package\fluffy\ >nul || exit /b 1
rem Empty log so Fluffy records it and removes it on uninstall.
type nul > package\fluffy\w1zsMenu.log
if not exist release mkdir release
set ZIP=release\w1zsMenu-v%VERSION%.zip
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path package\fluffy\modinfo.ini,package\fluffy\screenshot.png,package\fluffy\winmm.dll,package\fluffy\w1zsMenu.ini,package\fluffy\w1zsMenu.log,package\fluffy\w1zsMenu-README.txt -DestinationPath '%ZIP%'" || exit /b 1
echo Built %ZIP%
