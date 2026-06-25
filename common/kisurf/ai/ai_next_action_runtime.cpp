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

#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_next_action_candidate_library.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
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


wxString fnv1a64Fingerprint( const wxString& aText )
{
    const std::string text = toUtf8String( aText );
    uint64_t          hash = 14695981039346656037ULL;

    for( unsigned char ch : text )
    {
        hash ^= static_cast<uint64_t>( ch );
        hash *= 1099511628211ULL;
    }

    return wxString::Format( wxS( "fnv1a64:%016llx" ),
                             static_cast<unsigned long long>( hash ) );
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


bool textContainsBlockingStatus( const std::string& aText )
{
    std::string text = aText;
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c )
                    {
                        return static_cast<char>( std::tolower( c ) );
                    } );

    return text == "blocked"
        || text == "blocking"
        || text == "failed"
        || text == "validation_failed"
        || text == "render_failed"
        || text == "invalid"
        || text == "error"
        || text == "fatal"
        || text == "hard_error";
}


bool issueArrayHasBlockingSeverity( const nlohmann::json& aIssues )
{
    if( !aIssues.is_array() )
        return false;

    for( const nlohmann::json& issue : aIssues )
    {
        if( !issue.is_object() || !issue.contains( "severity" )
            || !issue["severity"].is_string() )
        {
            continue;
        }

        if( textContainsBlockingStatus( issue["severity"].get<std::string>() ) )
            return true;
    }

    return false;
}


bool validationObjectBlocksPublish( const nlohmann::json& aValidation )
{
    if( !aValidation.is_object() )
        return false;

    if( aValidation.contains( "status" ) && aValidation["status"].is_string()
        && textContainsBlockingStatus( aValidation["status"].get<std::string>() ) )
    {
        return true;
    }

    if( aValidation.contains( "issues" )
        && issueArrayHasBlockingSeverity( aValidation["issues"] ) )
    {
        return true;
    }

    return false;
}


bool validationFactsBlockPreviewPublish( const wxString& aValidationFactsJson )
{
    nlohmann::json facts =
            nlohmann::json::parse( toUtf8String( aValidationFactsJson ), nullptr, false );

    if( facts.is_discarded() || !facts.is_object() )
        return true;

    if( !facts.contains( "service_connected" )
        || !facts["service_connected"].is_boolean()
        || !facts["service_connected"].get<bool>() )
    {
        return true;
    }

    if( facts.contains( "status" ) && facts["status"].is_string()
        && textContainsBlockingStatus( facts["status"].get<std::string>() ) )
    {
        return true;
    }

    if( facts.contains( "error_code" ) && facts["error_code"].is_string()
        && !facts["error_code"].get<std::string>().empty() )
    {
        return true;
    }

    if( facts.contains( "service_result" ) && facts["service_result"].is_object()
        && facts["service_result"].contains( "validation" )
        && validationObjectBlocksPublish( facts["service_result"]["validation"] ) )
    {
        return true;
    }

    if( facts.contains( "validation" )
        && validationObjectBlocksPublish( facts["validation"] ) )
    {
        return true;
    }

    return false;
}


bool renderObjectBlocksPublish( const nlohmann::json& aRender,
                                bool aRequireServiceConnected )
{
    if( !aRender.is_object() )
        return true;

    if( aRequireServiceConnected
        && ( !aRender.contains( "service_connected" )
        || !aRender["service_connected"].is_boolean()
        || !aRender["service_connected"].get<bool>() ) )
    {
        return true;
    }

    if( aRender.contains( "render_valid" ) && aRender["render_valid"].is_boolean()
        && !aRender["render_valid"].get<bool>() )
    {
        return true;
    }

    if( aRender.contains( "status" ) && aRender["status"].is_string()
        && textContainsBlockingStatus( aRender["status"].get<std::string>() ) )
    {
        return true;
    }

    if( aRender.contains( "error_code" ) && aRender["error_code"].is_string()
        && !aRender["error_code"].get<std::string>().empty() )
    {
        return true;
    }

    return false;
}


bool renderFactsBlockPreviewPublish( const wxString& aRenderOutputsJson )
{
    nlohmann::json render =
            nlohmann::json::parse( toUtf8String( aRenderOutputsJson ), nullptr, false );

    if( render.is_discarded() || !render.is_object() )
        return true;

    if( renderObjectBlocksPublish( render, true ) )
        return true;

    if( render.contains( "service_result" )
        && renderObjectBlocksPublish( render["service_result"], false ) )
    {
        return true;
    }

    return false;
}


bool requiredBoolIsTrue( const nlohmann::json& aObject, const char* aKey )
{
    return aObject.contains( aKey ) && aObject[aKey].is_boolean()
           && aObject[aKey].get<bool>();
}


bool reviewBasisAllowsPreviewPublish( const wxString& aReviewJson )
{
    nlohmann::json review =
            nlohmann::json::parse( toUtf8String( aReviewJson ), nullptr, false );

    if( review.is_discarded() || !review.is_object()
        || !review.contains( "review_basis" )
        || !review["review_basis"].is_object() )
    {
        return false;
    }

    const nlohmann::json& basis = review["review_basis"];
    return requiredBoolIsTrue( basis, "render_valid" )
           && requiredBoolIsTrue( basis, "validation_passed" )
           && requiredBoolIsTrue( basis, "budget_within_limits" )
           && requiredBoolIsTrue( basis, "self_review_passed" );
}


void appendGateReason( AI_NEXT_ACTION_GATE_RESULT& aGate, const wxString& aReason )
{
    aGate.m_Reasons.push_back( aReason );
    aGate.m_Allowed = false;
}


wxString reviewJsonWithPreviewGateResult(
        const wxString& aReviewJson,
        const AI_NEXT_ACTION_GATE_RESULT& aGate )
{
    nlohmann::json review =
            nlohmann::json::parse( toUtf8String( aReviewJson ), nullptr, false );

    if( review.is_discarded() || !review.is_object() )
        review = nlohmann::json::object();

    review["preview_gate_result"] =
            nlohmann::json::parse( toUtf8String( aGate.AsJsonText() ) );
    return fromUtf8String( review.dump() );
}


std::string candidateSourceToolName( const AI_SUGGESTION_RECORD& aSuggestion );


std::string lowerAscii( std::string aText )
{
    std::transform( aText.begin(), aText.end(), aText.begin(),
                    []( unsigned char c )
                    {
                        return static_cast<char>( std::tolower( c ) );
                    } );
    return aText;
}


bool stringStartsWith( const std::string& aText, const char* aPrefix )
{
    const std::string prefix( aPrefix );
    return aText.size() >= prefix.size()
           && aText.compare( 0, prefix.size(), prefix ) == 0;
}


bool isBoundedPlanMutationTool( const std::string& aToolName )
{
    return aToolName == "script.run_bounded_plan"
           || aToolName == "repair.apply_bounded_plan";
}


bool isSurfaceRepairPatchTool( const std::string& aToolName )
{
    return aToolName == "surface.repair_patch";
}


bool isPlacementRepairViaTool( const std::string& aToolName )
{
    return aToolName == "placement.repair_via";
}


bool isPlacementRepairMoveItemsTool( const std::string& aToolName )
{
    return aToolName == "placement.repair_move_items";
}


bool isRoutingRepairSegmentTool( const std::string& aToolName )
{
    return aToolName == "routing.repair_segment";
}


bool isRoutingRepairPolylineTool( const std::string& aToolName )
{
    return aToolName == "routing.repair_polyline";
}


bool isRepairWrapperTool( const std::string& aToolName )
{
    return isSurfaceRepairPatchTool( aToolName )
           || isPlacementRepairViaTool( aToolName )
           || isPlacementRepairMoveItemsTool( aToolName )
           || isRoutingRepairSegmentTool( aToolName )
           || isRoutingRepairPolylineTool( aToolName );
}


bool isHiddenMutationBatchTool( const std::string& aToolName )
{
    return isBoundedPlanMutationTool( aToolName )
           || isRepairWrapperTool( aToolName );
}


bool jsonStringFieldEquals( const nlohmann::json& aObject, const char* aKey,
                            const wxString& aExpected )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_string() )
    {
        return true;
    }

    const std::string value = lowerAscii( aObject[aKey].get<std::string>() );
    const std::string expected = lowerAscii( toUtf8String( aExpected ) );

    return value.empty() || expected.empty() || value == expected;
}


bool jsonAnyStringFieldEquals( const nlohmann::json& aObject,
                               std::initializer_list<const char*> aKeys,
                               const wxString& aExpected )
{
    for( const char* key : aKeys )
    {
        if( !jsonStringFieldEquals( aObject, key, aExpected ) )
            return false;
    }

    return true;
}


bool decisionPcbTargetFieldsMatchCandidate(
        const nlohmann::json& aDecision,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aCandidate.m_ArgumentsJson );

    if( !operation )
        return true;

    if( !jsonAnyStringFieldEquals( aDecision,
                                   { "net", "target_net", "net_name" },
                                   operation->m_NetName )
        || !jsonAnyStringFieldEquals( aDecision,
                                      { "layer", "target_layer", "layer_name" },
                                      operation->m_LayerName ) )
    {
        return false;
    }

    if( aDecision.contains( "target_scope" )
        && aDecision["target_scope"].is_object() )
    {
        const nlohmann::json& scope = aDecision["target_scope"];

        if( !jsonAnyStringFieldEquals( scope,
                                       { "net", "target_net", "net_name" },
                                       operation->m_NetName )
            || !jsonAnyStringFieldEquals( scope,
                                          { "layer", "target_layer",
                                            "layer_name" },
                                          operation->m_LayerName ) )
        {
            return false;
        }
    }

    return true;
}


bool decisionSurfaceTargetScopeMatchesCandidate(
        const nlohmann::json& aDecision,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    if( !aDecision.is_object() || !aDecision.contains( "target_scope" )
        || !aDecision["target_scope"].is_object() )
    {
        return true;
    }

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aCandidate.m_ArgumentsJson );

    if( !operation || !operation->IsPanelFillColumnPreview() )
        return true;

    const nlohmann::json& scope = aDecision["target_scope"];

    return jsonStringFieldEquals( scope, "panel_id", operation->m_PanelId )
           && jsonStringFieldEquals( scope, "surface_id", operation->m_PanelId )
           && jsonStringFieldEquals( scope, "table_id", operation->m_TableId )
           && jsonStringFieldEquals( scope, "column", operation->m_ColumnId )
           && jsonStringFieldEquals( scope, "column_id", operation->m_ColumnId );
}


bool decisionOpportunityMatchesCandidate(
        const wxString& aDecisionJson,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    nlohmann::json decision =
            nlohmann::json::parse( toUtf8String( aDecisionJson ), nullptr, false );

    if( decision.is_discarded() || !decision.is_object() )
        return true;

    if( !decisionPcbTargetFieldsMatchCandidate( decision, aCandidate ) )
        return false;

    if( !decisionSurfaceTargetScopeMatchesCandidate( decision, aCandidate ) )
        return false;

    if( !decision.contains( "opportunity_type" )
        || !decision["opportunity_type"].is_string() )
    {
        return true;
    }

    const std::string opportunity =
            lowerAscii( decision["opportunity_type"].get<std::string>() );
    const std::string selectedTool = candidateSourceToolName( aCandidate );

    if( opportunity.empty() )
        return true;

    if( opportunity == "placement" || opportunity == "layout" )
        return stringStartsWith( selectedTool, "placement." );

    if( opportunity == "routing" || opportunity == "route" )
        return stringStartsWith( selectedTool, "routing." );

    if( opportunity == "autofill" || opportunity == "auto_fill"
        || opportunity == "refill" || opportunity == "panel"
        || opportunity == "surface" || opportunity == "structured_surface" )
    {
        return stringStartsWith( selectedTool, "surface." );
    }

    return true;
}


wxString dependencyFingerprint( const AI_NEXT_ACTION_CONTEXT_VERSION& aVersion )
{
    nlohmann::json fingerprint =
            { { "board_base_hash", toUtf8String( aVersion.m_BoardBaseHash ) },
              { "document_revision",
                aVersion.m_ContextVersion.m_DocumentRevision },
              { "selection_revision",
                aVersion.m_ContextVersion.m_SelectionRevision },
              { "view_revision", aVersion.m_ContextVersion.m_ViewRevision },
              { "tool_mode_version", aVersion.m_ToolModeVersion },
              { "ui_focus_version", aVersion.m_UiFocusVersion },
              { "activity_sequence", aVersion.m_ActivitySequence },
              { "viewport_fingerprint",
                toUtf8String( aVersion.m_ViewportFingerprint ) },
              { "cursor_region_fingerprint",
                toUtf8String( aVersion.m_CursorRegionFingerprint ) } };

    return fromUtf8String( fingerprint.dump() );
}


nlohmann::json parseObjectBody( const wxString& aBody );
nlohmann::json objectFromJsonText( const wxString& aJsonText );
wxString sessionJournalJson( const AI_EXECUTION_SESSION& aSession,
                             const AI_SESSION_OBSERVATION& aObservation );


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


bool isNextActionRuntimeSuggestion( const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    return provenance.is_object()
           && provenance.value( "runtime", std::string() ) == "next_action";
}


void deactivateRuntimePreviewLease( AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object() )
        return;

    if( provenance.contains( "preview_lease" )
        && provenance["preview_lease"].is_object() )
    {
        provenance["preview_lease"]["active"] = false;
        aSuggestion.m_RuntimeProvenanceJson = fromUtf8String( provenance.dump() );
    }
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

    if( token.value( "dependency_fingerprint", std::string() ).empty()
        || token.value( "touched_object_set_fingerprint", std::string() ).empty() )
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


bool runtimeAcceptTokenMatchesDependency(
        const AI_SUGGESTION_RECORD& aSuggestion,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object() || !provenance.contains( "accept_token" )
        || !provenance["accept_token"].is_object() )
    {
        return false;
    }

    const nlohmann::json& token = provenance["accept_token"];
    return token.value( "dependency_fingerprint", std::string() )
           == toUtf8String( dependencyFingerprint( aCurrentContextVersion ) );
}


bool validationAcceptGradeSufficient( const nlohmann::json& aValidationFacts )
{
    if( !aValidationFacts.is_object() )
        return true;

    if( aValidationFacts.contains( "accept_validation_sufficient" )
        && aValidationFacts["accept_validation_sufficient"].is_boolean() )
    {
        if( !aValidationFacts["accept_validation_sufficient"].get<bool>() )
            return false;

        return aValidationFacts.contains( "preview_state_exact" )
               && aValidationFacts["preview_state_exact"].is_boolean()
               && aValidationFacts["preview_state_exact"].get<bool>();
    }

    if( aValidationFacts.contains( "validation" )
        && !validationAcceptGradeSufficient( aValidationFacts["validation"] ) )
    {
        return false;
    }

    if( aValidationFacts.contains( "service_result" )
        && !validationAcceptGradeSufficient( aValidationFacts["service_result"] ) )
    {
        return false;
    }

    return true;
}


bool runtimeAttemptAcceptValidationSufficient(
        const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object()
        || provenance.value( "runtime", std::string() ) != "next_action" )
    {
        return true;
    }

    if( !provenance.contains( "attempt" )
        || !provenance["attempt"].is_object()
        || !provenance["attempt"].contains( "tool_results" )
        || !provenance["attempt"]["tool_results"].is_array() )
    {
        return true;
    }

    for( const nlohmann::json& result : provenance["attempt"]["tool_results"] )
    {
        if( !result.is_object()
            || result.value( "tool", std::string() ) != "validate.hidden_attempt" )
        {
            continue;
        }

        if( !validationAcceptGradeSufficient( result ) )
            return false;
    }

    return true;
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


std::optional<size_t> optionalSizeField( const nlohmann::json& aJson,
                                         const char* aKey )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) )
        return std::nullopt;

    const nlohmann::json& value = aJson[aKey];

    if( value.is_number_unsigned() )
        return value.get<size_t>();

    if( value.is_number_integer() )
    {
        const int64_t index = value.get<int64_t>();

        if( index >= 0 )
            return static_cast<size_t>( index );
    }

    return std::nullopt;
}


uint64_t unsignedField( const nlohmann::json& aJson, const char* aKey,
                        uint64_t aFallback = 0 )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) )
        return aFallback;

    const nlohmann::json& value = aJson[aKey];

    if( value.is_number_unsigned() )
        return value.get<uint64_t>();

    if( value.is_number_integer() )
    {
        const int64_t signedValue = value.get<int64_t>();

        if( signedValue >= 0 )
            return static_cast<uint64_t>( signedValue );
    }

    return aFallback;
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


void attachGateResultToSuggestion( AI_SUGGESTION_RECORD& aSuggestion,
                                   const wxString& aKey,
                                   const AI_NEXT_ACTION_GATE_RESULT& aGate )
{
    if( aSuggestion.m_RuntimeProvenanceJson.IsEmpty() || aKey.IsEmpty() )
        return;

    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object() )
        provenance = nlohmann::json::object();

    nlohmann::json gate =
            nlohmann::json::parse( toUtf8String( aGate.AsJsonText() ), nullptr,
                                   false );

    if( gate.is_discarded() || !gate.is_object() )
        gate = nlohmann::json::object();

    provenance[toUtf8String( aKey )] = std::move( gate );
    aSuggestion.m_RuntimeProvenanceJson = fromUtf8String( provenance.dump() );
}


AI_NEXT_ACTION_GATE_RESULT acceptGateResult( bool aAllowed,
                                             std::initializer_list<wxString> aReasons )
{
    AI_NEXT_ACTION_GATE_RESULT gate;
    gate.m_Gate = wxS( "accept" );
    gate.m_Allowed = aAllowed;

    for( const wxString& reason : aReasons )
        gate.m_Reasons.push_back( reason );

    return gate;
}


void attachAcceptGateResult( AI_SUGGESTION_RECORD& aSuggestion,
                             bool aAllowed,
                             std::initializer_list<wxString> aReasons )
{
    attachGateResultToSuggestion( aSuggestion, wxS( "accept_gate_result" ),
                                  acceptGateResult( aAllowed, aReasons ) );
}


nlohmann::json toolCallRecordJson( const AI_TOOL_CALL_RECORD& aToolCall )
{
    nlohmann::json record =
            { { "request_id", aToolCall.m_RequestId },
              { "tool_call_id", toUtf8String( aToolCall.m_ToolCallId ) },
              { "tool_name", toUtf8String( aToolCall.m_ToolName ) },
              { "arguments_json", toUtf8String( aToolCall.m_ArgumentsJson ) },
              { "allowed", aToolCall.m_Allowed },
              { "executed", aToolCall.m_Executed },
              { "error_code", toUtf8String( aToolCall.m_ErrorCode ) },
              { "message", toUtf8String( aToolCall.m_Message ) } };

    nlohmann::json result =
            nlohmann::json::parse( toUtf8String( aToolCall.m_ResultJson ), nullptr,
                                   false );

    if( result.is_discarded() )
        result = nlohmann::json::object();

    record["result"] = std::move( result );
    return record;
}


nlohmann::json toolCallRecordsJson(
        const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    nlohmann::json records = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
        records.push_back( toolCallRecordJson( toolCall ) );

    return records;
}


void attachProviderToolResults( nlohmann::json& aBody,
                                const AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aBody.is_object() || aResponse.m_ToolCalls.empty() )
        return;

    aBody["provider_tool_results"] =
            toolCallRecordsJson( aResponse.m_ToolCalls );
}


uint64_t maxHandleIdInJsonHandle( const nlohmann::json& aHandle,
                                  uint64_t aCurrentMax )
{
    if( !aHandle.is_object() || !aHandle.contains( "handle_id" ) )
        return aCurrentMax;

    const nlohmann::json& handleId = aHandle["handle_id"];

    if( handleId.is_number_unsigned() )
        return std::max( aCurrentMax, handleId.get<uint64_t>() );

    if( handleId.is_number_integer() && handleId.get<int64_t>() >= 0 )
        return std::max( aCurrentMax,
                         static_cast<uint64_t>( handleId.get<int64_t>() ) );

    return aCurrentMax;
}


uint64_t maxHandleIdInJsonHandleArray( const nlohmann::json& aHandles,
                                       uint64_t aCurrentMax )
{
    if( !aHandles.is_array() )
        return aCurrentMax;

    for( const nlohmann::json& handle : aHandles )
        aCurrentMax = maxHandleIdInJsonHandle( handle, aCurrentMax );

    return aCurrentMax;
}


uint64_t maxHandleIdInAttemptJournal( const nlohmann::json& aJournal )
{
    uint64_t maxHandleId = 0;

    if( aJournal.contains( "shadow_items" )
        && aJournal["shadow_items"].is_array() )
    {
        for( const nlohmann::json& item : aJournal["shadow_items"] )
        {
            if( item.is_object() && item.contains( "handle" ) )
                maxHandleId = maxHandleIdInJsonHandle( item["handle"], maxHandleId );
        }
    }

    if( aJournal.contains( "operations" ) && aJournal["operations"].is_array() )
    {
        for( const nlohmann::json& operation : aJournal["operations"] )
        {
            if( !operation.is_object() )
                continue;

            if( operation.contains( "created_handles" ) )
                maxHandleId = maxHandleIdInJsonHandleArray(
                        operation["created_handles"], maxHandleId );

            if( operation.contains( "resolved_handles" ) )
                maxHandleId = maxHandleIdInJsonHandleArray(
                        operation["resolved_handles"], maxHandleId );
        }
    }

    return maxHandleId;
}


std::string jsonHandleRemapKey( const nlohmann::json& aHandle )
{
    if( !aHandle.is_object() )
        return std::string();

    return std::to_string( aHandle.value( "session_id", 0ULL ) )
           + "|"
           + std::to_string( aHandle.value( "handle_id", 0ULL ) )
           + "|"
           + std::to_string( aHandle.value( "generation", 0ULL ) )
           + "|"
           + aHandle.value( "alias", std::string() );
}


nlohmann::json remapScriptJsonHandle(
        const nlohmann::json& aHandle,
        std::map<std::string, nlohmann::json>& aHandleRemap,
        uint64_t& aNextHandleId,
        uint64_t aTargetSessionId )
{
    if( !aHandle.is_object() )
        return aHandle;

    const std::string key = jsonHandleRemapKey( aHandle );

    if( key.empty() )
        return aHandle;

    auto it = aHandleRemap.find( key );

    if( it != aHandleRemap.end() )
        return it->second;

    nlohmann::json remapped = aHandle;
    remapped["session_id"] = aTargetSessionId;
    remapped["handle_id"] = aNextHandleId++;

    if( !remapped.contains( "generation" )
        || !remapped["generation"].is_number_unsigned()
        || remapped["generation"].get<uint64_t>() == 0 )
    {
        remapped["generation"] = 1;
    }

    aHandleRemap[key] = remapped;
    return remapped;
}


void remapScriptJsonHandleArray(
        nlohmann::json& aObject,
        const char* aField,
        std::map<std::string, nlohmann::json>& aHandleRemap,
        uint64_t& aNextHandleId,
        uint64_t aTargetSessionId )
{
    if( !aObject.is_object() || !aObject.contains( aField )
        || !aObject[aField].is_array() )
    {
        return;
    }

    for( nlohmann::json& handle : aObject[aField] )
    {
        handle = remapScriptJsonHandle( handle, aHandleRemap, aNextHandleId,
                                        aTargetSessionId );
    }
}


void remapScriptJournalHandles(
        nlohmann::json& aOperation,
        std::map<std::string, nlohmann::json>& aHandleRemap,
        uint64_t& aNextHandleId,
        uint64_t aTargetSessionId )
{
    remapScriptJsonHandleArray( aOperation, "created_handles", aHandleRemap,
                                aNextHandleId, aTargetSessionId );
    remapScriptJsonHandleArray( aOperation, "resolved_handles", aHandleRemap,
                                aNextHandleId, aTargetSessionId );
}


void syncAttemptBudgetCountersToProvenance(
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    nlohmann::json provenance = parseObjectBody( aAttempt.m_ProvenanceJson );

    if( !provenance.is_object() )
        provenance = nlohmann::json::object();

    provenance["budget_counters"] = nlohmann::json::parse(
            toUtf8String( aAttempt.m_BudgetCounters.AsJsonText() ), nullptr,
            false );

    if( provenance["budget_counters"].is_discarded() )
        provenance["budget_counters"] = nlohmann::json::object();

    aAttempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
}


std::string jsonSessionHandleUri( const nlohmann::json& aHandle )
{
    if( !aHandle.is_object() )
        return std::string();

    const uint64_t sessionId = unsignedField( aHandle, "session_id" );
    const uint64_t handleId = unsignedField( aHandle, "handle_id" );

    if( sessionId == 0 || handleId == 0 )
        return std::string();

    const uint64_t generation = unsignedField( aHandle, "generation", 1 );
    std::string uri = "ai://session/" + std::to_string( sessionId )
                      + "/handle/" + std::to_string( handleId )
                      + "/generation/" + std::to_string( generation == 0 ? 1
                                                                          : generation );

    if( aHandle.contains( "alias" ) && aHandle["alias"].is_string()
        && !aHandle["alias"].get<std::string>().empty() )
    {
        uri += "/alias/" + aHandle["alias"].get<std::string>();
    }

    return uri;
}


void collectJsonHandleUris( const nlohmann::json& aHandles,
                            std::set<std::string>& aTouchedObjects )
{
    if( !aHandles.is_array() )
        return;

    for( const nlohmann::json& handle : aHandles )
    {
        const std::string uri = jsonSessionHandleUri( handle );

        if( !uri.empty() )
            aTouchedObjects.insert( uri );
    }
}


void syncAttemptMutationCountersFromJournal(
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    nlohmann::json journal = parseObjectBody( aAttempt.m_JournalJson );

    if( !journal.is_object() || !journal.contains( "operations" )
        || !journal["operations"].is_array() )
    {
        return;
    }

    uint64_t              mutationCount = 0;
    uint64_t              createdObjectCount = 0;
    std::set<std::string> touchedObjects;

    for( const nlohmann::json& operation : journal["operations"] )
    {
        if( !operation.is_object() )
            continue;

        if( operation.value( "is_mutation", false ) )
            ++mutationCount;

        if( operation.contains( "created_handles" )
            && operation["created_handles"].is_array() )
        {
            createdObjectCount += operation["created_handles"].size();
            collectJsonHandleUris( operation["created_handles"], touchedObjects );
        }

        collectJsonHandleUris( operation.value( "resolved_handles",
                                                nlohmann::json::array() ),
                               touchedObjects );
    }

    nlohmann::json touched = nlohmann::json::array();

    for( const std::string& uri : touchedObjects )
        touched.push_back( uri );

    aAttempt.m_BudgetCounters.m_MutationCount = mutationCount;
    aAttempt.m_BudgetCounters.m_CreatedObjectCount = createdObjectCount;
    aAttempt.m_BudgetCounters.m_TouchedObjectCount = touchedObjects.size();
    aAttempt.m_BudgetCounters.m_TouchedObjectSetJson =
            fromUtf8String( touched.dump() );
}


nlohmann::json rollbackMergedToolBatch(
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
        AI_EXECUTION_SESSION* aAttemptSession,
        const nlohmann::json& aArgs )
{
    nlohmann::json attemptJournal = parseObjectBody( aAttempt.m_JournalJson );

    if( !attemptJournal.is_object() )
    {
        return { { "tool", "rollback.attempt" },
                 { "status", "missing_attempt_journal" },
                 { "rolled_back", false },
                 { "publish_allowed", false } };
    }

    nlohmann::json mergedToolBatches =
            attemptJournal.contains( "merged_tool_batches" )
            && attemptJournal["merged_tool_batches"].is_array()
                    ? attemptJournal["merged_tool_batches"]
                    : nlohmann::json::array();

    std::string targetToolCallId =
            aArgs.value( "tool_call_id", std::string() );
    nlohmann::json targetBatch = nlohmann::json::object();

    if( targetToolCallId.empty() && !mergedToolBatches.empty() )
    {
        for( auto it = mergedToolBatches.rbegin();
             it != mergedToolBatches.rend(); ++it )
        {
            if( it->is_object()
                && isHiddenMutationBatchTool(
                        it->value( "tool", std::string() ) )
                && it->contains( "tool_call_id" )
                && ( *it )["tool_call_id"].is_string() )
            {
                targetToolCallId = ( *it )["tool_call_id"].get<std::string>();
                targetBatch = *it;
                break;
            }
        }
    }

    for( const nlohmann::json& batch : mergedToolBatches )
    {
        if( !batch.is_object()
            || batch.value( "tool_call_id", std::string() )
                       != targetToolCallId )
        {
            continue;
        }

        targetBatch = batch;
        break;
    }

    const uint64_t checkpointId =
            unsignedField( targetBatch, "checkpoint_id",
                           unsignedField( aArgs, "checkpoint_id",
                                          aAttempt.m_BaseCheckpointId ) );

    if( targetToolCallId.empty() || targetBatch.empty() )
    {
        nlohmann::json payload =
                { { "tool", "rollback.attempt" },
                  { "status", "nothing_to_rollback" },
                  { "checkpoint_id", checkpointId },
                  { "rolled_back", false },
                  { "publish_allowed", false } };

        aAttempt.m_RollbackJson = fromUtf8String( payload.dump() );
        return payload;
    }

    if( aAttemptSession )
    {
        const bool rolledBack = aAttemptSession->RollbackTo( checkpointId );

        nlohmann::json keptBatches = nlohmann::json::array();

        for( const nlohmann::json& batch : mergedToolBatches )
        {
            if( batch.is_object()
                && batch.value( "tool_call_id", std::string() )
                           == targetToolCallId )
            {
                continue;
            }

            keptBatches.push_back( batch );
        }

        AI_SESSION_OBSERVATION observation;
        observation.m_Epoch = aAttemptSession->Epoch();
        observation.m_OperationCount =
                aAttemptSession->Journal().Operations().size();
        observation.m_Summary = rolledBack ? wxS( "rollback.attempt" )
                                           : wxS( "rollback.failed" );

        nlohmann::json frameJournal =
                objectFromJsonText( sessionJournalJson( *aAttemptSession,
                                                        observation ) );

        if( !keptBatches.empty() )
            frameJournal["merged_tool_batches"] = keptBatches;

        nlohmann::json rolledBackToolCallIds =
                attemptJournal.contains( "rolled_back_tool_call_ids" )
                && attemptJournal["rolled_back_tool_call_ids"].is_array()
                        ? attemptJournal["rolled_back_tool_call_ids"]
                        : nlohmann::json::array();
        bool alreadyTrackedRollback = false;

        for( const nlohmann::json& id : rolledBackToolCallIds )
        {
            if( id.is_string() && id.get<std::string>() == targetToolCallId )
            {
                alreadyTrackedRollback = true;
                break;
            }
        }

        if( !alreadyTrackedRollback )
            rolledBackToolCallIds.push_back( targetToolCallId );

        frameJournal["rolled_back_tool_call_ids"] =
                std::move( rolledBackToolCallIds );

        aAttempt.m_JournalJson = fromUtf8String( frameJournal.dump() );
        syncAttemptMutationCountersFromJournal( aAttempt );

        nlohmann::json payload =
                { { "tool", "rollback.attempt" },
                  { "status", rolledBack ? "rolled_back" : "rollback_failed" },
                  { "checkpoint_id", checkpointId },
                  { "rolled_back", rolledBack },
                  { "rollback_scope", "active_attempt_frame" },
                  { "rolled_back_tool_call_id", targetToolCallId },
                  { "active_attempt_frame", true },
                  { "attempt_session_journal", frameJournal },
                  { "publish_allowed", false } };

        aAttempt.m_RollbackJson = fromUtf8String( payload.dump() );

        nlohmann::json provenance = parseObjectBody( aAttempt.m_ProvenanceJson );

        if( !provenance.is_object() )
            provenance = nlohmann::json::object();

        provenance["session_journal"] = frameJournal;

        nlohmann::json rollbackHistory =
                provenance.contains( "rollback_history" )
                && provenance["rollback_history"].is_array()
                        ? provenance["rollback_history"]
                        : nlohmann::json::array();
        rollbackHistory.push_back( payload );
        provenance["rollback_history"] = std::move( rollbackHistory );
        provenance["last_rollback"] = payload;
        aAttempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
        syncAttemptBudgetCountersToProvenance( aAttempt );

        return payload;
    }

    size_t removedOperationCount = 0;
    size_t removedShadowItemCount = 0;

    if( attemptJournal.contains( "operations" )
        && attemptJournal["operations"].is_array() )
    {
        nlohmann::json keptOperations = nlohmann::json::array();

        for( const nlohmann::json& operation : attemptJournal["operations"] )
        {
            if( operation.is_object()
                && operation.value( "merged_from_tool_call_id",
                                    std::string() )
                           == targetToolCallId )
            {
                ++removedOperationCount;
                continue;
            }

            keptOperations.push_back( operation );
        }

        attemptJournal["operations"] = std::move( keptOperations );
    }

    if( attemptJournal.contains( "shadow_items" )
        && attemptJournal["shadow_items"].is_array() )
    {
        nlohmann::json keptShadowItems = nlohmann::json::array();

        for( const nlohmann::json& item : attemptJournal["shadow_items"] )
        {
            if( item.is_object()
                && item.value( "merged_from_tool_call_id",
                               std::string() )
                           == targetToolCallId )
            {
                ++removedShadowItemCount;
                continue;
            }

            keptShadowItems.push_back( item );
        }

        attemptJournal["shadow_items"] = std::move( keptShadowItems );
    }

    nlohmann::json keptBatches = nlohmann::json::array();

    for( const nlohmann::json& batch : mergedToolBatches )
    {
        if( batch.is_object()
            && batch.value( "tool_call_id", std::string() )
                       == targetToolCallId )
        {
            continue;
        }

        keptBatches.push_back( batch );
    }

    if( keptBatches.empty() )
        attemptJournal.erase( "merged_tool_batches" );
    else
        attemptJournal["merged_tool_batches"] = std::move( keptBatches );

    nlohmann::json rolledBackToolCallIds =
            attemptJournal.contains( "rolled_back_tool_call_ids" )
            && attemptJournal["rolled_back_tool_call_ids"].is_array()
                    ? attemptJournal["rolled_back_tool_call_ids"]
                    : nlohmann::json::array();
    bool alreadyTrackedRollback = false;

    for( const nlohmann::json& id : rolledBackToolCallIds )
    {
        if( id.is_string() && id.get<std::string>() == targetToolCallId )
        {
            alreadyTrackedRollback = true;
            break;
        }
    }

    if( !alreadyTrackedRollback )
        rolledBackToolCallIds.push_back( targetToolCallId );

    attemptJournal["rolled_back_tool_call_ids"] =
            std::move( rolledBackToolCallIds );

    aAttempt.m_JournalJson = fromUtf8String( attemptJournal.dump() );
    syncAttemptMutationCountersFromJournal( aAttempt );

    nlohmann::json payload =
            { { "tool", "rollback.attempt" },
              { "status", "rolled_back" },
              { "checkpoint_id", checkpointId },
              { "rolled_back", true },
              { "rollback_scope", "merged_tool_batch" },
              { "rolled_back_tool_call_id", targetToolCallId },
              { "removed_operation_count", removedOperationCount },
              { "removed_shadow_item_count", removedShadowItemCount },
              { "attempt_session_journal", attemptJournal },
              { "publish_allowed", false } };

    aAttempt.m_RollbackJson = fromUtf8String( payload.dump() );

    nlohmann::json provenance = parseObjectBody( aAttempt.m_ProvenanceJson );

    if( !provenance.is_object() )
        provenance = nlohmann::json::object();

    provenance["session_journal"] = attemptJournal;

    nlohmann::json rollbackHistory =
            provenance.contains( "rollback_history" )
            && provenance["rollback_history"].is_array()
                    ? provenance["rollback_history"]
                    : nlohmann::json::array();
    rollbackHistory.push_back( payload );
    provenance["rollback_history"] = std::move( rollbackHistory );
    provenance["last_rollback"] = payload;
    aAttempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
    syncAttemptBudgetCountersToProvenance( aAttempt );

    return payload;
}


void attachReviewProviderToolResultsToAttempt(
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
        const wxString& aReviewJson )
{
    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return;
    }

    nlohmann::json provenance = parseObjectBody( aAttempt.m_ProvenanceJson );

    if( !provenance.is_object() )
        provenance = nlohmann::json::object();

    nlohmann::json attemptJournal = parseObjectBody( aAttempt.m_JournalJson );
    bool           journalMerged = false;

    if( !attemptJournal.is_object() )
        attemptJournal = nlohmann::json::object();

    if( !attemptJournal.contains( "operations" )
        || !attemptJournal["operations"].is_array() )
    {
        attemptJournal["operations"] = nlohmann::json::array();
    }

    if( !attemptJournal.contains( "shadow_items" )
        || !attemptJournal["shadow_items"].is_array() )
    {
        attemptJournal["shadow_items"] = nlohmann::json::array();
    }

    uint64_t nextOperationId = 1;

    for( const nlohmann::json& operation : attemptJournal["operations"] )
    {
        if( operation.is_object() && operation.contains( "operation_id" )
            && operation["operation_id"].is_number_unsigned() )
        {
            nextOperationId = std::max( nextOperationId,
                                        operation["operation_id"].get<uint64_t>() + 1 );
        }
    }

    nlohmann::json mergedToolBatches =
            attemptJournal.contains( "merged_tool_batches" )
            && attemptJournal["merged_tool_batches"].is_array()
                    ? attemptJournal["merged_tool_batches"]
                    : nlohmann::json::array();
    std::set<std::string> mergedToolCallIds;
    std::set<std::string> rolledBackToolCallIds;
    uint64_t nextHandleId = maxHandleIdInAttemptJournal( attemptJournal ) + 1;

    for( const nlohmann::json& batch : mergedToolBatches )
    {
        if( batch.is_object() && batch.contains( "tool_call_id" )
            && batch["tool_call_id"].is_string() )
        {
            const std::string id = batch["tool_call_id"].get<std::string>();

            if( !id.empty() )
                mergedToolCallIds.insert( id );
        }
    }

    if( attemptJournal.contains( "rolled_back_tool_call_ids" )
        && attemptJournal["rolled_back_tool_call_ids"].is_array() )
    {
        for( const nlohmann::json& id : attemptJournal["rolled_back_tool_call_ids"] )
        {
            if( id.is_string() && !id.get<std::string>().empty() )
                rolledBackToolCallIds.insert( id.get<std::string>() );
        }
    }

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() || !toolRecord.contains( "result" )
            || !toolRecord["result"].is_object() )
        {
            continue;
        }

        const nlohmann::json& result = toolRecord["result"];

        const std::string resultTool = result.value( "tool", std::string() );

        if( !isHiddenMutationBatchTool( resultTool )
            || result.value( "status", std::string() ) != "script_plan_executed"
            || !result.contains( "session_journal" )
            || !result["session_journal"].is_object() )
        {
            continue;
        }

        const nlohmann::json& scriptJournal = result["session_journal"];
        const std::string toolCallId =
                toolRecord.value( "tool_call_id", std::string() );
        std::map<std::string, nlohmann::json> handleRemap;

        if( !toolCallId.empty() && mergedToolCallIds.count( toolCallId ) > 0 )
            continue;

        if( !toolCallId.empty() && rolledBackToolCallIds.count( toolCallId ) > 0 )
            continue;

        if( result.value( "active_attempt_frame", false ) )
        {
            attemptJournal = scriptJournal;

            if( !attemptJournal.contains( "operations" )
                || !attemptJournal["operations"].is_array() )
            {
                attemptJournal["operations"] = nlohmann::json::array();
            }

            if( !attemptJournal.contains( "shadow_items" )
                || !attemptJournal["shadow_items"].is_array() )
            {
                attemptJournal["shadow_items"] = nlohmann::json::array();
            }

            if( attemptJournal.contains( "merged_tool_batches" )
                && attemptJournal["merged_tool_batches"].is_array() )
            {
                mergedToolBatches = attemptJournal["merged_tool_batches"];
            }

            aAttempt.m_JournalJson = fromUtf8String( attemptJournal.dump() );
            syncAttemptMutationCountersFromJournal( aAttempt );
            provenance["session_journal"] = attemptJournal;
            journalMerged = false;

            if( !toolCallId.empty() )
                mergedToolCallIds.insert( toolCallId );

            continue;
        }

        size_t mergedOperationCount = 0;

        if( scriptJournal.contains( "operations" )
            && scriptJournal["operations"].is_array() )
        {
            for( const nlohmann::json& operation : scriptJournal["operations"] )
            {
                if( !operation.is_object() )
                    continue;

                nlohmann::json mergedOperation = operation;

                if( mergedOperation.contains( "operation_id" ) )
                    mergedOperation["source_operation_id"] = mergedOperation["operation_id"];

                if( mergedOperation.contains( "step_id" ) )
                    mergedOperation["source_step_id"] = mergedOperation["step_id"];

                remapScriptJournalHandles( mergedOperation, handleRemap,
                                           nextHandleId,
                                           aAttempt.m_HiddenSessionId );
                mergedOperation["operation_id"] = nextOperationId++;
                mergedOperation["merged_from_tool"] = resultTool;
                mergedOperation["merged_from_tool_call_id"] = toolCallId;
                attemptJournal["operations"].push_back( std::move( mergedOperation ) );
                ++mergedOperationCount;
                journalMerged = true;
            }
        }

        if( scriptJournal.contains( "shadow_items" )
            && scriptJournal["shadow_items"].is_array() )
        {
            for( const nlohmann::json& item : scriptJournal["shadow_items"] )
            {
                if( !item.is_object() )
                    continue;

                nlohmann::json mergedItem = item;
                if( mergedItem.contains( "handle" ) )
                {
                    mergedItem["handle"] = remapScriptJsonHandle(
                            mergedItem["handle"], handleRemap, nextHandleId,
                            aAttempt.m_HiddenSessionId );
                }
                mergedItem["merged_from_tool"] = resultTool;
                mergedItem["merged_from_tool_call_id"] = toolCallId;
                attemptJournal["shadow_items"].push_back( std::move( mergedItem ) );
                journalMerged = true;
            }
        }

        if( mergedOperationCount > 0 )
        {
            mergedToolBatches.push_back(
                    { { "tool", resultTool },
                      { "tool_call_id", toolCallId },
                      { "operation_count", mergedOperationCount },
                      { "script_step_id", result.value( "script_step_id", 0ULL ) },
                      { "checkpoint_id", result.value( "checkpoint_id", 0ULL ) } } );

            if( !toolCallId.empty() )
                mergedToolCallIds.insert( toolCallId );
        }
    }

    if( journalMerged )
    {
        attemptJournal["merged_tool_batches"] = std::move( mergedToolBatches );
        aAttempt.m_JournalJson = fromUtf8String( attemptJournal.dump() );
        syncAttemptMutationCountersFromJournal( aAttempt );
        provenance["session_journal"] = attemptJournal;
    }

    provenance["provider_tool_results"] = reviewTrace["provider_tool_results"];
    aAttempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
    syncAttemptBudgetCountersToProvenance( aAttempt );
}


std::optional<nlohmann::json> contextObjectField( const wxString& aJsonText,
                                                  const char* aField )
{
    nlohmann::json context = parseObjectBody( aJsonText );

    if( !context.is_object() || !context.contains( aField ) )
        return std::nullopt;

    const nlohmann::json& value = context[aField];

    if( value.is_object() || value.is_array() )
        return value;

    return std::nullopt;
}


std::optional<nlohmann::json> toolContextObjectField(
        const AI_TOOL_STATE_SNAPSHOT& aToolState, const char* aField )
{
    if( std::optional<nlohmann::json> field =
                contextObjectField( aToolState.m_ModeContextJson, aField ) )
    {
        return field;
    }

    return contextObjectField( aToolState.m_SharedContextJson, aField );
}


wxString jsonFingerprint( const nlohmann::json& aValue )
{
    if( aValue.is_discarded() || aValue.is_null() )
        return wxString();

    return fnv1a64Fingerprint( fromUtf8String( aValue.dump() ) );
}


wxString viewportFingerprintForSnapshot( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    if( std::optional<nlohmann::json> viewport =
                toolContextObjectField( aSnapshot.m_ToolState, "viewport" ) )
    {
        return jsonFingerprint( *viewport );
    }

    return wxString();
}


