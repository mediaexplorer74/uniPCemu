@echo off
rem Save old directory!
set old=%cd%
rem Goto build directory!
cd %rootdir%/projects_build/%PROJECT%/PSP
del *.o /s>nul
del *.sfo /s>nul
del *.elf /s>nul
rem Only current directory prx: We don't want the exception prx removed!
del *.prx>nul
del *.pbp>nul
rem Return!
cd %old%