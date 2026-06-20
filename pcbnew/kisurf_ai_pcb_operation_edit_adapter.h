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

#include <vector>
#include <wx/string.h>

class BOARD_ITEM;
class COMMIT;
class KISURF_AI_PCB_OBJECT_RESOLVER;

class KISURF_AI_PCB_OPERATION_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER( KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                                          COMMIT& aCommit,
                                          wxString aCommitMessage = wxS( "Apply AI PCB edit" ) );

    bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount ) override;
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override;
    bool EndApply() override;
    void AbortApply() override;

    const std::vector<BOARD_ITEM*>& AddedItems() const { return m_AddedItems; }
    const std::vector<AI_OBJECT_REF>& FailedObjects() const { return m_FailedObjects; }
    bool WasCommitted() const { return m_WasCommitted; }
    bool WasReverted() const { return m_WasReverted; }

private:
    KISURF_AI_PCB_OBJECT_RESOLVER& m_Resolver;
    COMMIT&                        m_Commit;
    wxString                       m_CommitMessage;
    std::vector<BOARD_ITEM*>        m_AddedItems;
    std::vector<AI_OBJECT_REF>      m_FailedObjects;
    bool                           m_WasCommitted = false;
    bool                           m_WasReverted = false;
};
