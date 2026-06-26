/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_next_action_runtime.h>

#include <qa_utils/utility_registry.h>

#include <common.h>

#include <wx/cmdline.h>
#include <wx/ffile.h>

#include <iostream>


static const wxCmdLineEntryDesc g_cmdLineDesc[] = {
    { wxCMD_LINE_SWITCH,
      "h",
      "help",
      _( "displays help on the command line parameters" ).mb_str(),
      wxCMD_LINE_VAL_NONE,
      wxCMD_LINE_OPTION_HELP },
    { wxCMD_LINE_PARAM,
      nullptr,
      nullptr,
      _( "golden dataset JSON file" ).mb_str(),
      wxCMD_LINE_VAL_STRING,
      wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_NONE }
};


static int nextActionReplayBatchMain( int argc, char** argv )
{
    wxCmdLineParser parser( argc, argv );
    parser.SetDesc( g_cmdLineDesc );
    parser.AddUsageText( _( "Evaluate a Next Action replay batch JSON file" ) );

    int parsed = parser.Parse();

    if( parsed != 0 )
        return parsed == -1 ? KI_TEST::RET_CODES::OK : KI_TEST::RET_CODES::BAD_CMDLINE;

    if( parser.GetParamCount() != 1 )
        return KI_TEST::RET_CODES::BAD_CMDLINE;

    wxFFile batchFile( parser.GetParam( 0 ), wxS( "rb" ) );

    if( !batchFile.IsOpened() )
        return KI_TEST::RET_CODES::BAD_CMDLINE;

    wxString batchJson;

    if( !batchFile.ReadAll( &batchJson ) )
        return KI_TEST::RET_CODES::BAD_CMDLINE;

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceBatchJson( batchJson );

    std::cout << evaluation.m_SummaryJson.ToStdString() << std::endl;

    return evaluation.m_Valid ? KI_TEST::RET_CODES::OK : KI_TEST::RET_CODES::TOOL_SPECIFIC;
}


static int nextActionReplayGoldenMain( int argc, char** argv )
{
    wxCmdLineParser parser( argc, argv );
    parser.SetDesc( g_cmdLineDesc );
    parser.AddUsageText( _( "Evaluate Next Action golden replay dataset files" ) );

    int parsed = parser.Parse();

    if( parsed != 0 )
        return parsed == -1 ? KI_TEST::RET_CODES::OK : KI_TEST::RET_CODES::BAD_CMDLINE;

    wxArrayString datasetPaths;

    for( unsigned i = 0; i < parser.GetParamCount(); ++i )
        datasetPaths.Add( parser.GetParam( i ) );

    if( datasetPaths.empty() )
        return KI_TEST::RET_CODES::BAD_CMDLINE;

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_BATCH_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDatasetFiles( datasetPaths );

    std::cout << evaluation.m_SummaryJson.ToStdString() << std::endl;

    return evaluation.m_Passed ? KI_TEST::RET_CODES::OK : KI_TEST::RET_CODES::TOOL_SPECIFIC;
}


static bool registered = UTILITY_REGISTRY::Register( {
        "next_action_replay_golden",
        "Evaluate Next Action golden replay dataset files",
        nextActionReplayGoldenMain,
} );

static bool registeredBatch = UTILITY_REGISTRY::Register( {
        "next_action_replay_batch",
        "Evaluate a Next Action replay batch JSON file",
        nextActionReplayBatchMain,
} );
