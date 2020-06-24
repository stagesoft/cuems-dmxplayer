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
// Stage Lab Cuems DMX player class code file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#include "dmxplayer.h"

using namespace std;

////////////////////////////////////////////
// Initializing static class members
std::atomic<long int> DmxPlayer::playHead(0);
bool DmxPlayer::followingMtc = true;
bool DmxPlayer::fading = false;
bool DmxPlayer::isSceneFull = false;
bool DmxPlayer::sceneSet = false;
bool DmxPlayer::endOfPlay = false;
bool DmxPlayer::outOfFile = false;

//////////////////////////////////////////////////////////
DmxPlayer::DmxPlayer(   int port, 
                        long int initOffset,
                        long int finalWait,
                        const string oscRoute,
                        const string filePath,
                        const string uuid,
                        const bool stopOnLostFlag )
                        :   // Members initialization
                        OscReceiver(port, oscRoute),
                        dmxCue(filePath),
                        headOffset(initOffset),
                        endWaitTime(finalWait),
                        playerUuid(uuid),
                        stopOnMTCLost(stopOnLostFlag)
{
    //////////////////////////////////////////////////////////
    // Config tasks to be implemented later maybe
    // loadNodeConfig();
	// loadMediaConfig();

    //////////////////////////////////////////////////////////
    // Set up working class members

    // Starting OLA logging
    ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);

    if (!olaClientWrapper.Setup()) {
        std::cerr << "OLA setup failed" << endl;
        CuemsLogger::getLogger()->logError("OLA setup failed!");
        CuemsLogger::getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_FAILED_OLA_SETUP) );

        exit( CUEMS_EXIT_FAILED_OLA_SETUP );
    }

    olaServer = olaClientWrapper.GetSelectServer();
    if ( olaServer == NULL ) {
        std::cerr << "OLA server not reached" << endl;
        CuemsLogger::getLogger()->logError("OLA server failed!");
        CuemsLogger::getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_FAILED_OLA_SEL_SERV) );

        exit( CUEMS_EXIT_FAILED_OLA_SEL_SERV );
    }
    else {
        // Let's set a callback for each universe deailed in the scene
        CuemsLogger::getLogger()->logInfo("Setting OLA callback for " + std::to_string(dmxCue.dmxScene.universes.size()) + " universes");
        for ( std::vector<DmxUniverse_v1>::iterator it = dmxCue.dmxScene.universes.begin() ; it != dmxCue.dmxScene.universes.end() ; it++ ) {
            olaServer->RegisterRepeatingTimeout( 100, ola::NewCallback( &DmxPlayer::SendUniverseData, this, &(*it) ) );
        }
    }

}

//////////////////////////////////////////////////////////
DmxPlayer::~DmxPlayer( void ) {

}

