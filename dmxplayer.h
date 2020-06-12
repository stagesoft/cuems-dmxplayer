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
// Stage Lab Cuems DMX player class header file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
#ifndef DMXPLAYER_H
#define DMXPLAYER_H

//////////////////////////////////////////////////////////
// Preprocessor definitions
#define PLAYHEAD_TOLLERANCE 100     // Play head ms displacement tollerance respect MTC

#include <atomic>
#include <chrono>
#include <csignal>
#include <vector>
#include <iostream>
#include <iomanip>
#include <rtmidi/RtMidi.h>

#include <ola/DmxBuffer.h>
#include <ola/client/ClientWrapper.h>
#include <ola/Logging.h>
#include <ola/Callback.h>
#include <ola/io/SelectServer.h>

#include "./mtcreceiver/mtcreceiver.h"
#include "./oscreceiver/oscreceiver.h"

#include "dmxcue_v1.h"
#include "./cuemslogger/cuemslogger.h"
#include "cuems_errors.h"

using namespace std;

class DmxPlayer : public OscReceiver
{
    //////////////////////////////////////////////////////////
    // Public members
    public:
        //////////////////////////////////////////
        // Constructors and destructors
        DmxPlayer(  int port = 8000, 
                    long int initOffset = 0,
                    long int finalWait = 0,
                    const string oscRoute = "", 
                    const string filePath = "dmx.xml",
                    const string uuid = "",
                    const bool stopOnLostFlag = true );
        ~DmxPlayer( void );
        //////////////////////////////////////////

        // MTC receiver object
        MtcReceiver mtcReceiver;                        // Our MTC receiver object

        // DMX Cue / XML manager
        DmxCue_v1 dmxCue;                               // Our CUE data structure

        // Playing head pointer
        static std::atomic<long int> playHead;          // Current playing head position in ms

        // float headSpeed;                             // Head speed (TO DO)
        // float headAccel;                             // Head acceleration (TO DO)
        std::atomic<int> playheadControl = {1};         // Head reading direction
        
        // Control flags and vars
        long int headOffset = 0;                        // Playing head offset respect to MTC
        long int endWaitTime = 0;                       // Do we wait when finished playing?
        bool offsetChanged = false;                     // Was the offset changed via OSC?
        long int endTimeStamp = 0;                      // Our finish timestamp to calculate end wait
        static bool followingMtc;                       // Is player following MTC?
        static bool fading;                             // Are we fading in the cue in or out?
        static bool isSceneFull;                        // Is the cue already full?
        static bool sceneSet;                           // Is the scene already set in the buffer?
        static bool endOfPlay;                          // Have we finished everthing so we can exit?
        static bool outOfFile;                          // Have we reached our end of play?

        // Process identification
        std::string playerUuid = "";                    // Payer UUID for identification porpouses

        bool stopOnMTCLost = true;                      // Do we go on playing if we lost MTC?
        bool mtcSignalLost = false;                     // Flag to check MTC signal lost?
        bool mtcSignalStarted = false;                  // Flag to check MTC signal started?

        // OLA components
        ola::client::OlaClientWrapper olaClientWrapper;
        ola::io::SelectServer *olaServer = NULL;

        // OLA Methods
        void run( void );
        
        // OLA send data callback
        // static bool SendUniverseData(   ola::client::OlaClientWrapper *wrapper, 
        //                                 DmxUniverse_v1* universe );

        static bool SendUniverseData(   DmxPlayer* dp, 
                                        DmxUniverse_v1* universe );

        long int startTimeStamp;

    //////////////////////////////////////////////////////////
    // Private members
    private:
        // Config functions, maybe to be implemented
        // bool loadNodeConfig( void );
        // bool loadMediaConfig( void );

        // Logging functions
        // void log( std::string* message );

        //////////////////////////////////////////////////////////
        // Callbacks

        // Runtime value
        int runError = 0;

    //////////////////////////////////////////////////////////
    // Protected members
    protected:
        // OSC messages processor
        virtual void ProcessMessage(    const osc::ReceivedMessage& m, 
                                    const IpEndpointName& /*remoteEndpoint*/ );


};

#endif // DMXPLAYER_H