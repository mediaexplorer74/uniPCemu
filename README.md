# UNIPCEMU

UniPCEmu, UWP Port. Draft/Sketch/Not ready

This is try to port UniPCEmu (x86 emulator for some PSPs...) on UWP "rails", as Proof of Concept

### What is this repository for? ###

* Quick summary
	- The UniPCemu x86 emulator project.
* Version
	- Currently unused. Releases are the commit date in format *YYYYMMDD_HHMM*.

### How do I get set up? ###

* Summary of set up
- This repository goes into a folder named UniPCemu.
- Pull the submodules as well for it's required Makefiles and source code files for compilation.
- Install Minimalist PSPSDK devkit(PSP, up to version 0.10.0 is compiling UniPCemu(although with lots of sprintf warnings). Newer versions currently fail linking.) or Visual C++, MinGW(Windows)/MSYS2(See **MSYS2.txt**) or GNU C++ toolchain(Linux).
- Install SDL packages for the devkit, in C:\SDL for Windows(copy SDL2-2.* folder contents to C:\SDL\SDL2 (and the SDL2_net folder to C:\SDL\SDL2_net), to use SDL 1.2.*, copy the folder contents to C:\SDL\SDL1.2.15(and the SDL_net folder to C:\SDL\SDL_net-1.2.8)), installers for MinPSPW and /mingw(SDL or SDL2, optionally SDL(2)_net (for network support) and winpcap/libpcap(requires SDL(2)_net. For packet server support).
- Install SDL(2) with or without SDL(2)_net for network support on your machine on Linux, according to the official build steps from libsdl.org.
- For Linux:
	- Run autogen.sh if the configure script doesn't run correctly.
	- Run the configure script to configure and build the Makefile required for the project.
	- Run make linux [re]build [SDL2] [SDL[2]_net] [x64] [pcap], with the optional parts being between brackets(SDL2+, SDL(2)_net for network support, x64 for 64-bit compilation is required. All others depend on the libraries to use).
	- Run (with sudo for installation rights) the above command, replacing [re]build with install to install the application for usage.
- For Playstation Vita (VitaSDK):
	- Run make vita [re]build [SDL2] [SDL[2]_net] (depends on SDL(2) without or without SDL(2)_net)
- For Nintendo Switch (devkitPro):
	- Make sure to export the PORTLIBS environment variable to be set to $DEVKITPRO/portlibs/switch
	- Make sure to set the PORTLIBS to the PKG_CONFIG_PATH:$PORTLIBS after the above.
	- Run make switch [re]build [SDL2] [SDL[2]_net]
- For Visual C++:
	- Open the projects within the VisualC subfolders(the solution file) and compile SDL2 and SDL2main. Also compile the SDL2_net project when used(after compiling SDL2 itself).
	- For SDL 1.2:
		- Add the paths **C:\SDL\SDL1.2.15\include** to both SDL 1.2 Win32 and x64 target include directories, as well as **C:\SDL\SDL1.2.15\VisualC\$(Platform)\$(Configuration)** to both Win32 and x64 target library directories.
		- Don't forget to change the Output directory to **$(SolutionDir)$(Platform)\$(Configuration)\ ** and the Intermediate directory to **$(Platform)\$(Configuration)\ ** for SDL 1.2 itself and SDLmain.
		- Don't forget to change the Output file for the linker to **$(OutputPath)SDL.dll** for SDL 1.2.
		- Don't forget to set the output file within the tab linker\general to **$(OutputPath)SDL.dll**.
	- For SDL_net:
		- Don't forget to add the paths **C:\SDL\SDL-1.2.15\include** to the include directories and **C:\SDL\SDL-1.2.15\VisualC\$(Platform)\$(Configuration)** to both Win32 and x64 target platform directories for SDL_net.	
		- Don't forget to set the output and intermediate directories for SDL_net to **$(Platform)\$(Configuration)\ **.
		- Don't forget to set the output file within the tab linker\general to **$(OutputPath)SDL_net.dll**.
	- For SDL2_net:
		- Don't forget to add the paths **C:\SDL\SDL2\include** to both Win32 and x64 target include directories, as well as **C:\SDL\SDL2\VisualC\$(Platform)\$(Configuration)** to both Win32 and x64 target library directories.
		- Add DLL_EXPORT to the preprocessor definitions.
	- Set the Visual C++ Local Windows Debugger for the project to use **$(TargetDir)** for it's working directory, to comply with the other paths set in the project.

* Configuration
	- Make sure there is a compile directory parallel to the project directory(projects_build\unipcemu) with a duplicate directory tree of the project repository(automatically createn by remake.bat on Windows and by Windows/Linux when building using the Makefile).
* Dependencies
	- See set up.
* Adding the Android SDL2 build to the Android project(directories relative to the SDL2 root directory, e.g. SDL2-2.X.X)
	- Download the latest source code version of SDL2 from the project homepage. 
	- Copy the **android-project\src\org** (**android-project\app\src\main\java\org** for SDL2.0.8+) directory to **android-project/src**.
	- Copy the **include** and **src** directories, as well as the **Android.mk** file to the **android-project/jni/SDL2** folder.
	- For SDL 2.0.9 and below: Edit **android-project/jni/SDL2/src/video/android/SDL_androidevents.c**, replace "#define SDL_ANDROID_BLOCK_ON_PAUSE  1" with "#define SDL_ANDROID_BLOCK_ON_PAUSE  0" (both unquoted).
* Adding Android SDL2_net to the Android project
	- Download the latest version of SDL2_net from the project homepage.
	- Copy all **SDLnet*.c/h**, **SDL_net.h** and **Android.mk** files to a newly created directory **android-project\jni\SDL2_net** folder.
	- Edit **android-project\src\org\libsdl\app\SDLActivity.java**, removing **//** before **// "SDL2_net",**.
* Adding required Android Studio symbolic links to the Android project
	- Execute android-studio\app\src\main\generatelinks.bat from an elevated command prompt to generate the symbolic links in the folder.
* How to run tests
	- Run the **remake.bat** file in the project directory(Requires tools repository) and use a PSP emulator to test(like JPCSP, which is supported by the batch file). On Windows, open the Visual C++ project, build and run.
* Deployment instructions
	- Simply build using the devkit(Makefile command **make psp/win/linux [re]build [SDL2[-static]]**(to (re)build SDL2 with(out) static linking)" (alternatives to [re]build exist, like [re]profile and analyze[2]. SDL[2]_net adds network support using said libraries) or Visual C++, copy the executable (UniPCemu_x[arch].exe for windows or EBOOT.PBP for the PSP) to the executable directory, add SDL libraries when needed(automatically done when using MSys/MinGW and most Makefiles), add disk images to use according to the documentation and run the executable. Setting up is integrated into the executable, and it will automatically open the Settings menu for setting up during first execution. The settings are saved into a SETTINGS.INI text file.
	- Add the mingw32/mingw64 parameter to the above command when using MSys/MinGW compilers.
	- To make the Android NDK use the SDL2_net library and compile with internet support, add " useSDL2_net=1" to the usual ndk-build command line or equivalent. A more simple version is using the build.bat to compile(which will automatically use the correct build with(out) SDL2_net). Otherwise, it isn't enabled/used. It's also an automatic process with Android Studio.
	- To use the server functionality on Windows, WinPCap 4.1.3 or a more modern replacement that's compatible needs to be installed to use network cards. Don't forget to remove wpcap.dll and packet.dll from the folder itself.

### Extra files ###

* Dial-up server
	- UniPCemu/modemconnect.slip.scp is a Windows 95 SLIP connect script to connect to the server using the SLIP protocol.
	- UniPCemu/modemconnect.ppp.scp is a Windows 95 PPP connect script to connect to the server using the PPP protocol. 

### Contribution guidelines ###

* Writing tests
	- Undocumented
* Code review
	- Add an issue to the issue tracker and report the change.
* Other guidelines

### Who do I talk to (about original)? ###

* Repo owner or admin / developer SuperFury

-- me 2022
