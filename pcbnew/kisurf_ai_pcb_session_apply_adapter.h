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

#include <kisurf/ai/ai_accept_applier.h>

#include <map>
#include <memory>
#include <utility>

class BOARD_COMMIT;
class BOARD_ITEM;
class BOARD;
class PCB_EDIT_FRAME;
class TOOL_MANAGER;

class KISURF_AI_PCB_SESSION_APPLY_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    explicit KISURF_AI_PCB_SESSION_APPLY_ADAPTER( PCB_EDIT_FRAME& aFrame );
    KISURF_AI_PCB_SESSION_APPLY_ADAPTER( BOARD& aBoard, TOOL_MANAGER& aToolManager );
    explicit KISURF_AI_PCB_SESSION_APPLY_ADAPTER( BOARD& aPreviewBoard );
    ~KISURF_AI_PCB_SESSION_APPLY_ADAPTER() override;

    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override;
    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override;
    bool CommitTransaction( wxString& aError ) override;
    bool HasBoardChanges() const override;
    void AbortTransaction() override;

private:
    using HANDLE_KEY = std::pair<uint64_t, uint64_t>;

    static HANDLE_KEY keyForHandle( const AI_SESSION_HANDLE& aHandle );
    void seedLiveItemHandleMap( const AI_EXECUTION_SESSION& aSession );
    void stageModify( BOARD_ITEM* aItem );
    void addCreatedItem( BOARD_ITEM* aItem );
    void removeItem( BOARD_ITEM* aItem );

    PCB_EDIT_FRAME*                   m_Frame = nullptr;
    BOARD*                            m_Board = nullptr;
    TOOL_MANAGER*                     m_ToolManager = nullptr;
    std::unique_ptr<BOARD_COMMIT>     m_Commit;
    std::map<HANDLE_KEY, BOARD_ITEM*> m_ItemsByHandle;
    bool                              m_DirectBoardApply = false;
    bool                              m_DirectBoardMutated = false;
};
