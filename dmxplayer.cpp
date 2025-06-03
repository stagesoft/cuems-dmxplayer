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
                        //dmxCue(filePath),
                        headOffset(initOffset),
                        endWaitTime(finalWait),
                        playerUuid(uuid),
                        stopOnMTCLost(stopOnLostFlag)
{
    (void)filePath;
    //////////////////////////////////////////////////////////
    // Config tasks to be implemented later maybe
    // loadNodeConfig();
	// loadMediaConfig();

    //////////////////////////////////////////////////////////
    // Set up working class members

    // Starting OLA logging
    ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);

    /*
    {
      DmxUniverse_v1 u1;
      u1.id = 1;
      u1.channels.resize(10);
      for (uint j = 0; j < u1.channels.size(); ++j) {
        u1.channels[j].id = j;
        u1.channels[j].value = 255;
        std::cout << "set channel " << j << std::endl;
      }
      m_universes.push_back(u1);
    }
    */

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
        /* TODO
        CuemsLogger::getLogger()->logInfo("Setting OLA callback for " + std::to_string(dmxCue.dmxScene.universes.size()) + " universes");
        for ( std::vector<DmxUniverse_v1>::iterator it = dmxCue.dmxScene.universes.begin() ; it != dmxCue.dmxScene.universes.end() ; it++ ) {
            olaServer->RegisterRepeatingTimeout( 100, ola::NewCallback( &DmxPlayer::SendUniverseData, this, &(*it) ) );
        }
        */
        CuemsLogger::getLogger()->logInfo("Setting OLA callback for " + std::to_string(m_universes.size()) + " universes");
        olaServer->RegisterRepeatingTimeout( 10, ola::NewCallback( &DmxPlayer::SendUniverseData, this ) );
    }

}

//////////////////////////////////////////////////////////
DmxPlayer::~DmxPlayer( void ) {

}

void DmxPlayer::ProcessBundle( const osc::ReceivedBundle& b,
                               const IpEndpointName& remoteEndpoint )
{
  ++m_inBundle;
  // set 'now' MTC by default;
  m_nextScene.m_mtcStart = playHead;
  std::cout << "DmxPlayer::ProcessBundle => " << m_inBundle << std::endl;
  OscReceiver::ProcessBundle(b, remoteEndpoint);
  --m_inBundle;
  std::cout << "DmxPlayer::ProcessBundle <= " << m_inBundle
    << "  values:" << m_nextScene.m_sceneValues.size() << std::endl;
  m_scenes.push_back(std::move(m_nextScene));
  /*
  std::cout << "   count = " << m_nextScene.m_sceneValues.size()
    << " ~> " << m_scenes.back().m_sceneValues.size() << std::endl;
  */
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
            std::cout << "/offset  " << m.ArgumentCount() << "   " << m.TypeTags() << std::endl;
            auto it = m.ArgumentsBegin();
            while (it != m.ArgumentsEnd()) {
              ++it;
            }

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
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/frame") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /frame command");
            auto stream = m.ArgumentStream();
            int universe_id;
            stream >> universe_id;
            //DmxUniverse_v1 u_new;
            //u_new.id = universe_id;
            std::cout << "OSC: /frame universe=" << universe_id << std::endl;
            auto &frame_values = m_nextScene.m_sceneValues[universe_id];
            while (!stream.Eos()) {
              int channel = -1;
              int value = -1;
              stream >> channel>> value;
              //DmxChannel_v1 ch;
              //stream >> ch.id >> ch.value;
              //std::cout << "channel: " << channel  << "  value: " << value << std::endl;
              //u_new.channels.push_back(DmxChannel_v1{{}, (unsigned int)channel, (unsigned char)value} );
              //u_new.channels.push_back({{}, (unsigned int)channel, (unsigned char)value, 0});
              frame_values[channel] = value;
            }
            //m_universes.push_back(u_new);
            // start now
            //headOffset = -playHead;

            /*
            olaClientWrapper.GetClient()->FetchDMX(universe_id, ola::NewSingleCallback(&DmxPlayer::OnFetchDMX, this));
            std::cout << "fetch sent" << std::endl;
            */
            //olaServer->RegisterRepeatingTimeout( 10, ola::NewCallback( &DmxPlayer::SendUniverseData, this ) );
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/fade_time") ) {
          int fade = 0;
          m.ArgumentStream() >> fade >> osc::EndMessage;
          m_fade = fade; // TODO: remove this
          m_nextScene.m_fadeTime = fade;
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/start_offset") ) {
          int ofs = 0;
          m.ArgumentStream() >> ofs >> osc::EndMessage;
          m_nextScene.m_mtcStart = playHead + ofs;
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


