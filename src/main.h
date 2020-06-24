/* LICENSE TEXT

    dmxplayer for linux based on OLA, RtMidi and oscpack libraries to 
    play DMX cues with MTC sync. It also receives OSC commands to do
    some configurations dynamically.
    Copyright (C) 2020  Stage Lab & bTactic.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Stage Lab Cuems DMX player main header file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#include <string>
#include <filesystem>
#include <csignal>
#include "commandlineparser.h"
#include "dmxplayer.h"
#include "cuems_errors.h"
#include "cuemslogger.h"
#include "cuems_dmxplayerConfig.h"

//////////////////////////////////////////////////////////
// Functions declarations

void showcopyright( void );
void showusage ( void );
void showwarrantydisclaimer( void );
void showcopydisclaimer( void );

// System signal handlers
void sigTermHandler( int signum );
void sigIntHandler( int signum );
void sigUsr1Handler( int signum );