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

#include <kisurf/ai/ai_edit_session.h>
#include <math/vector2d.h>

#include <vector>
#include <wx/string.h>

class COMMIT;
class KISURF_AI_SCH_OBJECT_RESOLVER;
class SCH_ITEM;
class SCH_SCREEN;

class KISURF_AI_SCH_MOVE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    KISURF_AI_SCH_MOVE_EDIT_ADAPTER( KISURF_AI_SCH_OBJECT_RESOLVER& aResolver,
                                     COMMIT& aCommit, SCH_SCREEN& aScreen,
                                     const VECTOR2I& aDelta,
                                     wxString aCommitMessage = wxS( "Apply AI schematic edit" ) );

    bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount ) override;
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override;
    bool EndApply() override;
    void AbortApply() override;

    const std::vector<SCH_ITEM*>& MovedItems() const { return m_MovedItems; }
    const std::vector<AI_OBJECT_REF>& FailedObjects() const { return m_FailedObjects; }
    bool WasCommitted() const { return m_WasCommitted; }
    bool WasReverted() const { return m_WasReverted; }

private:
    KISURF_AI_SCH_OBJECT_RESOLVER& m_Resolver;
    COMMIT&                        m_Commit;
    SCH_SCREEN&                    m_Screen;
    VECTOR2I                       m_Delta;
    wxString                       m_CommitMessage;
    std::vector<SCH_ITEM*>         m_MovedItems;
    std::vector<AI_OBJECT_REF>     m_FailedObjects;
    bool                           m_WasCommitted = false;
    bool                           m_WasReverted = false;
};