//static
void DmxPlayer::OnFetchDMX(DmxPlayer* dp, uint32_t univ_id, const ola::client::Result& result,
    const ola::client::DMXMetadata& metadata, const ola::DmxBuffer& buffer)
{
  std::cout
    << "DmxPlayer::OnFetchDMX"
    << " Result=" << result.Success()
    << " id=" << metadata.universe << "/" << univ_id
    << " buffer size=" << buffer.Size()
    << std::endl;
  /*
  for (auto &u : dp->m_universes) {
    if (u.id == metadata.universe) {
      std::cout << u.id << "  values: (" << u.channels.size() << ") ";
      for (auto &ch : u.channels) {
        ch.start_value = buffer.Get(ch.id);
        std::cout << (int)ch.start_value << " ";
      }
      std::cout << std::endl;
      u.transition_ready = true;
    }
  }
  */
  auto it = dp->m_activeUniverses.find(univ_id);
  if (it != dp->m_activeUniverses.end()) {
    if (result.Success()) {
      it->second.m_state = 2;
      it->second.m_channelsBuffer = buffer;
    }
    else {
      it->second.m_state = 3;
    }
  }
}

bool DmxPlayer::SendUniverseData(DmxPlayer* dp) {
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
        dp->processScenes();
    }
    else {
        if ( ! dp->mtcReceiver.isTimecodeRunning && dp->mtcSignalStarted && !dp->mtcSignalLost ) {
            CuemsLogger::getLogger()->logInfo("MTC signal lost");
            dp->mtcSignalLost = true;
        }
    }

    return true;
}

void DmxPlayer::processScenes() {
  for (auto it = m_scenes.begin(); it != m_scenes.end(); ) {
    SceneTransitionInfo &sc = *it++;
    if (sc.m_mtcStart > playHead) {
      // No more scenes to process now
      break;
    }
    std::cout << "Processing scene transition at " << sc.m_mtcStart
              << "  now = " << playHead
              << "  fade = " << sc.m_fadeTime
              << std::endl;
    for (auto it_univ = sc.m_sceneValues.begin(); it_univ != sc.m_sceneValues.end();) {
      uint32_t univ_id = it_univ->first;
      bool remove = false;
      auto &active_universe = m_activeUniverses[univ_id];
      if (0 == active_universe.m_state) {
        // Just created, init it first
        active_universe.m_id = univ_id;
        active_universe.m_mtcLast = sc.m_mtcStart;
        active_universe.m_state = 1;
        olaClientWrapper.GetClient()->FetchDMX(univ_id, ola::NewSingleCallback(&DmxPlayer::OnFetchDMX, this, univ_id));
        std::cout << "fetch sent for " << univ_id << std::endl;
      }
      else if (2 == active_universe.m_state) {
        // Buffer is fetched, ready to go
        remove = true;
        int c = 0;
        for (auto it_val = it_univ->second.begin(); it_val != it_univ->second.end(); ++it_val) {
          auto &trs = active_universe.m_channelTransitions[it_val->first];
          trs.mtc0 = sc.m_mtcStart;
          trs.mtc1 = sc.m_mtcStart + sc.m_fadeTime;
          // We transition from the curent channel value to the requested one
          trs.val0 = active_universe.m_channelsBuffer.Get(it_val->first);
          trs.val1 = it_val->second;
          ++c;
        }
        std::cout << "  set channels: " << c << std::endl;
      }
      else if (3 == active_universe.m_state) {
        std::cout << "Failed to fetch channels for universe " << univ_id
                  << ", removing it" << std::endl;
        remove = true;
      }

      if (remove) {
        it_univ = sc.m_sceneValues.erase(it_univ);
      }
      else {
        ++it_univ;
      }
    }
    if (sc.m_sceneValues.empty()) {
      it = m_scenes.erase(--it);
    }
  }

  for (auto it = m_activeUniverses.begin(); it != m_activeUniverses.end();) {
    auto &univ = it->second;
    // skip non-ready universes
    if (univ.m_state != 2) {
      ++it;
      continue;
    }
    for (auto it_trs = univ.m_channelTransitions.begin(); it_trs != univ.m_channelTransitions.end();) {
      auto &trs = it_trs->second;
      double ph = 1.0 * (playHead - trs.mtc0) / (trs.mtc1 - trs.mtc0);
      bool ch_remove = false;
      if (0.0 < ph) {
        if (1.0 > ph) {
          uint8_t v = std::round(trs.val0 + ph * (trs.val1 - trs.val0));
          univ.m_channelsBuffer.SetChannel(it_trs->first, v);
        }
        else {
          univ.m_channelsBuffer.SetChannel(it_trs->first, trs.val1);
          ch_remove = true;
        }
      }
      if (ch_remove) {
        it_trs = univ.m_channelTransitions.erase(it_trs);
      }
      else {
        ++it_trs;
      }
    }

    olaClientWrapper.GetClient()->SendDMX(univ.m_id, univ.m_channelsBuffer, ola::client::SendDMXArgs());

    if (univ.m_channelTransitions.empty()) {
      it = m_activeUniverses.erase(it);
      std::cout << "removing universe " << univ.m_id << " from active universes (all done)" << std::endl;
    }
    else {
      ++it;
    }
  }
}

