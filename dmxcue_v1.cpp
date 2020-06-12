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
// Stage Lab Cuems DMX Cue class v.1 source file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#include "dmxcue_v1.h"

//////////////////////////////////////////////////////////
DmxCue_v1::DmxCue_v1( string xmlPath ): DmxCue_v0( xmlPath )
{
    initParser();

    getCueFromXml();

}

//////////////////////////////////////////////////////////
DmxCue_v1::~DmxCue_v1( void )
{
    delete parser;
    delete errHandler;

    finishParser();
}

//////////////////////////////////////////////////////////
void DmxCue_v1::initParser( void )
{
    //////////////////////////////////////////////////////////
    // Start XML platform
    try {
        XMLPlatformUtils::Initialize();
    }
    catch (const XMLException& toCatch) {
        // Do your failure processing here
        std::string str = "Error initializing XML platform.";
        std::cerr << str << endl;

        CuemsLogger::getLogger()->logError( str );
        CuemsLogger::getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_FAILED_XML_INIT) );

        exit( CUEMS_EXIT_FAILED_XML_INIT );
    }

    // Set our XML parser options
    parser = new XercesDOMParser();
    parser->setValidationScheme(XercesDOMParser::Val_Always);
    // Enable schema processing
    parser->setDoSchema(true);
    parser->setDoNamespaces(true);    // optional
    parser->setValidationScheme(AbstractDOMParser::ValSchemes::Val_Always);

    errHandler = (ErrorHandler*) new HandlerBase();
    parser->setErrorHandler(errHandler);


    // Let's try to parse our file
    try {
        parser->parse(xmlPath.c_str());

        DOMDocument* xmlDoc = parser->getDocument();
        DOMElement *root = xmlDoc->getDocumentElement();

        if( !root ) throw (std::runtime_error( "Empty XML document" ));

        cueNodeList = root->getChildNodes();

    }
    catch (const XMLException& toCatch) {
        char* message = XMLString::transcode(toCatch.getMessage());

        std::string str = "XML exception, message is: " +  (string) message;
        cerr << str << endl;

        CuemsLogger::getLogger()->logError( str );

        XMLString::release(&message);

    }
    catch (const DOMException& toCatch) {
        char* message = XMLString::transcode(toCatch.msg);

        std::string str = "XML exception, message is: " + (string) message;
        cerr << str << endl;

        CuemsLogger::getLogger()->logError( str );

        XMLString::release(&message);

    }
    catch (...) {
        std::string str = "Unexpected XML DOM exception";
        cerr << str << endl;

        CuemsLogger::getLogger()->logError( str );
    }

}

//////////////////////////////////////////////////////////
void DmxCue_v1::finishParser( void )
{
    //////////////////////////////////////////////////////////
    // Terminate the XML platform
    xercesc::XMLPlatformUtils::Terminate();
}

//////////////////////////////////////////////////////////
void DmxCue_v1::getCueFromXml( void ) {

    // Let's run over all children nodes of "root" in the XML tree
    // it is all now in our Cue Node List
    const XMLSize_t nodeCount = cueNodeList->getLength();
    for( XMLSize_t i = 0; i < nodeCount; ++i )
    {
        // Here we hace a current node in our run
        DOMNode* currentNode = cueNodeList->item(i);
        if( currentNode->getNodeType() &&  
            currentNode->getNodeType() == DOMNode::ELEMENT_NODE ) {

            // If it's an element node, recast it to use it
            DOMElement* currentElement = dynamic_cast< xercesc::DOMElement* >( currentNode );

            string str = XMLString::transcode( currentElement->getTagName() );

            if( str == "DmxScene" )
            {
                // It is an scene node so we have to explore also its children
                DOMNodeList* sceneNodeList = currentNode->getChildNodes();

                getSceneFromXml(sceneNodeList);

            }
            else if( str == "Offset" ) {
                // It is an offset element, we need it's value
                const XMLCh* offsetValue = currentElement->getTextContent();
                
                str = XMLString::transcode(offsetValue);
                
                strcpy(offsetTime, XMLString::transcode(offsetValue));

            }
            else if( str == "InTime" ) {
                // It is an InTime element, we need it's value
                const XMLCh* inTimeValue = currentElement->getTextContent();
                
                inTime = atoi(XMLString::transcode(inTimeValue));

            }
            else if( str == "Length" ) {
                // It is an InTime element, we need it's value
                const XMLCh* lengthValue = currentElement->getTextContent();
                
                length = atoi(XMLString::transcode(lengthValue));

            }
            else if( str == "OutTime") {
                // It is an OutTime element, we need it's value
                const XMLCh* outTimeValue = currentElement->getTextContent();
                
                outTime = atoi(XMLString::transcode(outTimeValue));

            }
        }
    }
}

