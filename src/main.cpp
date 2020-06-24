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
// Stage Lab Cuems DMX player main source code file
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

#include "main.h"

using namespace std;
namespace fs = std::filesystem;


//////////////////////////////////////////////////////////
// Initializing static class members and global vars
DmxPlayer* myDmxPlayer;
CuemsLogger* logger = NULL;

//////////////////////////////////////////////////////////
// Main application function
int main( int argc, char *argv[] ) {

    // SIGTERM catcher
    signal(SIGTERM, sigTermHandler);
    signal(SIGUSR1, sigUsr1Handler);
    signal(SIGINT, sigIntHandler);

    // We instantiate here our singleton logger object to be accessed
    // via the CuemsLogger::getLogger() function across the app
    logger = new CuemsLogger( "dmxplayer" );

    //////////////////////////////////////////////////////////
    // Parse command line
    // If there are no parameters at all...
    if ( argc < 2 ) {
        showcopyright();        
        showusage();
        
        logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

        exit( CUEMS_EXIT_WRONG_PARAMETERS );
    }

    // If there is at least one we parse the command line
    CommandLineParser* argParser = new CommandLineParser(argc, argv);

    // --show or -s command parse
    if ( argParser->optionExists("--show") ) {
        std::string arg = argParser->getParam("--show");

        showcopyright();

        if ( arg.empty() ) {
            showusage();
        } else if ( arg == "w" ) {
            showwarrantydisclaimer();
        } else if ( arg == "c" ) {
            showcopydisclaimer();
        }

        logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

        exit( CUEMS_EXIT_WRONG_PARAMETERS );
    }

    // --file or -f command parse and filename retreival and check
    fs::path filePath;
    // retreival
    if ( argParser->optionExists("--file") || argParser->optionExists("-f") ) {
        filePath = argParser->getParam("--file");

        if ( filePath.empty() ) filePath = argParser->getParam("-f");

        if ( filePath.empty() ) {
            // Not file path specified after file option
            std::cout << "File not specified after --file or -f option." << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_DATA_FILE) );

            exit( CUEMS_EXIT_WRONG_DATA_FILE );
        }
    }
    else {
        filePath = argParser->getEndingFilename();
    }
    // check
    if ( !filePath.empty() && !fs::exists(filePath) ) {
            // File does not exist
            std::cout << "Unable to locate file: " << filePath << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_DATA_FILE) );

            exit( CUEMS_EXIT_WRONG_DATA_FILE );
    }

    // --port or -p command parse and port number retreival and check
    unsigned int portNumber = 0;

    if ( argParser->optionExists("--port") || argParser->optionExists("-p") ) {
        std::string portParam = argParser->getParam("--port");

        if ( portParam.empty() ) portParam = argParser->getParam("-p");

        if ( portParam.empty() ) {
            // Not valid port number specified after port option
            std::cout << "Not valid port number after --port or -p option." << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

            exit( CUEMS_EXIT_WRONG_PARAMETERS );
        }
        else {
            portNumber = std::stoi( portParam );
        }
    }

    // --offset or -o command parse and offset retreival and check
    long int offsetMilliseconds = 0;

    if ( argParser->optionExists("--offset") || argParser->optionExists("-o") ) {
        std::string offsetParam = argParser->getParam("--offset");

        if ( offsetParam.empty() ) offsetParam = argParser->getParam("-o");

        if ( offsetParam.empty() ) {
            // Not valid port number specified after port option
            std::cout << "Not valid offset integer after --offset or -o option." << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

            exit( CUEMS_EXIT_WRONG_PARAMETERS );
        }
        else {
            offsetMilliseconds = std::stoi( offsetParam );
        }
    }

    // --wait or -w command parse and offset retreival and check
    long int endWaitMilliseconds = 0;

    if ( argParser->optionExists("--wait") || argParser->optionExists("-w") ) {
        std::string waitParam = argParser->getParam("--wait");

        if ( waitParam.empty() ) waitParam = argParser->getParam("-w");

        if ( waitParam.empty() ) {
            // Not valid port number specified after port option
            std::cout << "Not valid wait integer after --wait or -w option." << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

            exit( CUEMS_EXIT_WRONG_PARAMETERS );
        }
        else {
            endWaitMilliseconds = std::stoi( waitParam );
        }
    }

    // --uuid or -u command parse and offset retreival and check
    string processUuid = "";

    if ( argParser->optionExists("--uuid") || argParser->optionExists("-u") ) {
        std::string uuidParam = argParser->getParam("--uuid");

        if ( uuidParam.empty() ) uuidParam = argParser->getParam("-u");

        if ( uuidParam.empty() ) {
            // Not valid port number specified after port option
            std::cout << "Not valid uuid string after --uuid or -u option." << endl;

            logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

            exit( CUEMS_EXIT_WRONG_PARAMETERS );
        }
        else {
            processUuid = uuidParam ;
        }
    }

    // --ciml or -c command parse and flag set
    bool stopOnLostFlag = true;

    if ( argParser->optionExists("--ciml") || argParser->optionExists("-c") ) {
            stopOnLostFlag = false ;
    }

    delete argParser;

    // End of command line parsing
    //////////////////////////////////////////////////////////

    // Now that we now a more detailed information on the specific player
    // we change the logger slug to reflect this identification on the logs
    logger->setNewSlug("d" + std::to_string(portNumber) + processUuid); 

    if ( filePath.empty() || portNumber == 0 ) {
        std::cout << "Wrong parameters! Check usage..." << endl << endl;
        showcopyright();
        showusage();

        logger->getLogger()->logError( "Exiting with result code: " + std::to_string(CUEMS_EXIT_WRONG_PARAMETERS) );

        exit( CUEMS_EXIT_FAILED_OLA_SETUP );
    }
    else {
        myDmxPlayer = new DmxPlayer(    portNumber, 
                                        offsetMilliseconds,
                                        endWaitMilliseconds,
                                        "", 
                                        filePath,
                                        processUuid,
                                        stopOnLostFlag );

    }

    //////////////////////////////////////////////////////////
    // We are running!! Let's check the USR1 signal handler
    // to let everyone know that we are running
    sigUsr1Handler( SIGUSR1 );

    try {
        myDmxPlayer->run();
    }
    catch( const std::exception &e ) {
        logger->logError(e.what());
    }

    //////////////////////////////////////////////////////////
    // Wait for it to finnish somehow
    while ( myDmxPlayer->olaServer->IsRunning() ) ;

    logger->logInfo( "Exiting with result code: " + std::to_string( EXIT_SUCCESS ) );

    //////////////////////////////////////////////////////////
    // Deleting dynamic assigned elements
    delete myDmxPlayer;
    delete logger;

}

