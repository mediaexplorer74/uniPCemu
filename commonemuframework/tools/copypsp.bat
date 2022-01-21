@echo off
if "%EBOOT_PATH%"=="" set EBOOT_PATH=%rootdir%\projects_build\%PROJECT%\EBOOT.PBP
IF EXIST %1: IF EXIST %1:\psp\game IF NOT EXIST %1:\psp\game\%PROJECT% md %1:\psp\game\%PROJECT%
IF EXIST %1:\psp\game\%PROJECT% xcopy %EBOOT_PATH% %1:\psp\game\%PROJECT% /y
IF EXIST %1:\psp\game\%PROJECT%\%EBOOT_PATH% echo Copied to PSP at %1: