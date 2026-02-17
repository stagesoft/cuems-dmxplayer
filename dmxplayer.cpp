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
#include "cuems_constants.h"
#include <thread>
#include <charconv>

using namespace std;

////////////////////////////////////////////
// Initializing static class members
std::atomic<long int> DmxPlayer::playHead(0);

//////////////////////////////////////////////////////////
DmxPlayer::DmxPlayer(   int port,
                        const string oscRoute,
                        const bool stopOnLostFlag,
                        const bool followMTCFlag,
                        const std::string &client_name)
                        :   // Members initialization
                        OscReceiver(port, oscRoute),
                        mtcReceiver(RtMidiIn::LINUX_ALSA, client_name),
                        stopOnMTCLost(stopOnLostFlag),
                        followMTC(followMTCFlag)
{
    //////////////////////////////////////////////////////////
    // Enable network mode for MTC over rtpmidid
    // This uses more tolerant timeouts for network latency/jitter
    MtcReceiver::setNetworkMode(true);

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
        CuemsLogger::getLogger()->logInfo("Setting OLA callback");
        olaServer->RegisterRepeatingTimeout( CuemsConstants::OLA_CALLBACK_TIMEOUT_MS, ola::NewCallback( &DmxPlayer::SendUniverseData, this ) );
    }

}

//////////////////////////////////////////////////////////
DmxPlayer::~DmxPlayer( void ) {

}

//////////////////////////////////////////////////////////
void DmxPlayer::ProcessBundle( const osc::ReceivedBundle& b,
                               const IpEndpointName& remoteEndpoint )
{
  // set 'now' MTC by default if it's a top-leven bundle;
  if (0 == m_inBundle) {
    m_nextScene.m_mtcStart = playHead;
  }
  ++m_inBundle;
  std::cout << "DmxPlayer::ProcessBundle => " << m_inBundle
    << " thread=" << std::this_thread::get_id()
    << std::endl;
  OscReceiver::ProcessBundle(b, remoteEndpoint);
  --m_inBundle;
  std::cout << "DmxPlayer::ProcessBundle <= " << m_inBundle
    << "  values:" << m_nextScene.m_sceneValues.size() << std::endl;

  // If it's a top-level bundle, add m_nextScene to scenes
  if (0 == m_inBundle) {
    std::lock_guard guard(m_scenesMutex);
    auto r_it = m_scenes.rbegin();
    for (; r_it != m_scenes.rend(); ++r_it) {
      if (r_it->m_mtcStart <= m_nextScene.m_mtcStart) {
        break;
      }
    }
    m_scenes.insert(r_it.base(), std::move(m_nextScene));
  }
}

//////////////////////////////////////////////////////////
void DmxPlayer::ProcessMessage( const osc::ReceivedMessage& m,
            const IpEndpointName& /*remoteEndpoint*/ )
{
    try {
        // Parsing OSC DmxPlayer messages
        // Quit
        if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/quit") ) {
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
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/mtcfollow") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /mtcfollow command");
            auto stream = m.ArgumentStream();
            if (!stream.Eos()) {
              int val = 0;
              stream >> val >> osc::EndMessage;
              followMTC = (val != 0);
            } else {
              followMTC = !followMTC;  // no argument: legacy toggle
            }

        // We only accept the following commands from a bundle
        } else if (0 < m_inBundle) {
          if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/frame") ) {
              CuemsLogger::getLogger()->logInfo("OSC: /frame command");
              auto stream = m.ArgumentStream();
              int universe_id;
              stream >> universe_id;
              if (universe_id < CuemsConstants::MIN_UNIVERSE_ID || universe_id > CuemsConstants::MAX_UNIVERSE_ID) {
                  CuemsLogger::getLogger()->logWarning("OSC: Invalid universe_id in /frame command: " + std::to_string(universe_id));
                  return;
              }
              std::cout << "OSC: /frame universe=" << universe_id << std::endl;
              auto &frame_values = m_nextScene.m_sceneValues[universe_id];
              while (!stream.Eos()) {
                int channel = -1;
                int value = -1;
                stream >> channel >> value;
                if (channel < CuemsConstants::MIN_CHANNEL_ID || channel > CuemsConstants::MAX_CHANNEL_ID) {
                    CuemsLogger::getLogger()->logWarning("OSC: Invalid channel in /frame command: " + std::to_string(channel));
                    continue;
                }
                if (value < CuemsConstants::MIN_DMX_VALUE || value > CuemsConstants::MAX_DMX_VALUE) {
                    CuemsLogger::getLogger()->logWarning("OSC: Invalid value in /frame command: " + std::to_string(value));
                    continue;
                }
                frame_values[channel] = value;
              }
          } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/fade_time") ) {
            float fade = 0;
            m.ArgumentStream() >> fade >> osc::EndMessage;
            m_nextScene.m_fadeTime = std::round(1000 * fade);
          } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/mtc_time") ) {
            const char *str = nullptr;
            m.ArgumentStream() >> str >> osc::EndMessage;
            std::string_view start_time(str);
            if ("now" == start_time) {
              m_nextScene.m_mtcStart = playHead;
            }
            else if ('+' == start_time[0]) {
              m_nextScene.m_mtcStart = playHead + convertTime(start_time.substr(1));
            }
            else {
              m_nextScene.m_mtcStart = std::max(playHead.load(), convertTime(start_time));
            }
          } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/start_offset") ) {
            int ofs = 0;
            m.ArgumentStream() >> ofs >> osc::EndMessage;
            m_nextScene.m_mtcStart = playHead + ofs;
          }
        }

    } catch ( osc::Exception& e ) {
        // any parsing errors such as unexpected argument types, or
        // missing arguments get thrown as exceptions.
        std::cout << "error while parsing message: "
            << m.AddressPattern() << ": " << e.what() << "\n";
    }
}

