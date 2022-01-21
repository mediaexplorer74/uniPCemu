@echo off
IF NOT EXIST %2 md %2

rem Process subdirectories specified by %1!
for /f "delims=|" %%f in ('dir /b %1\*.') do IF NOT "%%f"=="Makefile" IF NOT "%%f"=="files" IF NOT "%%f"=="headers" call %toolsdir%\copydirectories.bat %1\%%f %2\%%f