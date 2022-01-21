@echo off
if "%PROJECT%"=="" goto finish

if NOT "%release%"=="release" goto dodebug
rem Release build of the libraries!
set release=Release
goto handlesdlsdl2
:dodebug
rem Debug build of the libraries!
set release=Debug
:handlesdlsdl2

rem SDL(2) library step!
if NOT "%SDL%"=="SDL2" goto useSDL
rem Copy SDL2 library to the destination
if "%whatarch%"=="x64" goto parseSDL264
rem Copy 32-bit SDL2 library!
xcopy C:\SDL\SDL2\VisualC\Win32\%release%\SDL2.dll ..\..\projects_build\%PROJECT% /Y>nul
goto parseNET
:parseSDL264
rem Copy 64-bit SDL2 library!
xcopy C:\SDL\SDL2\VisualC\x64\%release%\SDL2.dll ..\..\projects_build\%PROJECT% /Y>nul
goto parseNET

:useSDL
rem Copy SDL2 library to the destination
if "%whatarch%"=="x64" goto parseSDL64
rem Copy 32-bit SDL library!
xcopy C:\SDL\SDL-1.2.15\VisualC\Win32\%release%\SDL.dll ..\..\projects_build\%PROJECT% /Y>nul
goto parseNET
:parseSDL64
rem Copy 64-bit SDL library!
xcopy C:\SDL\SDL-1.2.15\VisualC\x64\%release%\SDL.dll ..\..\projects_build\%PROJECT% /Y>nul
goto parseNET

:parseNET
rem SDL(2)_net library step!
if NOT "%useNET%"=="yes" goto finish

if NOT "%SDL%"=="SDL2" goto useSDLnet
rem Copy SDL2 library to the destination
if "%whatarch%"=="x64" goto parseSDL2net64
rem Copy 32-bit SDL2 library!
xcopy C:\SDL\SDL2_net\VisualC\Win32\%release%\SDL2_net.dll ..\..\projects_build\%PROJECT% /Y>nul
goto finish
:parseSDL2net64
rem Copy 64-bit SDL2 library!
xcopy C:\SDL\SDL2_net\VisualC\x64\%release%\SDL2_net.dll ..\..\projects_build\%PROJECT% /Y>nul
goto finish

:useSDLnet
rem Copy SDL2 library to the destination
if "%whatarch%"=="x64" goto parseSDLnet64
rem Copy 32-bit SDL library!
xcopy C:\SDL\SDL_net-1.2.8\VisualC\Win32\%release%\SDL_net.dll ..\..\projects_build\%PROJECT% /Y>nul
goto finish
:parseSDLnet64
rem Copy 64-bit SDL library!
xcopy C:\SDL\SDL_net-1.2.8\VisualC\x64\%release%\SDL_net.dll ..\..\projects_build\%PROJECT% /Y>nul
goto finish

:finish
echo Dependencies are updated!