//////////////////////////////////////////////////////////
void DmxCue_v1::getSceneFromXml( DOMNodeList* sceneNodeList )
{
    // Let's run over all children nodes of "root" in the XML tree
    // it is all now in our Cue Node List
    const XMLSize_t nodeCount = sceneNodeList->getLength();
    for( XMLSize_t i = 0; i < nodeCount; ++i )
    {
        DOMNode* currentNode = sceneNodeList->item(i);
        if( currentNode->getNodeType() &&  
            currentNode->getNodeType() == DOMNode::ELEMENT_NODE ) {
                
            // Found node which is an Element. Re-cast node as element
            DOMElement* currentElement = dynamic_cast< xercesc::DOMElement* >( currentNode );
            string str = XMLString::transcode( currentElement->getTagName() );

            if( str == "DmxUniverse" )
            {
                DmxUniverse_v1 universe;
                XMLCh* attrTag = XMLString::transcode("id");
                const XMLCh* idAttr = currentElement->getAttribute(attrTag);
                universe.id = atoi(XMLString::transcode(idAttr));

                dmxScene.universes.push_back(universe);

                DOMNodeList* universeNodeList = currentNode->getChildNodes();
                getUniverseFromXml(dmxScene.universes.size() - 1, universeNodeList);


            }
        }
    }
}

//////////////////////////////////////////////////////////
void DmxCue_v1::getUniverseFromXml( XMLSize_t index, DOMNodeList* universeNodeList )
{
    std::string logMessage = "Channels : ";

    // Let's run over all children nodes of "root" in the XML tree
    // it is all now in our Cue Node List
    const XMLSize_t nodeCount = universeNodeList->getLength();
    for( XMLSize_t i = 0; i < nodeCount; ++i )
    {
        DOMNode* currentNode = universeNodeList->item(i);
        if( currentNode->getNodeType() &&  
            currentNode->getNodeType() == DOMNode::ELEMENT_NODE ) {
                
            // Found node which is an Element. Re-cast node as element
            DOMElement* currentElement = dynamic_cast< xercesc::DOMElement* >( currentNode );
            string str = XMLString::transcode( currentElement->getTagName() );

            if( str == "DmxChannel" )
            {
                DmxChannel_v1 channel;

                XMLCh* attrTag = XMLString::transcode("id");
                const XMLCh* idAttr = currentElement->getAttribute(attrTag);

                channel.id = atoi(XMLString::transcode(idAttr)) - 1;

                // It is an OutTime element, we need it's value
                const XMLCh* dmxValue = currentElement->getTextContent();
                
                channel.value = atoi(XMLString::transcode(dmxValue));

                logMessage += std::to_string(channel.id) + " ";

                dmxScene.universes[index].channels.push_back(channel);

                // dmxScene.universes[index].channelsBuffer.SetChannel(channel.id, channel.value);
                dmxScene.universes[index].channelsBuffer.SetChannel(atoi(XMLString::transcode(idAttr)) - 1, atoi(XMLString::transcode(dmxValue)));

            }
        }
    }

}

//////////////////////////////////////////////////////////
long int DmxCue_v1::getOffsetMs( unsigned char curFrameRate ) {
    long int calc;
    char buf[3] = {0x00,0x00,0x00};

    buf[0] = offsetTime[0];
    buf[1] = offsetTime[1];
    calc = atoi(buf) * 3600000;
    buf[0] = offsetTime[3];
    buf[1] = offsetTime[4];
    calc += atoi(buf) * 60000;
    buf[0] = offsetTime[6];
    buf[1] = offsetTime[7];
    calc += atoi(buf) * 1000;
    buf[0] = offsetTime[9];
    buf[1] = offsetTime[10];
    calc += atoi(buf) * 1000 / curFrameRate;

    return calc;
}