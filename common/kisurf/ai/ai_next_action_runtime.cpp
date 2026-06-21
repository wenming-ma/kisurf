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

#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_next_action_provider.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


bool sameVersion( const AI_CONTEXT_VERSION& aLeft, const AI_CONTEXT_VERSION& aRight )
{
    return aLeft.m_DocumentRevision == aRight.m_DocumentRevision
        && aLeft.m_SelectionRevision == aRight.m_SelectionRevision
        && aLeft.m_ViewRevision == aRight.m_ViewRevision;
}


bool isActive( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return aSuggestion.m_Status == AI_SUGGESTION_STATUS::Pending
        || aSuggestion.m_Status == AI_SUGGESTION_STATUS::Previewing;
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

    nlohmann::json args = nlohmann::json::parse( toUtf8String( aSuggestion.m_ArgumentsJson ),
                                                 nullptr, false );

    return args.is_object() && args.contains( "operation" ) && args["operation"].is_string()
           && args["operation"].get<std::string>() == "action_preview"
           && args.contains( "action" ) && args["action"].is_string()
           && !args["action"].get_ref<const std::string&>().empty();
}


nlohmann::json parseObjectBody( const wxString& aBody );


void bindRuntimeProvenanceToSuggestion( AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( provenance.empty() )
        return;

    if( provenance.contains( "preview_lease" ) && provenance["preview_lease"].is_object() )
        provenance["preview_lease"]["suggestion_id"] = aSuggestion.m_Id;

    if( provenance.contains( "accept_token" ) && provenance["accept_token"].is_object() )
        provenance["accept_token"]["preview_id"] = aSuggestion.m_Id;

    aSuggestion.m_RuntimeProvenanceJson = fromUtf8String( provenance.dump() );
}


bool hasValidRuntimeAcceptToken( const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object()
        || !provenance.contains( "runtime" )
        || !provenance["runtime"].is_string()
        || provenance["runtime"].get<std::string>() != "next_action"
        || !provenance.contains( "preview_lease" )
        || !provenance.contains( "accept_token" )
        || !provenance["preview_lease"].is_object()
        || !provenance["accept_token"].is_object() )
    {
        return false;
    }

    const nlohmann::json& lease = provenance["preview_lease"];
    const nlohmann::json& token = provenance["accept_token"];

    if( !lease.value( "active", false ) )
        return false;

    if( lease.value( "suggestion_id", uint64_t( 0 ) ) != aSuggestion.m_Id
        || token.value( "preview_id", uint64_t( 0 ) ) != aSuggestion.m_Id )
    {
        return false;
    }

    if( lease.value( "lease_id", uint64_t( 0 ) )
        != token.value( "lease_id", uint64_t( 0 ) ) )
    {
        return false;
    }

    if( lease.value( "owner_namespace", std::string() )
        != token.value( "owner_namespace", std::string() ) )
    {
        return false;
    }

    if( lease.value( "owner_namespace", std::string() ) != "nextaction"
        || token.value( "attempt_id", uint64_t( 0 ) ) == 0 )
    {
        return false;
    }

    if( !token.contains( "context_version" ) || !token["context_version"].is_object() )
        return false;

    const nlohmann::json& version = token["context_version"];
    return version.value( "document_revision", uint64_t( 0 ) )
                   == aSuggestion.m_ContextVersion.m_DocumentRevision
           && version.value( "selection_revision", uint64_t( 0 ) )
                      == aSuggestion.m_ContextVersion.m_SelectionRevision
           && version.value( "view_revision", uint64_t( 0 ) )
                      == aSuggestion.m_ContextVersion.m_ViewRevision;
}


AI_NEXT_ACTION_DECISION_KIND parseDecisionKind( const nlohmann::json& aJson )
{
    if( !aJson.is_object() || !aJson.contains( "decision_kind" )
        || !aJson["decision_kind"].is_string() )
    {
        return AI_NEXT_ACTION_DECISION_KIND::Abandon;
    }

    const std::string kind = aJson["decision_kind"].get<std::string>();

    if( kind == "wait" )
        return AI_NEXT_ACTION_DECISION_KIND::Wait;

    if( kind == "gather" )
        return AI_NEXT_ACTION_DECISION_KIND::Gather;

    if( kind == "attempt" )
        return AI_NEXT_ACTION_DECISION_KIND::Attempt;

    if( kind == "retry" )
        return AI_NEXT_ACTION_DECISION_KIND::Retry;

    if( kind == "rollback_retry" )
        return AI_NEXT_ACTION_DECISION_KIND::RollbackRetry;

    if( kind == "publish" )
        return AI_NEXT_ACTION_DECISION_KIND::Publish;

    return AI_NEXT_ACTION_DECISION_KIND::Abandon;
}


wxString optionalString( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return wxString();

    return fromUtf8String( aJson[aKey].get<std::string>() );
}


