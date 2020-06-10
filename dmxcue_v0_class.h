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
// Stage Lab SysQ DMX Cue class v.0 header file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#ifndef DMXCUE_V0_CLASS_H
#define DMXCUE_V0_CLASS_H

#include <string>
#include <vector>
#include <iostream>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/StdOutFormatTarget.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/sax/HandlerBase.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>


using namespace std;
using namespace xercesc;

typedef struct {
} DmxChannel_v0;

typedef struct {
} DmxUniverse_v0;

typedef struct {
} DmxScene_v0;

class DmxCue_v0 
{
    public:
        DmxCue_v0( string path ) : xmlPath(path) { };
        // virtual ~DmxCue_v0( void ) = 0;

    private:

    protected:
        std::string xmlPath;
        // Pure virtual function to be implemented on inherintance
        virtual void getCueFromXml( void ) = 0;
};

#endif // DMXCUE_V0_CLASS_H