wxString cursorRegionFingerprintForSnapshot( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    if( std::optional<nlohmann::json> region =
                toolContextObjectField( aSnapshot.m_ToolState, "cursor_region" ) )
    {
        return jsonFingerprint( *region );
    }

    if( std::optional<nlohmann::json> cursor =
                toolContextObjectField( aSnapshot.m_ToolState, "cursor" ) )
    {
        return jsonFingerprint( { { "source", "mode_context_cursor" },
                                  { "cursor", *cursor } } );
    }

    if( aSnapshot.m_ToolState.m_HasCursorBoardPosition )
    {
        static constexpr int bucketSize = 100000;
        const VECTOR2I&      cursor = aSnapshot.m_ToolState.m_CursorBoardPosition;
        return jsonFingerprint( { { "source", "tool_state_cursor_bucket" },
                                  { "bucket_size", bucketSize },
                                  { "x_bucket", cursor.x / bucketSize },
                                  { "y_bucket", cursor.y / bucketSize } } );
    }

    return wxString();
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


struct ATTEMPT_BUDGET_POLICY
{
    size_t   m_MaxAttempts = 1;
    uint64_t m_MaxToolRounds = 3;
    uint64_t m_MaxMutations = 4;
    uint64_t m_MaxRenderCount = 1;
    uint64_t m_MaxValidationCount = 1;
    uint64_t m_MaxWallTimeMs = 250;
};


ATTEMPT_BUDGET_POLICY attemptPolicyForWorkState( const wxString& aWorkState )
{
    const wxString workState = aWorkState.Lower();

    if( workState == wxS( "routing" ) )
        return { 5, 12, 16, 6, 6, 250 };

    if( workState == wxS( "layout" ) || workState == wxS( "placement" ) )
        return { 3, 8, 8, 4, 4, 250 };

    if( workState == wxS( "autofill" ) || workState == wxS( "panel" )
        || workState == wxS( "structured_surface" ) )
    {
        return { 2, 6, 8, 3, 3, 250 };
    }

    return {};
}


size_t attemptLimitForWorkState( const wxString& aWorkState )
{
    return attemptPolicyForWorkState( aWorkState ).m_MaxAttempts;
}


nlohmann::json attemptPolicyJson( const wxString& aWorkState )
{
    const ATTEMPT_BUDGET_POLICY policy = attemptPolicyForWorkState( aWorkState );

    return { { "work_state", toUtf8String( aWorkState ) },
             { "max_attempts", policy.m_MaxAttempts },
             { "max_tool_rounds", policy.m_MaxToolRounds },
             { "max_mutations", policy.m_MaxMutations },
             { "max_render_count", policy.m_MaxRenderCount },
             { "max_validation_count", policy.m_MaxValidationCount },
             { "max_wall_time_ms", policy.m_MaxWallTimeMs },
             { "policy_owner", "native_runtime" } };
}


wxString budgetPolicyWorkStateForCandidate(
        const AI_SUGGESTION_RECORD& aCandidate )
{
    const std::string selectedTool = candidateSourceToolName( aCandidate );

    if( stringStartsWith( selectedTool, "routing." ) )
        return wxS( "routing" );

    if( stringStartsWith( selectedTool, "surface." ) )
        return wxS( "structured_surface" );

    if( stringStartsWith( selectedTool, "placement." ) )
        return wxS( "placement" );

    return wxS( "unknown" );
}


bool budgetCountersWithinPolicy(
        const AI_NEXT_ACTION_BUDGET_COUNTERS& aCounters,
        const ATTEMPT_BUDGET_POLICY& aPolicy )
{
    return aCounters.m_ToolRoundCount <= aPolicy.m_MaxToolRounds
           && aCounters.m_MutationCount <= aPolicy.m_MaxMutations
           && aCounters.m_RenderCount <= aPolicy.m_MaxRenderCount
           && aCounters.m_ValidationCount <= aPolicy.m_MaxValidationCount
           && aCounters.m_WallTimeMs <= aPolicy.m_MaxWallTimeMs;
}


bool attemptBudgetWithinPolicy( const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    return budgetCountersWithinPolicy(
            aAttempt.m_BudgetCounters,
            attemptPolicyForWorkState( budgetPolicyWorkStateForCandidate(
                    aAttempt.m_Candidate ) ) );
}


std::string placeableKindForToolState( AI_TOOL_STATE_KIND aToolState )
{
    switch( aToolState )
    {
    case AI_TOOL_STATE_KIND::PlacingVia:
        return "via";

    case AI_TOOL_STATE_KIND::PlacingFootprint:
        return "footprint";

    case AI_TOOL_STATE_KIND::DrawingZone:
        return "zone";

    default:
        return "unknown";
    }
}


std::string packetKindForContext( const AI_CONTEXT_SNAPSHOT& aContext,
                                  const wxString& aWorkState )
{
    switch( aContext.m_ToolState.m_Kind )
    {
    case AI_TOOL_STATE_KIND::RoutingTrack:
        return "routing";

    case AI_TOOL_STATE_KIND::PlacingVia:
    case AI_TOOL_STATE_KIND::PlacingFootprint:
    case AI_TOOL_STATE_KIND::DrawingZone:
        return "placement";

    default:
        break;
    }

    if( !aContext.m_PanelStates.empty() )
        return "structured_surface";

    return toUtf8String( aWorkState );
}


nlohmann::json cursorPositionJson( const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    if( !aToolState.m_HasCursorBoardPosition )
        return { { "known", false } };

    return { { "known", true },
             { "x", aToolState.m_CursorBoardPosition.x },
             { "y", aToolState.m_CursorBoardPosition.y } };
}


nlohmann::json anchorIdArrayJson( const std::vector<AI_CONTEXT_ANCHOR>& aAnchors )
{
    nlohmann::json ids = nlohmann::json::array();

    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( !anchor.m_Id.IsEmpty() )
            ids.push_back( toUtf8String( anchor.m_Id ) );
    }

    return ids;
}


nlohmann::json anchorPositionJson( const AI_CONTEXT_ANCHOR& aAnchor )
{
    if( !aAnchor.m_HasPosition )
        return { { "known", false } };

    return { { "known", true },
             { "x", aAnchor.m_Position.x },
             { "y", aAnchor.m_Position.y } };
}


bool isRoutingPacketAnchor( AI_CONTEXT_ANCHOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_CONTEXT_ANCHOR_KIND::RouteStart:
    case AI_CONTEXT_ANCHOR_KIND::RouteTarget:
    case AI_CONTEXT_ANCHOR_KIND::RouteCandidate:
    case AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout:
    case AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection:
        return true;

    default:
        return false;
    }
}


bool isPlacementPacketAnchor( AI_CONTEXT_ANCHOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_CONTEXT_ANCHOR_KIND::PlacementCandidate:
    case AI_CONTEXT_ANCHOR_KIND::PatternContinuation:
    case AI_CONTEXT_ANCHOR_KIND::ShapeCorner:
    case AI_CONTEXT_ANCHOR_KIND::ZoneVertex:
        return true;

    default:
        return false;
    }
}


nlohmann::json anchorRecordJson( const AI_CONTEXT_ANCHOR& aAnchor )
{
    nlohmann::json record =
            { { "id", toUtf8String( aAnchor.m_Id ) },
              { "kind", toUtf8String( aAnchor.KindAsString() ) },
              { "label", toUtf8String( aAnchor.m_Label ) },
              { "summary", toUtf8String( aAnchor.m_Summary ) },
              { "position", anchorPositionJson( aAnchor ) },
              { "layer", aAnchor.m_Layer },
              { "confidence", aAnchor.m_Confidence } };

    if( !aAnchor.m_DetailsJson.IsEmpty() )
    {
        nlohmann::json details = objectFromJsonText( aAnchor.m_DetailsJson );

        if( details.is_object() )
            record["details"] = std::move( details );
    }

    return record;
}


nlohmann::json anchorRecordsJson(
        const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
        bool ( *aPredicate )( AI_CONTEXT_ANCHOR_KIND ) )
{
    nlohmann::json anchors = nlohmann::json::array();

    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( !aPredicate( anchor.m_Kind ) )
            continue;

        anchors.push_back( anchorRecordJson( anchor ) );
    }

    return anchors;
}


nlohmann::json visibleObjectSummaryJson( const AI_OBJECT_REF& aObject )
{
    nlohmann::json summary =
            { { "label", toUtf8String( aObject.m_Label ) },
              { "type", static_cast<int>( aObject.m_Type ) },
              { "uuid", toUtf8String( aObject.m_Uuid.AsString() ) } };

    if( !aObject.m_DetailsJson.IsEmpty() )
    {
        nlohmann::json details = objectFromJsonText( aObject.m_DetailsJson );

        if( details.is_object() && !details.empty() )
            summary["details"] = std::move( details );
        else
            summary["details_json"] = toUtf8String( aObject.m_DetailsJson );
    }

    return summary;
}


nlohmann::json visibleObjectSummariesJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json summaries = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aObjects.size(), 16 );

    for( size_t ii = 0; ii < count; ++ii )
        summaries.push_back( visibleObjectSummaryJson( aObjects[ii] ) );

    return summaries;
}


void copyJsonFieldIfPresent( nlohmann::json& aTarget,
                             const nlohmann::json& aSource,
                             const char* aField )
{
    if( aSource.contains( aField ) && !aSource[aField].is_null() )
        aTarget[aField] = aSource[aField];
}


nlohmann::json objectDetailsJson( const AI_OBJECT_REF& aObject )
{
    if( aObject.m_DetailsJson.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json details = objectFromJsonText( aObject.m_DetailsJson );
    return details.is_object() ? details : nlohmann::json::object();
}


bool detailsKindEquals( const nlohmann::json& aDetails,
                        const char* aKind )
{
    return aDetails.contains( "kind" ) && aDetails["kind"].is_string()
           && aDetails["kind"].get<std::string>() == aKind;
}


nlohmann::json visibleObjectBaseFact( const AI_OBJECT_REF& aObject )
{
    return { { "source", "visible_object" },
             { "label", toUtf8String( aObject.m_Label ) },
             { "type", static_cast<int>( aObject.m_Type ) },
             { "uuid", toUtf8String( aObject.m_Uuid.AsString() ) } };
}


nlohmann::json placementObstacleFactJson( const AI_OBJECT_REF& aObject )
{
    const nlohmann::json details = objectDetailsJson( aObject );

    if( details.empty() )
        return nlohmann::json::object();

    if( aObject.m_Type == PCB_VIA_T || detailsKindEquals( details, "via" ) )
    {
        if( !details.contains( "position" ) )
            return nlohmann::json::object();

        nlohmann::json fact = visibleObjectBaseFact( aObject );
        fact["kind"] = "via_obstacle";
        copyJsonFieldIfPresent( fact, details, "position" );
        copyJsonFieldIfPresent( fact, details, "diameter" );
        copyJsonFieldIfPresent( fact, details, "drill" );
        copyJsonFieldIfPresent( fact, details, "net_name" );
        copyJsonFieldIfPresent( fact, details, "layer" );
        copyJsonFieldIfPresent( fact, details, "layer_pair" );
        return fact;
    }

    if( aObject.m_Type == PCB_PAD_T || detailsKindEquals( details, "pad" ) )
    {
        if( !details.contains( "position" ) && !details.contains( "bbox" ) )
            return nlohmann::json::object();

        nlohmann::json fact = visibleObjectBaseFact( aObject );
        fact["kind"] = "pad_obstacle";
        copyJsonFieldIfPresent( fact, details, "footprint" );
        copyJsonFieldIfPresent( fact, details, "pad_name" );
        copyJsonFieldIfPresent( fact, details, "position" );
        copyJsonFieldIfPresent( fact, details, "bbox" );
        copyJsonFieldIfPresent( fact, details, "net_name" );
        copyJsonFieldIfPresent( fact, details, "layer" );
        copyJsonFieldIfPresent( fact, details, "layer_set" );
        return fact;
    }

    if( aObject.m_Type == PCB_FOOTPRINT_T || detailsKindEquals( details, "footprint" ) )
    {
        if( !details.contains( "bbox" ) )
            return nlohmann::json::object();

        nlohmann::json fact = visibleObjectBaseFact( aObject );
        fact["kind"] = "footprint_obstacle";
        copyJsonFieldIfPresent( fact, details, "reference" );
        copyJsonFieldIfPresent( fact, details, "bbox" );
        copyJsonFieldIfPresent( fact, details, "courtyard_bbox" );
        copyJsonFieldIfPresent( fact, details, "layer" );
        copyJsonFieldIfPresent( fact, details, "side" );
        return fact;
    }

    return nlohmann::json::object();
}


nlohmann::json placementObstacleFactsJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json facts = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aObjects.size(), 32 );

    for( size_t ii = 0; ii < count; ++ii )
    {
        nlohmann::json fact = placementObstacleFactJson( aObjects[ii] );

        if( !fact.empty() )
            facts.push_back( std::move( fact ) );
    }

    return facts;
}


bool isKeepoutDetails( const nlohmann::json& aDetails )
{
    if( detailsKindEquals( aDetails, "keepout" ) )
        return true;

    if( aDetails.contains( "zone_kind" ) && aDetails["zone_kind"].is_string()
        && aDetails["zone_kind"].get<std::string>() == "keepout" )
    {
        return true;
    }

    if( aDetails.contains( "has_keepout" ) && aDetails["has_keepout"].is_boolean()
        && aDetails["has_keepout"].get<bool>() )
    {
        return true;
    }

    if( aDetails.contains( "rule" ) && aDetails["rule"].is_string()
        && aDetails["rule"].get<std::string>().find( "keepout" )
                   != std::string::npos )
    {
        return true;
    }

    if( aDetails.contains( "rule" ) && aDetails["rule"].is_string()
        && aDetails["rule"].get<std::string>() == "no_placement" )
    {
        return true;
    }

    return aDetails.contains( "keepout" ) && aDetails["keepout"].is_boolean()
           && aDetails["keepout"].get<bool>();
}


nlohmann::json placementKeepoutFactsJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json facts = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aObjects.size(), 32 );

    for( size_t ii = 0; ii < count; ++ii )
    {
        const AI_OBJECT_REF& object = aObjects[ii];
        const nlohmann::json details = objectDetailsJson( object );

        if( details.empty() || !isKeepoutDetails( details ) )
            continue;

        nlohmann::json fact = visibleObjectBaseFact( object );
        fact["kind"] = "keepout";
        copyJsonFieldIfPresent( fact, details, "rule" );
        copyJsonFieldIfPresent( fact, details, "zone_kind" );
        copyJsonFieldIfPresent( fact, details, "name" );
        copyJsonFieldIfPresent( fact, details, "bbox" );
        copyJsonFieldIfPresent( fact, details, "polygon" );
        copyJsonFieldIfPresent( fact, details, "layer" );
        copyJsonFieldIfPresent( fact, details, "layers" );
        copyJsonFieldIfPresent( fact, details, "layer_set" );
        copyJsonFieldIfPresent( fact, details, "has_keepout" );
        copyJsonFieldIfPresent( fact, details, "keepout" );
        facts.push_back( std::move( fact ) );
    }

    return facts;
}


nlohmann::json placementFootprintGeometryFactsJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json facts = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aObjects.size(), 32 );

    for( size_t ii = 0; ii < count; ++ii )
    {
        const AI_OBJECT_REF& object = aObjects[ii];
        const nlohmann::json details = objectDetailsJson( object );

        if( details.empty()
            || ( object.m_Type != PCB_FOOTPRINT_T
                 && !detailsKindEquals( details, "footprint" ) ) )
        {
            continue;
        }

        nlohmann::json fact = visibleObjectBaseFact( object );
        fact["kind"] = "footprint_geometry";
        copyJsonFieldIfPresent( fact, details, "reference" );
        copyJsonFieldIfPresent( fact, details, "bbox" );
        copyJsonFieldIfPresent( fact, details, "courtyard_bbox" );
        copyJsonFieldIfPresent( fact, details, "pads_bbox" );
        copyJsonFieldIfPresent( fact, details, "layer" );
        copyJsonFieldIfPresent( fact, details, "side" );
        facts.push_back( std::move( fact ) );
    }

    return facts;
}


struct LOCALITY_REGION
{
    int            m_Left = 0;
    int            m_Top = 0;
    int            m_Width = 0;
    int            m_Height = 0;
    nlohmann::json m_Record = nlohmann::json::object();
};


bool jsonNumberAsInt( const nlohmann::json& aObject, const char* aField, int& aValue )
{
    if( !aObject.contains( aField ) || !aObject[aField].is_number() )
        return false;

    aValue = aObject[aField].get<int>();
    return true;
}


nlohmann::json bboxRecordJson( int aLeft, int aTop, int aWidth, int aHeight )
{
    return { { "x", aLeft },
             { "y", aTop },
             { "width", aWidth },
             { "height", aHeight } };
}


bool appendPointFromField( std::vector<std::pair<int, int>>& aPoints,
                           const nlohmann::json& aDetails, const char* aField )
{
    if( !aDetails.contains( aField ) || !aDetails[aField].is_object() )
        return false;

    int x = 0;
    int y = 0;

    if( !jsonNumberAsInt( aDetails[aField], "x", x )
        || !jsonNumberAsInt( aDetails[aField], "y", y ) )
    {
        return false;
    }

    aPoints.emplace_back( x, y );
    return true;
}


std::optional<nlohmann::json> bboxFromPointFields(
        const nlohmann::json& aDetails, const std::vector<const char*>& aFields )
{
    std::vector<std::pair<int, int>> points;

    for( const char* field : aFields )
        appendPointFromField( points, aDetails, field );

    if( points.empty() )
        return std::nullopt;

    int minX = points.front().first;
    int maxX = points.front().first;
    int minY = points.front().second;
    int maxY = points.front().second;

    for( const std::pair<int, int>& point : points )
    {
        minX = std::min( minX, point.first );
        maxX = std::max( maxX, point.first );
        minY = std::min( minY, point.second );
        maxY = std::max( maxY, point.second );
    }

    return bboxRecordJson( minX, minY, maxX - minX, maxY - minY );
}


nlohmann::json routingObstacleFactJson( const AI_OBJECT_REF& aObject )
{
    const nlohmann::json details = objectDetailsJson( aObject );

    if( details.empty() )
        return nlohmann::json::object();

    if( aObject.m_Type != PCB_TRACE_T && aObject.m_Type != PCB_ARC_T
        && !detailsKindEquals( details, "track" )
        && !detailsKindEquals( details, "arc" ) )
    {
        return nlohmann::json::object();
    }

    const bool isArc = aObject.m_Type == PCB_ARC_T || detailsKindEquals( details, "arc" );
    std::optional<nlohmann::json> bbox =
            isArc ? bboxFromPointFields( details, { "start", "mid", "end" } )
                  : bboxFromPointFields( details, { "start", "end" } );

    if( !bbox )
        return nlohmann::json::object();

    nlohmann::json fact = visibleObjectBaseFact( aObject );
    fact["kind"] = isArc ? "routing_arc_obstacle" : "routing_track_obstacle";
    fact["bbox"] = *bbox;
    copyJsonFieldIfPresent( fact, details, "start" );
    copyJsonFieldIfPresent( fact, details, "mid" );
    copyJsonFieldIfPresent( fact, details, "end" );
    copyJsonFieldIfPresent( fact, details, "layer" );
    copyJsonFieldIfPresent( fact, details, "width" );
    copyJsonFieldIfPresent( fact, details, "net_code" );
    copyJsonFieldIfPresent( fact, details, "net_name" );
    return fact;
}


nlohmann::json routingObstacleFactsJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json facts = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aObjects.size(), 32 );

    for( size_t ii = 0; ii < count; ++ii )
    {
        nlohmann::json fact = routingObstacleFactJson( aObjects[ii] );

        if( !fact.empty() )
            facts.push_back( std::move( fact ) );
    }

    return facts;
}


std::optional<LOCALITY_REGION> localityRegionFromObject( const char* aSource,
                                                         const nlohmann::json& aObject )
{
    int centerX = 0;
    int centerY = 0;
    int width = 0;
    int height = 0;

    if( !jsonNumberAsInt( aObject, "width", width )
        || !jsonNumberAsInt( aObject, "height", height )
        || width <= 0 || height <= 0 )
    {
        return std::nullopt;
    }

    if( aObject.contains( "center" ) && aObject["center"].is_object() )
    {
        const nlohmann::json& center = aObject["center"];

        if( !jsonNumberAsInt( center, "x", centerX )
            || !jsonNumberAsInt( center, "y", centerY ) )
        {
            return std::nullopt;
        }
    }
    else
    {
        if( !jsonNumberAsInt( aObject, "x", centerX )
            || !jsonNumberAsInt( aObject, "y", centerY ) )
        {
            return std::nullopt;
        }
    }

    LOCALITY_REGION region;
    region.m_Left = centerX - width / 2;
    region.m_Top = centerY - height / 2;
    region.m_Width = width;
    region.m_Height = height;
    region.m_Record = { { "source", aSource },
                        { "bbox", bboxRecordJson( region.m_Left, region.m_Top,
                                                   region.m_Width, region.m_Height ) },
                        { "raw", aObject } };
    return region;
}


std::optional<LOCALITY_REGION> localityRegionForToolState(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    if( std::optional<nlohmann::json> region =
                toolContextObjectField( aToolState, "cursor_region" ) )
    {
        if( std::optional<LOCALITY_REGION> locality =
                    localityRegionFromObject( "cursor_region", *region ) )
        {
            return locality;
        }
    }

    if( std::optional<nlohmann::json> viewport =
                toolContextObjectField( aToolState, "viewport" ) )
    {
        if( std::optional<LOCALITY_REGION> locality =
                    localityRegionFromObject( "viewport", *viewport ) )
        {
            return locality;
        }
    }

    return std::nullopt;
}


bool pointIntersectsRegion( const nlohmann::json& aPoint,
                            const LOCALITY_REGION& aRegion )
{
    int x = 0;
    int y = 0;

    if( !jsonNumberAsInt( aPoint, "x", x )
        || !jsonNumberAsInt( aPoint, "y", y ) )
    {
        return false;
    }

    return x >= aRegion.m_Left && x < aRegion.m_Left + aRegion.m_Width
           && y >= aRegion.m_Top && y < aRegion.m_Top + aRegion.m_Height;
}


bool bboxIntersectsRegion( const nlohmann::json& aBbox,
                           const LOCALITY_REGION& aRegion )
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    if( !jsonNumberAsInt( aBbox, "x", x )
        || !jsonNumberAsInt( aBbox, "y", y )
        || !jsonNumberAsInt( aBbox, "width", width )
        || !jsonNumberAsInt( aBbox, "height", height ) )
    {
        return false;
    }

    return x < aRegion.m_Left + aRegion.m_Width
           && x + width > aRegion.m_Left
           && y < aRegion.m_Top + aRegion.m_Height
           && y + height > aRegion.m_Top;
}


bool factIntersectsRegion( const nlohmann::json& aFact,
                           const LOCALITY_REGION& aRegion )
{
    if( aFact.contains( "bbox" ) && aFact["bbox"].is_object()
        && bboxIntersectsRegion( aFact["bbox"], aRegion ) )
    {
        return true;
    }

    if( aFact.contains( "position" ) && aFact["position"].is_object()
        && pointIntersectsRegion( aFact["position"], aRegion ) )
    {
        return true;
    }

    return false;
}


