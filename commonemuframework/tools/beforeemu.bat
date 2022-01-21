@echo off
echo Copy to PSP if possible!


rem Detection procedure:
IF "%disablepsp%"=="y" goto skippsp
IF "%psp%"=="" goto detectpsp
goto skipdetection
:detectpsp
echo Detecting PSP...
rem Detect it only once:
set LW=
FOR %%I in (D E F G H I J K L M N O P Q R S T U V W X Y Z) DO IF "%psp%"=="" IF exist %%I:\PSP\GAME set psp=%%I
if "%psp%"=="" set disablepsp=y
:skipdetection
rem Detected!
rem Only detect once! Now copy files to psp if found!
IF NOT "%psp%"=="" echo Copying EBOOT to PSP (%psp%)...
IF NOT "%psp%"=="" call %toolsdir%/copypsp.bat %psp%
IF "%psp%"=="" echo PSP not found!
:skippsp