//////////////////////////////////////////////////////////
void showcopyright( void ) {
    std::cout << "dmxplayer-cuems v." << cuems_dmxplayer_VERSION_MAJOR << "." << 
        cuems_dmxplayer_VERSION_MINOR << " - Copyright (C) 2020 Stage Lab & bTactic" << endl <<
        "This program comes with ABSOLUTELY NO WARRANTY; for details type `--show w'." << endl <<
        "This is free software, and you are welcome to redistribute it" << endl <<
        "under certain conditions; type `--show c' for details." << endl << endl;

}

//////////////////////////////////////////////////////////
void showusage( void ) {
    std::cout << "Usage :    dmxplayer-cuems --port <osc_port> [other options] <xml_file_path>" << endl << endl <<
        "           COMPULSORY OPTIONS:" << endl << 
        "           --file , -f <file_path> : xml file to read DMX scenes from." << endl <<
        "               File name can also be stated as the last argument with no option indicator." << endl << endl <<
        "           --port , -p <port_number> : OSC port to listen to." << endl << endl <<
        "           OPTIONAL OPTIONS:" << endl << 
        "           --ciml , -c : Continue If Mtc is Lost, flag to define that the player should continue" << endl <<
        "               if the MTC sync signal is lost. If not specified (standard mode) it stops on lost." << endl << endl <<
        "           --offset , -o <milliseconds>: playing time offset in milliseconds." << endl <<
        "               Positive (+) or (-) negative integer indicating time displacement." << endl << 
        "               Default is 0." << endl << endl <<
        "           --uuid , -u <uuid_string> : indicates a unique identifier for the dmxplayer to be" << endl <<
        "               recognized in different internal identification porpouses such as OLA environment." << endl << endl <<
        "           --wait , -w <milliseconds> : waiting time after reaching the end of the DMX scene and" << endl <<
        "               before quiting the program. Default is 0. -1 indicates the program remains" << endl <<
        "               running till SIG-TERM or OSC command /quit is received." << endl << endl <<
        "           OTHER OPTIONS:" << endl << 
        "           --show : shows license disclaimers." << endl <<
        "               w : shows warranty disclaimer." << endl << 
        "               c : shows copyright disclaimer." << endl << endl << 
        "           Default audio device params are : 2 ch x 44.1K -> default device." << endl << endl <<
        "           audioplayer uses Jack Audio environment, make sure it's running." << endl << endl;
}

//////////////////////////////////////////////////////////
void showwarrantydisclaimer( void ) {
    std::cout << "Warranty disclaimer : " << endl << endl <<
        "THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY" << endl <<
        "APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT" << endl <<
        "HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY" << endl <<
        "OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO," << endl <<
        "THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR" << endl <<
        "PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM" << endl <<
        "IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF" << endl <<
        "ALL NECESSARY SERVICING, REPAIR OR CORRECTION." << endl << endl;

}

//////////////////////////////////////////////////////////
void showcopydisclaimer( void ) {
    std::cout << "Copyright disclaimer : " << endl << endl <<
        "This program is free software: you can redistribute it and/or modify" << endl <<
        "it under the terms of the GNU General Public License as published by" << endl <<
        "the Free Software Foundation, either version 3 of the License, or" << endl <<
        "(at your option) any later version." << endl << endl <<
        "This program is distributed in the hope that it will be useful," << endl <<
        "but WITHOUT ANY WARRANTY; without even the implied warranty of" << endl <<
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" << endl <<
        "GNU General Public License for more details." << endl << endl << 
        "You should have received a copy of the GNU General Public License" << endl <<
        "along with this program.  If not, see <https://www.gnu.org/licenses/>." << endl << endl;
}

//////////////////////////////////////////////////////////
void sigTermHandler( int signum ) {

    std::string str = "SIGTERM received! Finishing.";
    cout << endl << endl << str << endl << endl;

    logger->getLogger()->logInfo( str );

    logger->getLogger()->logInfo( "Exiting with result code: " + std::to_string(signum) );

    if ( myDmxPlayer != NULL )
        delete myDmxPlayer;

    if ( logger != NULL )
        delete logger;

    exit(signum);

}

//////////////////////////////////////////////////////////
void sigUsr1Handler( int /* signum */  ) {
    std::string str = "RUNNING!";
    std::cout << "[" << logger->getSlug() << "] [OK] " << str << endl;
    logger->getLogger()->logOK( str );
}

//////////////////////////////////////////////////////////
void sigIntHandler( int signum ) {
    logger->getLogger()->logInfo( "SIGINT received!" );

    logger->getLogger()->logInfo( "Exiting with result code: " + std::to_string(signum) );

    std::cout << endl;

    if ( myDmxPlayer != NULL )
        delete myDmxPlayer;

    if ( logger != NULL )
        delete logger;

    exit(signum);

}