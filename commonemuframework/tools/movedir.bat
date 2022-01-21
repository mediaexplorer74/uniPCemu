@echo off
rem Syntax: movedir SRC FILTER DEST
rem e.g. movedir c:\temp\sub *.txt c:\test2\sub
set curdir=%1
set filter=%2
set destdir=%3
for /f "delims=|" %%f in ('dir /b %curdir%\%filter%') do call %toolsdir%/movefile.bat %curdir%\%%f %destdir%\%%f