#if 0
//////////////////////////////////////////////////////////
// OLA per DMX universe MTC frame player
{
    static float fadeMult = 0;
    static ola::DmxBuffer buffer;

    /*
    std::cout << "DmxPlayer::SendUniverseData: " << dp->playHead
              << "  " << dp->mtcReceiver.mtcHead
              << std::endl;
    // */

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
        //long in_time = dp->dmxCue.getInTime();
        //long out_time = dp->dmxCue.getOutTime();
        //long dmx_len = dp->dmxCue.getLength();
        long in_time = dp->m_fade; // TODO
        //long out_time = 1000;
        //long dmx_len = 1000;

        //long int totalLength = dmx_len + in_time;
        //long int totalLengthOut = totalLength + out_time;

        // Calculate enveloping moment and multiplier
        if ( currentPos < 0 ) {
            dp->fading = false;
            dp->isSceneFull = false;
            dp->sceneSet = true;
            dp->outOfFile = false;
        }
        else if ( currentPos < in_time ) {
            dp->fading = true;
            // Fade multiplier calculus
            fadeMult = (float)currentPos / in_time;
            dp->isSceneFull = false;
            dp->sceneSet = false;
            dp->outOfFile = false;
        }
        else {
            dp->fading = false;
            // Fade multiplier calculus
            fadeMult = 1.0;
            dp->isSceneFull = true;
            //dp->sceneSet = false;
            dp->outOfFile = false;
        }
        /*
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
            fadeMult = (float) (totalLengthOut - currentPos) / out_time;
            dp->isSceneFull = false;
            dp->sceneSet = false;
            dp->outOfFile = false;
        }
        else {
            dp->fading = false;
            dp->isSceneFull = false;
            dp->sceneSet = false;
            //dp->outOfFile = true;
        }
        */

        //////////////////////////////////////////////////////////
        // DMX PLAY
        for (auto u_it = dp->m_universes.begin(); u_it != dp->m_universes.end(); ) {
          auto *universe = &(*u_it);
          ++u_it;
          if (!universe->transition_ready) {
            continue;
          }
          int ch_cnt = 0;
          if ( /*!dp->isSceneFull*/ dp->fading ) {
              // We are not in a full scene moment, fading in or out
              float fade_start = 1.0f - fadeMult;
              for (   std::vector<DmxChannel_v1>::iterator it = universe->channels.begin() ;
                      it != universe->channels.end() ; it++ ) {

                  // Let's send all this dmx data
                  float v = std::round((*it).start_value * fade_start + (*it).value * fadeMult);
                  buffer.SetChannel( (*it).id, (uint8_t) v);
                  ++ch_cnt;
              }
          }
          else /*if ( !dp->sceneSet ) */ {
              // We are in a full scene moment, if not set yet, set it full
              for (   std::vector<DmxChannel_v1>::iterator it = universe->channels.begin() ;
                      it != universe->channels.end() ; it++ ) {

                  // Let's send all this dmx data
                  buffer.SetChannel( (*it).id, (*it).value );
                  ++ch_cnt;
              }

              //dp->sceneSet = true;
              --u_it;
              u_it = dp->m_universes.erase(u_it);
          }

          //std::cout << "sending data to universe " << universe->id << "  cnt: " << ch_cnt << std::endl;
          if (0 < ch_cnt) {
            dp->olaClientWrapper.GetClient()->SendDMX(universe->id, buffer, ola::client::SendDMXArgs());
          }
        }
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
#endif

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
