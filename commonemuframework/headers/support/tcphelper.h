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

#ifndef TCPHELPER_H
#define TCPHELPER_H

#include "headers/types.h" //Basic types!
#include "headers/support/fifobuffer.h" //FIFO buffer for transferred data!

//Based on http://stephenmeier.net/2015/12/23/sdl-2-0-tutorial-04-networking/

//General support for the backend.
void initTCP(); //Initialize us!
void doneTCP(void); //Finish us!

//Server side
byte TCP_ConnectServer(word port, word numConnections); //Try to connect as a server. 1 when successful, 0 otherwise(not ready).
byte TCPServerRunning(); //Is the server running?
sword acceptTCPServer(); //Accept and connect when asked of on the TCP server! 1=Connected to a client, 0=Not connected.
void TCPServer_Unavailable(); //We can't accept TCP connection now?
void stopTCPServer(); //Stop the TCP server!

//Client side(also used when the server is connected).
sword TCP_ConnectClient(const char *destination, word port); //Connect as a client!
byte TCP_SendData(sword id, byte data); //Send data to the other side(both from server and client).
sbyte TCP_ReceiveData(sword id, byte *result); //Receive data, if available. 0=No data, 1=Received data, -1=Not connected anymore!
byte TCP_DisconnectClientServer(sword id); //Disconnect either the client or server, whatever state we're currently using.

#endif