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

#include <memory>

class KICOMMON_API AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    virtual ~AI_STRUCTURED_SURFACE_STATE_BACKEND() = default;

    virtual bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                          wxString& aSurfaceStateJson,
                                          wxString& aError ) = 0;
    virtual bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                           bool aChanged,
                                           wxString& aError ) = 0;
    virtual void AbortSurfaceTransaction() {}
};


class KICOMMON_API AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    explicit AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND(
            wxString& aSurfaceStateJson );

    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override;
    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override;

private:
    wxString& m_SurfaceStateJson;
};


class AI_STRUCTURED_SURFACE_APPLY_ADAPTER :
        public AI_ACCEPT_APPLY_ADAPTER
{
public:
    explicit KICOMMON_API AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
            wxString& aSurfaceStateJson );
    explicit KICOMMON_API AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
            AI_STRUCTURED_SURFACE_STATE_BACKEND& aBackend );

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

    std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND> m_OwnedBackend;
    AI_STRUCTURED_SURFACE_STATE_BACKEND&                 m_Backend;
    wxString                                             m_WorkingStateJson;
    bool                                                 m_InTransaction = false;
    bool                                                 m_SurfaceChanged = false;
};