nlohmann::json localObstacleFactsJson( const std::vector<AI_OBJECT_REF>& aObjects,
                                       const LOCALITY_REGION& aRegion )
{
    nlohmann::json facts = nlohmann::json::array();
    nlohmann::json candidates = placementObstacleFactsJson( aObjects );

    for( nlohmann::json keepout : placementKeepoutFactsJson( aObjects ) )
        candidates.push_back( std::move( keepout ) );

    for( nlohmann::json routingObstacle : routingObstacleFactsJson( aObjects ) )
        candidates.push_back( std::move( routingObstacle ) );

    for( nlohmann::json fact : candidates )
    {
        if( !factIntersectsRegion( fact, aRegion ) )
            continue;

        fact["locality_source"] = aRegion.m_Record["source"];
        facts.push_back( std::move( fact ) );
    }

    return facts;
}


const AI_PANEL_STATE_RECORD* focusedPanelState(
        const std::vector<AI_PANEL_STATE_RECORD>& aPanels )
{
    for( const AI_PANEL_STATE_RECORD& panel : aPanels )
    {
        if( !panel.m_FocusedControlId.IsEmpty()
            || !panel.m_FocusedControlLabel.IsEmpty()
            || !panel.m_SelectedText.IsEmpty() )
        {
            return &panel;
        }
    }

    return aPanels.empty() ? nullptr : &aPanels.front();
}


nlohmann::json panelSummaryArrayJson(
        const std::vector<AI_PANEL_STATE_RECORD>& aPanels )
{
    nlohmann::json panels = nlohmann::json::array();
    const size_t    count = std::min<size_t>( aPanels.size(), 8 );

    for( size_t ii = 0; ii < count; ++ii )
    {
        const AI_PANEL_STATE_RECORD& panel = aPanels[ii];
        panels.push_back( { { "id", toUtf8String( panel.m_Id ) },
                            { "title", toUtf8String( panel.m_Title ) },
                            { "focused_control_id",
                              toUtf8String( panel.m_FocusedControlId ) },
                            { "focused_control_label",
                              toUtf8String( panel.m_FocusedControlLabel ) },
                            { "summary", toUtf8String( panel.m_Summary ) } } );
    }

    return panels;
}


nlohmann::json panelStateObject( const AI_PANEL_STATE_RECORD& aPanel )
{
    if( aPanel.m_StateJson.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json state =
            nlohmann::json::parse( toUtf8String( aPanel.m_StateJson ), nullptr,
                                   false );

    return state.is_object() ? state : nlohmann::json::object();
}


nlohmann::json normalizedFieldJson( const nlohmann::json& aColumn )
{
    if( aColumn.is_string() )
    {
        const std::string id = aColumn.get<std::string>();
        return { { "id", id },
                 { "label", id },
                 { "kind", "column" },
                 { "value_type", "unknown" } };
    }

    if( !aColumn.is_object() )
        return nlohmann::json::object();

    nlohmann::json field =
            { { "kind", "column" }, { "value_type", "unknown" } };

    if( aColumn.contains( "id" ) && aColumn["id"].is_string() )
        field["id"] = aColumn["id"];
    else if( aColumn.contains( "name" ) && aColumn["name"].is_string() )
        field["id"] = aColumn["name"];

    if( !field.contains( "id" ) )
        return nlohmann::json::object();

    if( aColumn.contains( "label" ) && aColumn["label"].is_string() )
        field["label"] = aColumn["label"];
    else
        field["label"] = field["id"];

    copyJsonFieldIfPresent( field, aColumn, "value_type" );
    copyJsonFieldIfPresent( field, aColumn, "type" );
    copyJsonFieldIfPresent( field, aColumn, "required" );
    copyJsonFieldIfPresent( field, aColumn, "read_only" );

    return field;
}


nlohmann::json normalizedFieldsJson( const nlohmann::json& aColumns )
{
    nlohmann::json fields = nlohmann::json::array();

    if( !aColumns.is_array() )
        return fields;

    for( const nlohmann::json& column : aColumns )
    {
        nlohmann::json field = normalizedFieldJson( column );

        if( !field.empty() )
            fields.push_back( std::move( field ) );
    }

    return fields;
}


nlohmann::json normalizedTablesJson( const nlohmann::json& aTables )
{
    nlohmann::json tables = nlohmann::json::array();

    if( !aTables.is_array() )
        return tables;

    for( const nlohmann::json& table : aTables )
    {
        if( !table.is_object() )
            continue;

        nlohmann::json normalized = nlohmann::json::object();
        copyJsonFieldIfPresent( normalized, table, "id" );
        copyJsonFieldIfPresent( normalized, table, "title" );

        if( table.contains( "columns" ) )
            normalized["fields"] = normalizedFieldsJson( table["columns"] );

        if( table.contains( "rows" ) && table["rows"].is_array() )
            normalized["row_count"] = table["rows"].size();

        if( !normalized.empty() )
            tables.push_back( std::move( normalized ) );
    }

    return tables;
}


nlohmann::json normalizedSurfaceSchemaJson(
        const AI_PANEL_STATE_RECORD& aPanel,
        const nlohmann::json& aState )
{
    nlohmann::json schema =
            { { "surface_id", toUtf8String( aPanel.m_Id ) },
              { "surface_title", toUtf8String( aPanel.m_Title ) },
              { "focused_control_id", toUtf8String( aPanel.m_FocusedControlId ) } };

    copyJsonFieldIfPresent( schema, aState, "schema_version" );
    copyJsonFieldIfPresent( schema, aState, "row_count" );
    copyJsonFieldIfPresent( schema, aState, "target_scope" );

    if( aState.contains( "schema" ) && aState["schema"].is_object() )
    {
        const nlohmann::json& rawSchema = aState["schema"];

        if( rawSchema.contains( "fields" ) )
            schema["fields"] = normalizedFieldsJson( rawSchema["fields"] );

        copyJsonFieldIfPresent( schema, rawSchema, "version" );
        copyJsonFieldIfPresent( schema, rawSchema, "surface_kind" );
    }

    if( !schema.contains( "fields" ) && aState.contains( "columns" ) )
        schema["fields"] = normalizedFieldsJson( aState["columns"] );

    if( aState.contains( "tables" ) )
    {
        nlohmann::json tables = normalizedTablesJson( aState["tables"] );

        if( !tables.empty() )
            schema["tables"] = std::move( tables );
    }

    if( !schema.contains( "fields" ) )
        schema["fields"] = nlohmann::json::array();

    return schema;
}


nlohmann::json fieldOriginFactJson( const AI_PANEL_STATE_RECORD& aPanel,
                                    const nlohmann::json& aState,
                                    const std::string& aFieldId,
                                    const nlohmann::json& aOrigin )
{
    nlohmann::json fact =
            { { "source", "value_provenance" },
              { "surface_id", toUtf8String( aPanel.m_Id ) },
              { "field_id", aFieldId } };

    if( aOrigin.is_string() )
    {
        fact["origin"] = aOrigin;
    }
    else if( aOrigin.is_object() )
    {
        copyJsonFieldIfPresent( fact, aOrigin, "origin" );
        copyJsonFieldIfPresent( fact, aOrigin, "source" );
        copyJsonFieldIfPresent( fact, aOrigin, "confidence" );
        copyJsonFieldIfPresent( fact, aOrigin, "reason" );
    }
    else
    {
        fact["origin"] = "unknown";
    }

    if( aState.contains( "target_scope" ) && aState["target_scope"].is_object() )
    {
        copyJsonFieldIfPresent( fact, aState["target_scope"], "table_id" );
        copyJsonFieldIfPresent( fact, aState["target_scope"], "row" );
        copyJsonFieldIfPresent( fact, aState["target_scope"], "row_id" );
        copyJsonFieldIfPresent( fact, aState["target_scope"], "column" );
    }

    return fact;
}


nlohmann::json fieldOriginFactsJson( const AI_PANEL_STATE_RECORD& aPanel,
                                     const nlohmann::json& aState )
{
    nlohmann::json facts = nlohmann::json::array();

    if( !aState.contains( "value_provenance" ) )
        return facts;

    const nlohmann::json& provenance = aState["value_provenance"];

    if( provenance.is_object() )
    {
        for( auto it = provenance.begin(); it != provenance.end(); ++it )
            facts.push_back( fieldOriginFactJson( aPanel, aState, it.key(), it.value() ) );
    }
    else if( provenance.is_array() )
    {
        for( const nlohmann::json& entry : provenance )
        {
            if( !entry.is_object() || !entry.contains( "field_id" )
                || !entry["field_id"].is_string() )
            {
                continue;
            }

            facts.push_back( fieldOriginFactJson( aPanel, aState,
                                                  entry["field_id"].get<std::string>(),
                                                  entry ) );
        }
    }

    return facts;
}


void copySurfaceStateField( nlohmann::json& aPacket,
                            const nlohmann::json& aState,
                            const char* aField )
{
    if( aState.contains( aField ) && !aState[aField].is_null() )
        aPacket[aField] = aState[aField];
}


nlohmann::json placementPlanningTargetJson(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    return { { "kind", "place_current_item" },
             { "placeable_kind", placeableKindForToolState( aToolState.m_Kind ) },
             { "position", cursorPositionJson( aToolState ) },
             { "position_source", "cursor_attached_item" },
             { "click_required_to_materialize", true },
             { "manual_click_supersedes_attempt", true } };
}


nlohmann::json routingPlanningTargetJson(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    nlohmann::json modeContext =
            objectFromJsonText( aToolState.m_ModeContextJson );
    nlohmann::json target =
            { { "kind", "next_landing_from_current_route_head" },
              { "route_head_source", "mode_context.start" },
              { "cursor", cursorPositionJson( aToolState ) },
              { "cursor_is_sorting_hint", true },
              { "manual_click_supersedes_attempt", true } };

    if( modeContext.contains( "start" ) && modeContext["start"].is_object() )
        target["route_head"] = modeContext["start"];

    if( modeContext.contains( "net" ) && modeContext["net"].is_string() )
        target["net"] = modeContext["net"];

    if( modeContext.contains( "layer" ) && modeContext["layer"].is_string() )
        target["layer"] = modeContext["layer"];

    if( modeContext.contains( "width" ) && modeContext["width"].is_number() )
        target["width"] = modeContext["width"];

    return target;
}


nlohmann::json routingActiveSegmentJson(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    nlohmann::json modeContext =
            objectFromJsonText( aToolState.m_ModeContextJson );
    nlohmann::json segment =
            { { "available", false },
              { "source", "mode_context.start_to_cursor" },
              { "cursor_is_sorting_hint", true } };

    if( modeContext.contains( "start" ) && modeContext["start"].is_object()
        && modeContext.contains( "cursor" ) && modeContext["cursor"].is_object() )
    {
        segment["available"] = true;
        segment["start"] = modeContext["start"];
        segment["end"] = modeContext["cursor"];
        segment["start_source"] = "mode_context.start";
        segment["end_source"] = "mode_context.cursor";
    }

    if( modeContext.contains( "net" ) && modeContext["net"].is_string() )
        segment["net"] = modeContext["net"];

    if( modeContext.contains( "layer" ) && modeContext["layer"].is_string() )
        segment["layer"] = modeContext["layer"];

    if( modeContext.contains( "width" ) && modeContext["width"].is_number() )
        segment["width"] = modeContext["width"];

    return segment;
}


nlohmann::json workStatePacketJson( const AI_SEMANTIC_EVENT& aEvent )
{
    const AI_CONTEXT_SNAPSHOT& context = aEvent.m_ContextSnapshot;
    const std::string          packetKind =
            packetKindForContext( context, aEvent.m_Kind );

    nlohmann::json packet =
            { { "packet_kind", packetKind },
              { "work_state", toUtf8String( aEvent.m_Kind ) },
              { "tool_state", toUtf8String( context.m_ToolState.KindAsString() ) },
              { "cursor_board_position",
                cursorPositionJson( context.m_ToolState ) },
              { "visible_object_count", context.m_VisibleObjects.size() },
              { "visible_object_summaries",
                visibleObjectSummariesJson( context.m_VisibleObjects ) },
              { "selected_object_count", context.m_SelectedObjects.size() },
              { "anchor_count", context.m_Anchors.size() } };

    if( std::optional<LOCALITY_REGION> locality =
                localityRegionForToolState( context.m_ToolState ) )
    {
        packet["locality_region"] = locality->m_Record;
        packet["local_obstacle_facts"] =
                localObstacleFactsJson( context.m_VisibleObjects, *locality );
    }

    if( packetKind == "routing" )
    {
        packet["routing_active"] =
                context.m_ToolState.m_Kind == AI_TOOL_STATE_KIND::RoutingTrack;
        packet["planning_target"] =
                routingPlanningTargetJson( context.m_ToolState );
        packet["active_route_segment"] =
                routingActiveSegmentJson( context.m_ToolState );
        packet["route_anchor_ids"] = anchorIdArrayJson( context.m_Anchors );
        packet["route_anchors"] =
                anchorRecordsJson( context.m_Anchors, isRoutingPacketAnchor );

        if( !context.m_ToolState.m_ModeContextJson.IsEmpty() )
            packet["mode_context_json"] =
                    toUtf8String( context.m_ToolState.m_ModeContextJson );
    }
    else if( packetKind == "placement" )
    {
        packet["placement_active"] =
                context.m_ToolState.m_Kind == AI_TOOL_STATE_KIND::PlacingVia
                || context.m_ToolState.m_Kind == AI_TOOL_STATE_KIND::PlacingFootprint
                || context.m_ToolState.m_Kind == AI_TOOL_STATE_KIND::DrawingZone;
        packet["placeable_kind"] =
                placeableKindForToolState( context.m_ToolState.m_Kind );
        packet["planning_target"] =
                placementPlanningTargetJson( context.m_ToolState );
        packet["placement_anchor_ids"] = anchorIdArrayJson( context.m_Anchors );
        packet["placement_anchors"] =
                anchorRecordsJson( context.m_Anchors, isPlacementPacketAnchor );
        packet["placement_obstacle_facts"] =
                placementObstacleFactsJson( context.m_VisibleObjects );
        packet["placement_keepout_facts"] =
                placementKeepoutFactsJson( context.m_VisibleObjects );
        packet["placement_footprint_geometry_facts"] =
                placementFootprintGeometryFactsJson( context.m_VisibleObjects );
    }
    else if( packetKind == "structured_surface" )
    {
        packet["surface_count"] = context.m_PanelStates.size();
        packet["surface_summaries"] = panelSummaryArrayJson( context.m_PanelStates );

        if( const AI_PANEL_STATE_RECORD* panel =
                    focusedPanelState( context.m_PanelStates ) )
        {
            packet["focused_surface_id"] = toUtf8String( panel->m_Id );
            packet["focused_surface_title"] = toUtf8String( panel->m_Title );
            packet["focused_control_id"] = toUtf8String( panel->m_FocusedControlId );
            packet["focused_control_label"] =
                    toUtf8String( panel->m_FocusedControlLabel );

            const nlohmann::json state = panelStateObject( *panel );
            copySurfaceStateField( packet, state, "schema_version" );
            copySurfaceStateField( packet, state, "schema" );
            copySurfaceStateField( packet, state, "target_scope" );
            copySurfaceStateField( packet, state, "neighbor_values" );
            copySurfaceStateField( packet, state, "value_provenance" );
            copySurfaceStateField( packet, state, "validation_state" );
            packet["normalized_schema"] =
                    normalizedSurfaceSchemaJson( *panel, state );
            packet["field_origin_facts"] =
                    fieldOriginFactsJson( *panel, state );
        }
    }

    return packet;
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


std::string candidateSourceToolName( const AI_SUGGESTION_RECORD& aSuggestion )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson );

    if( !operation )
        return "placement.generate_shape_candidates";

    if( operation->IsPlaceViaPreview() )
        return "placement.generate_via_pattern_candidates";

    if( operation->IsRouteSegmentPreview() )
        return "routing.generate_segment_candidates";

    if( operation->IsPanelFillColumnPreview() )
        return "surface.generate_fill_candidates";

    if( operation->IsAnchorFocusPreview() )
        return "observation.generate_anchor_focus_candidates";

    return "placement.generate_shape_candidates";
}


nlohmann::json candidateGenerationTraceJson(
        const AI_OBSERVATION_PACKET& aObservation,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    const std::string selectedTool = candidateSourceToolName( aCandidate );
    nlohmann::json    skipped = nlohmann::json::array();

    for( const std::string& tool : {
                "placement.generate_via_pattern_candidates",
                "routing.generate_segment_candidates",
                "surface.generate_fill_candidates" } )
    {
        if( tool != selectedTool )
            skipped.push_back( tool );
    }

    return { { "selection_mode", "work_state_selected" },
             { "selected_tool", selectedTool },
             { "work_state_packet_kind",
               packetKindForContext( aObservation.m_ContextSnapshot,
                                     aObservation.m_Kind ) },
             { "candidate_title", toUtf8String( aCandidate.m_Title ) },
             { "skipped_tools", skipped } };
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


AI_SESSION_OPERATION_KIND sessionOperationKindFromId( const std::string& aKind )
{
    if( aKind == "pcb.create_via" )
        return AI_SESSION_OPERATION_KIND::CreateVia;

    if( aKind == "pcb.create_track_segment" )
        return AI_SESSION_OPERATION_KIND::CreateTrackSegment;

    if( aKind == "pcb.create_track_polyline" )
        return AI_SESSION_OPERATION_KIND::CreateTrackPolyline;

    if( aKind == "pcb.create_zone" )
        return AI_SESSION_OPERATION_KIND::CreateZone;

    if( aKind == "pcb.create_shape" )
        return AI_SESSION_OPERATION_KIND::CreateShape;

    if( aKind == "pcb.move_items" )
        return AI_SESSION_OPERATION_KIND::MoveItems;

    if( aKind == "pcb.delete_items" )
        return AI_SESSION_OPERATION_KIND::DeleteItems;

    if( aKind == "pcb.update_item_geometry" )
        return AI_SESSION_OPERATION_KIND::UpdateItemGeometry;

    if( aKind == "pcb.set_item_net" )
        return AI_SESSION_OPERATION_KIND::SetItemNet;

    if( aKind == "pcb.set_item_layer" )
        return AI_SESSION_OPERATION_KIND::SetItemLayer;

    if( aKind == "pcb.set_item_properties" )
        return AI_SESSION_OPERATION_KIND::SetItemProperties;

    if( aKind == "pcb.set_metadata" )
        return AI_SESSION_OPERATION_KIND::SetMetadata;

    if( aKind == "pcb.refill_zones" )
        return AI_SESSION_OPERATION_KIND::RefillZones;

    if( aKind == "pcb.rebuild_connectivity" )
        return AI_SESSION_OPERATION_KIND::RebuildConnectivity;

    if( aKind == "pcb.run_validation" )
        return AI_SESSION_OPERATION_KIND::RunValidation;

    if( aKind == "surface.apply_patch" )
        return AI_SESSION_OPERATION_KIND::ApplySurfacePatch;

    return AI_SESSION_OPERATION_KIND::Unknown;
}


nlohmann::json sessionHandleJson( const AI_SESSION_HANDLE& aHandle )
{
    nlohmann::json handle =
            { { "session_id", aHandle.m_SessionId },
              { "handle_id", aHandle.m_HandleId },
              { "generation", aHandle.m_Generation },
              { "handle", toUtf8String( aHandle.AsString() ) } };

    if( !aHandle.m_Alias.IsEmpty() )
        handle["alias"] = toUtf8String( aHandle.m_Alias );

    return handle;
}


wxString jsonValueText( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return fromUtf8String( aValue.get<std::string>() );

    return fromUtf8String( aValue.dump() );
}


AI_SESSION_HANDLE sessionHandleFromJson( const nlohmann::json& aHandle )
{
    AI_SESSION_HANDLE handle;

    if( !aHandle.is_object() )
        return handle;

    handle.m_SessionId = unsignedField( aHandle, "session_id" );
    handle.m_HandleId = unsignedField( aHandle, "handle_id" );
    handle.m_Generation = unsignedField( aHandle, "generation" );

    if( aHandle.contains( "alias" ) )
        handle.m_Alias = jsonValueText( aHandle["alias"] );

    return handle;
}


std::vector<AI_SESSION_HANDLE> sessionHandleArrayFromJson(
        const nlohmann::json& aHandles )
{
    std::vector<AI_SESSION_HANDLE> handles;

    if( !aHandles.is_array() )
        return handles;

    for( const nlohmann::json& handleJson : aHandles )
    {
        AI_SESSION_HANDLE handle = sessionHandleFromJson( handleJson );

        if( handle.IsValid() )
            handles.push_back( handle );
    }

    return handles;
}


std::vector<wxString> stringArrayFromJson( const nlohmann::json& aValues )
{
    std::vector<wxString> values;

    if( !aValues.is_array() )
        return values;

    for( const nlohmann::json& value : aValues )
        values.push_back( jsonValueText( value ) );

    return values;
}


std::map<wxString, wxString> stringMapFromJson( const nlohmann::json& aObject )
{
    std::map<wxString, wxString> values;

    if( !aObject.is_object() )
        return values;

    for( auto it = aObject.begin(); it != aObject.end(); ++it )
        values[fromUtf8String( it.key() )] = jsonValueText( it.value() );

    return values;
}


AI_SHADOW_ITEM shadowItemFromJson( const nlohmann::json& aItem )
{
    AI_SHADOW_ITEM item;

    if( !aItem.is_object() )
        return item;

    if( aItem.contains( "handle" ) )
        item.m_Handle = sessionHandleFromJson( aItem["handle"] );

    item.m_CreatedBy =
            sessionOperationKindFromId( aItem.value( "created_by", std::string() ) );

    if( aItem.contains( "type" ) )
        item.m_Type = jsonValueText( aItem["type"] );

    if( aItem.contains( "alias" ) )
        item.m_Alias = jsonValueText( aItem["alias"] );

    if( aItem.contains( "net" ) )
        item.m_Net = jsonValueText( aItem["net"] );

    if( aItem.contains( "layer" ) )
        item.m_Layer = jsonValueText( aItem["layer"] );

    if( aItem.contains( "layers" ) )
        item.m_Layers = stringArrayFromJson( aItem["layers"] );

    if( aItem.contains( "geometry" ) )
        item.m_GeometryJson = fromUtf8String( aItem["geometry"].dump() );

    if( aItem.contains( "properties" ) )
        item.m_PropertiesJson = fromUtf8String( aItem["properties"].dump() );

    if( aItem.contains( "metadata" ) )
        item.m_Metadata = stringMapFromJson( aItem["metadata"] );

    item.m_CreatedEpoch = unsignedField( aItem, "created_epoch" );
    item.m_UpdatedEpoch = unsignedField( aItem, "updated_epoch" );
    item.m_Deleted = aItem.value( "deleted", false );
    return item;
}


std::vector<wxString> warningArrayFromJson( const nlohmann::json& aWarnings )
{
    std::vector<wxString> warnings;

    if( !aWarnings.is_array() )
        return warnings;

    for( const nlohmann::json& warning : aWarnings )
        warnings.push_back( jsonValueText( warning ) );

    return warnings;
}


AI_SESSION_OPERATION_RECORD sessionOperationFromJson(
        const nlohmann::json& aOperation )
{
    AI_SESSION_OPERATION_RECORD record;

    if( !aOperation.is_object() )
        return record;

    record.m_Kind =
            sessionOperationKindFromId( aOperation.value( "kind", std::string() ) );

    if( aOperation.contains( "arguments" ) )
        record.m_ArgumentsJson = fromUtf8String( aOperation["arguments"].dump() );

    if( aOperation.contains( "resolved_handles" ) )
        record.m_ResolvedHandles =
                sessionHandleArrayFromJson( aOperation["resolved_handles"] );

    if( aOperation.contains( "created_handles" ) )
        record.m_CreatedHandles =
                sessionHandleArrayFromJson( aOperation["created_handles"] );

    if( aOperation.contains( "warnings" ) )
        record.m_Warnings = warningArrayFromJson( aOperation["warnings"] );

    if( aOperation.contains( "result" ) )
        record.m_ResultJson = fromUtf8String( aOperation["result"].dump() );

    return record;
}


std::unique_ptr<AI_EXECUTION_SESSION> attemptSessionFromJournal(
        const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    nlohmann::json journal = parseObjectBody( aAttempt.m_JournalJson );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = aAttempt.m_HiddenSessionId == 0
                                  ? aAttempt.m_Id
                                  : aAttempt.m_HiddenSessionId;
    options.m_BoardId = wxS( "next-action-hidden-rehydrated" );
    options.m_BaseHash =
            fromUtf8String( journal.value( "base_hash", std::string() ) );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;

    auto session = std::make_unique<AI_EXECUTION_SESSION>( options );

    if( journal.contains( "shadow_items" ) && journal["shadow_items"].is_array() )
    {
        for( const nlohmann::json& itemJson : journal["shadow_items"] )
        {
            AI_SHADOW_ITEM item = shadowItemFromJson( itemJson );

            if( item.m_Handle.IsValid() )
                session->ShadowBoard().UpsertItem( std::move( item ) );
        }
    }

    if( journal.contains( "operations" ) && journal["operations"].is_array() )
    {
        for( const nlohmann::json& operationJson : journal["operations"] )
        {
            AI_SESSION_OPERATION_RECORD record =
                    sessionOperationFromJson( operationJson );

            if( record.m_Kind != AI_SESSION_OPERATION_KIND::Unknown )
                session->AppendOperation( std::move( record ) );
        }
    }

    return session;
}


std::string sessionHandleUri( const AI_SESSION_HANDLE& aHandle )
{
    std::string uri = "ai://session/" + std::to_string( aHandle.m_SessionId )
                      + "/handle/" + std::to_string( aHandle.m_HandleId )
                      + "/generation/" + std::to_string( aHandle.m_Generation );

    if( !aHandle.m_Alias.IsEmpty() )
        uri += "/alias/" + toUtf8String( aHandle.m_Alias );

    return uri;
}


nlohmann::json sessionHandleArrayJson(
        const std::vector<AI_SESSION_HANDLE>& aHandles )
{
    nlohmann::json handles = nlohmann::json::array();

    for( const AI_SESSION_HANDLE& handle : aHandles )
        handles.push_back( sessionHandleJson( handle ) );

    return handles;
}


nlohmann::json warningArrayJson( const std::vector<wxString>& aWarnings )
{
    nlohmann::json warnings = nlohmann::json::array();

    for( const wxString& warning : aWarnings )
        warnings.push_back( toUtf8String( warning ) );

    return warnings;
}


nlohmann::json stringArrayJson( const std::vector<wxString>& aValues )
{
    nlohmann::json values = nlohmann::json::array();

    for( const wxString& value : aValues )
        values.push_back( toUtf8String( value ) );

    return values;
}


nlohmann::json objectFromJsonText( const wxString& aJsonText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aJsonText ), nullptr, false );

    return parsed.is_discarded() ? nlohmann::json::object() : parsed;
}


