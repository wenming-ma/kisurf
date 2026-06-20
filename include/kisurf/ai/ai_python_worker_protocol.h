/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_python_worker.h>

#include <string>
#include <wx/string.h>

class KICOMMON_API AI_PYTHON_WORKER_PROTOCOL
{
public:
    static std::string EncodeFrame( const std::string& aPayload );
    static bool DecodeFrame( const std::string& aFrame, std::string* aPayload,
                             wxString* aError = nullptr );

    static std::string EncodeRunCellRequest( const AI_PYTHON_CELL_REQUEST& aRequest );
    static std::string EncodeCancelSessionRequest( uint64_t aSessionId,
                                                   const wxString& aReason );
    static std::string EncodeShutdownRequest();

    static AI_PYTHON_CELL_RESULT DecodeCellResult( const std::string& aResponse,
                                                   wxString* aError = nullptr );
    static bool DecodeEvent( const std::string& aResponse, AI_PYTHON_EVENT* aEvent,
                             wxString* aError = nullptr );

};
