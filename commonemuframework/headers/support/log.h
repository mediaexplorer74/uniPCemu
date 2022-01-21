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

#ifndef LOG_H
#define LOG_H

void initlog(); //Init log info!
void dolog(char *filename, const char *format, ...); //Logging functionality!
byte log_logtimestamp(byte logtimestamp); //Set/get the timestamp logging setting. 0/1=Set, 2+=Get only. Result: Old timestamp setting.
void closeLogFile(byte islocked); //Are we closing the log file?

#endif