nlohmann::json pointJson( const VECTOR2I& aPoint )
{
    return { { "x", aPoint.x }, { "y", aPoint.y } };
}


nlohmann::json candidateLandingFactsJson(
        const AI_SUGGESTION_RECORD& aCandidate )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aCandidate.m_ArgumentsJson );

    if( !operation )
        return nlohmann::json::object();

    if( operation->IsPlaceViaPreview() )
    {
        return { { "kind", "placement_landing" },
                 { "source", "operation.position" },
                 { "position", pointJson( operation->m_Position ) },
                 { "net", toUtf8String( operation->m_NetName ) },
                 { "diameter", operation->m_Diameter },
                 { "drill", operation->m_Drill } };
    }

    if( operation->IsRouteSegmentPreview() )
    {
        return { { "kind", "routing_landing" },
                 { "source", "operation.end" },
                 { "point", pointJson( operation->m_End ) },
                 { "start", pointJson( operation->m_Start ) },
                 { "net", toUtf8String( operation->m_NetName ) },
                 { "layer", toUtf8String( operation->m_LayerName ) },
                 { "width", operation->m_Width } };
    }

    return nlohmann::json::object();
}


nlohmann::json candidateRecordJson( const AI_SUGGESTION_RECORD& aCandidate,
                                    size_t aIndex )
{
    nlohmann::json arguments = objectFromJsonText( aCandidate.m_ArgumentsJson );
    nlohmann::json record =
            { { "index", aIndex },
              { "title", toUtf8String( aCandidate.m_Title ) },
              { "body", toUtf8String( aCandidate.m_Body ) },
              { "source_tool", candidateSourceToolName( aCandidate ) },
              { "context_kind", toUtf8String( aCandidate.m_ContextKind ) },
              { "preview_object_count", aCandidate.m_PreviewObjects.size() },
              { "edit_object_count", aCandidate.m_EditObjects.size() },
              { "arguments", arguments },
              { "publish_allowed", false } };

    if( arguments.contains( "operation" ) )
        record["operation"] = arguments["operation"];

    nlohmann::json landingFacts = candidateLandingFactsJson( aCandidate );

    if( !landingFacts.empty() )
        record["landing_facts"] = std::move( landingFacts );

    return record;
}


nlohmann::json metadataJson( const std::map<wxString, wxString>& aMetadata )
{
    nlohmann::json metadata = nlohmann::json::object();

    for( const auto& [key, value] : aMetadata )
        metadata[toUtf8String( key )] = toUtf8String( value );

    return metadata;
}


nlohmann::json shadowItemJson( const AI_SHADOW_ITEM& aItem )
{
    return {
        { "handle", sessionHandleJson( aItem.m_Handle ) },
        { "created_by", toUtf8String( AiSessionOperationKindId( aItem.m_CreatedBy ) ) },
        { "type", toUtf8String( aItem.m_Type ) },
        { "alias", toUtf8String( aItem.m_Alias ) },
        { "net", toUtf8String( aItem.m_Net ) },
        { "layer", toUtf8String( aItem.m_Layer ) },
        { "layers", stringArrayJson( aItem.m_Layers ) },
        { "geometry", objectFromJsonText( aItem.m_GeometryJson ) },
        { "properties", objectFromJsonText( aItem.m_PropertiesJson ) },
        { "metadata", metadataJson( aItem.m_Metadata ) },
        { "created_epoch", aItem.m_CreatedEpoch },
        { "updated_epoch", aItem.m_UpdatedEpoch },
        { "deleted", aItem.m_Deleted }
    };
}


nlohmann::json shadowItemsJson( const AI_EXECUTION_SESSION& aSession )
{
    nlohmann::json items = nlohmann::json::array();

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
        items.push_back( shadowItemJson( item ) );

    return items;
}


AI_NEXT_ACTION_BUDGET_COUNTERS attemptBudgetCounters(
        const AI_EXECUTION_SESSION& aSession,
        const wxString& aRenderOutputsJson,
        const wxString& aValidationFactsJson,
        const std::chrono::steady_clock::time_point& aStartedAt,
        const std::chrono::steady_clock::time_point& aFinishedAt )
{
    AI_NEXT_ACTION_BUDGET_COUNTERS counters;
    std::set<std::string>          touchedObjects;

    counters.m_ToolRoundCount = 1;

    if( !aRenderOutputsJson.IsEmpty() )
    {
        counters.m_RenderCount = 1;
        ++counters.m_ToolRoundCount;
    }

    if( !aValidationFactsJson.IsEmpty() )
    {
        counters.m_ValidationCount = 1;
        ++counters.m_ToolRoundCount;
    }

    const auto wallTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    aFinishedAt - aStartedAt ).count();

    counters.m_WallTimeMs = wallTimeMs < 0 ? 0 : static_cast<uint64_t>( wallTimeMs );

    auto recordTouchedHandle =
            [&]( const AI_SESSION_HANDLE& aHandle )
            {
                if( aHandle.IsValid() )
                    touchedObjects.insert( sessionHandleUri( aHandle ) );
            };

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        if( operation.IsMutation() )
            ++counters.m_MutationCount;

        counters.m_CreatedObjectCount += operation.m_CreatedHandles.size();

        for( const AI_SESSION_HANDLE& handle : operation.m_CreatedHandles )
            recordTouchedHandle( handle );

        for( const AI_SESSION_HANDLE& handle : operation.m_ResolvedHandles )
            recordTouchedHandle( handle );
    }

    nlohmann::json touched = nlohmann::json::array();

    for( const std::string& handle : touchedObjects )
        touched.push_back( handle );

    counters.m_TouchedObjectCount = touchedObjects.size();
    counters.m_TouchedObjectSetJson = fromUtf8String( touched.dump() );
    return counters;
}


wxString sessionJournalJson( const AI_EXECUTION_SESSION& aSession,
                             const AI_SESSION_OBSERVATION& aObservation )
{
    nlohmann::json operations = nlohmann::json::array();

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        nlohmann::json result =
                nlohmann::json::parse( toUtf8String( operation.m_ResultJson ), nullptr,
                                       false );

        if( result.is_discarded() )
            result = nlohmann::json::object();

        operations.push_back(
                { { "operation_id", operation.m_Id },
                  { "step_id", operation.m_StepId },
                  { "kind", toUtf8String( operation.OperationId() ) },
                  { "arguments", nlohmann::json::parse(
                            toUtf8String( operation.m_ArgumentsJson ), nullptr, false ) },
                  { "resolved_handles",
                    sessionHandleArrayJson( operation.m_ResolvedHandles ) },
                  { "created_handles",
                    sessionHandleArrayJson( operation.m_CreatedHandles ) },
                  { "warnings", warningArrayJson( operation.m_Warnings ) },
                  { "result", result },
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
              { "shadow_items", shadowItemsJson( aSession ) },
              { "operations", operations } };

    return fromUtf8String( payload.dump() );
}


std::string jsonStringOrEmpty( const nlohmann::json& aObject, const char* aKey )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_string() )
    {
        return std::string();
    }

    return aObject[aKey].get<std::string>();
}


const nlohmann::json* surfacePatchOperationArray( const nlohmann::json& aPatch )
{
    for( const char* key : { "operations", "ops", "changes" } )
    {
        if( aPatch.contains( key ) && aPatch[key].is_array() )
            return &aPatch[key];
    }

    return nullptr;
}


nlohmann::json surfacePatchDiffEntriesJson( const nlohmann::json& aArgs )
{
    nlohmann::json entries = nlohmann::json::array();

    if( !aArgs.is_object() || !aArgs.contains( "patch" )
        || !aArgs["patch"].is_object() )
    {
        return entries;
    }

    const nlohmann::json* operations =
            surfacePatchOperationArray( aArgs["patch"] );

    if( !operations )
        return entries;

    const std::string surfaceId = jsonStringOrEmpty( aArgs, "surface_id" );
    const std::string tableId = jsonStringOrEmpty( aArgs, "table_id" );

    for( const nlohmann::json& op : *operations )
    {
        const std::string opName = jsonStringOrEmpty( op, "op" );

        if( opName == "set_cell" )
        {
            const std::string rowId = jsonStringOrEmpty( op, "row_id" );
            const std::string columnId = jsonStringOrEmpty( op, "column_id" );
            const std::string tableOverride = jsonStringOrEmpty( op, "table_id" );
            const std::string opTableId =
                    tableOverride.empty() ? tableId : tableOverride;

            if( surfaceId.empty() || opTableId.empty() || rowId.empty()
                || columnId.empty() || !op.contains( "value" ) )
            {
                continue;
            }

            entries.push_back(
                    { { "kind", "set_cell" },
                      { "surface_id", surfaceId },
                      { "table_id", opTableId },
                      { "row_id", rowId },
                      { "column_id", columnId },
                      { "value", op["value"] },
                      { "target_path",
                        "surfaces." + surfaceId + ".tables." + opTableId
                                + ".rows." + rowId + ".cells." + columnId } } );
            continue;
        }

        if( opName == "set_field" )
        {
            const std::string fieldId = jsonStringOrEmpty( op, "field_id" );

            if( surfaceId.empty() || fieldId.empty() || !op.contains( "value" ) )
                continue;

            entries.push_back(
                    { { "kind", "set_field" },
                      { "surface_id", surfaceId },
                      { "field_id", fieldId },
                      { "value", op["value"] },
                      { "target_path",
                        "surfaces." + surfaceId + ".fields." + fieldId } } );
        }
    }

    return entries;
}


nlohmann::json surfacePatchPreviewFactsJson(
        const AI_EXECUTION_SESSION& aSession )
{
    nlohmann::json previews = nlohmann::json::array();

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        if( operation.m_Kind != AI_SESSION_OPERATION_KIND::ApplySurfacePatch )
            continue;

        nlohmann::json args = objectFromJsonText( operation.m_ArgumentsJson );
        nlohmann::json result = objectFromJsonText( operation.m_ResultJson );

        if( !args.is_object() )
            args = nlohmann::json::object();

        if( !result.is_object() )
            result = nlohmann::json::object();

        nlohmann::json preview =
                { { "kind", "surface_patch_preview" },
                  { "operation_id", operation.m_Id },
                  { "step_id", operation.m_StepId },
                  { "source_operation_kind", "surface.apply_patch" },
                  { "shadow_only", true },
                  { "live_board_touched", false },
                  { "direct_publish", false },
                  { "publish_allowed", false },
                  { "result", result } };

        if( args.contains( "surface_id" ) && args["surface_id"].is_string() )
            preview["surface_id"] = args["surface_id"];

        if( args.contains( "table_id" ) && args["table_id"].is_string() )
            preview["table_id"] = args["table_id"];

        if( args.contains( "alias" ) && args["alias"].is_string() )
            preview["alias"] = args["alias"];

        if( args.contains( "target_scope" ) && args["target_scope"].is_object() )
            preview["target_scope"] = args["target_scope"];

        if( args.contains( "patch" ) && args["patch"].is_object() )
        {
            preview["patch"] = args["patch"];

            const nlohmann::json& patch = args["patch"];
            size_t patchOperationCount = 0;

            for( const char* key : { "operations", "ops", "changes" } )
            {
                if( patch.contains( key ) && patch[key].is_array() )
                {
                    patchOperationCount = patch[key].size();
                    break;
                }
            }

            if( patchOperationCount != 0 )
                preview["patch_operation_count"] = patchOperationCount;

            nlohmann::json diffEntries = surfacePatchDiffEntriesJson( args );

            if( !diffEntries.empty() )
            {
                preview["surface_patch_diff_entry_count"] = diffEntries.size();
                preview["surface_patch_diff_entries"] = std::move( diffEntries );
            }
        }

        if( !preview.contains( "patch_operation_count" )
            && result.contains( "patch_operation_count" ) )
        {
            preview["patch_operation_count"] = result["patch_operation_count"];
        }

        previews.push_back( std::move( preview ) );
    }

    return previews;
}
} // namespace


bool AI_NEXT_ACTION_CONTEXT_VERSION::IsValid() const
{
    return m_ContextVersion.IsValid() || !m_BoardBaseHash.IsEmpty()
           || m_ActivitySequence != 0 || !m_ViewportFingerprint.IsEmpty()
           || !m_CursorRegionFingerprint.IsEmpty();
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
              { "activity_sequence", m_ActivitySequence },
              { "viewport_fingerprint",
                toUtf8String( m_ViewportFingerprint ) },
              { "cursor_region_fingerprint",
                toUtf8String( m_CursorRegionFingerprint ) } };

    return fromUtf8String( payload.dump() );
}


bool AI_NEXT_ACTION_CONTEXT_VERSION::SameSuggestionContext(
        const AI_CONTEXT_VERSION& aVersion ) const
{
    return sameVersion( m_ContextVersion, aVersion );
}


AI_NEXT_ACTION_CONTEXT_VERSION AiNextActionContextVersionFromSnapshot(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        uint64_t aActivitySequence,
        const wxString& aBoardBaseHash )
{
    AI_NEXT_ACTION_CONTEXT_VERSION version;
    version.m_BoardBaseHash = aBoardBaseHash;
    version.m_ContextVersion = aSnapshot.m_Version;
    version.m_ToolModeVersion =
            static_cast<uint64_t>( aSnapshot.m_ToolState.m_Kind );
    version.m_UiFocusVersion =
            static_cast<uint64_t>( aSnapshot.m_PanelStates.size() );
    version.m_ActivitySequence = aActivitySequence;
    version.m_ViewportFingerprint = viewportFingerprintForSnapshot( aSnapshot );
    version.m_CursorRegionFingerprint =
            cursorRegionFingerprintForSnapshot( aSnapshot );
    return version;
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
    return m_LeaseId != 0 && !m_OwnerNamespace.IsEmpty() && m_AttemptId != 0
           && !m_DependencyFingerprint.IsEmpty()
           && !m_TouchedObjectSetFingerprint.IsEmpty();
}


wxString AI_ACCEPT_OWNERSHIP_TOKEN::AsJsonText() const
{
    nlohmann::json payload =
            { { "preview_id", m_PreviewId },
              { "lease_id", m_LeaseId },
              { "owner_namespace", toUtf8String( m_OwnerNamespace ) },
              { "attempt_id", m_AttemptId },
              { "dependency_fingerprint",
                toUtf8String( m_DependencyFingerprint ) },
              { "touched_object_set_fingerprint",
                toUtf8String( m_TouchedObjectSetFingerprint ) },
              { "context_version",
                nlohmann::json::parse( toUtf8String( m_ContextVersion.AsJsonText() ) ) } };

    return fromUtf8String( payload.dump() );
}


wxString AI_NEXT_ACTION_GATE_RESULT::AsJsonText() const
{
    nlohmann::json reasons = nlohmann::json::array();

    for( const wxString& reason : m_Reasons )
        reasons.push_back( toUtf8String( reason ) );

    nlohmann::json payload =
            { { "gate", toUtf8String( m_Gate ) },
              { "allowed", m_Allowed },
              { "reasons", reasons } };

    return fromUtf8String( payload.dump() );
}


bool AI_NEXT_ACTION_PUBLISH_DECISION::IsValid() const
{
    return m_Publish && m_AttemptId != 0 && m_PreviewLease.IsValid()
           && m_AcceptToken.IsValid();
}


wxString AI_NEXT_ACTION_BUDGET_COUNTERS::AsJsonText() const
{
    nlohmann::json touchedObjects =
            nlohmann::json::parse( toUtf8String( m_TouchedObjectSetJson ), nullptr,
                                   false );

    if( touchedObjects.is_discarded() || !touchedObjects.is_array() )
        touchedObjects = nlohmann::json::array();

    nlohmann::json payload =
            { { "tool_round_count", m_ToolRoundCount },
              { "mutation_count", m_MutationCount },
              { "render_count", m_RenderCount },
              { "validation_count", m_ValidationCount },
              { "wall_time_ms", m_WallTimeMs },
              { "created_object_count", m_CreatedObjectCount },
              { "touched_object_count", m_TouchedObjectCount },
              { "touched_object_set", touchedObjects } };

    return fromUtf8String( payload.dump() );
}


bool isActiveNextActionToolState( AI_TOOL_STATE_KIND aToolState )
{
    return aToolState == AI_TOOL_STATE_KIND::RoutingTrack
           || aToolState == AI_TOOL_STATE_KIND::PlacingVia
           || aToolState == AI_TOOL_STATE_KIND::PlacingFootprint
           || aToolState == AI_TOOL_STATE_KIND::DrawingZone;
}


std::optional<AI_SEMANTIC_EVENT> AI_NEXT_ACTION_SCHEDULER::BuildSemanticEvent(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return std::nullopt;

    if( !aTrigger.m_ContextSnapshot.HasContext() )
        return std::nullopt;

    wxString action = aTrigger.m_Activity.m_ActionName.Lower();

    const bool rawPointerMove =
            action.Contains( wxS( "mouse.move" ) )
            || action.Contains( wxS( "cursor.move" ) )
            || action.Contains( wxS( "pointer.move" ) );

    if( rawPointerMove
        && !isActiveNextActionToolState(
                aTrigger.m_ContextSnapshot.m_ToolState.m_Kind ) )
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
    event.m_ContextVersion = AiNextActionContextVersionFromSnapshot(
            aTrigger.m_ContextSnapshot, aTrigger.m_Activity.m_Sequence );

    if( aTrigger.m_ContextVersion.IsValid() )
        event.m_ContextVersion.m_ContextVersion = aTrigger.m_ContextVersion;

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
    const std::string packetKind =
            packetKindForContext( aObservation.m_ContextSnapshot,
                                  aObservation.m_Kind );

    if( packetKind == "placement" )
    {
        if( std::optional<AI_SUGGESTION_RECORD> via =
                    AiGenerateViaPatternCandidate( trigger ) )
        {
            candidates.push_back( *via );
        }
    }
    else if( packetKind == "routing" )
    {
        if( std::optional<AI_SUGGESTION_RECORD> route =
                    AiGenerateRoutingSegmentCandidate( trigger ) )
        {
            candidates.push_back( *route );
        }
    }
    else if( packetKind == "structured_surface" )
    {
        if( std::optional<AI_SUGGESTION_RECORD> panel =
                    AiGeneratePanelTableFillCandidate( trigger ) )
        {
            candidates.push_back( *panel );
        }
    }

    return candidates;
}


AI_NEXT_ACTION_TOOL_REGISTRY::AI_NEXT_ACTION_TOOL_REGISTRY(
        AI_SESSION_VALIDATION_SERVICE* aValidationService,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService ) :
        m_ValidationService( aValidationService ),
        m_PreviewService( aPreviewService )
{
}