nlohmann::json parseObjectBody( const wxString& aBody )
{
    const std::string body = toUtf8String( aBody );
    const size_t      first = body.find( '{' );
    const size_t      last = body.rfind( '}' );

    if( first == std::string::npos || last == std::string::npos || last < first )
        return nlohmann::json::object();

    nlohmann::json parsed = nlohmann::json::parse( body.substr( first, last - first + 1 ),
                                                   nullptr, false );
    return parsed.is_object() ? parsed : nlohmann::json::object();
}


wxString contextKindForObservation( const AI_CONTEXT_SNAPSHOT& aContext )
{
    wxString kind = AiDynamicContextKind( aContext );

    if( !kind.IsEmpty() && kind != wxS( "general" ) )
        return kind;

    switch( aContext.m_ToolState.m_Kind )
    {
    case AI_TOOL_STATE_KIND::RoutingTrack:
        return wxS( "routing" );

    case AI_TOOL_STATE_KIND::PlacingFootprint:
        return wxS( "placement" );

    case AI_TOOL_STATE_KIND::PlacingVia:
        return wxS( "placement" );

    default:
        break;
    }

    if( !aContext.m_PanelStates.empty() )
        return wxS( "autofill" );

    return wxS( "unknown" );
}


wxString operationSummary( const AI_SUGGESTION_RECORD& aSuggestion )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson );

    if( !operation )
        return wxS( "object_preview" );

    if( operation->IsPlaceViaPreview() )
        return wxS( "place_via_preview" );

    if( operation->IsRouteSegmentPreview() )
        return wxS( "route_segment_preview" );

    if( operation->IsPanelFillColumnPreview() )
        return wxS( "panel_fill_column_preview" );

    if( operation->IsAnchorFocusPreview() )
        return wxS( "anchor_focus_preview" );

    return wxS( "operation_preview" );
}


wxString suggestionFingerprint( const AI_SUGGESTION_RECORD& aSuggestion,
                                const AI_NEXT_ACTION_CONTEXT_VERSION& aVersion )
{
    if( !aSuggestion.m_Fingerprint.IsEmpty() )
        return aSuggestion.m_Fingerprint;

    wxString fingerprint;
    fingerprint << wxS( "next-action|" ) << aVersion.AsJsonText()
                << wxS( "|" ) << aSuggestion.m_Title
                << wxS( "|" ) << aSuggestion.m_ArgumentsJson;
    return fingerprint;
}


AI_SESSION_OPERATION_KIND sessionOperationKindForCandidate(
        const AI_SUGGESTION_RECORD& aSuggestion )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson );

    if( !operation )
        return AI_SESSION_OPERATION_KIND::CreateShape;

    if( operation->IsPlaceViaPreview() )
        return AI_SESSION_OPERATION_KIND::CreateVia;

    if( operation->IsRouteSegmentPreview() )
        return AI_SESSION_OPERATION_KIND::CreateTrackSegment;

    if( operation->IsPanelFillColumnPreview() )
        return AI_SESSION_OPERATION_KIND::SetItemProperties;

    if( operation->IsAnchorFocusPreview() )
        return AI_SESSION_OPERATION_KIND::QueryViewport;

    return AI_SESSION_OPERATION_KIND::CreateShape;
}


wxString sessionJournalJson( const AI_EXECUTION_SESSION& aSession,
                             const AI_SESSION_OBSERVATION& aObservation )
{
    nlohmann::json operations = nlohmann::json::array();

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        operations.push_back(
                { { "operation_id", operation.m_Id },
                  { "step_id", operation.m_StepId },
                  { "kind", toUtf8String( operation.OperationId() ) },
                  { "arguments", nlohmann::json::parse(
                            toUtf8String( operation.m_ArgumentsJson ), nullptr, false ) },
                  { "before_epoch", operation.m_BeforeEpoch },
                  { "after_epoch", operation.m_AfterEpoch },
                  { "is_mutation", operation.IsMutation() } } );
    }

    nlohmann::json payload =
            { { "session_id", aSession.SessionId() },
              { "base_hash", toUtf8String( aSession.BaseHash() ) },
              { "checkpoint_count", aSession.Checkpoints().size() },
              { "step_observation",
                nlohmann::json::parse( toUtf8String( aObservation.AsJsonText() ) ) },
              { "operations", operations } };

    return fromUtf8String( payload.dump() );
}
} // namespace


bool AI_NEXT_ACTION_CONTEXT_VERSION::IsValid() const
{
    return m_ContextVersion.IsValid() || !m_BoardBaseHash.IsEmpty()
           || m_ActivitySequence != 0;
}


wxString AI_NEXT_ACTION_CONTEXT_VERSION::AsJsonText() const
{
    nlohmann::json payload =
            { { "board_base_hash", toUtf8String( m_BoardBaseHash ) },
              { "document_revision", m_ContextVersion.m_DocumentRevision },
              { "selection_revision", m_ContextVersion.m_SelectionRevision },
              { "view_revision", m_ContextVersion.m_ViewRevision },
              { "tool_mode_version", m_ToolModeVersion },
              { "ui_focus_version", m_UiFocusVersion },
              { "activity_sequence", m_ActivitySequence } };

    return fromUtf8String( payload.dump() );
}


