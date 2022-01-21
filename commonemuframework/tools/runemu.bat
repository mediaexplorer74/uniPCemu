@echo off
if "%EBOOT_PATH%"=="" set EBOOT_PATH=%rootdir%\projects_build\%PROJECT%\EBOOT.PBP
echo EBOOT: %EBOOT_PATH%
set /p runemu=Run emulator? Y/N: 
if NOT "%runemu%"=="y" goto skipemu
rem save old path!
set oldpath=%CD%
rem first, copy our file!
rem Generate game path if needed!
set builddir=%oldpath%\%rootdir%\projects_build

rem 32-bit version
echo Copying EBOOT to x86 version...
if NOT EXIST %builddir%\jpcsp-windows-x86\ms0\psp\game\%PROJECT% md %builddir%\jpcsp-windows-x86\ms0\psp\game\%PROJECT%
xcopy %EBOOT_PATH% %builddir%\jpcsp-windows-x86\ms0\PSP\GAME\%PROJECT% /y>NUL

rem 64-bit version
echo Copying EBOOT to x64 version...
if NOT EXIST %builddir%\jpcsp-windows-amd64\ms0\psp\game\%PROJECT% md %builddir%\jpcsp-windows-amd64\ms0\psp\game\%PROJECT%
xcopy %EBOOT_PATH% %builddir%\jpcsp-windows-amd64\ms0\PSP\GAME\%PROJECT% /y>NUL

rem Detect CPU architecture... Default to 32-bits!
set cpusize=32
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set cpusize=64

rem 64-bit? Enable force32!
if "%cpusize%"=="64" set /p Force32=Force 32-bits? y/n: 
if "%cpusize%"=="64" if "%Force32%"=="" set Force32=y

rem Apply 32/64 bit!
if "%Force32%"=="y" set cpusize=32

rem Execute the emultor according to CPU architecture:
if "%cpusize%"=="32" goto jpcsp32
if "%cpusize%"=="64" goto jpcsp64
goto skipemu

:jpcsp32
cd %builddir%/jpcsp-windows-x86
start cmd.exe /c start-windows-x86.bat
goto emudone

:jpcsp64
cd %builddir%/jpcsp-windows-amd64
start cmd.exe /c start-windows-amd64.bat
goto emudone

:emudone
set EMUDIR=%CD%
rem now return to ourselves!
cd %oldpath%
:skipemu