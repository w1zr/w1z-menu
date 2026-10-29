@echo off
rem Builds winmm.dll (w1z's menu) and scan_test.exe with the MSVC x64 toolchain.
rem Requires Visual Studio 2022 Build Tools with the "Desktop development with C++" workload.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo MSVC x64 tools not found. Install Visual Studio 2022 Build Tools with the C++ workload.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0"
if not exist build\obj mkdir build\obj
if not exist build\obj3 mkdir build\obj3
del /q build\obj\*.obj 2>nul

rem /EHa: C++ destructors also run for SEH exceptions caught by the overlay's guard.
set COMMON=/nologo /O2 /MT /GS /guard:cf /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set CFLAGS=%COMMON% /std:c++17 /W4 /WX /EHa /DIMGUI_DISABLE_OBSOLETE_FUNCTIONS /external:W0 /external:Ithird_party\imgui
set TPFLAGS=%COMMON% /W3 /EHa /DIMGUI_DISABLE_OBSOLETE_FUNCTIONS /Ithird_party\imgui

set SRC=src\dllmain.cpp src\ammo.cpp src\game.cpp src\scan.cpp src\settings.cpp src\features.cpp src\log.cpp ^
 src\proxy_winmm.cpp src\dscan.cpp src\engine.cpp src\skins.cpp src\skinscan.cpp src\character.cpp src\render.cpp src\input.cpp src\hotkeys.cpp src\menu.cpp src\pause.cpp
set IMGUI=third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp ^
 third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_dx12.cpp
set MINHOOK=third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c ^
 third_party\minhook\src\hde\hde64.c

cl %TPFLAGS% /c %IMGUI% /Fo"build\obj3\\" || exit /b 1
cl %COMMON% /W3 /c %MINHOOK% /Fo"build\obj3\\" || exit /b 1
cl %CFLAGS% /c %SRC% /Fo"build\obj\\" || exit /b 1
ml64 /nologo /c /Fo"build\obj\winmm_stubs.obj" src\winmm_stubs.asm || exit /b 1
if exist build\version.dll del build\version.dll
link /nologo /DLL /DEF:src\winmm.def /GUARD:CF /OUT:build\winmm.dll build\obj\*.obj build\obj3\*.obj ^
 user32.lib kernel32.lib d3d12.lib dxgi.lib || exit /b 1
cl %CFLAGS% tests\scan_test.cpp src\scan.cpp src\dscan.cpp src\skinscan.cpp /Fo"build\\" /Fe"build\scan_test.exe" || exit /b 1
cl %CFLAGS% tests\proxy_test.cpp /Fo"build\\" /Fe"build\proxy_test.exe" || exit /b 1

echo.
echo Built build\winmm.dll, build\scan_test.exe and build\proxy_test.exe