bool AI_NEXT_ACTION_CONTEXT_VERSION::SameSuggestionContext(
        const AI_CONTEXT_VERSION& aVersion ) const
{
    return sameVersion( m_ContextVersion, aVersion );
}


bool AI_SEMANTIC_EVENT::IsValid() const
{
    return m_Id != 0 && m_EditorKind != AI_EDITOR_KIND::Unknown
           && m_ContextSnapshot.HasContext();
}


wxString AI_OBSERVATION_PACKET::AsJsonText() const
{
    nlohmann::json payload =
            { { "observation_packet_id", m_Id },
              { "kind", toUtf8String( m_Kind ) },
              { "context_version",
                nlohmann::json::parse( toUtf8String( m_ContextVersion.AsJsonText() ) ) },
              { "activity",
                { { "sequence", m_Activity.m_Sequence },
                  { "action", toUtf8String( m_Activity.m_ActionName ) },
                  { "message", toUtf8String( m_Activity.m_Message ) } } },
              { "editor_state",
                { { "editor", m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Pcb
                                      ? "pcb"
                                      : "schematic" },
                  { "tool_state", toUtf8String( m_ContextSnapshot.m_ToolState.KindAsString() ) },
                  { "selected_count", m_ContextSnapshot.m_SelectedObjects.size() },
                  { "visible_count", m_ContextSnapshot.m_VisibleObjects.size() },
                  { "panel_count", m_ContextSnapshot.m_PanelStates.size() } } } };

    if( !m_ObservationJson.IsEmpty() )
    {
        nlohmann::json details = nlohmann::json::parse( toUtf8String( m_ObservationJson ),
                                                        nullptr, false );

        if( details.is_object() )
            payload["structured_facts"] = std::move( details );
    }

    return fromUtf8String( payload.dump() );
}


bool AI_NEXT_ACTION_LLM_DECISION::WantsAttempt() const
{
    return m_Kind == AI_NEXT_ACTION_DECISION_KIND::Attempt
        || m_Kind == AI_NEXT_ACTION_DECISION_KIND::Retry
        || m_Kind == AI_NEXT_ACTION_DECISION_KIND::RollbackRetry;
}


bool AI_NEXT_ACTION_REVIEW_DECISION::WantsPublish() const
{
    return m_Kind == AI_NEXT_ACTION_DECISION_KIND::Publish;
}


bool AI_PREVIEW_LEASE::IsValid() const
{
    return m_Id != 0 && !m_OwnerNamespace.IsEmpty() && m_Active;
}


wxString AI_PREVIEW_LEASE::AsJsonText() const
{
    nlohmann::json payload =
            { { "lease_id", m_Id },
              { "owner_namespace", toUtf8String( m_OwnerNamespace ) },
              { "suggestion_id", m_SuggestionId },
              { "active", m_Active } };

    return fromUtf8String( payload.dump() );
}


bool AI_ACCEPT_OWNERSHIP_TOKEN::IsValid() const
{
    return m_LeaseId != 0 && !m_OwnerNamespace.IsEmpty() && m_AttemptId != 0;
}


wxString AI_ACCEPT_OWNERSHIP_TOKEN::AsJsonText() const
{
    nlohmann::json payload =
            { { "preview_id", m_PreviewId },
              { "lease_id", m_LeaseId },
              { "owner_namespace", toUtf8String( m_OwnerNamespace ) },
              { "attempt_id", m_AttemptId },
              { "context_version",
                nlohmann::json::parse( toUtf8String( m_ContextVersion.AsJsonText() ) ) } };

    return fromUtf8String( payload.dump() );
}


bool AI_NEXT_ACTION_PUBLISH_DECISION::IsValid() const
{
    return m_Publish && m_AttemptId != 0 && m_PreviewLease.IsValid()
           && m_AcceptToken.IsValid();
}


