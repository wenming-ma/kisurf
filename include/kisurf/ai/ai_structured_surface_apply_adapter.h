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
#include <kisurf/ai/ai_accept_applier.h>

class AI_STRUCTURED_SURFACE_APPLY_ADAPTER :
        public AI_ACCEPT_APPLY_ADAPTER
{
public:
    explicit KICOMMON_API AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
            wxString& aSurfaceStateJson );

    KICOMMON_API bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                                        wxString& aError ) override;
    KICOMMON_API bool ApplyOperation(
            const AI_SESSION_OPERATION_RECORD& aOperation,
            wxString& aError ) override;
    KICOMMON_API bool CommitTransaction( wxString& aError ) override;
    KICOMMON_API bool HasBoardChanges() const override;
    KICOMMON_API void AbortTransaction() override;

    KICOMMON_API bool HasSurfaceChanges() const;

private:
    bool applySurfacePatch( const AI_SESSION_OPERATION_RECORD& aOperation,
                            wxString& aError );

    wxString& m_SurfaceStateJson;
    wxString  m_WorkingStateJson;
    bool      m_InTransaction = false;
    bool      m_SurfaceChanged = false;
};