//////////////////////////////////////////////////////////
void DmxPlayer::ProcessMessage( const osc::ReceivedMessage& m, 
            const IpEndpointName& /*remoteEndpoint*/ )
{
    try {
        // Parsing OSC DmxPlayer messages
        // Offset
        if ( (string) m.AddressPattern() == (OscReceiver::oscAddress + "/offset") ) {
            // osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
            // args >> volumeMaster[0] >> osc::EndMessage;
            float offsetOSC;
            m.ArgumentStream() >> offsetOSC >> osc::EndMessage;
            offsetOSC = floor(offsetOSC);

            CuemsLogger::getLogger()->logInfo("OSC: new offset value " + std::to_string((long int)offsetOSC));

            // Offset argument in OSC command is in milliseconds
            // so we need to calculate in bytes in our file

            headOffset = offsetOSC;

            offsetChanged = true;

        // Wait
        } else if ( (string) m.AddressPattern() == (OscReceiver::oscAddress + "/wait") ) {
            // osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
            // args >> volumeMaster[0] >> osc::EndMessage;
            float waitOSC;
            m.ArgumentStream() >> waitOSC >> osc::EndMessage;
            waitOSC = floor(waitOSC);

            CuemsLogger::getLogger()->logInfo("OSC: new end wait value " + std::to_string((long int)waitOSC));

            endWaitTime = waitOSC;             // In milliseconds
        // Load
        } else if ( (string) m.AddressPattern() == (OscReceiver::oscAddress + "/load") ) {
            const char* newPath;
            m.ArgumentStream() >> newPath >> osc::EndMessage;
            // dmxCue.xmlPath = newPath;
            CuemsLogger::getLogger()->logInfo("OSC: /load command, not working yet");
            // audioFile.close();
            // CuemsLogger::getLogger()->logInfo("OSC: previous file closed");
            // audioFile.loadFile(audioPath);
            // CuemsLogger::getLogger()->logInfo("OSC: loaded new path -> " + audioPath);
        // Play/pause
        } else if ( (string) m.AddressPattern() == (OscReceiver::oscAddress + "/play") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /play command");
            if ( playheadControl != 0 )
                playheadControl = 0;
            else 
                playheadControl = 1;
        // Stop
        } else if ( (string) m.AddressPattern() == (OscReceiver::oscAddress + "/stop") ) {
            // TO DO : right now is the same as play/pause... Don't know if there 
            //          will be other implementations of the command...
            CuemsLogger::getLogger()->logInfo("OSC: /stop command");
            if ( playheadControl != 0 )
                playheadControl = 0;
            else 
                playheadControl = 1;
        // Quit
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/quit") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /quit command");
            raise(SIGTERM);
        // Check
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/check") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /check command");
            raise(SIGUSR1);
        // Stop on lost
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/stoponlost") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /stoponlost command");
            stopOnMTCLost = !stopOnMTCLost;
        // In
        /*
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/in") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /in command");
            fading = true;
            */
        // Out
        /*
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/out") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /out command");
            fading = true;
            */
        }

    } catch ( osc::Exception& e ) {
        // any parsing errors such as unexpected argument types, or 
        // missing arguments get thrown as exceptions.
        std::cout << "error while parsing message: "
            << m.AddressPattern() << ": " << e.what() << "\n";
    }
}