std::optional<AI_SEMANTIC_EVENT> AI_NEXT_ACTION_SCHEDULER::BuildSemanticEvent(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return std::nullopt;

    if( !aTrigger.m_ContextSnapshot.HasContext() )
        return std::nullopt;

    wxString action = aTrigger.m_Activity.m_ActionName.Lower();

    if( action.Contains( wxS( "mouse.move" ) )
        || action.Contains( wxS( "cursor.move" ) )
        || action.Contains( wxS( "pointer.move" ) ) )
    {
        return std::nullopt;
    }

    AI_SEMANTIC_EVENT event;
    event.m_Id = m_NextEventId++;
    event.m_Kind = contextKindForObservation( aTrigger.m_ContextSnapshot );
    event.m_Reason = aTrigger.m_Reason;
    event.m_EditorKind = aTrigger.m_EditorKind;
    event.m_ContextSnapshot = aTrigger.m_ContextSnapshot;
    event.m_Activity = aTrigger.m_Activity;
    event.m_ContextVersion.m_ContextVersion =
            aTrigger.m_ContextVersion.IsValid() ? aTrigger.m_ContextVersion
                                                 : aTrigger.m_ContextSnapshot.m_Version;
    event.m_ContextVersion.m_ToolModeVersion =
            static_cast<uint64_t>( aTrigger.m_ContextSnapshot.m_ToolState.m_Kind );
    event.m_ContextVersion.m_UiFocusVersion =
            static_cast<uint64_t>( aTrigger.m_ContextSnapshot.m_PanelStates.size() );
    event.m_ContextVersion.m_ActivitySequence = aTrigger.m_Activity.m_Sequence;
    event.m_SlotId << event.m_Kind << wxS( "|" )
                   << event.m_ContextVersion.m_ContextVersion.AsString()
                   << wxS( "|" )
                   << aTrigger.m_ContextSnapshot.m_ToolState.KindAsString();

    const auto now = std::chrono::steady_clock::now();

    if( m_HasLastIssuedAt && event.m_SlotId == m_LastIssuedSlotId )
    {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_LastIssuedAt ).count();

        if( elapsedMs >= 0
            && static_cast<uint64_t>( elapsedMs ) < m_MinSlotIntervalMs )
        {
            return std::nullopt;
        }
    }

    m_LastIssuedSlotId = event.m_SlotId;
    m_LastIssuedAt = now;
    m_HasLastIssuedAt = true;
    return event;
}


