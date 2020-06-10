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
// Stage Lab SysQ DMX Cue class v.1 header file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#ifndef DMXCUE_V1_CLASS_H
#define DMXCUE_V1_CLASS_H

#include <string>
#include "dmxcue_v0_class.h"
#include "sysq_errors.h"
#include "sysqlogger_class/sysqlogger_class.h"
#include "ola/DmxBuffer.h"

using namespace std;
using namespace xercesc;

typedef struct : public DmxChannel_v0 {
    unsigned int id;
    unsigned char value;
} DmxChannel_v1;

typedef struct : public DmxUniverse_v0 {
    unsigned int id;
    std::vector<DmxChannel_v1> channels;

    ola::DmxBuffer channelsBuffer;
} DmxUniverse_v1;

typedef struct : public DmxScene_v0 {
    std::vector<DmxUniverse_v1> universes;
} DmxScene_v1;

class DmxCue_v1 : public DmxCue_v0
{
    public:
        DmxCue_v1( string path = "dmx.xml" );
        virtual ~DmxCue_v1 ( void );

        DmxScene_v1 dmxScene;

        inline long getInTime( void ) { return inTime; };
        inline long getOutTime( void ) { return outTime; };
        inline long getLength( void ) { return length; };
        long int getOffsetMs( unsigned char curFrameRate );

    private:

    protected:
        char offsetTime[12] = "00:00:00:00";
        long inTime = 0;
        long outTime = 0;
        long length = 0;

        XercesDOMParser* parser = NULL;
        ErrorHandler* errHandler = NULL;

        void initParser( void );
        void finishParser( void );
        DOMNodeList* cueNodeList = NULL;
        void getCueFromXml( void );

        void getSceneFromXml( DOMNodeList* sceneNodeList );

        void getUniverseFromXml( XMLSize_t index, DOMNodeList* universeNodeList );
};

#endif // DMXCUE_V1_CLASS_H