#include <stdio.h> //I/O support!
#include <windows.h>

int patchexecutable(char *filename)
{
	FILE *f;
	unsigned long int offset;
	WORD subsystem = 2; //The Windows GUI application subsystem!
	f = fopen(filename,"rb+");
	if (!f) return 0; //Abort if not found!
	fseek(f,0x3C,SEEK_SET); //Goto offset of our PE header location!
	if (fread(&offset,1,4,f)!=4) //Read the offset we need!
	{
		fclose(f);
		return 1; //Abort error!
	}
	fseek(f,offset+0x5C,SEEK_SET); //Goto offset of our subsystem!
	fwrite(&subsystem,1,2,f);
	fclose(f); //Finished!
	return 0; //OK!
}

// Our application entry point.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	//Main builds
	//First, current directory!
	//First, 32-bit version!
	if (patchexecutable("UniPCemu_x86.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("UniPCemu_x64.exe")) return 1; //Error!

	//mingw subdirectories!
	//First, 32-bit version!
	if (patchexecutable("mingw32/UniPCemu_x86.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("mingw64/UniPCemu_x64.exe")) return 1; //Error!
	//First, 32-bit version!
	if (patchexecutable("mingw32_winpcap/UniPCemu_x86.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("mingw64_winpcap/UniPCemu_x64.exe")) return 1; //Error!

	//msys subdirectories!
	//First, 32-bit version!
	if (patchexecutable("msys32/UniPCemu_x86.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("msys64/UniPCemu_x64.exe")) return 1; //Error!
	//First, 32-bit version!
	if (patchexecutable("msys32_winpcap/UniPCemu_x86.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("msys64_winpcap/UniPCemu_x64.exe")) return 1; //Error!

	//server version
	//First, 32-bit version!
	if (patchexecutable("server/UniPCemu_x64.exe")) return 1; //Error!
	//Second, 64-bit version!
	if (patchexecutable("server/UniPCemu_x64.exe")) return 1; //Error!

	return 0; //Finished!
}