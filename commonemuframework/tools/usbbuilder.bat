@echo off
if exist c:\pspsdk set drive=c
if not exist c:\pspsdk set /p drive=What drive is pspsdk on? 
set pspsdk=%drive%:\pspsdk\psp\sdk
set path=%path%;%drive%:\pspsdk\bin