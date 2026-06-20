/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_suggestion_orchestrator.h>

#include <kisurf/ai/ai_suggestion_operations.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

namespace
{
bool hasActivity( const AI_ACTIVITY_RECORD& aActivity )
{
    return aActivity.m_Sequence != 0 || !aActivity.m_ActionName.IsEmpty();
}


AI_CONTEXT_VERSION effectiveVersion( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_ContextVersion.IsValid() )
        return aTrigger.m_ContextVersion;

    return aTrigger.m_ContextSnapshot.m_Version;
}


bool validTrigger( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return false;

    return aTrigger.m_ContextSnapshot.HasContext() || hasActivity( aTrigger.m_Activity );
}


bool validSuggestion( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return !aSuggestion.m_Title.IsEmpty() || !aSuggestion.m_Body.IsEmpty()
           || !aSuggestion.m_PreviewObjects.empty() || !aSuggestion.m_EditObjects.empty();
}


bool isActive( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return aSuggestion.m_Status == AI_SUGGESTION_STATUS::Pending
        || aSuggestion.m_Status == AI_SUGGESTION_STATUS::Previewing;
}


bool isTerminal( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return aSuggestion.m_Status == AI_SUGGESTION_STATUS::Accepted
        || aSuggestion.m_Status == AI_SUGGESTION_STATUS::Rejected
        || aSuggestion.m_Status == AI_SUGGESTION_STATUS::Expired;
}


bool hasPreviewableOperation( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return aSuggestion.m_Kind == AI_SUGGESTION_KIND::Preview
           && ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson ).has_value();
}


bool hasActionPreviewOperation( const AI_SUGGESTION_RECORD& aSuggestion )
{
    if( aSuggestion.m_Kind != AI_SUGGESTION_KIND::Preview
        || aSuggestion.m_ArgumentsJson.IsEmpty() )
    {
        return false;
    }

    wxScopedCharBuffer buffer = aSuggestion.m_ArgumentsJson.ToUTF8();
    nlohmann::json args = nlohmann::json::parse(
            buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string(),
            nullptr, false );

    if( args.is_discarded() || !args.is_object()
        || !args.contains( "operation" ) || !args["operation"].is_string()
        || args["operation"].get<std::string>() != "action_preview" )
    {
        return false;
    }

    return args.contains( "action" ) && args["action"].is_string()
           && !args["action"].get_ref<const std::string&>().empty();
}


bool recordCanPreview( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return isActive( aSuggestion )
           && ( !aSuggestion.m_PreviewObjects.empty()
                || hasPreviewableOperation( aSuggestion ) );
}


bool recordCanAccept( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return isActive( aSuggestion ) && !aSuggestion.m_PreviewOnly
           && ( !aSuggestion.m_EditObjects.empty()
                || hasActionPreviewOperation( aSuggestion ) );
}


wxString editorKindToken( AI_EDITOR_KIND aEditorKind )
{
    switch( aEditorKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return wxS( "pcb" );

    case AI_EDITOR_KIND::Schematic:
        return wxS( "sch" );

    default:
        return wxS( "unknown" );
    }
}


wxString computeFingerprint( const AI_SUGGESTION_TRIGGER& aTrigger,
                             const AI_CONTEXT_VERSION& aVersion )
{
    wxString fingerprint;
    fingerprint << editorKindToken( aTrigger.m_EditorKind ) << wxS( "|" )
                << aVersion.AsString() << wxS( "|" )
                << aTrigger.m_Activity.m_Sequence << wxS( "|" )
                << aTrigger.m_Activity.m_ActionName;

    const size_t selectedCount =
            std::min<size_t>( aTrigger.m_ContextSnapshot.m_SelectedObjects.size(), 8 );

    for( size_t i = 0; i < selectedCount; ++i )
        fingerprint << wxS( "|" ) << aTrigger.m_ContextSnapshot.m_SelectedObjects[i].m_Label;

    return fingerprint;
}


