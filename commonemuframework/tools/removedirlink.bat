@echo off
rem First, try to remove the link itself, if it's there.
rd %1 /q
IF ERRORLEVEL 1 echo ERROR: %1 could not be deleted!
rem Next, remove the actual unlinked dir with subdirectories if it's there.
rd %1 /s /q
IF ERRORLEVEL 1 echo ERROR: %1 could not be deleted!