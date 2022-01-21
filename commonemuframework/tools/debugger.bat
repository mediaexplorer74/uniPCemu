@echo off
if "%ELF_PATH%"=="" set ELF_PATH=%rootdir%\projects_build\%PROJECT%\PSP\EBOOT.ELF
:redebug
set addr=
set /p addr=Address (first 8 to 0 if appearant): 0x

rem Now call debugger!
if "%addr%"=="" goto done
if "%ispsp%"=="" goto detectpsp
if "%ispsp%"=="y" goto ispsp
goto isstd

:detectpsp
rem PSP debugger:
set ispsp=n
set /p ispsp=PSP exception? Y/N: 
:ispsp
if "%ispsp%"=="y" psp-addr2line -e %ELF_PATH% -f -C 0x%addr%
if "%ispsp%"=="y" goto done

rem STD debugger:
:isstd
IF NOT "%addr%"=="" psp-addr2line -fe %ELF_PATH% 0x%addr% 
IF NOT "%addr%"=="" goto redebug

:done
rem We're done debugging!
rem IF EXIST %rootdir%/pspemu/ms0/psp/game/%PROJECT%/SCREEN.TXT start notepad %rootdir%/pspemu/ms0/psp/game/%PROJECT%/SCREEN.TXT
rem IF EXIST %rootdir%/pspemu/ms0/psp/game/%PROJECT%/INT10.TXT start notepad %rootdir%/pspemu/ms0/psp/game/%PROJECT%/INT10.TXT
rem IF EXIST %rootdir%/pspemu/ms0/psp/game/%PROJECT%/ROM_log.TXT start notepad %rootdir%/pspemu/ms0/psp/game/%PROJECT%/ROM_log.TXT