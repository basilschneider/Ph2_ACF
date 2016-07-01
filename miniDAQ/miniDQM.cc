#include <cstring>
#include <stdint.h>
#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <sstream>
#include <inttypes.h>

#include "../Utils/Utilities.h"
#include "../Utils/Data.h"
#include "../Utils/Event.h"
#include "../Utils/Timer.h"
#include "../Utils/argvparser.h"
#include "../Utils/ConsoleColor.h"
#include "../System/SystemController.h"

#include "TROOT.h"
#include "publisher.h"
#include "DQMHistogrammer.h"

using namespace Ph2_HwDescription;
using namespace Ph2_HwInterface;
using namespace Ph2_System;

using namespace CommandLineProcessing;

void tokenize ( const std::string& str, std::vector<std::string>& tokens, const std::string& delimiters )
{
    // Skip delimiters at beginning.
    std::string::size_type lastPos = str.find_first_not_of ( delimiters, 0 );

    // Find first "non-delimiter".
    std::string::size_type pos = str.find_first_of ( delimiters, lastPos );

    while ( std::string::npos != pos || std::string::npos != lastPos )
    {
        // Found a token, add it to the vector.
        tokens.push_back ( str.substr ( lastPos, pos - lastPos ) );

        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of ( delimiters, pos );

        // Find next "non-delimiter"
        pos = str.find_first_of ( delimiters, lastPos );
    }
}

void dumpEvents ( const std::vector<Event*>& elist )
{
    for ( int i = 0; i < elist.size(); i++ )
    {
        std::cout << "Event index: " << i + 1 << std::endl;
        std::cout << *elist[i] << std::endl;
    }
}

