/*

Copyright (C) 2019 - 2021 Superfury

This file is part of The Common Emulator Framework.

The Common Emulator Framework is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The Common Emulator Framework is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with The Common Emulator Framework.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "headers/types.h" //Basic type support!
#include "headers/support/log.h" //Logging support!
#include "headers/support/tcphelper.h" //TCP module support!

#define NET_LOGFILE "net"

#if defined(SDL_NET) || defined(SDL2_NET)
#if defined(SDL2) && defined(SDL2_NET)
#define GOTNET
#include "SDL_net.h" //SDL2 NET support!
#else
#ifdef SDL_NET
#define GOTNET
#include "SDL_net.h"
#endif
#endif
#endif

//Some server port to use when listening and sending data packets.
#ifdef GOTNET
TCPsocket server_socket;
TCPsocket mysock[0x100]; //Connection 0 is reserved!
byte allocatedconnections[0x100]; //Connection 0 is reserved!
SDLNet_SocketSet listensocketset[0x100]; //Socket set of a connection for listening!
byte Client_READY[0x100]; //Client ready for use?
word availableconnections = 1; //Available connections(one less than can be allocated(reserved connection), because of sending connection(#0))?
word totalconnections = 1; //Total amount of connections!
word SERVER_PORT = 23; //What server port to apply?
#endif

byte NET_READY = 0; //Are we ready to be used?
byte Server_READY = 0; //Server ready for use?

void initTCP() //Initialize us!
{
#ifdef GOTNET
	atexit(&doneTCP); //Automatically terminate us when used!
#ifdef SDL2
	if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_EVENTS) != 0) {
#else
	if (SDL_Init(SDL_INIT_TIMER) != 0) {
#endif
		dolog(NET_LOGFILE, "ER: SDL_Init: %s\n", SDL_GetError());
		NET_READY  = 0; //Not ready!
		return;
	}
 
	if(SDLNet_Init() == -1) {
		dolog(NET_LOGFILE, "ER: SDLNet_Init: %s\n", SDLNet_GetError());
		NET_READY = 0; //Not ready!
		return;
	}
	memset(&allocatedconnections, 0, sizeof(allocatedconnections)); //Save if allocated or not!
	memset(&Client_READY, 0, sizeof(Client_READY)); //Init client ready status!
	availableconnections = 1; //init!
	totalconnections = availableconnections; //init!
	NET_READY = 1; //NET is ready!
#endif
	//Initialize buffers for the server, as well as for the client, if used!
}

void TCPServer_INTERNAL_startserver();
void TCPServer_INTERNAL_stopserver(byte fullstop);

byte TCP_ConnectServer(word port, word numConnections)
{
#ifdef GOTNET
	if (Server_READY) return 1; //Already started!
	Server_READY = 0; //Not ready by default!
	if (!NET_READY)
	{
		return 0; //Fail automatically!
	}
	availableconnections = MIN(numConnections,NUMITEMS(allocatedconnections)-1); //How many to allocate maximum!
	totalconnections = availableconnections; //Available = Total connections!

	SERVER_PORT = port; //Set the port to use!

	//Clear the server I/O buffers!
	return Server_READY; //Connected!
#else
return 0; //Cannot connect!
#endif
}
#ifdef GOTNET
sword allocTCPid(byte isClient) //Allocate an connection to use!
{
	byte *connection, *endconnection;
	sword connectionid;
	if (!availableconnections) return -1; //Couldn't allocate!
	--availableconnections; //Indicate we're allocating one!
	connection = &allocatedconnections[0]; //Check them all!
	endconnection = &allocatedconnections[NUMITEMS(allocatedconnections)]; //Where to end!
	connectionid = 0; //Init ID!
	for (; (connection != endconnection);) //Check them all!
	{
		if (!*connection) //Not allocated?
		{
			*connection = (isClient?1:2); //Allocated to use for client or server!
			return connectionid; //Give the ID(base 0)!
		}
		++connection; //Next connection!
		++connectionid; //Next ID!
		if (isClient)
		{
			++availableconnections; //Restore!
			return -1; //Client connection is reserved #0 only, so can't allocate!
		}
	}
	++availableconnections; //One more available again!
	return -1; //Couldn't find any!
}

byte freeTCPid(sword id) //Free a connection to use!
{
	if (id < 0) return 0; //Can't release when not allocated!
	if (availableconnections>=NUMITEMS(allocatedconnections)) return 0; //Couldn't release: nothing allocated!
	if (id >= NUMITEMS(allocatedconnections)) return 0; //Invalid ID!
	if (allocatedconnections[id]) //Allocated?
	{
		allocatedconnections[id] = 0; //Free!
		++availableconnections; //One more available!
		return 1; //Deallocated!
	}
	return 0; //Not found!
}

sword TCP_connectClientFromServer(sword id, TCPsocket source)
{
	//Accept a client as a new server?
	Client_READY[id] = 0; //Init ready status!
	
	mysock[id]=0;
	listensocketset[id]=0;
	if(source!=0) {
		mysock[id] = source;
		listensocketset[id] = SDLNet_AllocSocketSet(1);
		if (!listensocketset[id])
		{
			freeTCPid(id); //Free the connection!
			mysock[id] = NULL; //Deallocated!
			return -1;
		}
		if (SDLNet_TCP_AddSocket(listensocketset[id], source) != -1)
		{
			Client_READY[id] = 2; //Connected as a server!
			if (availableconnections == 0) TCPServer_INTERNAL_stopserver(0); //Stop serving if no connections left!
			return id; //Successfully connected!
		}
		//Free resources!
		SDLNet_TCP_Close(mysock[id]); //Close the TCP connection!
		mysock[id] = NULL; //Deallocated!
		SDLNet_FreeSocketSet(listensocketset[id]); //Free the socket set allocated!
		listensocketset[id] = NULL; //Deallocated!
	}
	freeTCPid(id); //Free the allocated ID!
	return -1; //Accepting calls aren't supported yet!
}
#endif

sword acceptTCPServer() //Update anything needed on the TCP server!
{
#ifdef GOTNET
	sword TCPid;
	if (NET_READY==0) return -1; //Not ready!
	TCPServer_INTERNAL_startserver(); //Start the server unconditionally, if required!
	if (Server_READY != 1) return -1; //Server not running? Not ready!
	TCPsocket new_tcpsock;
	new_tcpsock=SDLNet_TCP_Accept(server_socket);
	if(!new_tcpsock) {
		return -1; //Nothing to connect!
	}

	if ((TCPid = allocTCPid(0)) < 0) //Couldn't allocate TCP ID!
	{
		SDLNet_TCP_Close(new_tcpsock); //Disconnect the connected client!
		return -1; //Abort: Nothing to connect!
	}

	return TCP_connectClientFromServer(TCPid,new_tcpsock); //Accept as a client!
#endif
	return -1; //Not supported!
}

byte TCPServerRunning()
{
	return Server_READY; //Is the server running?
}

void stopTCPServer()
{
#ifdef GOTNET
	TCPServer_INTERNAL_stopserver(1); //Perform a full stop!
	Server_READY = 0; //Not ready anymore!
#endif
}

sword TCP_ConnectClient(const char *destination, word port)
{
#ifdef GOTNET
	IPaddress openip;
	byte TCP_Serving;
	sword id;
	if ((id = allocTCPid(1))<0) return -1; //Invalid ID allocated?
	if (Client_READY[id]) //Already connected?
	{
		return -1; //Can't connect: already connected!
	}
	TCP_Serving = Server_READY; //Were we serving?
	if (TCP_Serving==1) //Is the server running? We need to stop it to prevent connecting to ourselves!
	{
		TCP_DisconnectClientServer(0); //Disconnect if connected already!
		TCPServer_INTERNAL_stopserver(0); //Stop the server to prevent connections to us! 
	}
	//Ancient versions of SDL_net had this as char*. People still appear to be using this one.
	if (!SDLNet_ResolveHost(&openip,destination,port))
	{
		listensocketset[id] = SDLNet_AllocSocketSet(1);
		if (!listensocketset[id]) {
			freeTCPid(id); //Free the used ID!
			return -1; //Failed to connect!
		}
		mysock[id] = SDLNet_TCP_Open(&openip);
		if (!mysock[id]) {
			mysock[id] = NULL; //Deallocated!
			SDLNet_FreeSocketSet(listensocketset[id]); //Free the socket set allocated!
			listensocketset[id] = NULL; //Deallocated!
			freeTCPid(id); //Free the used ID!
			return -1; //Failed to connect!
		}
		if (SDLNet_TCP_AddSocket(listensocketset[id], mysock[id])!=-1)
		{
			Client_READY[id]=1; //Connected as a client!
			return id; //Successfully connected!
		}
		//Free resources!
		SDLNet_TCP_Close(mysock[id]); //Close the TCP connection!
		mysock[id] = NULL; //Deallocated!
		SDLNet_FreeSocketSet(listensocketset[id]); //Free the socket set allocated!
		listensocketset[id] = NULL; //Deallocated!
	}
	freeTCPid(id); //Free the used ID!
	return -1; //Failed to connect!
#endif
	return -1; //Not supported!
}

byte TCP_SendData(sword id, byte data)
{
#ifdef GOTNET
	if (id < 0) return 0; //Invalid ID!
	if (id >= NUMITEMS(allocatedconnections)) return 0; //Invalid ID!
	if (!allocatedconnections[id]) return 0; //Not allocated!
	if (!Client_READY[id]) return 0; //Not connected?
	if(SDLNet_TCP_Send(mysock[id], &data, 1)!=1) {
		return 0;
	}
	return 1;
#endif
	return 0; //Not supported!
}

sbyte TCP_ReceiveData(sword id, byte *result)
{
#ifdef GOTNET
	if (id < 0) return -1; //Invalid ID!
	if (id >= NUMITEMS(allocatedconnections)) return -1; //Invalid ID!
	if (!allocatedconnections[id]) return -1; //Not allocated!
	if (!Client_READY[id]) return -1; //Not connected?
	if(SDLNet_CheckSockets(listensocketset[id],0))
	{
		byte retval=0;
		if(SDLNet_TCP_Recv(mysock[id], &retval, 1)!=1) {
			return -1; //Socket closed
		} else
		{
			*result = retval; //Data read!
			return 1; //Got data!
		}
	}
	else return 0; //No data to receive!
#endif
	return -1; //No socket by default!
}

byte TCP_DisconnectClientServer(sword id)
{
#ifdef GOTNET
	if (id < 0) return 0; //Invalid ID!
	if (id >= NUMITEMS(allocatedconnections)) return 0; //Invalid ID!
	if (!allocatedconnections[id]) return 0; //Not allocated!
	if (!Client_READY[id]) return 0; //Not connected?
	if (listensocketset[id])
	{
		if (mysock[id]) //Valid socket to remove?
		{
			SDLNet_TCP_DelSocket(listensocketset[id], mysock[id]); //Remove from the socket set!
		}
		SDLNet_FreeSocketSet(listensocketset[id]); //Free the set!
		listensocketset[id] = NULL; //Not allocated anymore!
	}
	if(mysock[id]) {
		SDLNet_TCP_Close(mysock[id]);
		mysock[id] = NULL; //Not allocated anymore!
	}

	Client_READY[id] = 0; //Ready again!
	if (freeTCPid(id)) //Freed the ID for other uses!
	{
		return 1; //Disconnected!
	}
#endif
	return 0; //Error: not connected!
}

void TCPServer_INTERNAL_stopserver(byte fullstop)
{
#ifdef GOTNET
	if (!NET_READY) return; //Abort when not running properly!
	if (Server_READY==1) //Loaded the server?
	{
		if (fullstop) //Full stop?
		{
			//Disconnect all that's connected!
			for (;;)
			{
				TCPsocket new_tcpsock;
				new_tcpsock = SDLNet_TCP_Accept(server_socket);
				if (!new_tcpsock) break; //Stop searching when none!
				SDLNet_TCP_Close(new_tcpsock); //Disconnect the connected client!
			}
		}
		SDLNet_TCP_Close(server_socket);
		server_socket = NULL; //Nothing anymore!
		--Server_READY; //Layer destroyed!
		if (!fullstop) //Not fully stopped(paused)?
		{
			Server_READY = 2; //We're paused!
		}
	}
#endif
}

void TCPServer_Unavailable()
{
	TCPServer_INTERNAL_stopserver(0); //Perform a partial stop, since we're unavailable!
}

void TCPServer_INTERNAL_startserver()
{
#ifdef GOTNET
	if (Server_READY != 1) //Not started?
	{
		if (availableconnections == 0) return; //No available connections prevent restart!
		IPaddress ip;
		if (SDLNet_ResolveHost(&ip, NULL, SERVER_PORT) == -1) {
			return; //Failed!
		}

		server_socket = SDLNet_TCP_Open(&ip);
		if (server_socket == NULL) {
			return; //Failed!
		}

		Server_READY = 1; //First step successful! Ready to receive!
	}
#endif
}

void doneTCP(void) //Finish us!
{
#ifdef GOTNET
	if (NET_READY) //Loaded?
	{
		SDLNet_Quit();
		NET_READY = 0; //Not ready anymore!
	}
#endif
}