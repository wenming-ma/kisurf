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
#include <kisurf/ai/ai_execution_session.h>

#include <map>
#include <vector>
#include <wx/string.h>

struct KICOMMON_API AI_SHADOW_ITEM
{
    AI_SESSION_HANDLE          m_Handle;
    AI_SESSION_OPERATION_KIND  m_CreatedBy = AI_SESSION_OPERATION_KIND::Unknown;
    wxString                   m_Type;
    wxString                   m_Alias;
    wxString                   m_Net;
    wxString                   m_Layer;
    std::vector<wxString>      m_Layers;
    wxString                   m_GeometryJson = wxS( "{}" );
    wxString                   m_PropertiesJson = wxS( "{}" );
    std::map<wxString, wxString> m_Metadata;
    uint64_t                  m_CreatedEpoch = 0;
    uint64_t                  m_UpdatedEpoch = 0;
    bool                      m_Deleted = false;
};

class KICOMMON_API AI_SHADOW_BOARD
{
public:
    void UpsertItem( AI_SHADOW_ITEM aItem );
    const AI_SHADOW_ITEM* FindItem( const AI_SESSION_HANDLE& aHandle ) const;
    AI_SHADOW_ITEM* FindMutableItem( const AI_SESSION_HANDLE& aHandle );

    std::vector<AI_SHADOW_ITEM> QueryItems( const wxString& aFilterJson = wxS( "{}" ) ) const;
    wxString QueryBoardSummary() const;
    size_t LiveItemCount() const;
    size_t LiveItemCountByType( const wxString& aType ) const;

    bool UpdateGeometry( const AI_SESSION_HANDLE& aHandle, wxString aGeometryJson,
                         uint64_t aEpoch );
    bool MoveItem( const AI_SESSION_HANDLE& aHandle, const wxString& aDeltaJson,
                   uint64_t aEpoch );
    bool MoveItemTo( const AI_SESSION_HANDLE& aHandle, const wxString& aTargetJson,
                     uint64_t aEpoch );
    bool DeleteItem( const AI_SESSION_HANDLE& aHandle, uint64_t aEpoch );
    bool SetItemNet( const AI_SESSION_HANDLE& aHandle, wxString aNet, uint64_t aEpoch );
    bool SetItemLayer( const AI_SESSION_HANDLE& aHandle, wxString aLayer, uint64_t aEpoch );
    bool SetItemLayers( const AI_SESSION_HANDLE& aHandle, std::vector<wxString> aLayers,
                        uint64_t aEpoch );
    bool SetItemProperties( const AI_SESSION_HANDLE& aHandle, wxString aPropertiesJson,
                            uint64_t aEpoch );
    bool SetMetadata( const AI_SESSION_HANDLE& aHandle,
                      const std::map<wxString, wxString>& aKeyValues, uint64_t aEpoch );

    void CaptureCheckpoint( const AI_SESSION_CHECKPOINT& aCheckpoint );
    bool RollbackTo( const AI_SESSION_CHECKPOINT& aCheckpoint );

private:
    struct CHECKPOINT_STATE
    {
        AI_SESSION_CHECKPOINT m_Checkpoint;
        std::map<uint64_t, AI_SHADOW_ITEM> m_Items;
    };

    std::map<uint64_t, AI_SHADOW_ITEM> m_Items;
    std::vector<CHECKPOINT_STATE>      m_Checkpoints;
};
