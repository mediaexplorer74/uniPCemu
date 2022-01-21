@echo off
title %PROJECT% builder
rem Automatically set our repository dir our builder files are at(tools repo directory)
if "%toolsdir%"=="" set toolsdir=..\tools
if "%rootdir%"=="" set rootdir=..
set manualpspsdk=0
set disablepsp=n
set /p disablepsp=Disable psp? Y/N(default): 
if "%pspsdk%"=="" set manualpspsdk=1
if "%manualpspsdk%"=="1" set pspsdk=c:\pspsdk\psp\sdk
rem Make sure we have the SDK in out path!
set path=%pspsdk%\..\..\bin;%path%
echo PSPSDK is at "%pspsdk%".
set cleanup=y
set /p havebuilder=Builder? Y/N: 
if "%havebuilder%"=="n" call %toolsdir%\usbbuilder.bat
if "%havebuilder%"=="y" if "%pspsdk%"=="" call %toolsdir%\usbbuilder.bat
:resetbuilder
cls
rem remove Object files for cleanup!
rem Remove EBOOT for checking of we've compiled fine.

rem Create directories if needed!
if not exist %rootdir%\projects_build md %rootdir%\projects_build
if not exist %rootdir%\projects_build\%PROJECT% md %rootdir%\projects_build\%PROJECT%
if not exist %rootdir%\projects_build\%PROJECT%\PSP md %rootdir%\projects_build\%PROJECT%\PSP
if exist ..\commonemuframework if not exist %rootdir%\projects_build\%PROJECT%\PSP\___ md %rootdir%\projects_build\%PROJECT%\PSP\___
if exist ..\commonemuframework if not exist %rootdir%\projects_build\%PROJECT%\PSP\___\commonemuframework md %rootdir%\projects_build\%PROJECT%\PSP\___\commonemuframework
call %toolsdir%\copydirectories.bat %cd% %cd%\%rootdir%\projects_build\%PROJECT%\PSP
if exist ..\commonemuframework call %toolsdir%\copydirectories.bat ..\commonemuframework %cd%\%rootdir%\projects_build\%PROJECT%\PSP\___\commonemuframework

rem Call the correct build for the PSP!
if "%makepsp%"=="psp" make psp build ROOTPATH=.
if "%makepsp%"=="" make
rem Copy file and try emulating it!
IF EXIST %rootdir%\projects_build\%PROJECT%\PSP\EBOOT.PBP copy %rootdir%\projects_build\%PROJECT%\PSP\EBOOT.PBP %rootdir%\projects_build\%PROJECT%\EBOOT.PBP>nul
IF EXIST %rootdir%\projects_build\%PROJECT%\EBOOT.PBP goto runemu
rem emu available? run emu!

:doneemu

if EXIST %toolsdir%\debugger.bat CALL %toolsdir%\debugger.bat

set /p cleanup=Cleanup? y/n: 
rem Cleanup unneeded files:
if "%cleanup%"=="y" call %toolsdir%\cleanup.bat
goto resetbuilder

:runemu
rem Preparations and emu itself!
IF EXIST %toolsdir%\beforeemu.bat CALL %toolsdir%\beforeemu.bat
IF EXIST %rootdir%\projects_build\jpcsp-windows-x86 goto doemu
IF EXIST %rootdir%\projects_build\jpcsp-windows-amd64 goto doemu
goto fromemu

:doemu
call %toolsdir%\runemu.bat
goto fromemu

:fromemu
rem Emulator is finished!
goto doneemu