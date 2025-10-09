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
// Stage Lab Cuems constants definitions
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#ifndef CUEMS_CONSTANTS_H
#define CUEMS_CONSTANTS_H

namespace CuemsConstants {

// DMX related constants
constexpr int MIN_UNIVERSE_ID = 0;
constexpr int MAX_UNIVERSE_ID = 65535;
constexpr int MIN_CHANNEL_ID = 0;
constexpr int MAX_CHANNEL_ID = 512;
constexpr int MIN_DMX_VALUE = 0;
constexpr int MAX_DMX_VALUE = 255;

// Network related constants
constexpr int MIN_PORT_NUMBER = 1;
constexpr int MAX_PORT_NUMBER = 65535;

// Timing related constants
constexpr int MILLISECONDS_PER_SECOND = 1000;
constexpr int MILLISECONDS_PER_MINUTE = 60 * MILLISECONDS_PER_SECOND;
constexpr int MILLISECONDS_PER_HOUR = 60 * MILLISECONDS_PER_MINUTE;

// OLA callback timeout (ms)
constexpr int OLA_CALLBACK_TIMEOUT_MS = 10;

// Universe fetch look ahead time (ms)
constexpr int UNIVERSE_FETCH_LOOK_AHEAD_MS = 50;

} // namespace CuemsConstants

#endif // CUEMS_CONSTANTS_H
