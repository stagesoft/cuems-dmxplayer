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
                        mtcReceiver(MTCRECV_DEFAULT_API, client_name),
                        stopOnMTCLost(stopOnLostFlag),
                        followMTC(followMTCFlag)
{
    //////////////////////////////////////////////////////////
    // Set up working class members

    // Enable network-tolerant MTC timeouts (for rtpmidid / MTC over network)
    mtcReceiver.setNetworkMode(true);

    // Starting OLA logging
    ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);

    // OLA connection is established in run() which handles reconnection.
    // Validate OLA is reachable at startup for fail-fast behavior.
    {
        ola::client::OlaClientWrapper probe;
        if (!probe.Setup()) {
            std::cerr << "OLA setup failed" << endl;
            CuemsLogger::getLogger()->logError("OLA setup failed!");
            CuemsLogger::getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_FAILED_OLA_SETUP) );
            exit( CUEMS_EXIT_FAILED_OLA_SETUP );
        }
        // probe is destroyed here — run() will create the real connection
    }

}

//////////////////////////////////////////////////////////
DmxPlayer::~DmxPlayer( void ) {

}

//////////////////////////////////////////////////////////
void DmxPlayer::setOutputLatencyMs(long ms) {
    if (ms < 0) ms = 0;
    if (ms > 500) ms = 500;
    m_outputLatencyMs.store(ms);
    CuemsLogger::getLogger()->logInfo(
        "DMX output latency compensation updated to "
        + std::to_string(ms) + " ms");
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
    {
      std::lock_guard guard(m_scenesMutex);
      auto r_it = m_scenes.rbegin();
      for (; r_it != m_scenes.rend(); ++r_it) {
        if (r_it->m_mtcStart <= m_nextScene.m_mtcStart) {
          break;
        }
      }
      m_scenes.insert(r_it.base(), std::move(m_nextScene));
    }

    // If on idle timer, wake up the SelectServer to switch to active (10ms).
    // Execute() is thread-safe and interrupts select() immediately.
    if (m_isIdleTimer && olaServer != nullptr && m_olaConnected) {
      olaServer->Execute(
          ola::NewSingleCallback(this, &DmxPlayer::switchToActiveTimer));
    }
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

        // Blackout: clear all scenes and fades, send zeros to OLA
        } else if ( (string)m.AddressPattern() == (OscReceiver::oscAddress + "/blackout") ) {
            CuemsLogger::getLogger()->logInfo("OSC: /blackout command");
            {
                std::lock_guard guard(m_scenesMutex);
                m_scenes.clear();
            }
            {
                std::lock_guard guard(m_universesMutex);
                for (auto &[univ_id, univ] : m_activeUniverses) {
                    univ.m_channelTransitions.clear();
                    univ.m_channelsBuffer.Blackout();
                    if (m_olaConnected) {
                        m_olaWrapper->GetClient()->SendDMX(univ.m_id, univ.m_channelsBuffer, ola::client::SendDMXArgs());
                    }
                }
                m_activeUniverses.clear();
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
    // When not following MTC: still process queued scenes and send to OLA
    // (e.g. "press Go" without timecode — scene is applied immediately)
    if (!dp->followMTC) {
      dp->playHead = 0;
      dp->processScenes();
      dp->updateActiveUniverses();
    }
    // If we are receiving MTC and following it...
    // Or we are not receiving it and we do not stop on its lost
    // And we haven't reached the end of playing time...
    else {
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

          dp->playHead = dp->mtcReceiver.estimatedCurrentHead()
                       + dp->m_outputLatencyMs.load();
          dp->processScenes();
          dp->updateActiveUniverses();
      }
      else {
          if ( ! timecode_running && dp->mtcSignalStarted && !dp->mtcSignalLost ) {
              CuemsLogger::getLogger()->logInfo("MTC signal lost");
              dp->mtcSignalLost = true;
          }
      }
    }

    // Adaptive timer: switch between idle (200ms) and active (10ms) intervals
    bool needsWork = dp->hasActiveWork();
    if (needsWork && dp->m_isIdleTimer) {
        dp->registerTimer(/*idle=*/false, /*fromCallback=*/true);
        return false;  // cancel this repeating timeout; new one already registered
    }
    if (!needsWork && !dp->m_isIdleTimer) {
        dp->registerTimer(/*idle=*/true, /*fromCallback=*/true);
        return false;
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
        m_olaWrapper->GetClient()->FetchDMX(univ_id, ola::NewSingleCallback(&DmxPlayer::OnFetchDMX, this, univ_id));
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
  std::lock_guard guard(m_universesMutex);
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

    if (m_olaConnected) {
        m_olaWrapper->GetClient()->SendDMX(univ.m_id, univ.m_channelsBuffer, ola::client::SendDMXArgs());
    }

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
// Register a repeating timer on the OLA SelectServer.
//
// IMPORTANT: fromCallback controls whether we call RemoveTimeout().
// When called from inside a repeating callback (SendUniverseData),
// the caller MUST return false to cancel the old timer — calling
// RemoveTimeout here would double-free the callback object since
// OLA also removes it on false return. When called from outside
// (e.g. switchToActiveTimer via Execute), we must call RemoveTimeout
// explicitly because there is no return-false mechanism.
void DmxPlayer::registerTimer(bool idle, bool fromCallback) {
    if (!fromCallback && m_currentTimeoutId != ola::thread::INVALID_TIMEOUT && olaServer != nullptr) {
        olaServer->RemoveTimeout(m_currentTimeoutId);
    }

    unsigned int interval = idle
        ? CuemsConstants::OLA_CALLBACK_TIMEOUT_IDLE_MS
        : CuemsConstants::OLA_CALLBACK_TIMEOUT_MS;

    m_isIdleTimer = idle;
    m_currentTimeoutId = olaServer->RegisterRepeatingTimeout(
        interval,
        ola::NewCallback(&DmxPlayer::SendUniverseData, this));
}

//////////////////////////////////////////////////////////
bool DmxPlayer::hasActiveWork() const {
    return !m_scenes.empty() || !m_activeUniverses.empty();
}

//////////////////////////////////////////////////////////
void DmxPlayer::switchToActiveTimer() {
    if (m_isIdleTimer) {
        registerTimer(/*idle=*/false, /*fromCallback=*/false);
    }
}

//////////////////////////////////////////////////////////
bool DmxPlayer::setupOlaConnection() {
    CuemsLogger::getLogger()->logInfo("Setting up OLA connection...");

    m_olaWrapper = std::make_unique<ola::client::OlaClientWrapper>();

    if (!m_olaWrapper->Setup()) {
        CuemsLogger::getLogger()->logError("OLA setup failed");
        m_olaWrapper.reset();
        return false;
    }

    olaServer = m_olaWrapper->GetSelectServer();
    if (olaServer == nullptr) {
        CuemsLogger::getLogger()->logError("OLA SelectServer is null");
        m_olaWrapper.reset();
        return false;
    }

    // Override the default close behavior (which just calls Terminate).
    // We set our flag first so the run() loop knows this is a disconnection.
    m_olaWrapper->SetCloseCallback(
        ola::NewCallback(this, &DmxPlayer::onOlaConnectionClosed));

    // Start in idle mode — switch to active when scenes arrive
    registerTimer(/*idle=*/!hasActiveWork());

    m_olaConnected = true;
    CuemsLogger::getLogger()->logInfo("OLA connection established");
    return true;
}

//////////////////////////////////////////////////////////
void DmxPlayer::teardownOlaConnection() {
    m_olaConnected = false;
    m_currentTimeoutId = ola::thread::INVALID_TIMEOUT;
    m_isIdleTimer = false;
    olaServer = nullptr;
    m_olaWrapper.reset();
}

//////////////////////////////////////////////////////////
void DmxPlayer::onOlaConnectionClosed() {
    CuemsLogger::getLogger()->logWarning("OLA connection closed");
    m_olaConnected = false;
    if (olaServer) {
        olaServer->Terminate();
    }
}

//////////////////////////////////////////////////////////
void DmxPlayer::purgeStaleScenes() {
    long int now = playHead.load();

    {
        std::lock_guard guard(m_scenesMutex);
        m_scenes.remove_if([now](const SceneTransitionInfo &sc) {
            return sc.m_mtcStart < now - 100;
        });
    }

    {
        // Clear active universes — pending FetchDMX callbacks from the old
        // connection will never fire, so entries stuck in state 1 must be reset.
        // Universes will be re-fetched on the next processScenes() cycle.
        std::lock_guard guard(m_universesMutex);
        m_activeUniverses.clear();
    }
}

//////////////////////////////////////////////////////////
void DmxPlayer::run( void ) {
    // Let's mark the playHead with the current time
    startTimeStamp = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();
    CuemsLogger::getLogger()->logInfo( "Start timestamp: " + std::to_string(startTimeStamp) );

    // Play head init set
    playHead = 0;

    CuemsLogger::getLogger()->logInfo(
        "DMX output latency compensation = "
        + std::to_string(m_outputLatencyMs.load()) + " ms");

    unsigned int reconnectDelay = CuemsConstants::OLA_RECONNECT_INITIAL_DELAY_MS;

    while (m_running) {
        if (!setupOlaConnection()) {
            CuemsLogger::getLogger()->logWarning(
                "OLA connection failed, retrying in " + std::to_string(reconnectDelay) + "ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelay));
            reconnectDelay = std::min(reconnectDelay * 2,
                (unsigned int)CuemsConstants::OLA_RECONNECT_MAX_DELAY_MS);
            continue;
        }

        // Reset backoff on successful connection
        reconnectDelay = CuemsConstants::OLA_RECONNECT_INITIAL_DELAY_MS;

        // Discard scenes queued during downtime and reset universe state
        purgeStaleScenes();

        CuemsLogger::getLogger()->logInfo("OLA SelectServer running");
        olaServer->Run();  // Blocks until Terminate() is called

        // Run() returned — check why
        if (m_olaConnected) {
            // Normal termination (e.g. SIGTERM via /quit)
            CuemsLogger::getLogger()->logInfo("OLA SelectServer terminated normally");
            break;
        }

        // Connection lost — clean up and reconnect
        CuemsLogger::getLogger()->logWarning("OLA connection lost, attempting reconnect...");
        teardownOlaConnection();
    }

    m_running = false;
}