bool hasActiveDuplicate( const std::vector<AI_SUGGESTION_RECORD>& aRecords,
                         const wxString& aFingerprint )
{
    for( const AI_SUGGESTION_RECORD& record : aRecords )
    {
        if( isActive( record ) && record.m_Fingerprint == aFingerprint )
            return true;
    }

    return false;
}


bool sameVersion( const AI_CONTEXT_VERSION& aLeft, const AI_CONTEXT_VERSION& aRight )
{
    return aLeft.m_DocumentRevision == aRight.m_DocumentRevision
        && aLeft.m_SelectionRevision == aRight.m_SelectionRevision
        && aLeft.m_ViewRevision == aRight.m_ViewRevision;
}


wxString fallbackFingerprint( const AI_SUGGESTION_RECORD& aSuggestion )
{
    wxString fingerprint;
    fingerprint << editorKindToken( aSuggestion.m_EditorKind ) << wxS( "|tool|" )
                << aSuggestion.m_ContextVersion.AsString() << wxS( "|" )
                << aSuggestion.m_Title << wxS( "|" )
                << aSuggestion.m_ArgumentsJson;
    return fingerprint;
}
}


AI_SUGGESTION_ORCHESTRATOR::AI_SUGGESTION_ORCHESTRATOR(
        AI_SUGGESTION_PROVIDER& aProvider, size_t aCapacity ) :
        m_Provider( aProvider ),
        m_Capacity( aCapacity )
{
}


std::optional<AI_SUGGESTION_RECORD> AI_SUGGESTION_ORCHESTRATOR::Update(
        AI_SUGGESTION_TRIGGER aTrigger )
{
    if( !validTrigger( aTrigger ) )
        return std::nullopt;

    std::optional<AI_SUGGESTION_RECORD> suggested = m_Provider.Suggest( aTrigger );

    if( !suggested || !validSuggestion( *suggested ) )
        return std::nullopt;

    AI_CONTEXT_VERSION version = effectiveVersion( aTrigger );
    AI_SUGGESTION_RECORD record = *suggested;

    if( record.m_Fingerprint.IsEmpty() )
        record.m_Fingerprint = computeFingerprint( aTrigger, version );

    if( hasActiveDuplicate( m_Records, record.m_Fingerprint ) )
        return std::nullopt;

    record.m_Id = m_NextId++;
    record.m_Sequence = m_NextSequence++;

    if( record.m_EditorKind == AI_EDITOR_KIND::Unknown )
        record.m_EditorKind = aTrigger.m_EditorKind;

    if( !record.m_ContextVersion.IsValid() )
        record.m_ContextVersion = version;

    if( record.m_TriggerActivitySequence == 0 )
        record.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;

    if( aTrigger.m_PreviewOnly )
    {
        record.m_PreviewOnly = true;
        record.m_EditObjects.clear();
    }

    record.m_Status = AI_SUGGESTION_STATUS::Pending;
    m_Records.push_back( record );
    trimToCapacity();

    return record;
}


std::optional<AI_SUGGESTION_RECORD> AI_SUGGESTION_ORCHESTRATOR::AddSuggestion(
        AI_SUGGESTION_RECORD aSuggestion )
{
    if( !validSuggestion( aSuggestion ) )
        return std::nullopt;

    if( aSuggestion.m_Fingerprint.IsEmpty() )
        aSuggestion.m_Fingerprint = fallbackFingerprint( aSuggestion );

    if( hasActiveDuplicate( m_Records, aSuggestion.m_Fingerprint ) )
        return std::nullopt;

    aSuggestion.m_Id = m_NextId++;
    aSuggestion.m_Sequence = m_NextSequence++;
    aSuggestion.m_Status = AI_SUGGESTION_STATUS::Pending;
    m_Records.push_back( aSuggestion );
    trimToCapacity();

    return aSuggestion;
}


std::vector<AI_SUGGESTION_RECORD> AI_SUGGESTION_ORCHESTRATOR::Records() const
{
    return m_Records;
}


