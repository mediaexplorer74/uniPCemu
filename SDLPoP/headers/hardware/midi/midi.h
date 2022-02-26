// Copyright (C) 2022 Superfury


#ifndef MIDI_H
#define MIDI_H

#include "headers/types.h" //Basic types!

void resetMPU(); //Fully resets the MPU!
byte MIDI_has_data(); //Do we have data to be read?
void MIDI_OUTT(byte data);
byte MIDI_IN();
byte init_MPU(char *filename, byte use_direct_MIDI); //Our own initialise function!
void done_MPU(); //Finish function!

void MPU401_Init(/*Section* sec*/); //From DOSBox (mpu.c)!

void MPU401_Done(); //Finish our MPU! Custom by superfury1!

void update_MPUTimer(DOUBLE timepassed);

void set_MPUTimer(DOUBLE timeout, Handler handler);
void remove_MPUTimer();

#endif