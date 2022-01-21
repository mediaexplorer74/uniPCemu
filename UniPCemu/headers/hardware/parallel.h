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

#ifndef PARALLEL_H
#define PARALLEL_H

typedef void (*ParallelOutputHandler)(byte data);
typedef void (*ParallelControlOUTHandler)(byte control);
typedef byte (*ParallelControlINHandler)();
typedef byte (*ParallelStatusHandler)();

void registerParallel(byte port, ParallelOutputHandler outputhandler, ParallelControlOUTHandler controlouthandler, ParallelControlINHandler controlinhandler, ParallelStatusHandler statushandler);
void tickParallel(DOUBLE timepassed);
void initParallelPorts(byte numports);

#endif
