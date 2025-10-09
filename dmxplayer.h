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

#include <atomic>
#include <chrono>
#include <csignal>
#include <vector>
#include <list>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <rtmidi/RtMidi.h>

#include <ola/DmxBuffer.h>
#include <ola/client/ClientWrapper.h>
#include <ola/Logging.h>
#include <ola/Callback.h>
#include <ola/io/SelectServer.h>

#include "./mtcreceiver/mtcreceiver.h"
#include "./oscreceiver/oscreceiver.h"

#include "./cuemslogger/cuemslogger.h"
#include "cuems_errors.h"
#include "cuems_constants.h"

//using namespace std;

class DmxPlayer : public OscReceiver
{
    //////////////////////////////////////////////////////////
    // Public members
    public:
        //////////////////////////////////////////
        // Constructors and destructors
        DmxPlayer(  int port = 8000,
                    const string oscRoute = "",
                    const bool stopOnLostFlag = true,
                    const bool followMTCFlag = false,
                    const std::string &client_name = "DMX_Player"
                    );
        ~DmxPlayer( void );
        //////////////////////////////////////////

        // OLA Methods
        void run( void );
        bool IsRunning() const {return olaServer->IsRunning();}

    protected:
        // MTC receiver object
        MtcReceiver mtcReceiver;                        // Our MTC receiver object

        // Playing head pointer
        static std::atomic<long int> playHead;          // Current playing head position in ms

        bool stopOnMTCLost = true;                      // Do we go on playing if we lost MTC?
        bool mtcSignalLost = false;                     // Flag to check MTC signal lost?
        bool mtcSignalStarted = false;                  // Flag to check MTC signal started?
        bool followMTC = false;                         // Do we follow MTC or paused

        // OLA components
        ola::client::OlaClientWrapper olaClientWrapper;
        ola::io::SelectServer *olaServer = NULL;

        // Data structures for managing scene transitions

        using FrameValues = std::map<uint16_t, uint8_t>;      // channel_id -> value
        using SceneValues = std::map<uint32_t, FrameValues>;  // universe_id -> FrameValues

        struct SceneTransitionInfo
        {
          SceneValues m_sceneValues;
          long int m_mtcStart = 0;
          int m_fadeTime = 0;
        };

        struct ChannelTransition
        {
          long int mtc0 = 0;
          long int mtc1 = 0;
          uint8_t val0 = 0;
          uint8_t val1 = 0;
        };

        using ChannelTransitions = std::map<uint16_t, ChannelTransition>; // channel_id -> ChannelTransition

        struct ActiveUniverse
        {
          uint32_t  m_id;
          ola::DmxBuffer m_channelsBuffer;
          int m_state = 0;
          ChannelTransitions m_channelTransitions;
        };

        // Scene transition data
        std::list<SceneTransitionInfo> m_scenes;              // SceneTransitionInfo sorted by MTC
        std::map<uint32_t, ActiveUniverse> m_activeUniverses; // universe_id -> ActiveUniverse
        SceneTransitionInfo m_nextScene;
        std::mutex m_scenesMutex;     // protects m_scenes

    protected:
        static bool SendUniverseData(   DmxPlayer* dp);
        static void OnFetchDMX(DmxPlayer* dp, uint32_t univ_id,
            const ola::client::Result&, const ola::client::DMXMetadata&, const ola::DmxBuffer&);

        void processScenes();
        void updateActiveUniverses();
        long int convertTime(const std::string_view &time);

        long int startTimeStamp;
        // Start fetching universe data before transition start time
        long int universeFetchLookAheadTime = CuemsConstants::UNIVERSE_FETCH_LOOK_AHEAD_MS;

        std::atomic<int> m_inBundle {0};

    //////////////////////////////////////////////////////////
    // Private members
    private:
        // Runtime value
        int runError = 0;

    //////////////////////////////////////////////////////////
    // Protected members
    protected:
        // OSC messages processor
        virtual void ProcessMessage(    const osc::ReceivedMessage& m,
                                    const IpEndpointName& /*remoteEndpoint*/ );

        virtual void ProcessBundle( const osc::ReceivedBundle& b,
                                    const IpEndpointName& remoteEndpoint );


};

#endif // DMXPLAYER_H
