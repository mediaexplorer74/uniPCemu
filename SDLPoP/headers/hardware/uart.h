/*

Copyright (C) 2019 - 2021 Superfury

This file is part of UniPCemu.

UniPCemu is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

UniPCemu is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with UniPCemu.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef UART_H
#define UART_H

typedef void (*UART_setmodemcontrol)(byte control);
typedef byte (*UART_getmodemstatus)(); //Retrieve the modem status!
typedef byte(*UART_hasdata)();
typedef byte (*UART_senddata)(byte value);
typedef byte(*UART_receivedata)();

void initUART(); //Init UART!
void UART_registerdevice(byte portnumber, UART_setmodemcontrol setmodemcontrol, UART_getmodemstatus getmodemstatus, UART_hasdata hasdata, UART_receivedata receivedata, UART_senddata senddata);
void updateUART(DOUBLE timepassed); //Update UART timing!
byte allocUARTport(); //UART port when set, 0xFF for no port available!
#endif