std::optional<AI_SUGGESTION_RECORD> AI_SUGGESTION_ORCHESTRATOR::Find(
        uint64_t aSuggestionId ) const
{
    for( const AI_SUGGESTION_RECORD& record : m_Records )
    {
        if( record.m_Id == aSuggestionId )
            return record;
    }

    return std::nullopt;
}


bool AI_SUGGESTION_ORCHESTRATOR::CanPreview( uint64_t aSuggestionId ) const
{
    std::optional<AI_SUGGESTION_RECORD> record = Find( aSuggestionId );
    return record && recordCanPreview( *record );
}


bool AI_SUGGESTION_ORCHESTRATOR::CanAccept( uint64_t aSuggestionId ) const
{
    std::optional<AI_SUGGESTION_RECORD> record = Find( aSuggestionId );
    return record && recordCanAccept( *record );
}


bool AI_SUGGESTION_ORCHESTRATOR::BeginPreview( uint64_t aSuggestionId,
                                               AI_PREVIEW_MANAGER& aPreviewManager )
{
    AI_SUGGESTION_RECORD* record = findMutable( aSuggestionId );

    if( !record || !recordCanPreview( *record ) )
        return false;

    aPreviewManager.BeginPreview();

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( record->m_ArgumentsJson );

    if( operation )
        aPreviewManager.ShowOperation( *operation );

    for( const AI_OBJECT_REF& object : record->m_PreviewObjects )
        aPreviewManager.ShowObject( object );

    record->m_Status = AI_SUGGESTION_STATUS::Previewing;
    return true;
}


bool AI_SUGGESTION_ORCHESTRATOR::Accept( uint64_t aSuggestionId,
                                         AI_EDIT_SESSION& aEditSession )
{
    AI_SUGGESTION_RECORD* record = findMutable( aSuggestionId );

    if( !record || !recordCanAccept( *record ) )
        return false;

    if( record->m_EditObjects.empty() )
        return false;

    if( !aEditSession.Apply( record->m_EditObjects, record->m_Validation ) )
        return false;

    record->m_Status = AI_SUGGESTION_STATUS::Accepted;
    return true;
}


bool AI_SUGGESTION_ORCHESTRATOR::MarkAccepted( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* record = findMutable( aSuggestionId );

    if( !record || !isActive( *record ) )
        return false;

    record->m_Status = AI_SUGGESTION_STATUS::Accepted;
    return true;
}


bool AI_SUGGESTION_ORCHESTRATOR::Reject( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* record = findMutable( aSuggestionId );

    if( !record || !isActive( *record ) )
        return false;

    record->m_Status = AI_SUGGESTION_STATUS::Rejected;
    return true;
}


size_t AI_SUGGESTION_ORCHESTRATOR::ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion )
{
    size_t expired = 0;

    for( AI_SUGGESTION_RECORD& record : m_Records )
    {
        if( !isActive( record ) || sameVersion( record.m_ContextVersion, aCurrentVersion ) )
            continue;

        record.m_Status = AI_SUGGESTION_STATUS::Expired;
        ++expired;
    }

    return expired;
}


AI_SUGGESTION_RECORD* AI_SUGGESTION_ORCHESTRATOR::findMutable( uint64_t aSuggestionId )
{
    for( AI_SUGGESTION_RECORD& record : m_Records )
    {
        if( record.m_Id == aSuggestionId )
            return &record;
    }

    return nullptr;
}


void AI_SUGGESTION_ORCHESTRATOR::trimToCapacity()
{
    if( m_Capacity == 0 )
    {
        m_Records.clear();
        return;
    }

    while( m_Records.size() > m_Capacity )
    {
        auto terminal = std::find_if( m_Records.begin(), m_Records.end(),
                                      []( const AI_SUGGESTION_RECORD& aRecord )
                                      {
                                          return isTerminal( aRecord );
                                      } );

        if( terminal != m_Records.end() )
            m_Records.erase( terminal );
        else
            m_Records.erase( m_Records.begin() );
    }
}