int main ( int argc, char* argv[] )
{
    ArgvParser cmd;

    // init
    cmd.setIntroductoryDescription ( "CMS Ph2_ACF  miniDQM application" );
    // error codes
    cmd.addErrorCode ( 0, "Success" );
    cmd.addErrorCode ( 1, "Error" );
    // options
    cmd.setHelpOption ( "h", "help", "Print this help page" );

    cmd.defineOption ( "file", "Binary Data File", ArgvParser::OptionRequiresValue /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "file", "f" );

    cmd.defineOption ( "output", "Output Directory for DQM plots & page. Default value: Results", ArgvParser::OptionRequiresValue /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "output", "o" );

    cmd.defineOption ( "dqm", "Build DQM webpage. Default = false", ArgvParser::NoOptionAttribute /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "dqm", "d" );

    cmd.defineOption ( "reverse", "reverse bit order for CBC data in Data::set. Default = false (needs to be used for Imperial FW);", ArgvParser::NoOptionAttribute /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "reverse", "r" );

    cmd.defineOption ( "swap", "Swap endianness in Data::set. Default = true (Ph2_ACF); should be true only for legacy GlibStreamer Data", ArgvParser::NoOptionAttribute /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "swap", "s" );

    cmd.defineOption ( "tree", "Create a ROOT tree also. Default = false", ArgvParser::NoOptionAttribute /*| ArgvParser::OptionRequired*/ );

    cmd.defineOption ( "ncolumn", "Specify no. of columns.", ArgvParser::OptionRequiresValue /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "ncolumn", "u" );

    cmd.defineOption ( "filter", "Select Event Filtering. Default = false", ArgvParser::NoOptionAttribute /*| ArgvParser::OptionRequired*/ );

    cmd.defineOption ( "cbcType", "Specify the CBC type(2,4,8 or 16).", ArgvParser::OptionRequiresValue /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "cbcType", "c" );

    cmd.defineOption ( "nevt", "Specify number of events to be processed", ArgvParser::OptionRequiresValue /*| ArgvParser::OptionRequired*/ );
    cmd.defineOptionAlternative ( "nevt", "n" );

    std::map<std::string, pair<int, std::string>>  cbcTypeEvtSizeMap;
    cbcTypeEvtSizeMap["2"] = { 2, XML_DESCRIPTION_FILE_2CBC };
    cbcTypeEvtSizeMap["4"] = { 4, XML_DESCRIPTION_FILE_4CBC };
    cbcTypeEvtSizeMap["8"] = { 8, XML_DESCRIPTION_FILE_8CBC };
    cbcTypeEvtSizeMap["16"] = { 16, XML_DESCRIPTION_FILE_16CBC };
    int result = cmd.parse ( argc, argv );

    if ( result != ArgvParser::NoParserError )
    {
        std::cout << cmd.parseErrorDescription ( result ) << std::endl;
        exit ( 1 );
    }

    // now query the parsing results
    std::string rawFilename = ( cmd.foundOption ( "file" ) ) ? cmd.optionValue ( "file" ) : "";

    if ( rawFilename.empty() )
    {
        std::cerr << "Error, no binary file provided. Quitting" << std::endl;
        exit ( 2 );
    }

    // Check if the file can be found
    if ( ! boost::filesystem::exists ( rawFilename ) )
    {
        std::cerr << "Error!! binary file " << rawFilename << " not found, exiting!" << std::endl;
        exit ( 3 );
    }

    std::string cbcType = ( cmd.foundOption ( "cbcType" ) ) ? cmd.optionValue ( "cbcType" ) : "";

    if ( cbcTypeEvtSizeMap.find ( cbcType ) == cbcTypeEvtSizeMap.end()  )
    {
        std::cerr << "Wrong CBC type specified!!!!" << std::endl;
        exit ( 4 );
    }

    bool cReverse = ( cmd.foundOption ( "reverse" ) ) ? true : false;
    bool cSwap = ( cmd.foundOption ( "swap" ) ) ? true : false;
    bool cDQMPage = ( cmd.foundOption ( "dqm" ) ) ? true : false;
    bool addTree = ( cmd.foundOption ( "tree" ) ) ? true : false;
    int ncol       = ( cmd.foundOption ( "ncolumn" ) ) ? stoi (cmd.optionValue ( "ncolumn" ) ) : 2;
    bool evtFilter = ( cmd.foundOption ( "filter" ) ) ? true : false;
    int maxevt     = ( cmd.foundOption ( "nevt" ) ) ? stoi (cmd.optionValue ( "nevt" ) ) : -1;

    // Create the Histogrammer object
    DQMHistogrammer* dqmh = new DQMHistogrammer (addTree, ncol, evtFilter);

    // Add File handler
    dqmh->addFileHandler ( rawFilename, 'r' );

    // Read the raw data file
    std::vector<uint32_t> dataVec;
    int eventSize = EVENT_HEADER_TDC_SIZE_32 + CBC_EVENT_SIZE_32 * cbcTypeEvtSizeMap[cbcType].first;
    //long nbytes = (maxevt > -1) ? maxevt * eventSize * 4 : -1;
    dqmh->readFile (dataVec);

    //alternatively in packets and pseudocode
    //for(uint32_t cCounter = 0; cCounter ...)
    //{
    //cSystemController.readFile(dataVec, pEventSize32 * 10);
    //.
    //.
    //}

    // Build the hardware setup
    std::string cHWFile = getenv ( "BASE_DIR" );
    cHWFile += "/";
    cHWFile += cbcTypeEvtSizeMap[cbcType].second;

    std::cout << "HWfile=" << cHWFile << std::endl;
    //dqmh->parseHWxml ( cHWFile );
    dqmh->InitializeHw ( cHWFile );
    //dqmh->fParser.parseHW (cHWFile, fBeBoardFWMap, fBoardVector, os);
    const BeBoard* pBoard = dqmh->getBoard ( 0 );

    // Now split the data buffer in events
    Data d;
    int nEvents = dataVec.size() / eventSize;
    std::cout << "eventSize = "  << eventSize
              << ", nEvents = " << nEvents
              << ", maxEvents = " << maxevt
              << std::endl;

    //call the Data::set() method - here is where i have to know the swap opitions
    d.Set ( pBoard, dataVec, nEvents, cReverse, cSwap );
    const std::vector<Event*>& elist = d.GetEvents ( pBoard );

    if ( cDQMPage && elist.size() > 0 )
    {
        gROOT->SetBatch ( true );
        dqmh->bookHistos (elist[0]->GetEventMap(), elist.size() );
        dqmh->fillHistos (elist);

        // Create the DQM plots and generate the root file
        // first of all strip the folder name
        std::vector<std::string> tokens;
        tokenize ( rawFilename, tokens, "/" );
        std::string fname = tokens.back();

        // now form the output Root filename
        tokens.clear();
        tokenize ( fname, tokens, "." );
        std::string runLabel = tokens[0];
        std::string dqmFilename =  runLabel + "_dqm.root";

        dqmh->saveHistos (dqmFilename);

        std::string cDirBasePath;

        if ( cmd.foundOption ( "output" ) )
        {
            cDirBasePath = cmd.optionValue ( "output" );
            cDirBasePath += "/";

        }
        else cDirBasePath = "Results/";

        // now read back the Root file and publish the histograms on the DQM page
        RootWeb::makeDQMmonitor ( dqmFilename, cDirBasePath, runLabel );
        std::cout << "Saving root file to " << dqmFilename << " and webpage to " << cDirBasePath << std::endl;
    }
    else dumpEvents ( elist );

    delete dqmh;
    return 0;
}