//////////////////////////////////////////////////////////
// OLA per DMX universe MTC frame player
bool DmxPlayer::SendUniverseData(DmxPlayer* dp, DmxUniverse_v1* universe) {

    static float fadeMult = 0;
    static ola::DmxBuffer buffer;

    // If we are receiving MTC and following it...
    // Or we are not receiving it and we do not stop on its lost
    // And we haven't reached the end of playing time...
    if (    ( (dp->mtcReceiver.isTimecodeRunning && dp->followingMtc) || 
            (dp->mtcSignalLost && !dp->stopOnMTCLost) ) &&
            !dp->endOfPlay && dp->playheadControl == 1 ) {
        // unsigned int count = 0;
        // unsigned int read = 0;

        // Check play control flags
        // If there is MTC signal and we haven't started, check it
        if ( dp->mtcReceiver.isTimecodeRunning ) {
            if ( !dp->mtcSignalStarted ) {
                CuemsLogger::getLogger()->logInfo("MTC -> Play started");
                dp->mtcSignalStarted = true;
            }
            else {
                if ( dp->mtcSignalLost ) {
                    CuemsLogger::getLogger()->logInfo("MTC -> Play resumed");
                }
            }

            // Receiving MTC, means that signal is not lost anymore
            dp->mtcSignalLost = false;
        }

        if ( dp->followingMtc ) {
            dp->playHead = dp->mtcReceiver.mtcHead;
        }
        else if ( dp->mtcSignalStarted ) {
            // If we are not following MTC but the start signal was already 
            // set... We update the play head the amount of one frame per
            // loop in here, to follow our own timing run
            playHead += ( 1000 / dp->mtcReceiver.curFrameRate );
        }

        long int currentPos = dp->playHead + dp->headOffset;
        long int totalLength = dp->dmxCue.getLength() + dp->dmxCue.getInTime();
        long int totalLengthOut = totalLength + dp->dmxCue.getOutTime();

        // Calculate enveloping moment and multiplier
        if ( currentPos < 0 ) {
            dp->fading = false;
            dp->isSceneFull = false;
            dp->sceneSet = true;
            dp->outOfFile = false;
        }
        else if ( currentPos < dp->dmxCue.getInTime() ) {
            dp->fading = true;
            // Fade multiplier calculus
            fadeMult = (float)currentPos / dp->dmxCue.getInTime();
            dp->isSceneFull = false;
            dp->sceneSet = false;
            dp->outOfFile = false;
        }
        else if ( currentPos < totalLength ) {
            dp->fading = false;
            // Fade multiplier calculus
            fadeMult = 1.0;
            dp->isSceneFull = true;
            dp->sceneSet = false;
            dp->outOfFile = false;
        }
        else if ( currentPos > totalLength && currentPos < totalLengthOut ) {
            dp->fading = true;
            // Fade multiplier calculus
            fadeMult = (float) (totalLengthOut - currentPos) / dp->dmxCue.getOutTime();
            dp->isSceneFull = false;
            dp->sceneSet = false;
            dp->outOfFile = false;
        }
        else {
            dp->fading = false;
            dp->isSceneFull = false;
            dp->sceneSet = false;
            dp->outOfFile = true;
        }

        //////////////////////////////////////////////////////////
        // DMX PLAY
        if ( !dp->isSceneFull ) {
            // We are not in a full scene moment, fading in or out
            for (   std::vector<DmxChannel_v1>::iterator it = universe->channels.begin() ; 
                    it != universe->channels.end() ; it++ ) {

                // Let's send all this dmx data
                buffer.SetChannel( (*it).id, (uint8_t)(*it).value * fadeMult );
            }
        }
        else if ( !dp->sceneSet ) {
            // We are in a full scene moment, if not set yet, set it full
            for (   std::vector<DmxChannel_v1>::iterator it = universe->channels.begin() ; 
                    it != universe->channels.end() ; it++ ) {

                // Let's send all this dmx data
                buffer.SetChannel( (*it).id, (*it).value );
            }

            dp->sceneSet = true;
        }

        dp->olaClientWrapper.GetClient()->SendDMX(universe->id, buffer, ola::client::SendDMXArgs());
        //////////////////////////////////////////////////////////

        // If we are already out of the play boundaries... We check for waiting times...
        if ( dp->outOfFile ) {
            // Maybe it is the end of the stream
            if ( dp->endWaitTime == 0 ) {
                // If there is not waiting time, we just finish
                // and we end the stream by returning a positive value
                CuemsLogger::getLogger()->logInfo("No end wait time set, ending audioplayer");
                dp->olaClientWrapper.GetSelectServer()->Terminate();
                dp->endOfPlay = true;
                return false;
            }
            else {
                // If we have waiting time set...
                if ( dp->endTimeStamp == 0 ) {
                    // We note down our timestamp
                    dp->endTimeStamp = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();

                    std::string str;
                    if ( dp->endWaitTime == __LONG_MAX__ ) 
                        str = "for quit command";
                    else
                        str = std::to_string( dp->endWaitTime ) + " ms";

                    CuemsLogger::getLogger()->logInfo("Out of file boundaries, waiting " + str);
                }

                long int timecodeNow = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();

                if ( ( timecodeNow - dp->endTimeStamp ) > dp->endWaitTime ) {
                    CuemsLogger::getLogger()->logInfo("Waiting time exceded, ending audioplayer");
                    dp->olaClientWrapper.GetSelectServer()->Terminate();
                    dp->endOfPlay = true;
                    return false;
                }
            }
        }
    }
    else {
        if ( ! dp->mtcReceiver.isTimecodeRunning && dp->mtcSignalStarted && !dp->mtcSignalLost ) {
            CuemsLogger::getLogger()->logInfo("MTC signal lost");
            dp->mtcSignalLost = true;
        }
    }

    return true;
}

//////////////////////////////////////////////////////////
void DmxPlayer::run( void ) {
    // Let's mark the playHead with the current time
    startTimeStamp = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();
    CuemsLogger::getLogger()->logInfo( "Start timestamp: " + std::to_string(startTimeStamp) );
    
    // Play head init set
    playHead = 0;

    // Run OLA set callbacks
    olaServer->Run();
}

//////////////////////////////////////////////////////////
/*
bool DmxPlayer::loadNodeConfig( void ) {
    cout << "Node config read!" << endl;
    return true;
}
*/

//////////////////////////////////////////////////////////
/*
bool DmxPlayer::loadMediaConfig( void ) {
    cout << "Media config read!" << endl;
    return true;
}
*/