//////////////////////////////////////////////////////////
long int DmxPlayer::convertTime(const std::string_view &time)
{
  // Time format: [[h:]m:]s
  double seconds = 0;
  int minutes = 0;
  int hours = 0;
  const char* p = time.data();

  int L = time.size() - 1;
  auto j = time.rfind(':', L) + 1;
  //std::cout << " DmxPlayer::convertTime: s = " << time.substr(j, L - j)
  //  << " j=" << j << " L=" << L << std::endl;
  std::from_chars(p+j, p+L+1, seconds);
  if (1 < j) {
    L = j - 2;
    j = time.rfind(':', L) + 1;
    //std::cout << "    m = " << time.substr(j, L - j + 1)
    //  << " j=" << j << " L=" << L << std::endl;
    std::from_chars(p+j, p+L+1, minutes);
  }
  if (1 < j) {
    L = j - 2;
    j = time.rfind(':', L) + 1;
    //std::cout << "    h = " << time.substr(j, L - j + 1)
    //  << " j=" << j << " L=" << L << std::endl;
    std::from_chars(p+j, p+L+1, hours);
  }
  //std::cout << "  conversion result: " << hours << ":" << minutes << ":" << seconds << std::endl;
  return std::round(seconds * 1000 + 60*1000 * minutes + 3600*1000 * hours);
}

//////////////////////////////////////////////////////////
//static
void DmxPlayer::OnFetchDMX(DmxPlayer* dp, uint32_t univ_id, const ola::client::Result& result,
    const ola::client::DMXMetadata& metadata, const ola::DmxBuffer& buffer)
{
  std::cout
    << "DmxPlayer::OnFetchDMX"
    << " Result=" << result.Success()
    << " id=" << metadata.universe << "/" << univ_id
    << " buffer size=" << buffer.Size()
    << " thread=" << std::this_thread::get_id()
    << std::endl;

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

//////////////////////////////////////////////////////////
bool DmxPlayer::SendUniverseData(DmxPlayer* dp) {
    // If we don't follow MTC, do nothing (no timecode = no output)
    if (!dp->followMTC) {
      return true;
    }
    // If we are receiving MTC and following it...
    // Or we are not receiving it and we do not stop on its lost
    // And we haven't reached the end of playing time...
    bool timecode_running = dp->mtcReceiver.isTimecodeActive(); //isTimecodeRunning
    if ( ( timecode_running ||
            (dp->mtcSignalLost && !dp->stopOnMTCLost) )   ) {

        // Check play control flags
        // If there is MTC signal and we haven't started, check it
        if ( timecode_running ) {
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

        dp->playHead = dp->mtcReceiver.estimatedCurrentHead();
        dp->processScenes();
        dp->updateActiveUniverses();
    }
    else {
        if ( ! timecode_running && dp->mtcSignalStarted && !dp->mtcSignalLost ) {
            CuemsLogger::getLogger()->logInfo("MTC signal lost");
            dp->mtcSignalLost = true;
        }
    }

    return true;
}

//////////////////////////////////////////////////////////
void DmxPlayer::processScenes() {
  std::lock_guard guard(m_scenesMutex);
  for (auto it = m_scenes.begin(); it != m_scenes.end(); ) {
    SceneTransitionInfo &sc = *it++;
    if (sc.m_mtcStart > playHead + universeFetchLookAheadTime) {
      // No more scenes to process now
      break;
    }
    std::cout << "Processing scene transition at " << sc.m_mtcStart
              << "  now = " << playHead
              << "  fade = " << sc.m_fadeTime
              << "  thread=" << std::this_thread::get_id()
              << std::endl;
    for (auto it_univ = sc.m_sceneValues.begin(); it_univ != sc.m_sceneValues.end();) {
      uint32_t univ_id = it_univ->first;
      bool remove = false;
      auto &active_universe = m_activeUniverses[univ_id];
      if (0 == active_universe.m_state) {
        // Just created, init it first
        active_universe.m_id = univ_id;
        active_universe.m_state = 1;
        olaClientWrapper.GetClient()->FetchDMX(univ_id, ola::NewSingleCallback(&DmxPlayer::OnFetchDMX, this, univ_id));
        std::cout << "fetch requested for universe " << univ_id << std::endl;
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
}

//////////////////////////////////////////////////////////
void DmxPlayer::updateActiveUniverses()
{
  for (auto it = m_activeUniverses.begin(); it != m_activeUniverses.end();) {
    auto &univ = it->second;
    // skip non-ready universes
    if (univ.m_state != 2) {
      ++it;
      continue;
    }
    for (auto it_trs = univ.m_channelTransitions.begin(); it_trs != univ.m_channelTransitions.end();) {
      auto &trs = it_trs->second;
      bool ch_remove = false;
      if (trs.mtc1 <= trs.mtc0) {
        // Instant transition (fade time 0)
        univ.m_channelsBuffer.SetChannel(it_trs->first, trs.val1);
        ch_remove = true;
      } else {
        double ph = 1.0 * (playHead - trs.mtc0) / (trs.mtc1 - trs.mtc0);
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