void AI_NEXT_ACTION_TOOL_REGISTRY::SetServices(
        AI_SESSION_VALIDATION_SERVICE* aValidationService,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService )
{
    m_ValidationService = aValidationService;
    m_PreviewService = aPreviewService;
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::ToolCatalogJson() const
{
    nlohmann::json tools = nlohmann::json::array(
            { { { "name", "observation.read" },
                { "layer", "atomic" },
                { "role", "facts" },
                { "side_effect", "read_only" },
                { "can_publish", false } },
              { { "name", "placement.generate_via_pattern_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "placement" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_via_pattern_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_segment_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_routing_segment_library" },
                { "can_publish", false } },
              { { "name", "surface.generate_fill_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "structured_surface" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_surface_fill_library" },
                { "can_publish", false } },
              { { "name", "shadow.apply_candidate" },
                { "layer", "atomic" },
                { "role", "hidden_mutation" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "live_board_touched", false } },
              { { "name", "render.hidden_attempt" },
                { "layer", "atomic" },
                { "role", "render" },
                { "side_effect", "render" },
                { "can_publish", false } },
              { { "name", "validate.hidden_attempt" },
                { "layer", "atomic" },
                { "role", "validation" },
                { "side_effect", "validate" },
                { "can_publish", false } },
              { { "name", "rollback.attempt" },
                { "layer", "atomic" },
                { "role", "checkpoint_rollback" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false } },
              { { "name", "placement.repair_via" },
                { "layer", "integrated" },
                { "role", "placement_repair" },
                { "work_state", "placement" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.create_via" },
                { "max_steps", 1 } },
              { { "name", "placement.repair_move_items" },
                { "layer", "integrated" },
                { "role", "placement_repair" },
                { "work_state", "placement" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.move_items" },
                { "max_steps", 1 } },
              { { "name", "routing.repair_segment" },
                { "layer", "integrated" },
                { "role", "routing_repair" },
                { "work_state", "routing" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.create_track_segment" },
                { "max_steps", 1 } },
              { { "name", "routing.repair_polyline" },
                { "layer", "integrated" },
                { "role", "routing_repair" },
                { "work_state", "routing" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.create_track_polyline" },
                { "max_steps", 1 } },
              { { "name", "surface.repair_patch" },
                { "layer", "integrated" },
                { "role", "surface_repair" },
                { "work_state", "structured_surface" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "surface.apply_patch" },
                { "max_steps", 1 } },
              { { "name", "repair.apply_bounded_plan" },
                { "layer", "integrated" },
                { "role", "bounded_repair" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "requires_lowering_to",
                  nlohmann::json::array( { "atomic", "integrated" } ) },
                { "max_steps", 32 } },
              { { "name", "script.run_bounded_plan" },
                { "layer", "script" },
                { "role", "bounded_batch_composition" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "requires_lowering_to",
                  nlohmann::json::array( { "atomic", "integrated" } ) },
                { "max_steps", 32 } },
              { { "name", "publish.preview" },
                { "layer", "runtime_gate" },
                { "role", "runtime_publication_gate" },
                { "side_effect", "publish_gated" },
                { "can_publish", false },
                { "requires_review_decision", "publish" } } } );

    return fromUtf8String( tools.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::CallableToolCatalogJson() const
{
    nlohmann::json fullCatalog =
            nlohmann::json::parse( toUtf8String( ToolCatalogJson() ), nullptr,
                                   false );

    if( !fullCatalog.is_array() )
        return wxS( "[]" );

    nlohmann::json callable = nlohmann::json::array();

    auto safeFunctionName =
            []( std::string aName )
            {
                std::replace( aName.begin(), aName.end(), '.', '_' );
                return aName;
            };

    auto parametersFor =
            []( const std::string& aName )
            {
                nlohmann::json parameters =
                        { { "type", "object" },
                          { "additionalProperties", false },
                          { "properties", nlohmann::json::object() } };

                if( aName == "shadow.apply_candidate" )
                {
                    parameters["properties"]["candidate_index"] =
                            { { "type", "integer" },
                              { "minimum", 0 },
                              { "description",
                                "Zero-based candidate index from the current hidden attempt." } };
                    parameters["required"] = nlohmann::json::array(
                            { "candidate_index" } );
                }
                else if( aName == "validate.hidden_attempt" )
                {
                    parameters["properties"]["level"] =
                            { { "type", "string" },
                              { "enum",
                                nlohmann::json::array(
                                        { "geometry", "drc_lite", "full_drc" } ) },
                              { "description", "Validation depth requested for the hidden attempt." } };
                }
                else if( aName == "rollback.attempt" )
                {
                    parameters["properties"]["checkpoint_id"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Checkpoint id to restore." } };
                    parameters["properties"]["tool_call_id"] =
                            { { "type", "string" },
                              { "description",
                                "Optional prior mutation tool call id to roll back. "
                                "When omitted, the latest merged script batch is restored." } };
                    parameters["required"] = nlohmann::json::array(
                            { "checkpoint_id" } );
                }
                else if( isPlacementRepairViaTool( aName ) )
                {
                    parameters["properties"]["position"] =
                            { { "type", "object" },
                              { "description",
                                "Board position for the repaired via, using integer "
                                "internal coordinates." } };
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description", "Net assigned to the repaired via." } };
                    parameters["properties"]["diameter"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Via diameter in internal units." } };
                    parameters["properties"]["drill"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Via drill diameter in internal units." } };
                    parameters["properties"]["layer_pair"] =
                            { { "type", "object" },
                              { "description",
                                "Layer pair for the via, for example start F.Cu and end B.Cu." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias for this repaired via." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "position", "net", "diameter", "drill", "layer_pair" } );
                }
                else if( isPlacementRepairMoveItemsTool( aName ) )
                {
                    parameters["properties"]["handles"] =
                            { { "type", "array" },
                              { "description",
                                "Session handles for hidden placement items to move. "
                                "Use handles returned by earlier hidden attempt tools." },
                              { "items", { { "type", "object" } } } };
                    parameters["properties"]["delta"] =
                            { { "type", "object" },
                              { "description",
                                "Integer internal-coordinate movement delta with x and y." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias for this move repair batch." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "handles", "delta" } );
                }
                else if( isRoutingRepairSegmentTool( aName ) )
                {
                    parameters["properties"]["start"] =
                            { { "type", "object" },
                              { "description",
                                "Start point for the repaired route segment, using "
                                "integer internal coordinates." } };
                    parameters["properties"]["end"] =
                            { { "type", "object" },
                              { "description",
                                "End point for the repaired route segment, using "
                                "integer internal coordinates." } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description", "Routing layer for the segment." } };
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description", "Net assigned to the segment." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Track width in internal units." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias for this repaired segment." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "start", "end", "layer", "net", "width" } );
                }
                else if( isRoutingRepairPolylineTool( aName ) )
                {
                    parameters["properties"]["points"] =
                            { { "type", "array" },
                              { "description",
                                "Ordered route polyline points using integer internal coordinates." },
                              { "items", { { "type", "object" } } },
                              { "minItems", 2 } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description", "Routing layer for all polyline segments." } };
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description", "Net assigned to the polyline segments." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Track width in internal units." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias prefix for the repaired polyline." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "points", "layer", "net", "width" } );
                }
                else if( isSurfaceRepairPatchTool( aName ) )
                {
                    parameters["properties"]["surface_id"] =
                            { { "type", "string" },
                              { "description",
                                "Structured surface id to patch, such as a table, "
                                "property panel, dialog, or rule form surface." } };
                    parameters["properties"]["table_id"] =
                            { { "type", "string" },
                              { "description",
                                "Optional table id inside the structured surface." } };
                    parameters["properties"]["target_scope"] =
                            { { "type", "object" },
                              { "description",
                                "Optional normalized scope chosen by the model, "
                                "for example a row, column, cell range, or panel field." } };
                    parameters["properties"]["patch"] =
                            { { "type", "object" },
                              { "description",
                                "SurfacePatch object. It must contain operations, "
                                "ops, or changes describing typed surface edits." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias for this patch attempt." } };
                    parameters["properties"]["expected_surface_revision"] =
                            { { "description",
                                "Optional surface revision captured during observation; "
                                "accept replay rejects the patch if the live surface revision "
                                "has changed." },
                              { "oneOf",
                                nlohmann::json::array(
                                        { { { "type", "integer" } },
                                          { { "type", "number" } },
                                          { { "type", "string" } } } ) } };
                    parameters["properties"]["expected_schema_version"] =
                            { { "type", "string" },
                              { "description",
                                "Optional schema version captured during observation; accept "
                                "replay rejects the patch if the current schema differs." } };
                    parameters["properties"]["expected_selection_fingerprint"] =
                            { { "type", "string" },
                              { "description",
                                "Optional focused selection fingerprint captured during "
                                "observation; accept replay rejects the patch if focus moved." } };
                    parameters["properties"]["expected_overlap_set"] =
                            { { "description",
                                "Optional overlap set captured during observation; accept replay "
                                "rejects the patch if overlapping rows, fields, or controls "
                                "changed." },
                              { "oneOf",
                                nlohmann::json::array(
                                        { { { "type", "array" } },
                                          { { "type", "object" } },
                                          { { "type", "string" } } } ) } };
                    parameters["required"] = nlohmann::json::array(
                            { "surface_id", "patch" } );
                }
                else if( isBoundedPlanMutationTool( aName ) )
                {
                    nlohmann::json allowedOperationKinds =
                            nlohmann::json::array(
                                    { "pcb.create_via",
                                      "pcb.create_track_segment",
                                      "pcb.create_track_polyline",
                                      "pcb.create_zone",
                                      "pcb.create_shape",
                                      "pcb.move_items",
                                      "pcb.delete_items",
                                      "pcb.update_item_geometry",
                                      "pcb.set_item_net",
                                      "pcb.set_item_layer",
                                      "pcb.set_item_properties",
                                      "pcb.set_metadata",
                                      "pcb.refill_zones",
                                      "pcb.rebuild_connectivity",
                                      "pcb.run_validation",
                                      "surface.apply_patch" } );

                    parameters["properties"]["plan"] =
                            { { "type", "object" },
                              { "additionalProperties", false },
                              { "properties",
                                { { "operations",
                                    { { "type", "array" },
                                      { "minItems", 1 },
                                      { "maxItems", 32 },
                                      { "items",
                                        { { "type", "object" },
                                          { "additionalProperties", false },
                                          { "properties",
                                            { { "kind",
                                                { { "type", "string" },
                                                  { "enum", allowedOperationKinds },
                                                  { "description",
                                                    "Operation kind to lower into the hidden "
                                                    "session journal." } } },
                                              { "arguments",
                                                { { "type", "object" },
                                                  { "description",
                                                    "Arguments for the selected operation kind. "
                                                    "surface.apply_patch requires surface_id and "
                                                    "patch.operations / patch.ops / patch.changes." } } } } },
                                          { "required",
                                            nlohmann::json::array(
                                                    { "kind", "arguments" } ) } } } } } } },
                              { "required",
                                nlohmann::json::array( { "operations" } ) },
                              { "description",
                                "Bounded script plan that lowers only to approved atomic or integrated tools." } };
                    parameters["properties"]["max_steps"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "maximum", 32 },
                              { "description", "Optional tighter step cap for this plan." } };
                    parameters["required"] = nlohmann::json::array( { "plan" } );
                }

                return parameters;
            };

    for( const nlohmann::json& tool : fullCatalog )
    {
        if( !tool.is_object() )
            continue;

        if( tool.value( "role", std::string() ) == "runtime_publication_gate"
            || tool.value( "side_effect", std::string() ) == "publish_gated"
            || tool.value( "layer", std::string() ) == "runtime_gate" )
        {
            continue;
        }

        const std::string name = tool.value( "name", std::string() );

        if( name.empty() )
            continue;

        const std::string description =
                "KiSurf Next Action tool: " + name + ". Layer="
                + tool.value( "layer", std::string( "unknown" ) )
                + ", role=" + tool.value( "role", std::string( "unknown" ) )
                + ", side_effect="
                + tool.value( "side_effect", std::string( "unknown" ) )
                + ". This tool cannot publish a user-visible preview.";

        callable.push_back(
                { { "type", "function" },
                  { "function",
                    { { "name", safeFunctionName( name ) },
                      { "description", description },
                      { "parameters", parametersFor( name ) } } } } );
    }

    return fromUtf8String( callable.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::BuildHiddenMutationResult(
        const AI_SUGGESTION_RECORD& aCandidate ) const
{
    nlohmann::json mutation =
            { { "tool", "shadow.apply_candidate" },
              { "candidate_tool", candidateSourceToolName( aCandidate ) },
              { "operation", toUtf8String( operationSummary( aCandidate ) ) },
              { "mutated_shadow", true },
              { "live_board_touched", false },
              { "publish_allowed", false } };

    return fromUtf8String( mutation.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::RenderAttempt(
        const AI_EXECUTION_SESSION& aSession,
        const AI_SUGGESTION_RECORD& aCandidate ) const
{
    nlohmann::json surfacePatchPreviews =
            surfacePatchPreviewFactsJson( aSession );

    if( m_PreviewService )
    {
        const wxString renderArgs =
                wxS( "{\"scope\":\"session\",\"mode\":\"hidden_attempt\"}" );
        AI_SESSION_PREVIEW_RESULT result =
                m_PreviewService->RenderPreview( aSession, renderArgs );
        nlohmann::json serviceResult =
                nlohmann::json::parse( toUtf8String( result.m_ResultJson ), nullptr,
                                       false );

        if( serviceResult.is_discarded() )
            serviceResult = nlohmann::json::object();

        nlohmann::json render =
                { { "tool", "render.hidden_attempt" },
                  { "service_connected", true },
                  { "hidden", true },
                  { "mode", "native_preview_candidate" },
                  { "operation", toUtf8String( operationSummary( aCandidate ) ) },
                  { "status", result.m_Ok ? "preview_rendered" : "render_failed" },
                  { "render_valid", result.m_Ok },
                  { "preview_id", result.m_PreviewId },
                  { "rendered_item_count", result.m_RenderedItemCount },
                  { "render_args",
                    nlohmann::json::parse( toUtf8String( renderArgs ) ) },
                  { "service_result", serviceResult },
                  { "publish_allowed", false } };

        if( !surfacePatchPreviews.empty() )
        {
            render["surface_patch_preview_count"] = surfacePatchPreviews.size();
            render["surface_patch_previews"] = surfacePatchPreviews;
        }

        if( !result.m_ErrorCode.IsEmpty() )
            render["error_code"] = toUtf8String( result.m_ErrorCode );

        if( !result.m_Message.IsEmpty() )
            render["message"] = toUtf8String( result.m_Message );

        return fromUtf8String( render.dump() );
    }

    nlohmann::json render =
            { { "tool", "render.hidden_attempt" },
              { "hidden", true },
              { "mode", "native_preview_candidate" },
              { "operation", toUtf8String( operationSummary( aCandidate ) ) },
              { "publish_allowed", false } };

    if( !surfacePatchPreviews.empty() )
    {
        render["surface_patch_preview_count"] = surfacePatchPreviews.size();
        render["surface_patch_previews"] = surfacePatchPreviews;
    }

    return fromUtf8String( render.dump() );
}


wxString AI_NEXT_ACTION_TOOL_REGISTRY::ValidateAttempt(
        const AI_EXECUTION_SESSION& aSession, const AI_SUGGESTION_RECORD& ) const
{
    if( m_ValidationService )
    {
        const wxString validationArgs =
                wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" );
        AI_SESSION_VALIDATION_RESULT result =
                m_ValidationService->RunValidation( aSession, validationArgs,
                                                    wxS( "{}" ) );
        nlohmann::json serviceResult =
                nlohmann::json::parse( toUtf8String( result.m_ResultJson ), nullptr,
                                       false );

        if( serviceResult.is_discarded() )
            serviceResult = nlohmann::json::object();

        nlohmann::json warnings = nlohmann::json::array();

        for( const wxString& warning : result.m_Warnings )
            warnings.push_back( toUtf8String( warning ) );

        nlohmann::json validation =
                { { "tool", "validate.hidden_attempt" },
                  { "service_connected", true },
                  { "validation_args",
                    nlohmann::json::parse( toUtf8String( validationArgs ) ) },
                  { "status", result.m_Ok ? "validated" : "validation_failed" },
                  { "service_result", serviceResult },
                  { "warnings", std::move( warnings ) },
                  { "publish_allowed", false } };

        if( serviceResult.contains( "validation" )
            && serviceResult["validation"].is_object()
            && serviceResult["validation"].contains( "issue_count" )
            && serviceResult["validation"]["issue_count"].is_number_unsigned() )
        {
            validation["drc_error_count"] =
                    serviceResult["validation"]["issue_count"].get<size_t>();
        }

        if( !result.m_ErrorCode.IsEmpty() )
            validation["error_code"] = toUtf8String( result.m_ErrorCode );

        if( !result.m_Message.IsEmpty() )
            validation["message"] = toUtf8String( result.m_Message );

        return fromUtf8String( validation.dump() );
    }

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


AI_TOOL_INVOCATION_RESULT AI_NEXT_ACTION_TOOL_REGISTRY::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_TOOL_CALL_RECORD& aToolCall,
        const AI_OBSERVATION_PACKET& aObservation,
        AI_NEXT_ACTION_ATTEMPT_RECORD* aAttempt,
        AI_EXECUTION_SESSION* aAttemptSession ) const
{
    auto internalToolName =
            []( const wxString& aProviderName )
            {
                const std::string name = toUtf8String( aProviderName );

                if( name == "observation_read" || name == "observation.read" )
                    return std::string( "observation.read" );

                if( name == "placement_generate_via_pattern_candidates"
                    || name == "placement.generate_via_pattern_candidates" )
                {
                    return std::string( "placement.generate_via_pattern_candidates" );
                }

                if( name == "routing_generate_segment_candidates"
                    || name == "routing.generate_segment_candidates" )
                {
                    return std::string( "routing.generate_segment_candidates" );
                }

                if( name == "surface_generate_fill_candidates"
                    || name == "surface.generate_fill_candidates" )
                {
                    return std::string( "surface.generate_fill_candidates" );
                }

                if( name == "shadow_apply_candidate"
                    || name == "shadow.apply_candidate" )
                {
                    return std::string( "shadow.apply_candidate" );
                }

                if( name == "render_hidden_attempt"
                    || name == "render.hidden_attempt" )
                {
                    return std::string( "render.hidden_attempt" );
                }

                if( name == "validate_hidden_attempt"
                    || name == "validate.hidden_attempt" )
                {
                    return std::string( "validate.hidden_attempt" );
                }

                if( name == "rollback_attempt" || name == "rollback.attempt" )
                    return std::string( "rollback.attempt" );

                if( name == "script_run_bounded_plan"
                    || name == "script.run_bounded_plan" )
                {
                    return std::string( "script.run_bounded_plan" );
                }

                if( name == "repair_apply_bounded_plan"
                    || name == "repair.apply_bounded_plan" )
                {
                    return std::string( "repair.apply_bounded_plan" );
                }

                if( name == "placement_repair_via"
                    || name == "placement.repair_via" )
                {
                    return std::string( "placement.repair_via" );
                }

                if( name == "placement_repair_move_items"
                    || name == "placement.repair_move_items" )
                {
                    return std::string( "placement.repair_move_items" );
                }

                if( name == "routing_repair_segment"
                    || name == "routing.repair_segment" )
                {
                    return std::string( "routing.repair_segment" );
                }

                if( name == "routing_repair_polyline"
                    || name == "routing.repair_polyline" )
                {
                    return std::string( "routing.repair_polyline" );
                }

                if( name == "surface_repair_patch"
                    || name == "surface.repair_patch" )
                {
                    return std::string( "surface.repair_patch" );
                }

                return std::string();
            };

    auto makeResult =
            [&]( bool aAllowed, bool aExecuted, const wxString& aErrorCode,
                 const wxString& aMessage, nlohmann::json aPayload )
            {
                AI_TOOL_INVOCATION_RESULT result;
                result.m_RequestId = aRequest.m_RequestId;
                result.m_ToolCallId = aToolCall.m_ToolCallId;
                result.m_ActionName = fromUtf8String(
                        aPayload.value( "tool", std::string() ) );
                result.m_Allowed = aAllowed;
                result.m_Executed = aExecuted;
                result.m_ErrorCode = aErrorCode;
                result.m_Message = aMessage;

                aPayload["provider_tool_name"] =
                        toUtf8String( aToolCall.m_ToolName );
                aPayload["tool_call_id"] =
                        toUtf8String( aToolCall.m_ToolCallId );
                aPayload["allowed"] = aAllowed;
                aPayload["executed"] = aExecuted;
                aPayload["publish_allowed"] = false;

                if( !aErrorCode.IsEmpty() )
                    aPayload["error_code"] = toUtf8String( aErrorCode );

                if( !aMessage.IsEmpty() )
                    aPayload["message"] = toUtf8String( aMessage );

                result.m_ResultJson = fromUtf8String( aPayload.dump() );
                return result;
            };

    const std::string toolName = internalToolName( aToolCall.m_ToolName );

    if( toolName.empty() )
    {
        return makeResult(
                false, false, wxS( "unknown_tool" ),
                wxS( "Unknown Next Action runtime tool." ),
                { { "tool", toUtf8String( aToolCall.m_ToolName ) },
                  { "status", "unknown_tool" } } );
    }

    if( toolName == "observation.read" )
    {
        nlohmann::json observation =
                nlohmann::json::parse( toUtf8String( aObservation.AsJsonText() ),
                                       nullptr, false );

        if( observation.is_discarded() )
            observation = nlohmann::json::object();

        return makeResult(
                true, true, wxString(), wxS( "Observation returned." ),
                           { { "tool", "observation.read" },
                             { "status", "observed" },
                             { "observation", observation } } );
    }

    if( toolName == "placement.generate_via_pattern_candidates"
        || toolName == "routing.generate_segment_candidates"
        || toolName == "surface.generate_fill_candidates" )
    {
        std::vector<AI_SUGGESTION_RECORD> generated =
                GenerateCandidates( aObservation );
        nlohmann::json candidates = nlohmann::json::array();
        size_t         index = 0;

        for( const AI_SUGGESTION_RECORD& candidate : generated )
        {
            if( candidateSourceToolName( candidate ) != toolName )
                continue;

            candidates.push_back( candidateRecordJson( candidate, index ) );
            ++index;
        }

        return makeResult(
                true, true, wxString(), wxS( "Candidates generated." ),
                { { "tool", toolName },
                  { "status", "candidates_generated" },
                  { "candidate_count", candidates.size() },
                  { "candidates", candidates } } );
    }

    if( toolName == "shadow.apply_candidate" )
    {
        if( !aAttempt )
        {
            return makeResult(
                    false, false, wxS( "missing_attempt_context" ),
                    wxS( "shadow.apply_candidate requires an active attempt." ),
                    { { "tool", "shadow.apply_candidate" },
                      { "status", "missing_attempt_context" } } );
        }

        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        std::optional<size_t> candidateIndex =
                optionalSizeField( args, "candidate_index" );

        if( !candidateIndex )
        {
            return makeResult(
                    false, false, wxS( "malformed_arguments" ),
                    wxS( "shadow.apply_candidate requires candidate_index." ),
                    { { "tool", "shadow.apply_candidate" },
                      { "status", "malformed_arguments" } } );
        }

        if( *candidateIndex != aAttempt->m_CandidateIndex )
        {
            return makeResult(
                    false, false, wxS( "candidate_index_mismatch" ),
                    wxS( "candidate_index does not match the active hidden attempt." ),
                    { { "tool", "shadow.apply_candidate" },
                      { "status", "candidate_index_mismatch" },
                      { "candidate_index", *candidateIndex },
                      { "active_candidate_index", aAttempt->m_CandidateIndex } } );
        }

        nlohmann::json mutation =
                objectFromJsonText( BuildHiddenMutationResult(
                        aAttempt->m_Candidate ) );
        nlohmann::json journal = objectFromJsonText( aAttempt->m_JournalJson );

        mutation["tool"] = "shadow.apply_candidate";
        mutation["status"] = "candidate_applied";
        mutation["candidate_index"] = aAttempt->m_CandidateIndex;
        mutation["attempt_id"] = aAttempt->m_Id;
        mutation["hidden_session_id"] = aAttempt->m_HiddenSessionId;
        mutation["checkpoint_id"] = aAttempt->m_BaseCheckpointId;
        mutation["hidden_step_id"] = aAttempt->m_HiddenStepId;
        mutation["mutation_applied"] = true;
        mutation["checkpoint_first"] = aAttempt->m_BaseCheckpointId != 0;
        mutation["journal_first"] = journal.contains( "operations" );
        mutation["session_journal"] = std::move( journal );

        return makeResult( true, true, wxString(),
                           wxS( "Candidate applied in hidden attempt." ),
                           std::move( mutation ) );
    }

    if( isHiddenMutationBatchTool( toolName ) )
    {
        const std::string boundedPlanToolName = toolName;

        if( !aAttempt )
        {
            return makeResult(
                    false, false, wxS( "missing_attempt_context" ),
                    wxS( "hidden mutation batch requires an active attempt." ),
                    { { "tool", boundedPlanToolName },
                      { "status", "missing_attempt_context" } } );
        }

        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );

        if( isRepairWrapperTool( toolName ) )
        {
            std::string operationKind;
            wxString    malformedMessage;
            bool        malformed = false;

            if( isPlacementRepairViaTool( toolName ) )
            {
                operationKind = "pcb.create_via";
                malformed = !args.contains( "position" )
                             || !args["position"].is_object()
                             || !args.contains( "net" )
                             || !args["net"].is_string()
                             || !args.contains( "diameter" )
                             || !args["diameter"].is_number_integer()
                             || !args.contains( "drill" )
                             || !args["drill"].is_number_integer()
                             || !args.contains( "layer_pair" )
                             || !args["layer_pair"].is_object();
                malformedMessage =
                        wxS( "placement.repair_via requires position, net, "
                             "diameter, drill, and layer_pair." );
            }
            else if( isPlacementRepairMoveItemsTool( toolName ) )
            {
                operationKind = "pcb.move_items";
                malformed = !args.contains( "handles" )
                             || !args["handles"].is_array()
                             || args["handles"].empty()
                             || !args.contains( "delta" )
                             || !args["delta"].is_object();
                malformedMessage =
                        wxS( "placement.repair_move_items requires handles and delta." );
            }
            else if( isRoutingRepairSegmentTool( toolName ) )
            {
                operationKind = "pcb.create_track_segment";
                malformed = !args.contains( "start" )
                             || !args["start"].is_object()
                             || !args.contains( "end" )
                             || !args["end"].is_object()
                             || !args.contains( "layer" )
                             || !args["layer"].is_string()
                             || !args.contains( "net" )
                             || !args["net"].is_string()
                             || !args.contains( "width" )
                             || !args["width"].is_number_integer();
                malformedMessage =
                        wxS( "routing.repair_segment requires start, end, "
                             "layer, net, and width." );
            }
            else if( isRoutingRepairPolylineTool( toolName ) )
            {
                operationKind = "pcb.create_track_polyline";
                malformed = !args.contains( "points" )
                             || !args["points"].is_array()
                             || args["points"].size() < 2
                             || !args.contains( "layer" )
                             || !args["layer"].is_string()
                             || !args.contains( "net" )
                             || !args["net"].is_string()
                             || !args.contains( "width" )
                             || !args["width"].is_number_integer();
                malformedMessage =
                        wxS( "routing.repair_polyline requires points, layer, "
                             "net, and width." );
            }
            else
            {
                operationKind = "surface.apply_patch";
                malformed = !args.contains( "surface_id" )
                             || !args["surface_id"].is_string()
                             || !args.contains( "patch" )
                             || !args["patch"].is_object();
                malformedMessage =
                        wxS( "surface.repair_patch requires surface_id and patch." );
            }

            if( malformed )
            {
                return makeResult(
                        false, false, wxS( "malformed_arguments" ),
                        malformedMessage,
                        { { "tool", boundedPlanToolName },
                          { "status", "malformed_arguments" } } );
            }

            nlohmann::json operation =
                    { { "kind", operationKind },
                      { "arguments", args } };
            args =
                    { { "plan",
                        { { "operations",
                            nlohmann::json::array( { std::move( operation ) } ) } } },
                      { "max_steps", 1 } };
        }

        if( !args.contains( "plan" ) || !args["plan"].is_object()
            || !args["plan"].contains( "operations" )
            || !args["plan"]["operations"].is_array() )
        {
            return makeResult(
                    false, false, wxS( "malformed_arguments" ),
                    wxS( "bounded plan mutation requires plan.operations." ),
                    { { "tool", boundedPlanToolName },
                      { "status", "malformed_arguments" } } );
        }

        const nlohmann::json& operations = args["plan"]["operations"];
        const size_t maxSteps =
                optionalSizeField( args, "max_steps" ).value_or( 32 );

        if( maxSteps == 0 || maxSteps > 32 || operations.size() > maxSteps )
        {
            return makeResult(
                    false, false, wxS( "script_budget_exceeded" ),
                    wxS( "bounded plan mutation exceeded its operation budget." ),
                    { { "tool", boundedPlanToolName },
                      { "status", "script_budget_exceeded" },
                      { "operation_count", operations.size() },
                      { "max_steps", maxSteps } } );
        }

        nlohmann::json activeJournal = objectFromJsonText( aAttempt->m_JournalJson );

        const bool usingActiveAttemptFrame = aAttemptSession != nullptr;
        std::unique_ptr<AI_EXECUTION_SESSION> fallbackSession;
        AI_EXECUTION_SESSION* session = aAttemptSession;

        if( !session )
        {
            AI_EXECUTION_SESSION::OPEN_OPTIONS options;
            options.m_SessionId = aAttempt->m_HiddenSessionId;
            options.m_BoardId = wxS( "next-action-hidden-script" );
            options.m_BaseHash = fromUtf8String(
                    activeJournal.value( "base_hash",
                                         std::string( "script-plan-base" ) ) );
            options.m_EditorKind = AI_EDITOR_KIND::Pcb;

            fallbackSession = std::make_unique<AI_EXECUTION_SESSION>( options );
            session = fallbackSession.get();
        }

        const size_t operationCountBefore =
                session->Journal().Operations().size();
        const uint64_t checkpointId =
                session->Checkpoint( wxS( "before_next_action_script_plan" ) );
        const uint64_t stepId =
                session->BeginStep( wxS( "next action bounded script plan" ) );

        nlohmann::json lowered = nlohmann::json::array();
        bool           failed = false;
        wxString       failureCode;
        wxString       failureMessage;
        size_t         index = 0;

        for( const nlohmann::json& operation : operations )
        {
            nlohmann::json loweredOperation =
                    { { "index", index },
                      { "status", "pending" },
                      { "publish_allowed", false } };

            if( !operation.is_object() || !operation.contains( "kind" )
                || !operation["kind"].is_string() )
            {
                failed = true;
                failureCode = wxS( "malformed_script_operation" );
                failureMessage = wxS( "Each bounded script operation requires kind." );
                loweredOperation["status"] = "malformed_script_operation";
                lowered.push_back( std::move( loweredOperation ) );
                break;
            }

            const std::string kindName = operation["kind"].get<std::string>();
            const AI_SESSION_OPERATION_KIND operationKind =
                    sessionOperationKindFromId( kindName );
            nlohmann::json operationArgs =
                    operation.contains( "arguments" )
                    && operation["arguments"].is_object()
                            ? operation["arguments"]
                            : nlohmann::json::object();

            loweredOperation["kind"] = kindName;
            loweredOperation["arguments"] = operationArgs;

            if( operationKind == AI_SESSION_OPERATION_KIND::Unknown )
            {
                failed = true;
                failureCode = wxS( "unsupported_script_operation" );
                failureMessage = wxS( "Bounded script plan contains an unsupported operation." );
                loweredOperation["status"] = "unsupported_script_operation";
                lowered.push_back( std::move( loweredOperation ) );
                break;
            }

            const wxString operationArgsJson =
                    fromUtf8String( operationArgs.dump() );
            AI_ATOMIC_EXECUTION_RESULT execution =
                    AI_ATOMIC_OPERATION_EXECUTOR::Execute( *session, operationKind,
                                                           operationArgsJson );
            nlohmann::json executionResult =
                    objectFromJsonText( execution.m_ResultJson );

            if( !execution.m_Ok )
            {
                AI_SESSION_OPERATION_RECORD record;
                record.m_Kind = operationKind;
                record.m_ArgumentsJson = operationArgsJson;
                record.m_ResultJson = execution.m_ResultJson.IsEmpty()
                                              ? wxString( wxS( "{}" ) )
                                              : execution.m_ResultJson;
                record.m_Warnings = execution.m_Warnings;
                session->AppendOperation( std::move( record ) );

                failed = true;
                failureCode = execution.m_ErrorCode.IsEmpty()
                                      ? wxString( wxS( "script_operation_failed" ) )
                                      : execution.m_ErrorCode;
                failureMessage = execution.m_Message.IsEmpty()
                                         ? wxString( wxS( "Bounded script operation failed." ) )
                                         : execution.m_Message;
                loweredOperation["status"] = "script_operation_failed";
            }
            else
            {
                loweredOperation["status"] = "operation_executed";
            }

            loweredOperation["ok"] = execution.m_Ok;
            loweredOperation["result"] = std::move( executionResult );
            loweredOperation["warnings"] = warningArrayJson( execution.m_Warnings );
            lowered.push_back( std::move( loweredOperation ) );

            if( failed )
                break;

            ++index;
        }

        AI_SESSION_OBSERVATION observation = session->EndStep( stepId );
        nlohmann::json journal =
                objectFromJsonText( sessionJournalJson( *session, observation ) );

        const size_t operationCountAfter = session->Journal().Operations().size();
        const size_t mergedOperationCount =
                operationCountAfter >= operationCountBefore
                        ? operationCountAfter - operationCountBefore
                        : 0;
        const std::string toolCallId = toUtf8String( aToolCall.m_ToolCallId );

        if( usingActiveAttemptFrame )
        {
            if( journal.contains( "operations" )
                && journal["operations"].is_array() )
            {
                for( nlohmann::json& operation : journal["operations"] )
                {
                    if( !operation.is_object() )
                        continue;

                    const uint64_t operationId =
                            unsignedField( operation, "operation_id", 0 );

                    if( operationId <= operationCountBefore )
                        continue;

                    operation["merged_from_tool"] = boundedPlanToolName;
                    operation["merged_from_tool_call_id"] = toolCallId;
                }
            }

            if( journal.contains( "shadow_items" )
                && journal["shadow_items"].is_array() )
            {
                for( nlohmann::json& item : journal["shadow_items"] )
                {
                    if( !item.is_object()
                        || unsignedField( item, "created_epoch", 0 )
                                   <= operationCountBefore )
                    {
                        continue;
                    }

                    item["merged_from_tool"] = boundedPlanToolName;
                    item["merged_from_tool_call_id"] = toolCallId;
                }
            }

            nlohmann::json mergedToolBatches =
                    activeJournal.contains( "merged_tool_batches" )
                    && activeJournal["merged_tool_batches"].is_array()
                            ? activeJournal["merged_tool_batches"]
                            : nlohmann::json::array();

            nlohmann::json keptBatches = nlohmann::json::array();

            for( const nlohmann::json& batch : mergedToolBatches )
            {
                if( batch.is_object()
                    && batch.value( "tool_call_id", std::string() )
                               == toolCallId )
                {
                    continue;
                }

                keptBatches.push_back( batch );
            }

            keptBatches.push_back(
                    { { "tool", boundedPlanToolName },
                      { "tool_call_id", toolCallId },
                      { "operation_count", mergedOperationCount },
                      { "script_step_id", stepId },
                      { "checkpoint_id", checkpointId },
                      { "active_attempt_frame", true } } );
            journal["merged_tool_batches"] = std::move( keptBatches );

            if( activeJournal.contains( "rolled_back_tool_call_ids" )
                && activeJournal["rolled_back_tool_call_ids"].is_array() )
            {
                journal["rolled_back_tool_call_ids"] =
                        activeJournal["rolled_back_tool_call_ids"];
            }

            aAttempt->m_JournalJson = fromUtf8String( journal.dump() );
            syncAttemptMutationCountersFromJournal( *aAttempt );

            nlohmann::json provenance =
                    parseObjectBody( aAttempt->m_ProvenanceJson );

            if( !provenance.is_object() )
                provenance = nlohmann::json::object();

            provenance["session_journal"] = journal;
            aAttempt->m_ProvenanceJson = fromUtf8String( provenance.dump() );
            syncAttemptBudgetCountersToProvenance( *aAttempt );
        }

        nlohmann::json payload =
                { { "tool", boundedPlanToolName },
                  { "status", failed ? "script_plan_failed"
                                      : "script_plan_executed" },
                  { "attempt_id", aAttempt->m_Id },
                  { "hidden_session_id", aAttempt->m_HiddenSessionId },
                  { "checkpoint_id", checkpointId },
                  { "script_step_id", stepId },
                  { "script_step_count", lowered.size() },
                  { "operation_count", mergedOperationCount },
                  { "lowered_operations", std::move( lowered ) },
                  { "session_journal", std::move( journal ) },
                  { "active_attempt_frame", usingActiveAttemptFrame },
                  { "journal_scope",
                    usingActiveAttemptFrame ? "active_attempt_frame"
                                            : "isolated_script_session" },
                  { "raw_board_access", false },
                  { "direct_publish", false },
                  { "mutation_applied", !operations.empty() && !failed },
                  { "checkpoint_first", checkpointId != 0 },
                  { "journal_first", true } };

        if( failed )
        {
            return makeResult( true, false, failureCode, failureMessage,
                               std::move( payload ) );
        }

        return makeResult( true, true, wxString(),
                           wxS( "Bounded plan mutation executed in hidden attempt." ),
                           std::move( payload ) );
    }

    if( toolName == "rollback.attempt" )
    {
        if( !aAttempt )
        {
            return makeResult(
                    false, false, wxS( "missing_attempt_context" ),
                    wxS( "rollback.attempt requires an active attempt." ),
                    { { "tool", "rollback.attempt" },
                      { "status", "missing_attempt_context" } } );
        }

        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json rollback =
                rollbackMergedToolBatch( *aAttempt, aAttemptSession, args );

        const bool rolledBack = rollback.value( "rolled_back", false );
        return makeResult( true, rolledBack, wxString(),
                           rolledBack
                                   ? wxS( "Hidden attempt rolled back." )
                                   : wxS( "No hidden attempt mutation batch was rolled back." ),
                           std::move( rollback ) );
    }

    if( toolName == "render.hidden_attempt" )
    {
        if( !aAttempt )
        {
            return makeResult(
                    false, false, wxS( "missing_attempt_context" ),
                    wxS( "render.hidden_attempt requires an active attempt." ),
                    { { "tool", "render.hidden_attempt" },
                      { "status", "missing_attempt_context" } } );
        }

        std::unique_ptr<AI_EXECUTION_SESSION> fallbackSession;
        AI_EXECUTION_SESSION* session = aAttemptSession;

        if( !session )
        {
            fallbackSession = attemptSessionFromJournal( *aAttempt );
            session = fallbackSession.get();
        }

        aAttempt->m_RenderOutputsJson =
                RenderAttempt( *session, aAttempt->m_Candidate );
        ++aAttempt->m_BudgetCounters.m_RenderCount;
        syncAttemptBudgetCountersToProvenance( *aAttempt );

        nlohmann::json render =
                nlohmann::json::parse( toUtf8String( aAttempt->m_RenderOutputsJson ),
                                       nullptr, false );

        if( render.is_discarded() )
            render = nlohmann::json::object();

        render["attempt_session_journal"] =
                objectFromJsonText( aAttempt->m_JournalJson );
        render["active_attempt_frame"] = aAttemptSession != nullptr;

        return makeResult( true, true, wxString(),
                           wxS( "Hidden attempt render facts returned." ),
                           std::move( render ) );
    }

    if( toolName == "validate.hidden_attempt" )
    {
        if( !aAttempt )
        {
            return makeResult(
                    false, false, wxS( "missing_attempt_context" ),
                    wxS( "validate.hidden_attempt requires an active attempt." ),
                    { { "tool", "validate.hidden_attempt" },
                      { "status", "missing_attempt_context" } } );
        }

        std::unique_ptr<AI_EXECUTION_SESSION> fallbackSession;
        AI_EXECUTION_SESSION* session = aAttemptSession;

        if( !session )
        {
            fallbackSession = attemptSessionFromJournal( *aAttempt );
            session = fallbackSession.get();
        }

        aAttempt->m_ValidationFactsJson =
                ValidateAttempt( *session, aAttempt->m_Candidate );
        ++aAttempt->m_BudgetCounters.m_ValidationCount;
        syncAttemptBudgetCountersToProvenance( *aAttempt );

        nlohmann::json validation =
                nlohmann::json::parse( toUtf8String( aAttempt->m_ValidationFactsJson ),
                                       nullptr, false );

        if( validation.is_discarded() )
            validation = nlohmann::json::object();

        validation["attempt_session_journal"] =
                objectFromJsonText( aAttempt->m_JournalJson );
        validation["active_attempt_frame"] = aAttemptSession != nullptr;

        return makeResult( true, true, wxString(),
                           wxS( "Hidden attempt validation facts returned." ),
                           std::move( validation ) );
    }

    return makeResult(
            false, false, wxS( "unsupported_tool_in_runtime_loop" ),
            wxS( "This Next Action tool is cataloged but not yet executable "
                 "inside the provider tool-call loop." ),
            { { "tool", toolName },
              { "status", "unsupported_tool_in_runtime_loop" } } );
}


AI_NEXT_ACTION_RUNTIME::AI_NEXT_ACTION_RUNTIME(
        std::unique_ptr<AI_PROVIDER> aProvider,
        AI_SESSION_VALIDATION_SERVICE* aValidationService,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService ) :
        m_Provider( std::move( aProvider ) ),
        m_Tools( aValidationService, aPreviewService )
{
}


void AI_NEXT_ACTION_RUNTIME::SetServices(
        AI_SESSION_VALIDATION_SERVICE* aValidationService,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService )
{
    m_Tools.SetServices( aValidationService, aPreviewService );
}


void AI_NEXT_ACTION_RUNTIME::SetCurrentContextSampler(
        std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> aSampler )
{
    m_CurrentContextSampler = std::move( aSampler );
}


AI_PROVIDER_RESPONSE AI_NEXT_ACTION_RUNTIME::generateWithToolLoop(
        AI_PROVIDER_REQUEST aRequest,
        const AI_OBSERVATION_PACKET& aObservation,
        AI_NEXT_ACTION_ATTEMPT_RECORD* aAttempt )
{
    AI_PROVIDER_RESPONSE response = m_Provider->Generate( aRequest );

    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls;
    size_t                           toolRounds = 0;
    AI_EXECUTION_SESSION*            attemptFrame = nullptr;

    if( aAttempt )
    {
        auto frameIt = m_AttemptFrames.find( aAttempt->m_Id );

        if( frameIt != m_AttemptFrames.end() )
            attemptFrame = frameIt->second.get();
    }

    while( !response.m_ToolCalls.empty()
           && toolRounds < aRequest.m_MaxToolRounds )
    {
        std::vector<AI_TOOL_CALL_RECORD> roundToolCalls =
                std::move( response.m_ToolCalls );

        for( AI_TOOL_CALL_RECORD& toolCall : roundToolCalls )
        {
            toolCall.m_RequestId = aRequest.m_RequestId;

            AI_TOOL_INVOCATION_RESULT result =
                    m_Tools.HandleToolCall( aRequest, toolCall, aObservation,
                                            aAttempt, attemptFrame );
            toolCall.m_Allowed = result.m_Allowed;
            toolCall.m_Executed = result.m_Executed;
            toolCall.m_ErrorCode = result.m_ErrorCode;
            toolCall.m_Message = result.m_Message;
            toolCall.m_ResultJson = result.m_ResultJson;
        }

        handledToolCalls.insert(
                handledToolCalls.end(),
                std::make_move_iterator( roundToolCalls.begin() ),
                std::make_move_iterator( roundToolCalls.end() ) );
        ++toolRounds;

        if( aAttempt )
        {
            ++aAttempt->m_BudgetCounters.m_ToolRoundCount;
            syncAttemptBudgetCountersToProvenance( *aAttempt );

            const nlohmann::json toolTrace =
                    { { "provider_tool_results",
                        toolCallRecordsJson( handledToolCalls ) } };
            attachReviewProviderToolResultsToAttempt(
                    *aAttempt, fromUtf8String( toolTrace.dump() ) );
        }

        AI_PROVIDER_REQUEST continuationRequest = aRequest;
        continuationRequest.m_ToolResults = handledToolCalls;
        response = m_Provider->Generate( continuationRequest );
        response.m_RequestId = aRequest.m_RequestId;
    }

    if( !handledToolCalls.empty() )
        response.m_ToolCalls = std::move( handledToolCalls );

    return response;
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

    if( decision.m_SelectedCandidateIndex
        && *decision.m_SelectedCandidateIndex >= candidates.size() )
    {
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Abandoned;
        m_Steps.push_back( step );
        return std::nullopt;
    }

    const bool   explicitCandidateSelected =
            decision.m_SelectedCandidateIndex.has_value();
    const size_t firstCandidateIndex =
            explicitCandidateSelected ? *decision.m_SelectedCandidateIndex : 0;
    const size_t attemptLimit = explicitCandidateSelected
                                        ? 1
                                        : attemptLimitForWorkState( observation.m_Kind );

    for( size_t ii = 0; ii < attemptLimit; ++ii )
    {
        step.m_Status = ii == 0 ? AI_NEXT_ACTION_STEP_STATUS::Attempting
                                : AI_NEXT_ACTION_STEP_STATUS::Retrying;

        const size_t candidateIndex =
                explicitCandidateSelected
                        ? firstCandidateIndex
                        : std::min( ii, candidates.size() - 1 );
        AI_NEXT_ACTION_ATTEMPT_RECORD attempt =
                buildAttempt( step, observation, candidateIndex,
                              candidates[candidateIndex] );
        m_Attempts.push_back( attempt );
        step.m_AttemptIds.push_back( attempt.m_Id );
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Reviewing;

        AI_NEXT_ACTION_REVIEW_DECISION review =
                runReviewTurn( step, observation, attempt );
        step.m_ReviewDecisionJson = review.m_RawJson;
        attachReviewProviderToolResultsToAttempt( attempt, review.m_RawJson );

        for( AI_NEXT_ACTION_ATTEMPT_RECORD& storedAttempt : m_Attempts )
        {
            if( storedAttempt.m_Id == attempt.m_Id )
            {
                storedAttempt.m_JournalJson = attempt.m_JournalJson;
                storedAttempt.m_ProvenanceJson = attempt.m_ProvenanceJson;
                storedAttempt.m_BudgetCounters = attempt.m_BudgetCounters;
                break;
            }
        }

        if( review.WantsPublish() )
        {
            AI_NEXT_ACTION_PUBLISH_DECISION publish =
                    buildPublishDecision( step, attempt, review );
            step.m_ReviewDecisionJson = publish.m_RawJson;

            if( !publish.IsValid() )
            {
                if( ii + 1 < candidates.size() )
                    continue;

                break;
            }

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


bool AI_NEXT_ACTION_RUNTIME::Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession,
                                     const AI_CONTEXT_VERSION& aCurrentContextVersion )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !CanAccept( aSuggestionId ) )
        return false;

    if( !sameVersion( suggestion->m_ContextVersion, aCurrentContextVersion ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "context_drift" ) } );
        suggestion->m_Status = AI_SUGGESTION_STATUS::Expired;
        deactivateRuntimePreviewLease( *suggestion );
        return false;
    }

    if( !runtimeAttemptAcceptValidationSufficient( *suggestion ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "accept_validation_failed" ) } );
        suggestion->m_Status = AI_SUGGESTION_STATUS::Expired;
        deactivateRuntimePreviewLease( *suggestion );
        return false;
    }

    if( !aEditSession.Apply( suggestion->m_EditObjects, suggestion->m_Validation ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "edit_apply_failed" ) } );
        return false;
    }

    attachAcceptGateResult( *suggestion, true, {} );
    suggestion->m_Status = AI_SUGGESTION_STATUS::Accepted;
    deactivateRuntimePreviewLease( *suggestion );
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::Accept(
        uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !CanAccept( aSuggestionId ) )
        return false;

    const bool runtimeSuggestion = isNextActionRuntimeSuggestion( *suggestion );

    if( !sameVersion( suggestion->m_ContextVersion,
                      aCurrentContextVersion.m_ContextVersion )
        || ( runtimeSuggestion
             && !runtimeAcceptTokenMatchesDependency( *suggestion,
                                                      aCurrentContextVersion ) ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "context_drift" ) } );
        suggestion->m_Status = AI_SUGGESTION_STATUS::Expired;
        deactivateRuntimePreviewLease( *suggestion );
        return false;
    }

    if( !runtimeAttemptAcceptValidationSufficient( *suggestion ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "accept_validation_failed" ) } );
        suggestion->m_Status = AI_SUGGESTION_STATUS::Expired;
        deactivateRuntimePreviewLease( *suggestion );
        return false;
    }

    if( !aEditSession.Apply( suggestion->m_EditObjects, suggestion->m_Validation ) )
    {
        attachAcceptGateResult( *suggestion, false,
                                { wxS( "edit_apply_failed" ) } );
        return false;
    }

    attachAcceptGateResult( *suggestion, true, {} );
    suggestion->m_Status = AI_SUGGESTION_STATUS::Accepted;
    deactivateRuntimePreviewLease( *suggestion );
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::RecordSuggestionGateResult(
        uint64_t aSuggestionId, const wxString& aKey,
        const AI_NEXT_ACTION_GATE_RESULT& aGate )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion )
        return false;

    attachGateResultToSuggestion( *suggestion, aKey, aGate );
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::MarkAccepted( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Accepted;
    deactivateRuntimePreviewLease( *suggestion );
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::Reject( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Rejected;
    deactivateRuntimePreviewLease( *suggestion );
    return true;
}


bool AI_NEXT_ACTION_RUNTIME::Expire( uint64_t aSuggestionId )
{
    AI_SUGGESTION_RECORD* suggestion = findMutable( aSuggestionId );

    if( !suggestion || !isActive( *suggestion ) )
        return false;

    suggestion->m_Status = AI_SUGGESTION_STATUS::Expired;
    deactivateRuntimePreviewLease( *suggestion );
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
        deactivateRuntimePreviewLease( suggestion );
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


size_t AI_NEXT_ACTION_RUNTIME::ExpireStale(
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentVersion )
{
    size_t expired = 0;

    for( AI_SUGGESTION_RECORD& suggestion : m_Suggestions )
    {
        if( !isActive( suggestion ) )
            continue;

        bool isStale = !sameVersion( suggestion.m_ContextVersion,
                                     aCurrentVersion.m_ContextVersion );

        if( !isStale && isNextActionRuntimeSuggestion( suggestion ) )
            isStale = !runtimeAcceptTokenMatchesDependency( suggestion,
                                                            aCurrentVersion );

        if( !isStale )
            continue;

        suggestion.m_Status = AI_SUGGESTION_STATUS::Expired;
        deactivateRuntimePreviewLease( suggestion );
        ++expired;
    }

    for( AI_NEXT_ACTION_RUNTIME_STEP& step : m_Steps )
    {
        if( step.m_Status != AI_NEXT_ACTION_STEP_STATUS::Published )
            continue;

        if( dependencyFingerprint( step.m_ContextVersion )
            != dependencyFingerprint( aCurrentVersion ) )
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
              { "work_state", toUtf8String( aEvent.m_Kind ) },
              { "attempt_policy", attemptPolicyJson( aEvent.m_Kind ) },
              { "reason", toUtf8String( aEvent.m_Reason ) },
              { "dependency_fingerprint",
                toUtf8String( dependencyFingerprint( aEvent.m_ContextVersion ) ) },
              { "viewport_fingerprint",
                toUtf8String( aEvent.m_ContextVersion.m_ViewportFingerprint ) },
              { "cursor_region_fingerprint",
                toUtf8String( aEvent.m_ContextVersion.m_CursorRegionFingerprint ) },
              { "dynamic_context", toUtf8String( AiDynamicContextKind(
                                      aEvent.m_ContextSnapshot ) ) },
              { "tool_state",
                toUtf8String( aEvent.m_ContextSnapshot.m_ToolState.KindAsString() ) },
              { "work_state_packet", workStatePacketJson( aEvent ) },
              { "visual",
                { { "source", toUtf8String( aEvent.m_ContextSnapshot.m_Visual.m_Source ) },
                  { "has_pixels", aEvent.m_ContextSnapshot.m_Visual.HasPixels() },
                  { "unavailable_reason",
                    toUtf8String( aEvent.m_ContextSnapshot.m_Visual.m_UnavailableReason ) } } } };

    nlohmann::json contextSnapshot =
            nlohmann::json::parse( toUtf8String(
                                           aEvent.m_ContextSnapshot.AsJsonText(
                                                   32, 64, 32, 32, 8 ) ),
                                   nullptr, false );

    if( contextSnapshot.is_discarded() || !contextSnapshot.is_object() )
        contextSnapshot = nlohmann::json::object();

    facts["context_snapshot"] = std::move( contextSnapshot );

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
    request.m_ToolCatalogJson = m_Tools.CallableToolCatalogJson();
    request.m_DisableDefaultTools = true;

    AI_PROVIDER_RESPONSE response =
            generateWithToolLoop( request, aObservation, nullptr );
    nlohmann::json       parsed = parseObjectBody( response.m_Body );
    attachProviderToolResults( parsed, response );

    decision.m_Kind = parseDecisionKind( parsed );
    decision.m_OpportunityType = optionalString( parsed, "opportunity_type" );
    decision.m_ReasonCode = optionalString( parsed, "reason_code" );
    decision.m_SelectedCandidateIndex =
            optionalSizeField( parsed, "selected_candidate_index" );
    decision.m_RawJson = parsed.empty() ? response.m_Body
                                        : fromUtf8String( parsed.dump() );
    return decision;
}


AI_NEXT_ACTION_REVIEW_DECISION AI_NEXT_ACTION_RUNTIME::runReviewTurn(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation,
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
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
              { "publish_contract",
                { { "required_review_basis",
                    nlohmann::json::array( { "render_valid",
                                             "validation_passed",
                                             "budget_within_limits",
                                             "self_review_passed" } ) },
                  { "publish_requires_review_basis", true },
                  { "publish_is_runtime_gated", true } } },
              { "attempt",
                { { "attempt_id", aAttempt.m_Id },
                  { "operation", toUtf8String( operationSummary( aAttempt.m_Candidate ) ) },
                  { "candidate_title", toUtf8String( aAttempt.m_Candidate.m_Title ) },
                  { "session_journal", parseObjectBody( aAttempt.m_JournalJson ) },
                  { "render_outputs", nlohmann::json::parse(
                                              toUtf8String( aAttempt.m_RenderOutputsJson ) ) },
                  { "validation_facts", nlohmann::json::parse(
                                                toUtf8String(
                                                        aAttempt.m_ValidationFactsJson ) ) },
                   { "budget_counters", nlohmann::json::parse(
                                                 toUtf8String(
                                                         aAttempt.m_BudgetCounters
                                                                 .AsJsonText() ) ) } } } };

    nlohmann::json previousAttempts = nlohmann::json::array();

    for( uint64_t attemptId : aStep.m_AttemptIds )
    {
        if( attemptId == aAttempt.m_Id )
            break;

        auto priorIt = std::find_if(
                m_Attempts.begin(), m_Attempts.end(),
                [attemptId]( const AI_NEXT_ACTION_ATTEMPT_RECORD& aPrior )
                {
                    return aPrior.m_Id == attemptId;
                } );

        if( priorIt == m_Attempts.end() )
            continue;

        nlohmann::json priorAttempt =
                { { "attempt_id", priorIt->m_Id },
                  { "operation", toUtf8String( operationSummary( priorIt->m_Candidate ) ) },
                  { "candidate_title", toUtf8String( priorIt->m_Candidate.m_Title ) },
                  { "session_journal", parseObjectBody( priorIt->m_JournalJson ) },
                  { "render_outputs", nlohmann::json::parse(
                                              toUtf8String( priorIt->m_RenderOutputsJson ) ) },
                  { "validation_facts", nlohmann::json::parse(
                                                toUtf8String(
                                                        priorIt->m_ValidationFactsJson ) ) },
                  { "budget_counters", nlohmann::json::parse(
                                                toUtf8String(
                                                        priorIt->m_BudgetCounters
                                                                .AsJsonText() ) ) } };

        if( !priorIt->m_RollbackJson.IsEmpty() )
            priorAttempt["rollback"] = parseObjectBody( priorIt->m_RollbackJson );

        previousAttempts.push_back( std::move( priorAttempt ) );
    }

    if( !previousAttempts.empty() )
    {
        reviewInput["previous_attempts"] = previousAttempts;

        if( !aStep.m_ReviewDecisionJson.IsEmpty() )
        {
            reviewInput["previous_review_decision"] =
                    parseObjectBody( aStep.m_ReviewDecisionJson );
        }
    }

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
                 "whether to publish, retry, rollback_retry, or abandon. "
                 "When previous_attempts are present, use their render, validation, "
                 "rollback, and review feedback to avoid repeating failed work. "
                 "Publishing requires review_basis.render_valid, "
                 "review_basis.validation_passed, review_basis.budget_within_limits, "
                 "and review_basis.self_review_passed to be true. Return JSON only." );
    request.m_ResponseFormatJson = wxS( "{\"type\":\"json_object\"}" );
    request.m_ToolCatalogJson = m_Tools.CallableToolCatalogJson();
    const ATTEMPT_BUDGET_POLICY policy = attemptPolicyForWorkState(
            budgetPolicyWorkStateForCandidate( aAttempt.m_Candidate ) );
    request.m_MaxToolRounds =
            policy.m_MaxToolRounds > aAttempt.m_BudgetCounters.m_ToolRoundCount
                    ? static_cast<size_t>( policy.m_MaxToolRounds
                                           - aAttempt.m_BudgetCounters
                                                     .m_ToolRoundCount )
                    : 0;
    request.m_DisableDefaultTools = true;

    AI_PROVIDER_RESPONSE response =
            generateWithToolLoop( request, aObservation, &aAttempt );
    nlohmann::json       parsed = parseObjectBody( response.m_Body );
    attachProviderToolResults( parsed, response );

    review.m_Kind = parseDecisionKind( parsed );
    review.m_ReasonCode = optionalString( parsed, "reason_code" );
    review.m_AttemptId = aAttempt.m_Id;
    review.m_RawJson = parsed.empty() ? response.m_Body : fromUtf8String( parsed.dump() );
    return review;
}


AI_NEXT_ACTION_ATTEMPT_RECORD AI_NEXT_ACTION_RUNTIME::buildAttempt(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation,
        size_t aCandidateIndex,
        const AI_SUGGESTION_RECORD& aCandidate )
{
    const auto attemptStartedAt = std::chrono::steady_clock::now();

    AI_NEXT_ACTION_ATTEMPT_RECORD attempt;
    attempt.m_Id = m_NextAttemptId++;
    attempt.m_RuntimeStepId = aStep.m_Id;
    attempt.m_CandidateIndex = aCandidateIndex;
    attempt.m_Candidate = aCandidate;

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = attempt.m_Id;
    options.m_BoardId = wxS( "next-action-hidden" );
    options.m_BaseHash = aStep.m_ContextVersion.m_BoardBaseHash.IsEmpty()
                                  ? aStep.m_ContextVersion.m_ContextVersion.AsString()
                                  : aStep.m_ContextVersion.m_BoardBaseHash;
    options.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    options.m_ContextVersion = aStep.m_ContextVersion.m_ContextVersion;

    auto session = std::make_unique<AI_EXECUTION_SESSION>( options );
    attempt.m_HiddenSessionId = session->SessionId();
    attempt.m_BaseCheckpointId =
            session->Checkpoint( wxS( "before_next_action_attempt" ) );
    attempt.m_HiddenStepId =
            session->BeginStep( wxS( "next action hidden attempt" ) );

    const AI_SESSION_OPERATION_KIND operationKind =
            sessionOperationKindForCandidate( aCandidate );
    const wxString argumentsJson = aCandidate.m_ArgumentsJson.IsEmpty()
                                           ? wxString( wxS( "{}" ) )
                                           : aCandidate.m_ArgumentsJson;
    AI_ATOMIC_EXECUTION_RESULT execution =
            AI_ATOMIC_OPERATION_EXECUTOR::Execute( *session, operationKind,
                                                   argumentsJson );

    if( !execution.m_Ok )
    {
        AI_SESSION_OPERATION_RECORD operation;
        operation.m_Kind = operationKind;
        operation.m_ArgumentsJson = argumentsJson;
        operation.m_ResultJson = execution.m_ResultJson.IsEmpty()
                                         ? wxString( wxS( "{}" ) )
                                         : execution.m_ResultJson;
        operation.m_Warnings = execution.m_Warnings;
        session->AppendOperation( std::move( operation ) );
    }

    AI_SESSION_OBSERVATION sessionObservation =
            session->EndStep( attempt.m_HiddenStepId );
    attempt.m_JournalJson = sessionJournalJson( *session, sessionObservation );

    wxString mutationResult = m_Tools.BuildHiddenMutationResult( aCandidate );
    attempt.m_RenderOutputsJson = m_Tools.RenderAttempt( *session, aCandidate );
    attempt.m_ValidationFactsJson = m_Tools.ValidateAttempt( *session, aCandidate );
    attempt.m_BudgetCounters =
            attemptBudgetCounters( *session, attempt.m_RenderOutputsJson,
                                   attempt.m_ValidationFactsJson, attemptStartedAt,
                                   std::chrono::steady_clock::now() );

    nlohmann::json provenance =
            { { "attempt_id", attempt.m_Id },
              { "runtime_step_id", attempt.m_RuntimeStepId },
              { "candidate_index", attempt.m_CandidateIndex },
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
              { "budget_counters",
                nlohmann::json::parse( toUtf8String(
                        attempt.m_BudgetCounters.AsJsonText() ) ) },
              { "candidate_tool", candidateSourceToolName( aCandidate ) },
              { "candidate_generation",
                candidateGenerationTraceJson( aObservation, aCandidate ) } };
    attempt.m_ProvenanceJson = fromUtf8String( provenance.dump() );
    m_AttemptFrames[attempt.m_Id] = std::move( session );
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
    publish.m_GateResult.m_Gate = wxS( "preview" );
    publish.m_GateResult.m_Allowed = publish.m_Publish;

    if( !publish.m_Publish )
        appendGateReason( publish.m_GateResult, wxS( "review_not_publish" ) );

    if( !decisionOpportunityMatchesCandidate( aStep.m_LlmDecisionJson,
                                              aAttempt.m_Candidate ) )
    {
        appendGateReason( publish.m_GateResult,
                          wxS( "semantic_relevance_failed" ) );
    }

    if( !reviewBasisAllowsPreviewPublish( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult, wxS( "review_basis_failed" ) );

    if( !attemptBudgetWithinPolicy( aAttempt ) )
        appendGateReason( publish.m_GateResult, wxS( "budget_policy_failed" ) );

    if( renderFactsBlockPreviewPublish( aAttempt.m_RenderOutputsJson ) )
        appendGateReason( publish.m_GateResult, wxS( "render_gate_failed" ) );

    if( validationFactsBlockPreviewPublish( aAttempt.m_ValidationFactsJson ) )
        appendGateReason( publish.m_GateResult, wxS( "validation_gate_failed" ) );

    if( m_CurrentContextSampler )
    {
        const AI_NEXT_ACTION_CONTEXT_VERSION current = m_CurrentContextSampler();

        if( dependencyFingerprint( current )
            != dependencyFingerprint( aStep.m_ContextVersion ) )
        {
            appendGateReason( publish.m_GateResult, wxS( "context_drift" ) );
        }
    }

    publish.m_Publish = publish.m_GateResult.m_Allowed;
    publish.m_RawJson = reviewJsonWithPreviewGateResult( aReview.m_RawJson,
                                                         publish.m_GateResult );

    if( !publish.m_Publish )
        return publish;

    publish.m_PreviewLease.m_Id = m_NextLeaseId++;
    publish.m_PreviewLease.m_OwnerNamespace = wxS( "nextaction" );
    publish.m_PreviewLease.m_Active = publish.m_Publish;

    publish.m_AcceptToken.m_LeaseId = publish.m_PreviewLease.m_Id;
    publish.m_AcceptToken.m_OwnerNamespace = publish.m_PreviewLease.m_OwnerNamespace;
    publish.m_AcceptToken.m_ContextVersion = aStep.m_ContextVersion;
    publish.m_AcceptToken.m_DependencyFingerprint =
            dependencyFingerprint( aStep.m_ContextVersion );
    publish.m_AcceptToken.m_TouchedObjectSetFingerprint =
            fnv1a64Fingerprint( aAttempt.m_BudgetCounters.m_TouchedObjectSetJson );
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

    nlohmann::json attemptProvenance =
            nlohmann::json::parse( toUtf8String( aAttempt.m_ProvenanceJson ),
                                   nullptr, false );

    if( attemptProvenance.is_discarded() || !attemptProvenance.is_object() )
        attemptProvenance = nlohmann::json::object();

    nlohmann::json reviewTrace = parseObjectBody( aStep.m_ReviewDecisionJson );

    if( reviewTrace.contains( "provider_tool_results" )
        && reviewTrace["provider_tool_results"].is_array() )
    {
        attemptProvenance["provider_tool_results"] =
                reviewTrace["provider_tool_results"];
    }

    nlohmann::json provenance =
            { { "runtime", "next_action" },
              { "runtime_step_id", aStep.m_Id },
              { "attempt_id", aAttempt.m_Id },
              { "dependency_fingerprint",
                toUtf8String( aPublish.m_AcceptToken.m_DependencyFingerprint ) },
              { "preview_lease",
                nlohmann::json::parse( toUtf8String(
                        aPublish.m_PreviewLease.AsJsonText() ) ) },
              { "accept_token",
                nlohmann::json::parse( toUtf8String(
                        aPublish.m_AcceptToken.AsJsonText() ) ) },
              { "preview_gate_result",
                nlohmann::json::parse( toUtf8String(
                        aPublish.m_GateResult.AsJsonText() ) ) },
              { "attempt", attemptProvenance } };
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

    for( const AI_SUGGESTION_RECORD& existing : m_Suggestions )
    {
        if( existing.m_Status != AI_SUGGESTION_STATUS::Rejected )
            continue;

        if( !isNextActionRuntimeSuggestion( existing )
            || !isNextActionRuntimeSuggestion( aSuggestion ) )
        {
            continue;
        }

        if( !existing.m_Fingerprint.IsEmpty()
            && existing.m_Fingerprint == aSuggestion.m_Fingerprint )
        {
            return std::nullopt;
        }
    }

    for( AI_SUGGESTION_RECORD& existing : m_Suggestions )
    {
        if( !isActive( existing ) )
            continue;

        const bool sameFingerprint = !existing.m_Fingerprint.IsEmpty()
                                     && existing.m_Fingerprint
                                                == aSuggestion.m_Fingerprint;
        const bool runtimeLeaseConflict = isNextActionRuntimeSuggestion( existing )
                                          && isNextActionRuntimeSuggestion( aSuggestion );

        if( sameFingerprint || runtimeLeaseConflict )
        {
            existing.m_Status = AI_SUGGESTION_STATUS::Superseded;
            deactivateRuntimePreviewLease( existing );
        }
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