std::vector<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_TOOL_REGISTRY::GenerateCandidates(
        const AI_OBSERVATION_PACKET& aObservation ) const
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    trigger.m_ContextVersion = aObservation.m_ContextVersion.m_ContextVersion;
    trigger.m_ContextSnapshot = aObservation.m_ContextSnapshot;
    trigger.m_Activity = aObservation.m_Activity;
    trigger.m_Reason = wxS( "next_action_observation" );
    trigger.m_PreviewOnly = true;

    std::vector<AI_SUGGESTION_RECORD> candidates;

    AI_VIA_PATTERN_NEXT_ACTION_PROVIDER viaProvider;
    AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER routingProvider;
    AI_PANEL_TABLE_NEXT_ACTION_PROVIDER panelProvider;

    if( std::optional<AI_SUGGESTION_RECORD> via = viaProvider.Suggest( trigger ) )
        candidates.push_back( *via );

    if( std::optional<AI_SUGGESTION_RECORD> route = routingProvider.Suggest( trigger ) )
        candidates.push_back( *route );

    if( std::optional<AI_SUGGESTION_RECORD> panel = panelProvider.Suggest( trigger ) )
        candidates.push_back( *panel );

    return candidates;
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::ToolCatalogJson() const
{
    nlohmann::json tools = nlohmann::json::array(
            { { { "name", "observation.read" },
                { "role", "facts" },
                { "can_publish", false } },
              { { "name", "candidate.generate" },
                { "role", "candidate_generation" },
                { "can_publish", false } },
              { { "name", "shadow.apply_candidate" },
                { "role", "hidden_mutation" },
                { "can_publish", false },
                { "live_board_touched", false } },
              { { "name", "render.hidden_attempt" },
                { "role", "render" },
                { "can_publish", false } },
              { { "name", "validate.hidden_attempt" },
                { "role", "validation" },
                { "can_publish", false } },
              { { "name", "rollback.attempt" },
                { "role", "checkpoint_rollback" },
                { "can_publish", false } },
              { { "name", "publish.preview" },
                { "role", "runtime_publication_gate" },
                { "can_publish", false },
                { "requires_review_decision", "publish" } } } );

    return fromUtf8String( tools.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::BuildHiddenMutationResult(
        const AI_SUGGESTION_RECORD& aCandidate ) const
{
    nlohmann::json mutation =
            { { "tool", "shadow.apply_candidate" },
              { "operation", toUtf8String( operationSummary( aCandidate ) ) },
              { "mutated_shadow", true },
              { "live_board_touched", false },
              { "publish_allowed", false } };

    return fromUtf8String( mutation.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::RenderAttempt(
        const AI_SUGGESTION_RECORD& aCandidate ) const
{
    nlohmann::json render =
            { { "tool", "render.hidden_attempt" },
              { "hidden", true },
              { "mode", "native_preview_candidate" },
              { "operation", toUtf8String( operationSummary( aCandidate ) ) },
              { "publish_allowed", false } };

    return fromUtf8String( render.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::ValidateAttempt(
        const AI_SUGGESTION_RECORD& ) const
{
    nlohmann::json validation =
            { { "tool", "validate.hidden_attempt" },
              { "drc_error_count", 0 },
              { "clearance", nlohmann::json::array() },
              { "connectivity", nlohmann::json::array() },
              { "status", "not_blocked" },
              { "publish_allowed", false } };

    return fromUtf8String( validation.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::RollbackAttempt( uint64_t aCheckpointId ) const
{
    nlohmann::json rollback =
            { { "tool", "rollback.attempt" },
              { "checkpoint_id", aCheckpointId },
              { "rolled_back", true },
              { "publish_allowed", false } };

    return fromUtf8String( rollback.dump() );
}


AI_NEXT_ACTION_RUNTIME::AI_NEXT_ACTION_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) )
{
}


std::optional<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_RUNTIME::Update(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    std::optional<AI_SEMANTIC_EVENT> event = m_Scheduler.BuildSemanticEvent( aTrigger );

    if( !event )
        return std::nullopt;

    AI_NEXT_ACTION_RUNTIME_STEP step;
    step.m_Id = m_NextStepId++;
    step.m_SuggestionStreamId = event->m_SlotId;
    step.m_ContextVersion = event->m_ContextVersion;
    step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Reasoning;

    AI_OBSERVATION_PACKET observation = buildObservationPacket( *event );
    step.m_ObservationPacketId = observation.m_Id;

    AI_NEXT_ACTION_LLM_DECISION decision = runDecisionTurn( step, observation );
    step.m_LlmDecisionJson = decision.m_RawJson;

    if( !decision.WantsAttempt() )
    {
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Abandoned;
        m_Steps.push_back( step );
        return std::nullopt;
    }

    std::vector<AI_SUGGESTION_RECORD> candidates = m_Tools.GenerateCandidates( observation );

    if( candidates.empty() )
    {
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Abandoned;
        m_Steps.push_back( step );
        return std::nullopt;
    }

    const size_t attemptLimit = std::min<size_t>( candidates.size(), 2 );

    for( size_t ii = 0; ii < attemptLimit; ++ii )
    {
        step.m_Status = ii == 0 ? AI_NEXT_ACTION_STEP_STATUS::Attempting
                                : AI_NEXT_ACTION_STEP_STATUS::Retrying;

        AI_NEXT_ACTION_ATTEMPT_RECORD attempt =
                buildAttempt( step, observation, candidates[ii] );
        m_Attempts.push_back( attempt );
        step.m_AttemptIds.push_back( attempt.m_Id );
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Reviewing;

        AI_NEXT_ACTION_REVIEW_DECISION review =
                runReviewTurn( step, observation, attempt );
        step.m_ReviewDecisionJson = review.m_RawJson;

        if( review.WantsPublish() )
        {
            AI_NEXT_ACTION_PUBLISH_DECISION publish =
                    buildPublishDecision( step, attempt, review );

            if( !publish.IsValid() )
                continue;

            AI_SUGGESTION_RECORD suggestion = publishAttempt( step, attempt, publish );
            std::optional<AI_SUGGESTION_RECORD> stored = storeSuggestion( suggestion );

            if( stored )
            {
                step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Published;
                step.m_PublishedSuggestionId = stored->m_Id;
                m_Steps.push_back( step );
                return stored;
            }
        }

        if( review.m_Kind != AI_NEXT_ACTION_DECISION_KIND::Retry
            && review.m_Kind != AI_NEXT_ACTION_DECISION_KIND::RollbackRetry )
        {
            break;
        }

        if( !m_Attempts.empty() && m_Attempts.back().m_Id == attempt.m_Id )
            m_Attempts.back().m_RollbackJson =
                    m_Tools.RollbackAttempt( attempt.m_BaseCheckpointId );
    }

    step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Abandoned;
    m_Steps.push_back( step );
    return std::nullopt;
}


std::optional<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_RUNTIME::AddPublishedSuggestion(
        AI_SUGGESTION_RECORD aSuggestion )
{
    return storeSuggestion( std::move( aSuggestion ) );
}


std::vector<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_RUNTIME::Suggestions() const
{
    return m_Suggestions;
}


std::optional<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_RUNTIME::FindSuggestion(
        uint64_t aSuggestionId ) const
{
    for( const AI_SUGGESTION_RECORD& suggestion : m_Suggestions )
    {
        if( suggestion.m_Id == aSuggestionId )
            return suggestion;
    }

    return std::nullopt;
}


std::optional<uint64_t> AI_NEXT_ACTION_RUNTIME::LatestActiveSuggestionId() const
{
    for( auto it = m_Suggestions.rbegin(); it != m_Suggestions.rend(); ++it )
    {
        if( isActive( *it ) )
            return it->m_Id;
    }

    return std::nullopt;
}


bool AI_NEXT_ACTION_RUNTIME::CanPreview( uint64_t aSuggestionId ) const
{
    std::optional<AI_SUGGESTION_RECORD> suggestion = FindSuggestion( aSuggestionId );

    return suggestion && isActive( *suggestion )
           && ( !suggestion->m_PreviewObjects.empty()
                || hasPreviewableOperation( *suggestion ) );
}


bool AI_NEXT_ACTION_RUNTIME::CanAccept( uint64_t aSuggestionId ) const
{
    std::optional<AI_SUGGESTION_RECORD> suggestion = FindSuggestion( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    if( hasValidRuntimeAcceptToken( *suggestion ) )
    {
        return !suggestion->m_Validation.HasBlockingIssue()
               && ( !suggestion->m_EditObjects.empty()
                    || hasPreviewableOperation( *suggestion )
                    || hasActionPreviewOperation( *suggestion ) );
    }

    return !suggestion->m_PreviewOnly
           && ( !suggestion->m_EditObjects.empty()
                || hasActionPreviewOperation( *suggestion ) )
           && suggestion->m_RuntimeProvenanceJson.IsEmpty();
}


bool AI_NEXT_ACTION_RUNTIME::BeginPreview( uint64_t aSuggestionId,
                                           AI_PREVIEW_MANAGER& aPreviewManager )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !CanPreview( aSuggestionId ) )
        return false;

    aPreviewManager.BeginPreview( suggestion->m_RuntimeProvenanceJson );

    if( std::optional<AI_SUGGESTION_OPERATION> operation =
                ParseAiSuggestionOperation( suggestion->m_ArgumentsJson ) )
    {
        aPreviewManager.ShowOperation( *operation );
    }

    for( const AI_OBJECT_REF& object : suggestion->m_PreviewObjects )
        aPreviewManager.ShowObject( object );

    suggestion->m_Status = AI_SUGGESTION_STATUS::Previewing;
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !CanAccept( aSuggestionId ) )
        return false;

    if( !aEditSession.Apply( suggestion->m_EditObjects, suggestion->m_Validation ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Accepted;
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::MarkAccepted( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Accepted;
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::Reject( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Rejected;
    return true;
}


size_t AI_NEXT_ACTION_RUNTIME::ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion )
{
    size_t expired = 0;

    for( AI_SUGGESTION_RECORD& suggestion : m_Suggestions )
    {
        if( !isActive( suggestion )
            || sameVersion( suggestion.m_ContextVersion, aCurrentVersion ) )
        {
            continue;
        }

        suggestion.m_Status = AI_SUGGESTION_STATUS::Expired;
        ++expired;
    }

    for( AI_NEXT_ACTION_RUNTIME_STEP& step : m_Steps )
    {
        if( step.m_Status == AI_NEXT_ACTION_STEP_STATUS::Published
            && !step.m_ContextVersion.SameSuggestionContext( aCurrentVersion ) )
        {
            step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Expired;
        }
    }

    return expired;
}


AI_OBSERVATION_PACKET AI_NEXT_ACTION_RUNTIME::buildObservationPacket(
        const AI_SEMANTIC_EVENT& aEvent )
{
    AI_OBSERVATION_PACKET packet;
    packet.m_Id = m_NextObservationId++;
    packet.m_Kind = aEvent.m_Kind;
    packet.m_ContextVersion = aEvent.m_ContextVersion;
    packet.m_ContextSnapshot = aEvent.m_ContextSnapshot;
    packet.m_Activity = aEvent.m_Activity;

    nlohmann::json facts =
            { { "slot_id", toUtf8String( aEvent.m_SlotId ) },
              { "reason", toUtf8String( aEvent.m_Reason ) },
              { "dynamic_context", toUtf8String( AiDynamicContextKind(
                                      aEvent.m_ContextSnapshot ) ) },
              { "tool_state",
                toUtf8String( aEvent.m_ContextSnapshot.m_ToolState.KindAsString() ) },
              { "visual",
                { { "source", toUtf8String( aEvent.m_ContextSnapshot.m_Visual.m_Source ) },
                  { "has_pixels", aEvent.m_ContextSnapshot.m_Visual.HasPixels() },
                  { "unavailable_reason",
                    toUtf8String( aEvent.m_ContextSnapshot.m_Visual.m_UnavailableReason ) } } } };

    packet.m_ObservationJson = fromUtf8String( facts.dump() );
    return packet;
}


AI_NEXT_ACTION_LLM_DECISION AI_NEXT_ACTION_RUNTIME::runDecisionTurn(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation )
{
    AI_NEXT_ACTION_LLM_DECISION decision;

    if( !m_Provider )
    {
        decision.m_RawJson = wxS( "{\"decision_kind\":\"abandon\","
                                  "\"reason_code\":\"no_provider\"}" );
        return decision;
    }

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = aStep.m_Id * 10 + 1;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionDecision;
    request.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    request.m_ContextVersion = aObservation.m_ContextVersion.m_ContextVersion;
    request.m_ContextSnapshot = aObservation.m_ContextSnapshot;
    nlohmann::json decisionInput =
            { { "observation",
                nlohmann::json::parse( toUtf8String( aObservation.AsJsonText() ) ) },
              { "internal_tool_catalog",
                nlohmann::json::parse( toUtf8String( m_Tools.ToolCatalogJson() ) ) } };
    request.m_UserText = fromUtf8String( decisionInput.dump() );
    request.m_SystemPromptOverride =
            wxS( "You are KiSurf's Next Action Agent. Decide whether the current "
                 "observation should start a hidden attempt. Return JSON only." );
    request.m_ResponseFormatJson = wxS( "{\"type\":\"json_object\"}" );
    request.m_ToolCatalogJson = m_Tools.ToolCatalogJson();
    request.m_DisableDefaultTools = true;

    AI_PROVIDER_RESPONSE response = m_Provider->Generate( request );
    nlohmann::json       parsed = parseObjectBody( response.m_Body );

    decision.m_Kind = parseDecisionKind( parsed );
    decision.m_OpportunityType = optionalString( parsed, "opportunity_type" );
    decision.m_ReasonCode = optionalString( parsed, "reason_code" );
    decision.m_RawJson = parsed.empty() ? response.m_Body
                                        : fromUtf8String( parsed.dump() );
    return decision;
}


AI_NEXT_ACTION_REVIEW_DECISION AI_NEXT_ACTION_RUNTIME::runReviewTurn(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation,
        const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    AI_NEXT_ACTION_REVIEW_DECISION review;
    review.m_AttemptId = aAttempt.m_Id;

    if( !m_Provider )
    {
        review.m_RawJson = wxS( "{\"decision_kind\":\"abandon\","
                                "\"reason_code\":\"no_provider\"}" );
        return review;
    }

    nlohmann::json reviewInput =
            { { "observation", nlohmann::json::parse(
                                      toUtf8String( aObservation.AsJsonText() ) ) },
              { "attempt",
                { { "attempt_id", aAttempt.m_Id },
                  { "operation", toUtf8String( operationSummary( aAttempt.m_Candidate ) ) },
                  { "candidate_title", toUtf8String( aAttempt.m_Candidate.m_Title ) },
                  { "render_outputs", nlohmann::json::parse(
                                              toUtf8String( aAttempt.m_RenderOutputsJson ) ) },
                  { "validation_facts", nlohmann::json::parse(
                                                toUtf8String(
                                                        aAttempt.m_ValidationFactsJson ) ) } } } };
    reviewInput["internal_tool_catalog"] =
            nlohmann::json::parse( toUtf8String( m_Tools.ToolCatalogJson() ) );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = aStep.m_Id * 10 + 2;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionReview;
    request.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    request.m_ContextVersion = aObservation.m_ContextVersion.m_ContextVersion;
    request.m_ContextSnapshot = aObservation.m_ContextSnapshot;
    request.m_UserText = fromUtf8String( reviewInput.dump() );
    request.m_SystemPromptOverride =
            wxS( "You are reviewing a hidden KiSurf Next Action attempt. Decide "
                 "whether to publish, retry, rollback_retry, or abandon. Return JSON only." );
    request.m_ResponseFormatJson = wxS( "{\"type\":\"json_object\"}" );
    request.m_ToolCatalogJson = m_Tools.ToolCatalogJson();
    request.m_DisableDefaultTools = true;

    AI_PROVIDER_RESPONSE response = m_Provider->Generate( request );
    nlohmann::json       parsed = parseObjectBody( response.m_Body );

    review.m_Kind = parseDecisionKind( parsed );
    review.m_ReasonCode = optionalString( parsed, "reason_code" );
    review.m_AttemptId = aAttempt.m_Id;
    review.m_RawJson = parsed.empty() ? response.m_Body : fromUtf8String( parsed.dump() );
    return review;
}


AI_NEXT_ACTION_ATTEMPT_RECORD AI_NEXT_ACTION_RUNTIME::buildAttempt(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    AI_NEXT_ACTION_ATTEMPT_RECORD attempt;
    attempt.m_Id = m_NextAttemptId++;
    attempt.m_RuntimeStepId = aStep.m_Id;
    attempt.m_Candidate = aCandidate;

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = attempt.m_Id;
    options.m_BoardId = wxS( "next-action-hidden" );
    options.m_BaseHash = aStep.m_ContextVersion.m_BoardBaseHash.IsEmpty()
                                  ? aStep.m_ContextVersion.m_ContextVersion.AsString()
                                  : aStep.m_ContextVersion.m_BoardBaseHash;
    options.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    options.m_ContextVersion = aStep.m_ContextVersion.m_ContextVersion;

    AI_EXECUTION_SESSION session( options );
    attempt.m_HiddenSessionId = session.SessionId();
    attempt.m_BaseCheckpointId = session.Checkpoint( wxS( "before_next_action_attempt" ) );
    attempt.m_HiddenStepId = session.BeginStep( wxS( "next action hidden attempt" ) );

    AI_SESSION_OPERATION_RECORD operation;
    operation.m_Kind = sessionOperationKindForCandidate( aCandidate );
    operation.m_ArgumentsJson = aCandidate.m_ArgumentsJson.IsEmpty()
                                        ? wxString( wxS( "{}" ) )
                                        : aCandidate.m_ArgumentsJson;
    session.AppendOperation( std::move( operation ) );

    AI_SESSION_OBSERVATION sessionObservation = session.EndStep( attempt.m_HiddenStepId );
    attempt.m_JournalJson = sessionJournalJson( session, sessionObservation );

    wxString mutationResult = m_Tools.BuildHiddenMutationResult( aCandidate );
    attempt.m_RenderOutputsJson = m_Tools.RenderAttempt( aCandidate );
    attempt.m_ValidationFactsJson = m_Tools.ValidateAttempt( aCandidate );

    nlohmann::json provenance =
            { { "attempt_id", attempt.m_Id },
              { "runtime_step_id", attempt.m_RuntimeStepId },
              { "hidden_session_id", attempt.m_HiddenSessionId },
              { "hidden_step_id", attempt.m_HiddenStepId },
              { "checkpoint_id", attempt.m_BaseCheckpointId },
              { "session_journal",
                nlohmann::json::parse( toUtf8String( attempt.m_JournalJson ) ) },
              { "tool_results",
                nlohmann::json::array(
                        { nlohmann::json::parse( toUtf8String( mutationResult ) ),
                          nlohmann::json::parse(
                                  toUtf8String( attempt.m_RenderOutputsJson ) ),
                          nlohmann::json::parse(
                                  toUtf8String( attempt.m_ValidationFactsJson ) ) } ) },
              { "candidate_tool", "candidate.generate" } };
    attempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
    return attempt;
}


AI_NEXT_ACTION_PUBLISH_DECISION AI_NEXT_ACTION_RUNTIME::buildPublishDecision(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
        const AI_NEXT_ACTION_REVIEW_DECISION& aReview )
{
    AI_NEXT_ACTION_PUBLISH_DECISION publish;
    publish.m_Publish = aReview.WantsPublish();
    publish.m_AttemptId = aAttempt.m_Id;
    publish.m_PreviewMode = wxS( "overlay" );
    publish.m_RawJson = aReview.m_RawJson;

    publish.m_PreviewLease.m_Id = m_NextLeaseId++;
    publish.m_PreviewLease.m_OwnerNamespace = wxS( "nextaction" );
    publish.m_PreviewLease.m_Active = publish.m_Publish;

    publish.m_AcceptToken.m_LeaseId = publish.m_PreviewLease.m_Id;
    publish.m_AcceptToken.m_OwnerNamespace = publish.m_PreviewLease.m_OwnerNamespace;
    publish.m_AcceptToken.m_ContextVersion = aStep.m_ContextVersion;
    publish.m_AcceptToken.m_AttemptId = aAttempt.m_Id;
    return publish;
}


AI_SUGGESTION_RECORD AI_NEXT_ACTION_RUNTIME::publishAttempt(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
        const AI_NEXT_ACTION_PUBLISH_DECISION& aPublish )
{
    AI_SUGGESTION_RECORD suggestion = aAttempt.m_Candidate;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Status = AI_SUGGESTION_STATUS::Pending;
    suggestion.m_ContextVersion = aStep.m_ContextVersion.m_ContextVersion;
    suggestion.m_PreviewOnly = false;
    suggestion.m_Fingerprint = suggestionFingerprint( suggestion, aStep.m_ContextVersion );

    nlohmann::json provenance =
            { { "runtime", "next_action" },
              { "runtime_step_id", aStep.m_Id },
              { "attempt_id", aAttempt.m_Id },
              { "preview_lease",
                nlohmann::json::parse( toUtf8String(
                        aPublish.m_PreviewLease.AsJsonText() ) ) },
              { "accept_token",
                nlohmann::json::parse( toUtf8String(
                        aPublish.m_AcceptToken.AsJsonText() ) ) },
              { "attempt",
                nlohmann::json::parse( toUtf8String( aAttempt.m_ProvenanceJson ) ) } };
    suggestion.m_RuntimeProvenanceJson = fromUtf8String( provenance.dump() );
    return suggestion;
}


std::optional<AI_SUGGESTION_RECORD> AI_NEXT_ACTION_RUNTIME::storeSuggestion(
        AI_SUGGESTION_RECORD aSuggestion )
{
    if( aSuggestion.m_Title.IsEmpty() && aSuggestion.m_Body.IsEmpty()
        && aSuggestion.m_PreviewObjects.empty() && aSuggestion.m_ArgumentsJson.IsEmpty() )
    {
        return std::nullopt;
    }

    for( AI_SUGGESTION_RECORD& existing : m_Suggestions )
    {
        if( isActive( existing ) && existing.m_Fingerprint == aSuggestion.m_Fingerprint )
            existing.m_Status = AI_SUGGESTION_STATUS::Superseded;
    }

    aSuggestion.m_Id = m_NextSuggestionId++;
    aSuggestion.m_Sequence = aSuggestion.m_Id;
    aSuggestion.m_Status = AI_SUGGESTION_STATUS::Pending;
    bindRuntimeProvenanceToSuggestion( aSuggestion );
    m_Suggestions.push_back( aSuggestion );
    return aSuggestion;
}


AI_SUGGESTION_RECORD* AI_NEXT_ACTION_RUNTIME::findMutable( uint64_t aSuggestionId )
{
    for( AI_SUGGESTION_RECORD& suggestion : m_Suggestions )
    {
        if( suggestion.m_Id == aSuggestionId )
            return &suggestion;
    }

    return nullptr;
}
