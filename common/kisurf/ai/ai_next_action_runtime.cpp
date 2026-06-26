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
#include <cstdlib>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

#include <wx/ffile.h>

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


bool jsonIntegerField( const nlohmann::json& aObject, const char* aKey,
                       int64_t& aValue )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_number() )
    {
        return false;
    }

    aValue = aObject[aKey].get<int64_t>();
    return true;
}


bool jsonBBoxFields( const nlohmann::json& aBox, int64_t& aX, int64_t& aY,
                     int64_t& aWidth, int64_t& aHeight )
{
    if( !jsonIntegerField( aBox, "x", aX )
        || !jsonIntegerField( aBox, "y", aY ) )
    {
        return false;
    }

    if( !jsonIntegerField( aBox, "width", aWidth )
        && !jsonIntegerField( aBox, "w", aWidth ) )
    {
        return false;
    }

    if( !jsonIntegerField( aBox, "height", aHeight )
        && !jsonIntegerField( aBox, "h", aHeight ) )
    {
        return false;
    }

    return aWidth >= 0 && aHeight >= 0;
}


void addMainAuxBBoxRelationFact( const nlohmann::json& aIssue,
                                 nlohmann::json& aFact )
{
    if( !aIssue.contains( "main_item_bbox" )
        || !aIssue.contains( "aux_item_bbox" ) )
    {
        return;
    }

    int64_t mainX = 0;
    int64_t mainY = 0;
    int64_t mainWidth = 0;
    int64_t mainHeight = 0;
    int64_t auxX = 0;
    int64_t auxY = 0;
    int64_t auxWidth = 0;
    int64_t auxHeight = 0;

    if( !jsonBBoxFields( aIssue["main_item_bbox"], mainX, mainY,
                         mainWidth, mainHeight )
        || !jsonBBoxFields( aIssue["aux_item_bbox"], auxX, auxY,
                            auxWidth, auxHeight ) )
    {
        return;
    }

    const int64_t mainRight = mainX + mainWidth;
    const int64_t mainBottom = mainY + mainHeight;
    const int64_t auxRight = auxX + auxWidth;
    const int64_t auxBottom = auxY + auxHeight;

    auto axisSpacing =
            []( int64_t aLeft, int64_t aRight, int64_t bLeft, int64_t bRight )
            {
                if( aRight < bLeft )
                    return bLeft - aRight;

                if( bRight < aLeft )
                    return aLeft - bRight;

                return int64_t( 0 );
            };

    const int64_t overlapX =
            std::max<int64_t>( 0, std::min( mainRight, auxRight )
                                      - std::max( mainX, auxX ) );
    const int64_t overlapY =
            std::max<int64_t>( 0, std::min( mainBottom, auxBottom )
                                      - std::max( mainY, auxY ) );
    const int64_t spacingX = axisSpacing( mainX, mainRight, auxX, auxRight );
    const int64_t spacingY =
            axisSpacing( mainY, mainBottom, auxY, auxBottom );
    const double mainCenterX = static_cast<double>( mainX ) + mainWidth / 2.0;
    const double mainCenterY = static_cast<double>( mainY ) + mainHeight / 2.0;
    const double auxCenterX = static_cast<double>( auxX ) + auxWidth / 2.0;
    const double auxCenterY = static_cast<double>( auxY ) + auxHeight / 2.0;

    aFact["main_aux_bbox_relation"] = {
        { "main_center", { { "x", mainCenterX }, { "y", mainCenterY } } },
        { "aux_center", { { "x", auxCenterX }, { "y", auxCenterY } } },
        { "center_delta",
          { { "x", auxCenterX - mainCenterX },
            { "y", auxCenterY - mainCenterY } } },
        { "spacing",
          { { "x", spacingX }, { "y", spacingY },
            { "manhattan", spacingX + spacingY } } },
        { "overlap",
          { { "x", overlapX }, { "y", overlapY },
            { "intersects", overlapX > 0 && overlapY > 0 } } }
    };

    const bool intersects = overlapX > 0 && overlapY > 0;
    const char* preferredAxis =
            intersects
                    ? ( overlapX <= overlapY ? "x" : "y" )
                    : ( spacingX <= spacingY ? "x" : "y" );

    aFact["suggested_fix_facts"] =
            nlohmann::json::array(
                    { { { "kind", "bbox_clearance_review_hint" },
                        { "source", "main_aux_bbox_relation" },
                        { "preferred_axis", preferredAxis },
                        { "current_spacing",
                          { { "x", spacingX }, { "y", spacingY },
                            { "manhattan", spacingX + spacingY } } },
                        { "overlap",
                          { { "x", overlapX }, { "y", overlapY },
                            { "intersects", intersects } } },
                        { "center_delta",
                          { { "x", auxCenterX - mainCenterX },
                            { "y", auxCenterY - mainCenterY } } },
                        { "requires_rule_clearance_context", true } } } );
}


bool isActive( const AI_SUGGESTION_RECORD& aSuggestion )
{
    return aSuggestion.m_Status == AI_SUGGESTION_STATUS::Pending
        || aSuggestion.m_Status == AI_SUGGESTION_STATUS::Previewing;
}


std::string editorKindJsonName( AI_EDITOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return "pcb";

    case AI_EDITOR_KIND::Schematic:
        return "schematic";

    case AI_EDITOR_KIND::Unknown:
    default:
        return "unknown";
    }
}


std::string nextActionStepStatusJsonName( AI_NEXT_ACTION_STEP_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_NEXT_ACTION_STEP_STATUS::Observed:
        return "observed";

    case AI_NEXT_ACTION_STEP_STATUS::Reasoning:
        return "reasoning";

    case AI_NEXT_ACTION_STEP_STATUS::Attempting:
        return "attempting";

    case AI_NEXT_ACTION_STEP_STATUS::Reviewing:
        return "reviewing";

    case AI_NEXT_ACTION_STEP_STATUS::Retrying:
        return "retrying";

    case AI_NEXT_ACTION_STEP_STATUS::Published:
        return "published";

    case AI_NEXT_ACTION_STEP_STATUS::Accepted:
        return "accepted";

    case AI_NEXT_ACTION_STEP_STATUS::Rejected:
        return "rejected";

    case AI_NEXT_ACTION_STEP_STATUS::Expired:
        return "expired";

    case AI_NEXT_ACTION_STEP_STATUS::Superseded:
        return "superseded";

    case AI_NEXT_ACTION_STEP_STATUS::Abandoned:
        return "abandoned";

    case AI_NEXT_ACTION_STEP_STATUS::Cancelled:
        return "cancelled";
    }

    return "unknown";
}


std::string suggestionStatusJsonName( AI_SUGGESTION_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_SUGGESTION_STATUS::Pending:
    case AI_SUGGESTION_STATUS::Previewing:
        return "published";

    case AI_SUGGESTION_STATUS::Accepted:
        return "accepted";

    case AI_SUGGESTION_STATUS::Rejected:
        return "rejected";

    case AI_SUGGESTION_STATUS::Expired:
        return "expired";

    case AI_SUGGESTION_STATUS::Superseded:
        return "superseded";

    case AI_SUGGESTION_STATUS::Abandoned:
        return "abandoned";

    case AI_SUGGESTION_STATUS::Cancelled:
        return "cancelled";
    }

    return "unknown";
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
        if( !issue.is_object() )
            continue;

        if( issue.contains( "blocking" ) && issue["blocking"].is_boolean()
            && issue["blocking"].get<bool>() )
        {
            return true;
        }

        if( issue.contains( "blocks_publish" )
            && issue["blocks_publish"].is_boolean()
            && issue["blocks_publish"].get<bool>() )
        {
            return true;
        }

        if( issue.contains( "severity" ) && issue["severity"].is_string()
            && textContainsBlockingStatus( issue["severity"].get<std::string>() ) )
        {
            return true;
        }
    }

    return false;
}


bool validationSubfactBlocksPublish( const nlohmann::json& aFact )
{
    if( aFact.is_array() )
    {
        for( const nlohmann::json& entry : aFact )
        {
            if( validationSubfactBlocksPublish( entry ) )
                return true;
        }

        return false;
    }

    if( !aFact.is_object() )
        return false;

    if( aFact.contains( "blocking" ) && aFact["blocking"].is_boolean()
        && aFact["blocking"].get<bool>() )
    {
        return true;
    }

    if( aFact.contains( "blocks_publish" ) && aFact["blocks_publish"].is_boolean()
        && aFact["blocks_publish"].get<bool>() )
    {
        return true;
    }

    if( aFact.contains( "status" ) && aFact["status"].is_string() )
    {
        std::string status = aFact["status"].get<std::string>();

        std::transform( status.begin(), status.end(), status.begin(),
                        []( unsigned char c )
                        {
                            return static_cast<char>( std::tolower( c ) );
                        } );

        if( textContainsBlockingStatus( status )
            || status == "stale"
            || status == "out_of_date"
            || status == "dirty"
            || status == "incomplete"
            || status == "required"
            || status == "needs_rebuild" )
        {
            return true;
        }
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

    for( const char* factName : { "rule_load", "connectivity", "refill" } )
    {
        if( aValidation.contains( factName )
            && validationSubfactBlocksPublish( aValidation[factName] ) )
        {
            return true;
        }
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


wxString atomicFailureResultJson( const AI_ATOMIC_EXECUTION_RESULT& aExecution,
                                  const char* aFallbackStatus )
{
    nlohmann::json result =
            nlohmann::json::parse( toUtf8String( aExecution.m_ResultJson ),
                                   nullptr, false );

    if( result.is_discarded() || !result.is_object() )
        result = nlohmann::json::object();

    result["ok"] = false;

    if( !result.contains( "status" ) || !result["status"].is_string()
        || result["status"].get<std::string>().empty() )
    {
        result["status"] = aFallbackStatus;
    }

    if( !result.contains( "error_code" ) || !result["error_code"].is_string()
        || result["error_code"].get<std::string>().empty() )
    {
        result["error_code"] = aExecution.m_ErrorCode.IsEmpty()
                                       ? std::string( aFallbackStatus )
                                       : toUtf8String( aExecution.m_ErrorCode );
    }

    if( !result.contains( "message" ) || !result["message"].is_string()
        || result["message"].get<std::string>().empty() )
    {
        result["message"] = aExecution.m_Message.IsEmpty()
                                    ? std::string( "Atomic operation failed." )
                                    : toUtf8String( aExecution.m_Message );
    }

    return fromUtf8String( result.dump() );
}


bool operationResultBlocksPreviewPublish( const nlohmann::json& aResult )
{
    if( aResult.contains( "ok" ) && aResult["ok"].is_boolean()
        && !aResult["ok"].get<bool>() )
    {
        return true;
    }

    if( aResult.contains( "error_code" ) && aResult["error_code"].is_string()
        && !aResult["error_code"].get<std::string>().empty() )
    {
        return true;
    }

    if( aResult.contains( "status" ) && aResult["status"].is_string()
        && textContainsBlockingStatus( aResult["status"].get<std::string>() ) )
    {
        return true;
    }

    return false;
}


bool sessionJournalBlocksPreviewPublish( const wxString& aJournalJson )
{
    nlohmann::json journal =
            nlohmann::json::parse( toUtf8String( aJournalJson ), nullptr, false );

    if( journal.is_discarded() || !journal.is_object()
        || !journal.contains( "operations" )
        || !journal["operations"].is_array() )
    {
        return true;
    }

    std::optional<bool> finalMutationFailed;

    for( const nlohmann::json& operation : journal["operations"] )
    {
        if( !operation.is_object()
            || !operation.contains( "result" )
            || !operation["result"].is_object() )
        {
            continue;
        }

        const nlohmann::json& result = operation["result"];

        const bool isMutation =
                operation.contains( "is_mutation" )
                && operation["is_mutation"].is_boolean()
                && operation["is_mutation"].get<bool>();

        const bool resultBlocks =
                operationResultBlocksPreviewPublish( result );

        if( isMutation )
        {
            finalMutationFailed = resultBlocks;
            continue;
        }

        if( resultBlocks )
        {
            return true;
        }
    }

    return finalMutationFailed.value_or( false );
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


bool reviewProviderToolResultsBlockPreviewPublish( const wxString& aReviewJson )
{
    nlohmann::json review =
            nlohmann::json::parse( toUtf8String( aReviewJson ), nullptr, false );

    if( review.is_discarded() || !review.is_object()
        || !review.contains( "provider_tool_results" )
        || !review["provider_tool_results"].is_array() )
    {
        return false;
    }

    for( const nlohmann::json& toolRecord : review["provider_tool_results"] )
    {
        if( !toolRecord.is_object() )
            continue;

        if( toolRecord.contains( "allowed" ) && toolRecord["allowed"].is_boolean()
            && !toolRecord["allowed"].get<bool>() )
        {
            return true;
        }

        if( toolRecord.contains( "executed" ) && toolRecord["executed"].is_boolean()
            && !toolRecord["executed"].get<bool>() )
        {
            return true;
        }

        if( toolRecord.contains( "error_code" )
            && toolRecord["error_code"].is_string()
            && !toolRecord["error_code"].get<std::string>().empty() )
        {
            return true;
        }

        if( toolRecord.contains( "result" )
            && operationResultBlocksPreviewPublish( toolRecord["result"] ) )
        {
            return true;
        }
    }

    return false;
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


bool isPlacementRepairFootprintOrientationTool( const std::string& aToolName )
{
    return aToolName == "placement.repair_footprint_orientation";
}


bool isRoutingRepairSegmentTool( const std::string& aToolName )
{
    return aToolName == "routing.repair_segment";
}


bool isRoutingRepairPolylineTool( const std::string& aToolName )
{
    return aToolName == "routing.repair_polyline";
}


bool isRoutingRepairBusSegmentsTool( const std::string& aToolName )
{
    return aToolName == "routing.repair_bus_segments";
}


bool isRoutingParallelCandidateTool( const std::string& aToolName )
{
    return aToolName == "routing.generate_parallel_segment_candidates";
}


bool isRoutingBusCandidateTool( const std::string& aToolName )
{
    return aToolName == "routing.generate_bus_segment_candidates";
}


bool isRoutingReplacePathCandidateTool( const std::string& aToolName )
{
    return aToolName == "routing.generate_replace_path_candidates";
}


bool isRoutingConstraintAwareRerouteCandidateTool( const std::string& aToolName )
{
    return aToolName == "routing.generate_constraint_aware_reroute_candidates";
}


bool isPlacementFootprintTransformCandidateTool( const std::string& aToolName )
{
    return aToolName == "placement.generate_footprint_transform_candidates";
}


bool isPlacementFootprintOrientationCandidateTool( const std::string& aToolName )
{
    return aToolName == "placement.generate_footprint_orientation_candidates";
}


bool isRepairWrapperTool( const std::string& aToolName )
{
    return isSurfaceRepairPatchTool( aToolName )
           || isPlacementRepairViaTool( aToolName )
           || isPlacementRepairMoveItemsTool( aToolName )
           || isPlacementRepairFootprintOrientationTool( aToolName )
           || isRoutingRepairSegmentTool( aToolName )
           || isRoutingRepairPolylineTool( aToolName );
}


bool isHiddenMutationBatchTool( const std::string& aToolName )
{
    return isBoundedPlanMutationTool( aToolName )
           || isRepairWrapperTool( aToolName )
           || isRoutingRepairBusSegmentsTool( aToolName );
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


std::optional<std::string> firstStringField(
        const nlohmann::json& aObject,
        std::initializer_list<const char*> aKeys )
{
    if( !aObject.is_object() )
        return std::nullopt;

    for( const char* key : aKeys )
    {
        if( aObject.contains( key ) && aObject[key].is_string() )
            return aObject[key].get<std::string>();
    }

    return std::nullopt;
}


bool visualTargetStringMatchesScope(
        const nlohmann::json& aVisualTarget,
        std::initializer_list<const char*> aActualKeys,
        const nlohmann::json& aScope,
        std::initializer_list<const char*> aScopeKeys )
{
    std::optional<std::string> expected = firstStringField( aScope, aScopeKeys );

    if( !expected || expected->empty() )
        return true;

    std::optional<std::string> actual =
            firstStringField( aVisualTarget, aActualKeys );

    if( !actual || actual->empty() )
        return false;

    return lowerAscii( *actual ) == lowerAscii( *expected );
}


bool surfacePatchVisualTargetMatchesScope(
        const nlohmann::json& aVisualTarget,
        const nlohmann::json& aScope )
{
    if( !aVisualTarget.is_object() || !aScope.is_object() )
        return true;

    if( !visualTargetStringMatchesScope( aVisualTarget, { "surface_id" },
                                         aScope, { "surface_id",
                                                   "panel_id" } ) )
    {
        return false;
    }

    if( !visualTargetStringMatchesScope( aVisualTarget, { "table_id" },
                                         aScope, { "table_id", "table" } ) )
    {
        return false;
    }

    if( !visualTargetStringMatchesScope( aVisualTarget, { "row_id", "row" },
                                         aScope, { "row_id", "row" } ) )
    {
        return false;
    }

    if( !visualTargetStringMatchesScope( aVisualTarget,
                                         { "column_id", "column" },
                                         aScope,
                                         { "column_id", "column" } ) )
    {
        return false;
    }

    if( !visualTargetStringMatchesScope( aVisualTarget,
                                         { "field_id", "field" },
                                         aScope,
                                         { "field_id", "field",
                                           "property_id", "property" } ) )
    {
        return false;
    }

    return true;
}


nlohmann::json parseObjectBody( const wxString& aBody );


bool decisionSurfaceTargetScopeMatchesRenderedPatchPreviews(
        const wxString& aDecisionJson,
        const wxString& aReviewJson )
{
    nlohmann::json decision =
            nlohmann::json::parse( toUtf8String( aDecisionJson ), nullptr, false );

    if( decision.is_discarded() || !decision.is_object()
        || !decision.contains( "target_scope" )
        || !decision["target_scope"].is_object() )
    {
        return true;
    }

    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return true;
    }

    const nlohmann::json& scope = decision["target_scope"];

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() || !toolRecord.contains( "result" )
            || !toolRecord["result"].is_object() )
        {
            continue;
        }

        const nlohmann::json& result = toolRecord["result"];

        if( !result.contains( "surface_patch_previews" )
            || !result["surface_patch_previews"].is_array() )
        {
            continue;
        }

        for( const nlohmann::json& preview : result["surface_patch_previews"] )
        {
            if( !preview.is_object()
                || !preview.contains( "surface_patch_diff_entries" )
                || !preview["surface_patch_diff_entries"].is_array() )
            {
                continue;
            }

            for( const nlohmann::json& entry :
                 preview["surface_patch_diff_entries"] )
            {
                if( !entry.is_object() || !entry.contains( "visual_target" )
                    || !entry["visual_target"].is_object() )
                {
                    continue;
                }

                if( !surfacePatchVisualTargetMatchesScope(
                            entry["visual_target"], scope ) )
                {
                    return false;
                }
            }
        }
    }

    return true;
}


bool jsonContainsValidationBeforePublishHint( const nlohmann::json& aValue )
{
    if( aValue.is_object() )
    {
        if( aValue.contains( "validation_hint" )
            && aValue["validation_hint"].is_string()
            && aValue["validation_hint"].get<std::string>()
                       == "run_validate_hidden_attempt_before_publish" )
        {
            return true;
        }

        for( const auto& entry : aValue.items() )
        {
            if( jsonContainsValidationBeforePublishHint( entry.value() ) )
                return true;
        }

        return false;
    }

    if( aValue.is_array() )
    {
        for( const nlohmann::json& entry : aValue )
        {
            if( jsonContainsValidationBeforePublishHint( entry ) )
                return true;
        }
    }

    return false;
}


bool toolRecordIsExecutedValidation( const nlohmann::json& aToolRecord )
{
    if( !aToolRecord.is_object()
        || !aToolRecord.value( "allowed", false )
        || !aToolRecord.value( "executed", false ) )
    {
        return false;
    }

    const std::string providerToolName =
            aToolRecord.value( "tool_name", std::string() );

    if( providerToolName == "validate_hidden_attempt"
        || providerToolName == "validate.hidden_attempt" )
    {
        return true;
    }

    if( !aToolRecord.contains( "result" )
        || !aToolRecord["result"].is_object() )
    {
        return false;
    }

    return aToolRecord["result"].value( "tool", std::string() )
           == "validate.hidden_attempt";
}


bool toolRecordIsExecutedRender( const nlohmann::json& aToolRecord )
{
    if( !aToolRecord.is_object()
        || !aToolRecord.value( "allowed", false )
        || !aToolRecord.value( "executed", false ) )
    {
        return false;
    }

    const std::string providerToolName =
            aToolRecord.value( "tool_name", std::string() );

    if( providerToolName == "render_hidden_attempt"
        || providerToolName == "render.hidden_attempt" )
    {
        return true;
    }

    if( !aToolRecord.contains( "result" )
        || !aToolRecord["result"].is_object() )
    {
        return false;
    }

    return aToolRecord["result"].value( "tool", std::string() )
           == "render.hidden_attempt";
}


bool toolRecordIsExecutedHiddenMutation( const nlohmann::json& aToolRecord )
{
    if( !aToolRecord.is_object()
        || !aToolRecord.value( "allowed", false )
        || !aToolRecord.value( "executed", false )
        || !aToolRecord.contains( "result" )
        || !aToolRecord["result"].is_object() )
    {
        return false;
    }

    const nlohmann::json& result = aToolRecord["result"];
    const std::string     resultTool =
            result.value( "tool", std::string() );

    if( resultTool == "shadow.apply_candidate" )
    {
        return result.value( "status", std::string() ) == "candidate_applied"
               && result.value( "mutation_applied", false );
    }

    return isHiddenMutationBatchTool( resultTool )
           && result.value( "status", std::string() ) == "script_plan_executed"
           && result.value( "mutation_applied", false );
}


std::string toolRecordRolledBackToolCallId( const nlohmann::json& aToolRecord )
{
    if( !aToolRecord.is_object()
        || !aToolRecord.value( "allowed", false )
        || !aToolRecord.value( "executed", false )
        || !aToolRecord.contains( "result" )
        || !aToolRecord["result"].is_object() )
    {
        return std::string();
    }

    const nlohmann::json& result = aToolRecord["result"];

    if( result.value( "tool", std::string() ) != "rollback.attempt"
        || result.value( "status", std::string() ) != "rolled_back" )
    {
        return std::string();
    }

    return result.value( "rolled_back_tool_call_id", std::string() );
}


bool reviewRenderHintsSatisfied( const wxString& aReviewJson )
{
    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return true;
    }

    bool                  renderHintActive = false;
    bool                  renderPendingWithoutToolId = false;
    std::set<std::string> pendingMutationToolCallIds;

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() )
            continue;

        if( toolRecordIsExecutedRender( toolRecord ) )
        {
            pendingMutationToolCallIds.clear();
            renderPendingWithoutToolId = false;
            continue;
        }

        const std::string rolledBackToolCallId =
                toolRecordRolledBackToolCallId( toolRecord );

        if( !rolledBackToolCallId.empty() )
            pendingMutationToolCallIds.erase( rolledBackToolCallId );

        if( toolRecord.contains( "result" )
            && jsonContainsValidationBeforePublishHint( toolRecord["result"] ) )
            renderHintActive = true;

        if( renderHintActive && toolRecordIsExecutedHiddenMutation( toolRecord ) )
        {
            const std::string toolCallId =
                    toolRecord.value( "tool_call_id", std::string() );

            if( toolCallId.empty() )
                renderPendingWithoutToolId = true;
            else
                pendingMutationToolCallIds.insert( toolCallId );
        }
    }

    return pendingMutationToolCallIds.empty()
           && !renderPendingWithoutToolId;
}


bool reviewHiddenMutationRenderFreshnessSatisfied( const wxString& aReviewJson )
{
    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return true;
    }

    bool                  renderPendingWithoutToolId = false;
    std::set<std::string> pendingMutationToolCallIds;

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() )
            continue;

        if( toolRecordIsExecutedRender( toolRecord ) )
        {
            pendingMutationToolCallIds.clear();
            renderPendingWithoutToolId = false;
            continue;
        }

        const std::string rolledBackToolCallId =
                toolRecordRolledBackToolCallId( toolRecord );

        if( !rolledBackToolCallId.empty() )
            pendingMutationToolCallIds.erase( rolledBackToolCallId );

        if( toolRecordIsExecutedHiddenMutation( toolRecord ) )
        {
            const std::string toolCallId =
                    toolRecord.value( "tool_call_id", std::string() );

            if( toolCallId.empty() )
                renderPendingWithoutToolId = true;
            else
                pendingMutationToolCallIds.insert( toolCallId );
        }
    }

    return pendingMutationToolCallIds.empty()
           && !renderPendingWithoutToolId;
}


bool reviewHiddenMutationValidationFreshnessSatisfied( const wxString& aReviewJson )
{
    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return true;
    }

    bool                  validationPendingWithoutToolId = false;
    std::set<std::string> pendingMutationToolCallIds;

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() )
            continue;

        if( toolRecordIsExecutedValidation( toolRecord ) )
        {
            pendingMutationToolCallIds.clear();
            validationPendingWithoutToolId = false;
            continue;
        }

        const std::string rolledBackToolCallId =
                toolRecordRolledBackToolCallId( toolRecord );

        if( !rolledBackToolCallId.empty() )
            pendingMutationToolCallIds.erase( rolledBackToolCallId );

        if( toolRecordIsExecutedHiddenMutation( toolRecord ) )
        {
            const std::string toolCallId =
                    toolRecord.value( "tool_call_id", std::string() );

            if( toolCallId.empty() )
                validationPendingWithoutToolId = true;
            else
                pendingMutationToolCallIds.insert( toolCallId );
        }
    }

    return pendingMutationToolCallIds.empty()
           && !validationPendingWithoutToolId;
}


bool reviewValidationHintsSatisfied( const wxString& aReviewJson )
{
    nlohmann::json reviewTrace = parseObjectBody( aReviewJson );

    if( !reviewTrace.contains( "provider_tool_results" )
        || !reviewTrace["provider_tool_results"].is_array() )
    {
        return true;
    }

    bool                  validationHintActive = false;
    bool                  validationPendingWithoutToolId = false;
    std::set<std::string> pendingMutationToolCallIds;

    for( const nlohmann::json& toolRecord : reviewTrace["provider_tool_results"] )
    {
        if( !toolRecord.is_object() )
            continue;

        if( toolRecordIsExecutedValidation( toolRecord ) )
        {
            pendingMutationToolCallIds.clear();
            validationPendingWithoutToolId = false;
            continue;
        }

        const std::string rolledBackToolCallId =
                toolRecordRolledBackToolCallId( toolRecord );

        if( !rolledBackToolCallId.empty() )
            pendingMutationToolCallIds.erase( rolledBackToolCallId );

        if( toolRecord.contains( "result" )
            && jsonContainsValidationBeforePublishHint( toolRecord["result"] ) )
            validationHintActive = true;

        if( validationHintActive
            && toolRecordIsExecutedHiddenMutation( toolRecord ) )
        {
            const std::string toolCallId =
                    toolRecord.value( "tool_call_id", std::string() );

            if( toolCallId.empty() )
                validationPendingWithoutToolId = true;
            else
                pendingMutationToolCallIds.insert( toolCallId );
        }
    }

    return pendingMutationToolCallIds.empty()
           && !validationPendingWithoutToolId;
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


bool hasNextActionRuntimePreviewArtifact(
        const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    return provenance.is_object()
           && provenance.value( "runtime", std::string() ) == "next_action"
           && provenance.contains( "attempt" )
           && provenance["attempt"].is_object()
           && provenance["attempt"].contains( "session_journal" )
           && provenance["attempt"]["session_journal"].is_object();
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


nlohmann::json parseJsonText( const wxString& aBody, nlohmann::json aFallback )
{
    if( aBody.IsEmpty() )
        return aFallback;

    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aBody ), nullptr, false );

    if( parsed.is_discarded() )
        return aFallback;

    return parsed;
}


nlohmann::json semanticEventJson( const AI_SEMANTIC_EVENT& aEvent )
{
    return {
        { "semantic_event_id", aEvent.m_Id },
        { "slot_id", toUtf8String( aEvent.m_SlotId ) },
        { "kind", toUtf8String( aEvent.m_Kind ) },
        { "reason", toUtf8String( aEvent.m_Reason ) },
        { "editor", editorKindJsonName( aEvent.m_EditorKind ) },
        { "context_version", parseObjectBody( aEvent.m_ContextVersion.AsJsonText() ) },
        { "activity",
          { { "sequence", aEvent.m_Activity.m_Sequence },
            { "action", toUtf8String( aEvent.m_Activity.m_ActionName ) },
            { "message", toUtf8String( aEvent.m_Activity.m_Message ) } } }
    };
}


nlohmann::json candidateReplayJson( const AI_SUGGESTION_RECORD& aCandidate )
{
    nlohmann::json candidate =
            { { "title", toUtf8String( aCandidate.m_Title ) },
              { "body", toUtf8String( aCandidate.m_Body ) },
              { "context_kind", toUtf8String( aCandidate.m_ContextKind ) },
              { "arguments", parseObjectBody( aCandidate.m_ArgumentsJson ) },
              { "context_details", parseObjectBody( aCandidate.m_ContextDetailsJson ) } };

    return candidate;
}


nlohmann::json attemptReplayJson( const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    return {
        { "attempt_id", aAttempt.m_Id },
        { "runtime_step_id", aAttempt.m_RuntimeStepId },
        { "candidate_index", aAttempt.m_CandidateIndex },
        { "hidden_session_id", aAttempt.m_HiddenSessionId },
        { "hidden_step_id", aAttempt.m_HiddenStepId },
        { "base_checkpoint_id", aAttempt.m_BaseCheckpointId },
        { "candidate", candidateReplayJson( aAttempt.m_Candidate ) },
        { "hidden_attempt_journal", parseObjectBody( aAttempt.m_JournalJson ) },
        { "render_outputs", parseObjectBody( aAttempt.m_RenderOutputsJson ) },
        { "validation_facts", parseObjectBody( aAttempt.m_ValidationFactsJson ) },
        { "rollback", parseObjectBody( aAttempt.m_RollbackJson ) },
        { "budget_counters", parseObjectBody( aAttempt.m_BudgetCounters.AsJsonText() ) },
        { "provenance", parseObjectBody( aAttempt.m_ProvenanceJson ) }
    };
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


std::vector<AI_TOOL_CALL_RECORD> toolCallRecordsFromReviewJson(
        const wxString& aReviewJson )
{
    std::vector<AI_TOOL_CALL_RECORD> records;
    nlohmann::json review = parseObjectBody( aReviewJson );

    if( !review.contains( "provider_tool_results" )
        || !review["provider_tool_results"].is_array() )
    {
        return records;
    }

    for( const nlohmann::json& item : review["provider_tool_results"] )
    {
        if( !item.is_object() )
            continue;

        AI_TOOL_CALL_RECORD record;

        if( item.contains( "request_id" ) && item["request_id"].is_number_unsigned() )
            record.m_RequestId = item["request_id"].get<uint64_t>();

        record.m_ToolCallId = fromUtf8String(
                item.value( "tool_call_id", std::string() ) );
        record.m_ToolName = fromUtf8String(
                item.value( "tool_name", std::string() ) );
        record.m_ArgumentsJson = fromUtf8String(
                item.value( "arguments_json", std::string( "{}" ) ) );
        record.m_Allowed = item.value( "allowed", false );
        record.m_Executed = item.value( "executed", false );
        record.m_ErrorCode = fromUtf8String(
                item.value( "error_code", std::string() ) );
        record.m_Message = fromUtf8String(
                item.value( "message", std::string() ) );

        if( item.contains( "result" ) )
            record.m_ResultJson = fromUtf8String( item["result"].dump() );
        else
            record.m_ResultJson = wxS( "{}" );

        records.push_back( std::move( record ) );
    }

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


struct ATTEMPT_POLICY_SIGNALS
{
    size_t m_RecentRejectHistoryCount = 0;
    size_t m_ContextChurnHistoryCount = 0;
    size_t m_LatencyOverBudgetHistoryCount = 0;
    size_t m_CandidateQualityFailureHistoryCount = 0;
};


ATTEMPT_BUDGET_POLICY attemptPolicyForWorkState( const wxString& aWorkState )
{
    const wxString workState = aWorkState.Lower();

    if( workState == wxS( "routing" ) )
        return { 5, 12, 16, 6, 6, 250 };

    if( workState == wxS( "layout" ) || workState == wxS( "placement" ) )
        return { 3, 12, 8, 4, 4, 250 };

    if( workState == wxS( "autofill" ) || workState == wxS( "panel" )
        || workState == wxS( "structured_surface" ) )
    {
        return { 2, 6, 8, 3, 3, 250 };
    }

    return {};
}


ATTEMPT_BUDGET_POLICY adjustedAttemptPolicyForWorkState(
        const wxString& aWorkState,
        const ATTEMPT_POLICY_SIGNALS& aSignals )
{
    ATTEMPT_BUDGET_POLICY policy = attemptPolicyForWorkState( aWorkState );

    if( aSignals.m_RecentRejectHistoryCount > 0
        || aSignals.m_ContextChurnHistoryCount > 0
        || aSignals.m_LatencyOverBudgetHistoryCount > 0
        || aSignals.m_CandidateQualityFailureHistoryCount > 0 )
    {
        policy.m_MaxAttempts = std::min<size_t>( policy.m_MaxAttempts, 1 );
    }

    return policy;
}


size_t attemptLimitForWorkState( const wxString& aWorkState,
                                 const ATTEMPT_POLICY_SIGNALS& aSignals )
{
    return adjustedAttemptPolicyForWorkState( aWorkState, aSignals ).m_MaxAttempts;
}


wxString normalizedAttemptPolicyFamily( const wxString& aWorkState )
{
    const wxString workState = aWorkState.Lower();

    if( workState == wxS( "layout" ) || workState == wxS( "placement" ) )
        return wxS( "placement" );

    if( workState == wxS( "autofill" ) || workState == wxS( "panel" )
        || workState == wxS( "structured_surface" ) )
    {
        return wxS( "structured_surface" );
    }

    return workState;
}


nlohmann::json attemptPolicyJson( const wxString& aWorkState,
                                  const ATTEMPT_POLICY_SIGNALS& aSignals = {} )
{
    const ATTEMPT_BUDGET_POLICY basePolicy = attemptPolicyForWorkState( aWorkState );
    const ATTEMPT_BUDGET_POLICY policy =
            adjustedAttemptPolicyForWorkState( aWorkState, aSignals );

    nlohmann::json adjustments = nlohmann::json::array();

    if( aSignals.m_RecentRejectHistoryCount > 0
        && policy.m_MaxAttempts < basePolicy.m_MaxAttempts )
    {
        adjustments.push_back( "recent_reject_history" );
    }

    if( aSignals.m_ContextChurnHistoryCount > 0
        && policy.m_MaxAttempts < basePolicy.m_MaxAttempts )
    {
        adjustments.push_back( "context_churn_history" );
    }

    if( aSignals.m_LatencyOverBudgetHistoryCount > 0
        && policy.m_MaxAttempts < basePolicy.m_MaxAttempts )
    {
        adjustments.push_back( "latency_over_budget_history" );
    }

    if( aSignals.m_CandidateQualityFailureHistoryCount > 0
        && policy.m_MaxAttempts < basePolicy.m_MaxAttempts )
    {
        adjustments.push_back( "candidate_quality_history" );
    }

    return { { "work_state", toUtf8String( aWorkState ) },
             { "max_attempts", policy.m_MaxAttempts },
             { "max_tool_rounds", policy.m_MaxToolRounds },
             { "max_mutations", policy.m_MaxMutations },
             { "max_render_count", policy.m_MaxRenderCount },
             { "max_validation_count", policy.m_MaxValidationCount },
             { "max_wall_time_ms", policy.m_MaxWallTimeMs },
             { "base_max_attempts", basePolicy.m_MaxAttempts },
             { "base_max_tool_rounds", basePolicy.m_MaxToolRounds },
             { "base_max_mutations", basePolicy.m_MaxMutations },
             { "base_max_render_count", basePolicy.m_MaxRenderCount },
             { "base_max_validation_count", basePolicy.m_MaxValidationCount },
             { "base_max_wall_time_ms", basePolicy.m_MaxWallTimeMs },
             { "reject_history_count", aSignals.m_RecentRejectHistoryCount },
             { "context_churn_count", aSignals.m_ContextChurnHistoryCount },
             { "latency_over_budget_count",
               aSignals.m_LatencyOverBudgetHistoryCount },
             { "candidate_quality_failure_count",
               aSignals.m_CandidateQualityFailureHistoryCount },
             { "adjustments", std::move( adjustments ) },
             { "policy_owner", "native_runtime" } };
}


wxString workStateForCandidateSourceToolName( const std::string& aSelectedTool )
{
    if( stringStartsWith( aSelectedTool, "routing." ) )
        return wxS( "routing" );

    if( stringStartsWith( aSelectedTool, "surface." ) )
        return wxS( "structured_surface" );

    if( stringStartsWith( aSelectedTool, "placement." ) )
        return wxS( "placement" );

    return wxS( "unknown" );
}


wxString budgetPolicyWorkStateForCandidate(
        const AI_SUGGESTION_RECORD& aCandidate )
{
    const std::string selectedTool = candidateSourceToolName( aCandidate );

    return workStateForCandidateSourceToolName( selectedTool );
}


ATTEMPT_POLICY_SIGNALS attemptPolicySignalsForWorkState(
        const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
        const std::vector<AI_NEXT_ACTION_ATTEMPT_RECORD>& aAttempts,
        const std::vector<AI_NEXT_ACTION_RUNTIME_STEP>& aSteps,
        const wxString& aWorkState )
{
    ATTEMPT_POLICY_SIGNALS signals;
    const wxString targetFamily = normalizedAttemptPolicyFamily( aWorkState );

    for( const AI_SUGGESTION_RECORD& suggestion : aSuggestions )
    {
        if( !isNextActionRuntimeSuggestion( suggestion ) )
            continue;

        const wxString suggestionFamily = normalizedAttemptPolicyFamily(
                budgetPolicyWorkStateForCandidate( suggestion ) );

        if( suggestionFamily != targetFamily )
            continue;

        if( suggestion.m_Status == AI_SUGGESTION_STATUS::Rejected )
            ++signals.m_RecentRejectHistoryCount;

        if( suggestion.m_Status == AI_SUGGESTION_STATUS::Expired
            || suggestion.m_Status == AI_SUGGESTION_STATUS::Superseded )
        {
            ++signals.m_ContextChurnHistoryCount;
        }
    }

    for( const AI_NEXT_ACTION_ATTEMPT_RECORD& attempt : aAttempts )
    {
        const wxString attemptWorkState =
                budgetPolicyWorkStateForCandidate( attempt.m_Candidate );

        if( normalizedAttemptPolicyFamily( attemptWorkState ) != targetFamily )
            continue;

        const ATTEMPT_BUDGET_POLICY policy =
                attemptPolicyForWorkState( attemptWorkState );

        if( attempt.m_BudgetCounters.m_WallTimeMs > policy.m_MaxWallTimeMs )
            ++signals.m_LatencyOverBudgetHistoryCount;
    }

    for( const AI_NEXT_ACTION_RUNTIME_STEP& step : aSteps )
    {
        if( step.m_Status != AI_NEXT_ACTION_STEP_STATUS::Abandoned )
            continue;

        nlohmann::json observation = parseObjectBody( step.m_ObservationPacketJson );
        wxString       stepWorkState;

        if( observation.contains( "structured_facts" )
            && observation["structured_facts"].is_object()
            && observation["structured_facts"].contains( "work_state" )
            && observation["structured_facts"]["work_state"].is_string() )
        {
            stepWorkState = fromUtf8String(
                    observation["structured_facts"]["work_state"]
                            .get<std::string>() );
        }
        else if( observation.contains( "kind" )
                 && observation["kind"].is_string() )
        {
            stepWorkState =
                    fromUtf8String( observation["kind"].get<std::string>() );
        }

        if( normalizedAttemptPolicyFamily( stepWorkState ) != targetFamily )
            continue;

        nlohmann::json review = parseObjectBody( step.m_ReviewDecisionJson );

        if( !review.contains( "preview_gate_result" )
            || !review["preview_gate_result"].is_object()
            || !review["preview_gate_result"].contains( "reasons" )
            || !review["preview_gate_result"]["reasons"].is_array() )
        {
            continue;
        }

        for( const nlohmann::json& reason : review["preview_gate_result"]["reasons"] )
        {
            if( !reason.is_string() )
                continue;

            const std::string reasonText = reason.get<std::string>();

            if( reasonText == "render_gate_failed"
                || reasonText == "validation_gate_failed"
                || reasonText == "semantic_relevance_failed"
                || reasonText == "surface_patch_target_scope_failed" )
            {
                ++signals.m_CandidateQualityFailureHistoryCount;
                break;
            }
        }
    }

    return signals;
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


bool attemptHasReviewToolRoundsRemaining(
        const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
{
    const ATTEMPT_BUDGET_POLICY policy = attemptPolicyForWorkState(
            budgetPolicyWorkStateForCandidate( aAttempt.m_Candidate ) );
    return aAttempt.m_BudgetCounters.m_ToolRoundCount < policy.m_MaxToolRounds;
}


bool reviewDeclaresRepairableGateFailure( const wxString& aReviewJson,
                                          const wxString& aReason )
{
    nlohmann::json review = parseObjectBody( aReviewJson );

    if( !review.contains( "repairable_gate_failures" )
        || !review["repairable_gate_failures"].is_array() )
    {
        return false;
    }

    const std::string reason = toUtf8String( aReason );

    for( const nlohmann::json& entry : review["repairable_gate_failures"] )
    {
        if( entry.is_string() && entry.get<std::string>() == reason )
            return true;
    }

    return false;
}


bool previewGateFailureCanReenterReview(
        const AI_NEXT_ACTION_GATE_RESULT& aGate,
        const wxString& aReviewJson )
{
    if( aGate.m_Allowed )
        return false;

    bool hasRepairableReason = false;

    for( const wxString& reason : aGate.m_Reasons )
    {
        if( reason == wxS( "context_drift" )
            || reason == wxS( "budget_policy_failed" )
            || reason == wxS( "semantic_relevance_failed" )
            || reason == wxS( "surface_patch_target_scope_failed" ) )
        {
            return false;
        }

        if( reason == wxS( "render_freshness_failed" )
            || reason == wxS( "render_hint_not_satisfied" )
            || reason == wxS( "validation_freshness_failed" )
            || reason == wxS( "validation_hint_not_satisfied" ) )
        {
            hasRepairableReason = true;
        }

        if( reason == wxS( "render_gate_failed" )
            || reason == wxS( "validation_gate_failed" ) )
        {
            if( reviewDeclaresRepairableGateFailure( aReviewJson, reason ) )
                hasRepairableReason = true;
            else
                return false;
        }
    }

    return hasRepairableReason;
}


AI_TOOL_CALL_RECORD previewGateFeedbackToolResult(
        uint64_t aRequestId,
        const AI_NEXT_ACTION_PUBLISH_DECISION& aPublish,
        size_t aFeedbackRound )
{
    AI_TOOL_CALL_RECORD feedback;
    feedback.m_RequestId = aRequestId;
    feedback.m_ToolCallId = wxString::Format(
            wxS( "runtime_preview_gate_feedback_%llu" ),
            static_cast<unsigned long long>( aFeedbackRound ) );
    feedback.m_ToolName = wxS( "preview_gate_feedback" );
    feedback.m_ArgumentsJson = wxS( "{}" );
    feedback.m_Allowed = true;
    feedback.m_Executed = true;
    feedback.m_Message = wxS( "preview gate rejected publish; continue review" );

    nlohmann::json result =
            { { "tool", "preview.gate_feedback" },
              { "direct_publish", false },
              { "publish_allowed", false },
              { "preview_gate_result",
                parseObjectBody( aPublish.m_GateResult.AsJsonText() ) },
              { "publish_decision", parseObjectBody( aPublish.m_RawJson ) } };
    feedback.m_ResultJson = fromUtf8String( result.dump() );
    return feedback;
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
              { "confidence", aAnchor.m_Confidence },
              { "provenance",
                { { "source", "context_anchor" },
                  { "editor_kind", editorKindJsonName( aAnchor.m_EditorKind ) },
                  { "has_position", aAnchor.m_HasPosition },
                  { "details_present", !aAnchor.m_DetailsJson.IsEmpty() } } } };

    if( !aAnchor.m_DetailsJson.IsEmpty() )
    {
        nlohmann::json details = objectFromJsonText( aAnchor.m_DetailsJson );

        if( details.is_object() )
        {
            if( details.contains( "source" ) && details["source"].is_string() )
                record["provenance"]["details_source"] = details["source"];

            record["details"] = std::move( details );
        }
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


nlohmann::json cappedJsonArray( const nlohmann::json& aSource, size_t aLimit,
                                bool& aTruncated )
{
    nlohmann::json result = nlohmann::json::array();

    if( !aSource.is_array() )
        return result;

    for( const nlohmann::json& entry : aSource )
    {
        if( result.size() >= aLimit )
        {
            aTruncated = true;
            break;
        }

        result.push_back( entry );
    }

    return result;
}


nlohmann::json effectiveConstraintSummaryJson( const nlohmann::json& aEffective )
{
    if( !aEffective.is_object() )
        return nlohmann::json::object();

    nlohmann::json summary = nlohmann::json::object();

    copyJsonFieldIfPresent( summary, aEffective, "drc_engine_present" );
    copyJsonFieldIfPresent( summary, aEffective, "rules_valid" );
    copyJsonFieldIfPresent( summary, aEffective, "geometry_dependent_rules_present" );
    copyJsonFieldIfPresent( summary, aEffective, "worst_constraint_sample_truncated" );
    copyJsonFieldIfPresent( summary, aEffective,
                            "pair_effective_constraint_sample_truncated" );

    bool worstTruncated = false;
    bool pairTruncated = false;
    bool coverageTruncated = false;
    summary["worst_constraints"] =
            cappedJsonArray( aEffective.value( "worst_constraints",
                                               nlohmann::json::array() ),
                             8, worstTruncated );
    summary["pair_effective_constraints"] =
            cappedJsonArray( aEffective.value( "pair_effective_constraints",
                                               nlohmann::json::array() ),
                             8, pairTruncated );
    summary["geometry_specific_rule_coverage"] =
            cappedJsonArray( aEffective.value( "geometry_specific_rule_coverage",
                                               nlohmann::json::array() ),
                             8, coverageTruncated );

    if( worstTruncated )
        summary["worst_constraint_sample_truncated"] = true;

    if( pairTruncated )
        summary["pair_effective_constraint_sample_truncated"] = true;

    if( coverageTruncated )
        summary["geometry_specific_rule_coverage_truncated"] = true;

    return summary;
}


nlohmann::json constraintSummaryJson( const AI_CONTEXT_SNAPSHOT& aContext )
{
    if( aContext.m_Summary.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json boardSummary = objectFromJsonText( aContext.m_Summary );

    if( !boardSummary.is_object() || !boardSummary.contains( "constraint_facts" )
        || !boardSummary["constraint_facts"].is_object() )
    {
        return nlohmann::json::object();
    }

    const nlohmann::json& constraints = boardSummary["constraint_facts"];
    nlohmann::json        summary =
            { { "source", "context_summary.constraint_facts" } };

    copyJsonFieldIfPresent( summary, constraints, "minimums" );
    copyJsonFieldIfPresent( summary, constraints, "rule_area_count" );
    copyJsonFieldIfPresent( summary, constraints, "keepout_count" );
    copyJsonFieldIfPresent( summary, constraints, "keepout_sample_truncated" );

    if( constraints.contains( "effective_constraints" ) )
    {
        nlohmann::json effective =
                effectiveConstraintSummaryJson( constraints["effective_constraints"] );

        if( !effective.empty() )
            summary["effective_constraints"] = std::move( effective );
    }

    return summary;
}


nlohmann::json connectivitySummaryJson( const AI_CONTEXT_SNAPSHOT& aContext )
{
    if( aContext.m_Summary.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json boardSummary = objectFromJsonText( aContext.m_Summary );

    if( !boardSummary.is_object() || !boardSummary.contains( "connectivity_summary" )
        || !boardSummary["connectivity_summary"].is_object() )
    {
        return nlohmann::json::object();
    }

    const nlohmann::json& connectivity = boardSummary["connectivity_summary"];
    nlohmann::json        summary =
            { { "source", "context_summary.connectivity_summary" } };

    copyJsonFieldIfPresent( summary, connectivity, "present" );
    copyJsonFieldIfPresent( summary, connectivity, "net_count" );
    copyJsonFieldIfPresent( summary, connectivity, "node_count" );
    copyJsonFieldIfPresent( summary, connectivity, "pad_count" );
    copyJsonFieldIfPresent( summary, connectivity, "ratsnest_unconnected_count" );
    copyJsonFieldIfPresent( summary, connectivity, "visible_ratsnest_unconnected_count" );
    copyJsonFieldIfPresent( summary, connectivity, "local_ratsnest_line_count" );
    copyJsonFieldIfPresent( summary, connectivity,
                            "net_component_summary_sample_truncated" );
    copyJsonFieldIfPresent( summary, connectivity, "unconnected_edge_sample_truncated" );

    bool componentTruncated = false;
    bool edgeTruncated = false;
    bool graphNodeTruncated = false;
    bool graphEdgeTruncated = false;
    summary["net_component_summaries"] =
            cappedJsonArray( connectivity.value( "net_component_summaries",
                                                 nlohmann::json::array() ),
                             16, componentTruncated );
    summary["unconnected_edges"] =
            cappedJsonArray( connectivity.value( "unconnected_edges",
                                                 nlohmann::json::array() ),
                             16, edgeTruncated );
    summary["component_graph_nodes"] =
            cappedJsonArray( connectivity.value( "component_graph_nodes",
                                                 nlohmann::json::array() ),
                             16, graphNodeTruncated );
    summary["component_graph_edges"] =
            cappedJsonArray( connectivity.value( "component_graph_edges",
                                                 nlohmann::json::array() ),
                             16, graphEdgeTruncated );

    if( componentTruncated )
        summary["net_component_summary_sample_truncated"] = true;

    if( edgeTruncated )
        summary["unconnected_edge_sample_truncated"] = true;

    if( graphNodeTruncated )
        summary["component_graph_node_sample_truncated"] = true;

    if( graphEdgeTruncated )
        summary["component_graph_edge_sample_truncated"] = true;

    return summary;
}


nlohmann::json layerContextSummaryJson( const nlohmann::json& aLayerContext )
{
    if( !aLayerContext.is_object() )
        return nlohmann::json::object();

    nlohmann::json summary = nlohmann::json::object();

    copyJsonFieldIfPresent( summary, aLayerContext, "source" );
    copyJsonFieldIfPresent( summary, aLayerContext, "visible_layers_source" );
    copyJsonFieldIfPresent( summary, aLayerContext, "copper_layer_count" );
    copyJsonFieldIfPresent( summary, aLayerContext, "enabled_layer_count" );
    copyJsonFieldIfPresent( summary, aLayerContext, "visible_layer_count" );
    copyJsonFieldIfPresent( summary, aLayerContext, "active_layer" );

    bool layersTruncated = false;
    summary["layers"] =
            cappedJsonArray( aLayerContext.value( "layers", nlohmann::json::array() ),
                             16, layersTruncated );

    if( layersTruncated )
        summary["layer_sample_truncated"] = true;

    return summary;
}


nlohmann::json boardContextSummaryJson( const AI_CONTEXT_SNAPSHOT& aContext )
{
    if( aContext.m_Summary.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json boardSummary = objectFromJsonText( aContext.m_Summary );

    if( !boardSummary.is_object() )
        return nlohmann::json::object();

    nlohmann::json summary =
            { { "source", "context_summary.pcb_board_summary" } };

    copyJsonFieldIfPresent( summary, boardSummary, "kind" );
    copyJsonFieldIfPresent( summary, boardSummary, "board_edges_bbox" );
    copyJsonFieldIfPresent( summary, boardSummary, "net_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "footprint_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "pad_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "track_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "arc_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "via_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "drawing_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "edge_cut_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "zone_count" );
    copyJsonFieldIfPresent( summary, boardSummary, "keepout_count" );

    if( boardSummary.contains( "layer_context" ) )
    {
        nlohmann::json layerSummary =
                layerContextSummaryJson( boardSummary["layer_context"] );

        if( !layerSummary.empty() )
            summary["layer_context"] = std::move( layerSummary );
    }

    return summary;
}


std::string normalizedNetName( std::string aNetName )
{
    if( !aNetName.empty() && aNetName.front() == '/' )
        aNetName.erase( aNetName.begin() );

    return aNetName;
}


bool netNamesMatch( const std::string& aLeft, const std::string& aRight )
{
    return aLeft == aRight
           || normalizedNetName( aLeft ) == normalizedNetName( aRight );
}


nlohmann::json activeNetSummaryJson( const AI_CONTEXT_SNAPSHOT& aContext )
{
    if( aContext.m_Summary.IsEmpty()
        || aContext.m_ToolState.m_ModeContextJson.IsEmpty() )
    {
        return nlohmann::json::object();
    }

    nlohmann::json modeContext =
            objectFromJsonText( aContext.m_ToolState.m_ModeContextJson );

    if( !modeContext.contains( "net" ) || !modeContext["net"].is_string() )
        return nlohmann::json::object();

    const std::string activeNet = modeContext["net"].get<std::string>();
    nlohmann::json    boardSummary = objectFromJsonText( aContext.m_Summary );

    if( !boardSummary.is_object() || !boardSummary.contains( "net_facts" )
        || !boardSummary["net_facts"].is_array() )
    {
        return nlohmann::json::object();
    }

    for( const nlohmann::json& netFact : boardSummary["net_facts"] )
    {
        if( !netFact.is_object() || !netFact.contains( "name" )
            || !netFact["name"].is_string() )
        {
            continue;
        }

        if( !netNamesMatch( activeNet, netFact["name"].get<std::string>() ) )
            continue;

        nlohmann::json summary = netFact;
        summary["source"] = "context_summary.net_facts";
        summary["requested_net"] = activeNet;

        if( boardSummary.contains( "connectivity_summary" )
            && boardSummary["connectivity_summary"].is_object() )
        {
            const nlohmann::json& connectivity = boardSummary["connectivity_summary"];
            const bool hasNetCode = netFact.contains( "code" )
                                    && netFact["code"].is_number_integer();
            const int  netCode = hasNetCode ? netFact["code"].get<int>() : 0;

            auto belongsToActiveNet =
                    [&]( const nlohmann::json& aFact ) -> bool
                    {
                        if( !aFact.is_object() )
                            return false;

                        if( hasNetCode && aFact.contains( "net_code" )
                            && aFact["net_code"].is_number_integer()
                            && aFact["net_code"].get<int>() == netCode )
                        {
                            return true;
                        }

                        return aFact.contains( "net_name" ) && aFact["net_name"].is_string()
                               && netNamesMatch( activeNet,
                                                 aFact["net_name"].get<std::string>() );
                    };

            auto filteredFacts =
                    [&]( const char* aFieldName ) -> nlohmann::json
                    {
                        nlohmann::json facts = nlohmann::json::array();

                        if( !connectivity.contains( aFieldName )
                            || !connectivity[aFieldName].is_array() )
                        {
                            return facts;
                        }

                        for( const nlohmann::json& fact : connectivity[aFieldName] )
                        {
                            if( belongsToActiveNet( fact ) )
                                facts.push_back( fact );
                        }

                        return facts;
                    };

            bool graphNodeTruncated = false;
            bool graphEdgeTruncated = false;
            summary["component_graph_nodes"] =
                    cappedJsonArray( filteredFacts( "component_graph_nodes" ), 16,
                                     graphNodeTruncated );
            summary["component_graph_edges"] =
                    cappedJsonArray( filteredFacts( "component_graph_edges" ), 16,
                                     graphEdgeTruncated );

            if( graphNodeTruncated )
                summary["component_graph_node_sample_truncated"] = true;

            if( graphEdgeTruncated )
                summary["component_graph_edge_sample_truncated"] = true;
        }

        return summary;
    }

    return nlohmann::json::object();
}


std::optional<nlohmann::json> componentGraphNodeByIdJson(
        const nlohmann::json& aNodes,
        const std::string& aId )
{
    if( !aNodes.is_array() || aId.empty() )
        return std::nullopt;

    for( const nlohmann::json& node : aNodes )
    {
        if( node.is_object() && node.contains( "id" ) && node["id"].is_string()
            && node["id"].get<std::string>() == aId )
        {
            return node;
        }
    }

    return std::nullopt;
}


bool jsonNumberAsInt( const nlohmann::json& aObject, const char* aField,
                      int& aValue );


bool jsonPointToInts( const nlohmann::json& aPoint, int& aX, int& aY );


nlohmann::json bboxRecordJson( int aLeft, int aTop, int aWidth, int aHeight );


nlohmann::json obstacleFactsInBBoxJson(
        const std::vector<AI_OBJECT_REF>& aObjects,
        const nlohmann::json& aBbox,
        const char* aSource );


bool graphEndpointPositionToInts( const nlohmann::json& aEdge,
                                  const char* aEndpointField,
                                  int& aX, int& aY )
{
    if( !aEdge.contains( aEndpointField )
        || !aEdge[aEndpointField].is_object()
        || !aEdge[aEndpointField].contains( "position" ) )
    {
        return false;
    }

    return jsonPointToInts( aEdge[aEndpointField]["position"], aX, aY );
}


nlohmann::json routingEndpointSweptBBoxJson(
        int aStartX, int aStartY,
        int aEndX, int aEndY,
        const nlohmann::json& aModeContext )
{
    int width = 0;
    int padding = 0;

    if( jsonNumberAsInt( aModeContext, "width", width ) && width > 0 )
        padding = std::max( 1, width / 2 );

    const int left = std::min( aStartX, aEndX ) - padding;
    const int top = std::min( aStartY, aEndY ) - padding;
    const int right = std::max( aStartX, aEndX ) + padding;
    const int bottom = std::max( aStartY, aEndY ) + padding;

    return bboxRecordJson( left, top, right - left, bottom - top );
}


nlohmann::json routingProgressFactsJson(
        const nlohmann::json& aActiveNetSummary,
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    if( !aActiveNetSummary.is_object()
        || !aActiveNetSummary.contains( "component_graph_edges" )
        || !aActiveNetSummary["component_graph_edges"].is_array() )
    {
        return nlohmann::json::object();
    }

    int  remainingEdgeCount = 0;
    int  visibleRemainingEdgeCount = 0;
    int  remainingLength = 0;
    int  shortestRemainingLength = 2147483647;
    bool hasRemainingLength = false;

    for( const nlohmann::json& edge :
         aActiveNetSummary["component_graph_edges"] )
    {
        if( !edge.is_object() )
            continue;

        ++remainingEdgeCount;

        if( edge.contains( "visible" ) && edge["visible"].is_boolean()
            && edge["visible"].get<bool>() )
        {
            ++visibleRemainingEdgeCount;
        }

        int edgeLength = 0;

        if( jsonNumberAsInt( edge, "estimated_manhattan_length", edgeLength )
            && edgeLength >= 0 )
        {
            hasRemainingLength = true;
            remainingLength += edgeLength;
            shortestRemainingLength =
                    std::min( shortestRemainingLength, edgeLength );
        }
    }

    nlohmann::json facts = {
        { "source", "active_net_summary.component_graph_edges" },
        { "purpose", "routing_progress_review" },
        { "remaining_component_edge_count", remainingEdgeCount },
        { "visible_remaining_component_edge_count",
          visibleRemainingEdgeCount } };

    if( aActiveNetSummary.contains( "name" )
        && aActiveNetSummary["name"].is_string() )
    {
        facts["active_net"] = aActiveNetSummary["name"];
    }

    if( hasRemainingLength )
    {
        facts["remaining_estimated_manhattan_length"] = remainingLength;
        facts["shortest_remaining_estimated_manhattan_length"] =
                shortestRemainingLength;
    }

    int routedTrackLength = 0;

    if( jsonNumberAsInt( aActiveNetSummary, "routed_track_length",
                         routedTrackLength )
        && routedTrackLength >= 0 )
    {
        facts["routed_track_length"] = routedTrackLength;

        if( hasRemainingLength )
            facts["estimated_total_work_length"] =
                    routedTrackLength + remainingLength;
    }

    int routedTrackSegmentCount = 0;

    if( jsonNumberAsInt( aActiveNetSummary, "routed_track_segment_count",
                         routedTrackSegmentCount )
        && routedTrackSegmentCount >= 0 )
    {
        facts["routed_track_segment_count"] = routedTrackSegmentCount;
    }

    int routedViaCount = 0;

    if( jsonNumberAsInt( aActiveNetSummary, "routed_via_count",
                         routedViaCount )
        && routedViaCount >= 0 )
    {
        facts["routed_via_count"] = routedViaCount;
    }

    if( aActiveNetSummary.contains( "routed_layer_lengths" )
        && aActiveNetSummary["routed_layer_lengths"].is_array() )
    {
        bool truncated = false;
        facts["routed_layer_lengths"] =
                cappedJsonArray( aActiveNetSummary["routed_layer_lengths"],
                                 16, truncated );

        if( truncated )
            facts["routed_layer_lengths_truncated"] = true;

        const nlohmann::json modeContext =
                objectFromJsonText( aToolState.m_ModeContextJson );

        if( modeContext.contains( "layer" ) && modeContext["layer"].is_string() )
        {
            const std::string activeLayer =
                    modeContext["layer"].get<std::string>();
            facts["active_layer"] = activeLayer;

            for( const nlohmann::json& layerFact :
                 facts["routed_layer_lengths"] )
            {
                if( !layerFact.is_object()
                    || !layerFact.contains( "layer" )
                    || !layerFact["layer"].is_string()
                    || layerFact["layer"].get<std::string>() != activeLayer )
                {
                    continue;
                }

                if( layerFact.contains( "routed_track_length" )
                    && !layerFact["routed_track_length"].is_null() )
                {
                    facts["active_layer_routed_track_length"] =
                            layerFact["routed_track_length"];
                }

                if( layerFact.contains( "routed_track_segment_count" )
                    && !layerFact["routed_track_segment_count"].is_null() )
                {
                    facts["active_layer_routed_track_segment_count"] =
                            layerFact["routed_track_segment_count"];
                }

                break;
            }
        }
    }

    return facts;
}


nlohmann::json routingLayerReachabilityFactsJson(
        const nlohmann::json& aActiveNetSummary,
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    nlohmann::json facts = nlohmann::json::array();

    if( !aActiveNetSummary.is_object()
        || !aActiveNetSummary.contains( "component_graph_edges" )
        || !aActiveNetSummary["component_graph_edges"].is_array() )
    {
        return facts;
    }

    const nlohmann::json modeContext =
            objectFromJsonText( aToolState.m_ModeContextJson );

    if( !modeContext.contains( "layer" ) || !modeContext["layer"].is_string() )
        return facts;

    const std::string activeLayer = modeContext["layer"].get<std::string>();
    int               candidateEdgeCount = 0;
    int               visibleCandidateEdgeCount = 0;
    int               estimatedLength = 0;
    int               layerSwitchEdgeCount = 0;
    bool              hasEstimatedLength = false;

    for( const nlohmann::json& edge : aActiveNetSummary["component_graph_edges"] )
    {
        if( !edge.is_object() )
            continue;

        std::string candidateLayer = activeLayer;

        if( edge.contains( "candidate_layer" ) && edge["candidate_layer"].is_string() )
            candidateLayer = edge["candidate_layer"].get<std::string>();
        else if( edge.contains( "layer" ) && edge["layer"].is_string() )
            candidateLayer = edge["layer"].get<std::string>();

        const bool reachesOnActiveLayer = candidateLayer == activeLayer;

        if( !reachesOnActiveLayer )
        {
            ++layerSwitchEdgeCount;
            continue;
        }

        ++candidateEdgeCount;

        if( edge.contains( "visible" ) && edge["visible"].is_boolean()
            && edge["visible"].get<bool>() )
        {
            ++visibleCandidateEdgeCount;
        }

        int edgeLength = 0;

        if( jsonNumberAsInt( edge, "estimated_manhattan_length", edgeLength )
            && edgeLength >= 0 )
        {
            hasEstimatedLength = true;
            estimatedLength += edgeLength;
        }
    }

    nlohmann::json activeLayerFact =
            { { "source",
                "active_net_summary.component_graph_edges+mode_context.layer" },
              { "purpose", "layer_specific_routing_reachability_review" },
              { "layer", activeLayer },
              { "active_layer", true },
              { "candidate_edge_count", candidateEdgeCount },
              { "visible_candidate_edge_count", visibleCandidateEdgeCount },
              { "requires_layer_switch", layerSwitchEdgeCount > 0 },
              { "layer_switch_edge_count", layerSwitchEdgeCount },
              { "via_transition_count_estimate", layerSwitchEdgeCount },
              { "cost_model", "heuristic_manhattan_active_layer" } };

    if( hasEstimatedLength )
        activeLayerFact["estimated_manhattan_length"] = estimatedLength;

    if( aActiveNetSummary.contains( "name" )
        && aActiveNetSummary["name"].is_string() )
    {
        activeLayerFact["active_net"] = aActiveNetSummary["name"];
    }

    if( aActiveNetSummary.contains( "routed_layer_lengths" )
        && aActiveNetSummary["routed_layer_lengths"].is_array() )
    {
        for( const nlohmann::json& layerFact :
             aActiveNetSummary["routed_layer_lengths"] )
        {
            if( !layerFact.is_object()
                || !layerFact.contains( "layer" )
                || !layerFact["layer"].is_string()
                || layerFact["layer"].get<std::string>() != activeLayer )
            {
                continue;
            }

            copyJsonFieldIfPresent( activeLayerFact, layerFact,
                                    "routed_track_length" );
            copyJsonFieldIfPresent( activeLayerFact, layerFact,
                                    "routed_track_segment_count" );
            break;
        }
    }

    facts.push_back( std::move( activeLayerFact ) );
    return facts;
}


nlohmann::json routingReachabilityFactsJson(
        const nlohmann::json& aActiveNetSummary,
        const AI_TOOL_STATE_SNAPSHOT& aToolState,
        const std::vector<AI_OBJECT_REF>& aVisibleObjects )
{
    nlohmann::json facts = nlohmann::json::array();

    if( !aActiveNetSummary.is_object()
        || !aActiveNetSummary.contains( "component_graph_edges" )
        || !aActiveNetSummary["component_graph_edges"].is_array() )
    {
        return facts;
    }

    const nlohmann::json nodes =
            aActiveNetSummary.value( "component_graph_nodes",
                                     nlohmann::json::array() );
    const std::string activeNet =
            aActiveNetSummary.contains( "name" )
                            && aActiveNetSummary["name"].is_string()
                    ? aActiveNetSummary["name"].get<std::string>()
                    : std::string();
    const nlohmann::json modeContext =
            objectFromJsonText( aToolState.m_ModeContextJson );
    std::vector<nlohmann::json> rankedFacts;

    for( const nlohmann::json& edge :
         aActiveNetSummary["component_graph_edges"] )
    {
        if( !edge.is_object() )
            continue;

        nlohmann::json fact = {
            { "source", "active_net_summary.component_graph_edges" },
            { "purpose", "route_remaining_connection" } };

        if( !activeNet.empty() )
            fact["active_net"] = activeNet;

        copyJsonFieldIfPresent( fact, edge, "from" );
        copyJsonFieldIfPresent( fact, edge, "to" );
        copyJsonFieldIfPresent( fact, edge, "net_code" );
        copyJsonFieldIfPresent( fact, edge, "net_name" );
        copyJsonFieldIfPresent( fact, edge, "visible" );
        copyJsonFieldIfPresent( fact, edge, "estimated_manhattan_length" );

        if( edge.contains( "source" ) && edge["source"].is_object() )
            fact["from_endpoint"] = edge["source"];

        if( edge.contains( "target" ) && edge["target"].is_object() )
            fact["to_endpoint"] = edge["target"];

        int startX = 0;
        int startY = 0;
        int endX = 0;
        int endY = 0;

        if( graphEndpointPositionToInts( edge, "source", startX, startY )
            && graphEndpointPositionToInts( edge, "target", endX, endY ) )
        {
            const nlohmann::json sweptBBox =
                    routingEndpointSweptBBoxJson( startX, startY, endX, endY,
                                                  modeContext );

            int routeHeadX = 0;
            int routeHeadY = 0;

            if( modeContext.contains( "start" )
                && jsonPointToInts( modeContext["start"], routeHeadX, routeHeadY ) )
            {
                const int fromDistance = std::abs( startX - routeHeadX )
                                         + std::abs( startY - routeHeadY );
                const int toDistance = std::abs( endX - routeHeadX )
                                       + std::abs( endY - routeHeadY );

                fact["route_head"] = modeContext["start"];
                fact["route_head_to_from_endpoint_manhattan"] = fromDistance;
                fact["route_head_to_to_endpoint_manhattan"] = toDistance;

                if( fromDistance <= toDistance )
                {
                    fact["nearest_endpoint_role"] = "from";
                    fact["suggested_landing_endpoint_role"] = "to";

                    if( fact.contains( "to_endpoint" ) )
                        fact["suggested_landing_endpoint"] = fact["to_endpoint"];
                }
                else
                {
                    fact["nearest_endpoint_role"] = "to";
                    fact["suggested_landing_endpoint_role"] = "from";

                    if( fact.contains( "from_endpoint" ) )
                        fact["suggested_landing_endpoint"] = fact["from_endpoint"];
                }

                if( fact.contains( "suggested_landing_endpoint" )
                    && fact["suggested_landing_endpoint"].is_object()
                    && fact["suggested_landing_endpoint"].contains( "position" )
                    && fact["suggested_landing_endpoint"]["position"].is_object() )
                {
                    nlohmann::json toolArgs =
                            { { "current_position", modeContext["start"] },
                              { "target_position",
                                fact["suggested_landing_endpoint"]["position"] } };

                    if( !activeNet.empty() )
                        toolArgs["net"] = activeNet;
                    else if( modeContext.contains( "net" )
                             && modeContext["net"].is_string() )
                        toolArgs["net"] = modeContext["net"];

                    if( modeContext.contains( "layer" )
                        && modeContext["layer"].is_string() )
                    {
                        toolArgs["layer"] = modeContext["layer"];
                    }

                    if( modeContext.contains( "width" )
                        && modeContext["width"].is_number() )
                    {
                        toolArgs["width"] = modeContext["width"];
                    }

                    fact["suggested_tool_call"] =
                            { { "name", "routing.repair_segment" },
                              { "purpose", "hidden_attempt_repair_hint" },
                              { "arguments", toolArgs } };
                }

                if( modeContext.contains( "layer" )
                    && modeContext["layer"].is_string() )
                {
                    const std::string activeLayer =
                            modeContext["layer"].get<std::string>();
                    fact["layer_reachability"] =
                            { { "source", "mode_context.layer" },
                              { "active_layer", activeLayer },
                              { "candidate_layer", activeLayer },
                              { "layer_known", true },
                              { "requires_layer_switch", false },
                              { "via_required", false },
                              { "strategy", "current_layer_first_hint" },
                              { "review_required", true } };
                }
            }

            fact["suggested_render_region"] =
                    { { "source", "routing_reachability_swept_bbox" },
                      { "mode", "routing_reachability_review" },
                      { "bbox", sweptBBox } };

            nlohmann::json obstacleFacts =
                    obstacleFactsInBBoxJson( aVisibleObjects, sweptBBox,
                                             "routing_reachability_swept_bbox" );
            fact["reachability_obstacle_facts"] = obstacleFacts;

            nlohmann::json routerCostHint =
                    { { "source", "routing_reachability_fact" },
                      { "cost_model", "heuristic_manhattan_obstacle_layer" },
                      { "remaining_endpoint_span_manhattan",
                        std::abs( startX - endX ) + std::abs( startY - endY ) },
                      { "candidate_obstacle_count",
                        static_cast<int>( obstacleFacts.size() ) },
                      { "review_required", true } };

            if( fact.contains( "nearest_endpoint_role" ) )
            {
                const std::string nearestRole =
                        fact["nearest_endpoint_role"].get<std::string>();

                if( nearestRole == "from"
                    && fact.contains(
                            "route_head_to_from_endpoint_manhattan" ) )
                {
                    routerCostHint["route_head_to_nearest_endpoint_manhattan"] =
                            fact["route_head_to_from_endpoint_manhattan"];
                }
                else if( nearestRole == "to"
                         && fact.contains(
                                 "route_head_to_to_endpoint_manhattan" ) )
                {
                    routerCostHint["route_head_to_nearest_endpoint_manhattan"] =
                            fact["route_head_to_to_endpoint_manhattan"];
                }
            }

            bool requiresLayerSwitch = false;
            int  viaTransitionEstimate = 0;

            if( fact.contains( "layer_reachability" )
                && fact["layer_reachability"].is_object() )
            {
                const nlohmann::json& layerReachability =
                        fact["layer_reachability"];

                if( layerReachability.contains( "requires_layer_switch" )
                    && layerReachability["requires_layer_switch"].is_boolean() )
                {
                    requiresLayerSwitch =
                            layerReachability["requires_layer_switch"]
                                    .get<bool>();
                }

                if( layerReachability.contains( "via_required" )
                    && layerReachability["via_required"].is_boolean()
                    && layerReachability["via_required"].get<bool>() )
                {
                    viaTransitionEstimate = 1;
                }
            }

            routerCostHint["requires_layer_switch"] = requiresLayerSwitch;
            routerCostHint["via_transition_count_estimate"] =
                    viaTransitionEstimate;
            fact["router_cost_hint"] = std::move( routerCostHint );
        }

        if( edge.contains( "kind" ) )
            fact["edge_kind"] = edge["kind"];

        if( fact.contains( "from" ) && fact["from"].is_string() )
        {
            if( std::optional<nlohmann::json> node =
                        componentGraphNodeByIdJson( nodes,
                                                    fact["from"].get<std::string>() ) )
            {
                fact["from_node"] = *node;
            }
        }

        if( fact.contains( "to" ) && fact["to"].is_string() )
        {
            if( std::optional<nlohmann::json> node =
                        componentGraphNodeByIdJson( nodes,
                                                    fact["to"].get<std::string>() ) )
            {
                fact["to_node"] = *node;
            }
        }

        rankedFacts.push_back( std::move( fact ) );

        if( rankedFacts.size() >= 16 )
            break;
    }

    auto factVisible =
            []( const nlohmann::json& aFact ) -> bool
            {
                return aFact.contains( "visible" ) && aFact["visible"].is_boolean()
                       && aFact["visible"].get<bool>();
            };

    auto estimatedLength =
            []( const nlohmann::json& aFact ) -> int
            {
                if( !aFact.contains( "estimated_manhattan_length" )
                    || !aFact["estimated_manhattan_length"].is_number() )
                {
                    return 2147483647;
                }

                return aFact["estimated_manhattan_length"].get<int>();
            };

    std::stable_sort( rankedFacts.begin(), rankedFacts.end(),
                      [&]( const nlohmann::json& aLeft,
                           const nlohmann::json& aRight )
                      {
                          const bool leftVisible = factVisible( aLeft );
                          const bool rightVisible = factVisible( aRight );

                          if( leftVisible != rightVisible )
                              return leftVisible;

                          return estimatedLength( aLeft ) < estimatedLength( aRight );
                      } );

    for( size_t ii = 0; ii < rankedFacts.size(); ++ii )
    {
        nlohmann::json fact = std::move( rankedFacts[ii] );
        fact["priority_rank"] = static_cast<int>( ii + 1 );

        if( factVisible( fact ) )
            fact["priority_reason"] = "visible_remaining_connection";
        else if( estimatedLength( fact ) != 2147483647 )
            fact["priority_reason"] = "shorter_estimated_connection";
        else
            fact["priority_reason"] = "component_graph_order";

        facts.push_back( std::move( fact ) );
    }

    return facts;
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


nlohmann::json obstacleCandidateFactsJson(
        const std::vector<AI_OBJECT_REF>& aObjects )
{
    nlohmann::json candidates = placementObstacleFactsJson( aObjects );

    for( nlohmann::json keepout : placementKeepoutFactsJson( aObjects ) )
        candidates.push_back( std::move( keepout ) );

    for( nlohmann::json routingObstacle : routingObstacleFactsJson( aObjects ) )
        candidates.push_back( std::move( routingObstacle ) );

    return candidates;
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


std::optional<LOCALITY_REGION> localityRegionFromBBox( const char* aSource,
                                                       const nlohmann::json& aBbox )
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    if( !jsonNumberAsInt( aBbox, "x", x )
        || !jsonNumberAsInt( aBbox, "y", y )
        || !jsonNumberAsInt( aBbox, "width", width )
        || !jsonNumberAsInt( aBbox, "height", height )
        || width < 0 || height < 0 )
    {
        return std::nullopt;
    }

    LOCALITY_REGION region;
    region.m_Left = x;
    region.m_Top = y;
    region.m_Width = width;
    region.m_Height = height;
    region.m_Record = { { "source", aSource },
                        { "bbox", aBbox },
                        { "raw", aBbox } };
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
    nlohmann::json candidates = obstacleCandidateFactsJson( aObjects );

    for( nlohmann::json fact : candidates )
    {
        if( !factIntersectsRegion( fact, aRegion ) )
            continue;

        fact["locality_source"] = aRegion.m_Record["source"];
        facts.push_back( std::move( fact ) );
    }

    return facts;
}


nlohmann::json obstacleFactsInBBoxJson( const std::vector<AI_OBJECT_REF>& aObjects,
                                        const nlohmann::json& aBbox,
                                        const char* aSource )
{
    std::optional<LOCALITY_REGION> region = localityRegionFromBBox( aSource, aBbox );

    if( !region )
        return nlohmann::json::array();

    nlohmann::json facts = nlohmann::json::array();

    for( nlohmann::json fact : obstacleCandidateFactsJson( aObjects ) )
    {
        if( !factIntersectsRegion( fact, *region ) )
            continue;

        fact["locality_source"] = (*region).m_Record["source"];
        facts.push_back( std::move( fact ) );

        if( facts.size() >= 16 )
            break;
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


const nlohmann::json* firstSurfaceGuardValue(
        const nlohmann::json& aState,
        std::initializer_list<const char*> aKeys,
        std::string& aMatchedKey )
{
    if( !aState.is_object() )
        return nullptr;

    for( const char* key : aKeys )
    {
        if( aState.contains( key ) && !aState[key].is_null() )
        {
            aMatchedKey = key;
            return &aState[key];
        }
    }

    aMatchedKey.clear();
    return nullptr;
}


nlohmann::json surfaceGuardFieldJson(
        const nlohmann::json& aState,
        std::initializer_list<const char*> aKeys,
        const char* aExpectedArgument )
{
    std::string           matchedKey;
    const nlohmann::json* value =
            firstSurfaceGuardValue( aState, aKeys, matchedKey );

    nlohmann::json field =
            { { "available", value != nullptr },
              { "expected_argument", aExpectedArgument } };

    if( value )
    {
        field["value"] = *value;
        field["source"] = "panel_state.state." + matchedKey;
    }

    return field;
}


nlohmann::json surfaceGuardFactsJson( const AI_PANEL_STATE_RECORD& aPanel,
                                      const nlohmann::json& aState )
{
    nlohmann::json facts =
            { { "surface_id", toUtf8String( aPanel.m_Id ) },
              { "surface_title", toUtf8String( aPanel.m_Title ) },
              { "source", "panel_state.state" } };

    facts["surface_revision"] =
            surfaceGuardFieldJson( aState, { "surface_revision", "revision" },
                                   "expected_surface_revision" );
    facts["schema_version"] =
            surfaceGuardFieldJson( aState, { "schema_version", "schemaVersion" },
                                   "expected_schema_version" );
    facts["selection_fingerprint"] =
            surfaceGuardFieldJson(
                    aState,
                    { "selection_fingerprint", "selectionFingerprint" },
                    "expected_selection_fingerprint" );
    facts["overlap_set"] =
            surfaceGuardFieldJson( aState, { "overlap_set", "overlapSet" },
                                   "expected_overlap_set" );
    facts["has_complete_accept_guard"] =
            facts["surface_revision"]["available"].get<bool>()
            && facts["schema_version"]["available"].get<bool>()
            && facts["selection_fingerprint"]["available"].get<bool>()
            && facts["overlap_set"]["available"].get<bool>();

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


bool jsonPointToInts( const nlohmann::json& aPoint, int& aX, int& aY )
{
    return aPoint.is_object()
           && jsonNumberAsInt( aPoint, "x", aX )
           && jsonNumberAsInt( aPoint, "y", aY );
}


nlohmann::json pointRecordJson( int aX, int aY )
{
    return { { "x", aX }, { "y", aY } };
}


nlohmann::json pointPairBoundingBoxJson( int aX1, int aY1, int aX2, int aY2 )
{
    const int left = std::min( aX1, aX2 );
    const int top = std::min( aY1, aY2 );
    const int right = std::max( aX1, aX2 );
    const int bottom = std::max( aY1, aY2 );
    const int width = right - left;
    const int height = bottom - top;

    return { { "x", left },
             { "y", top },
             { "width", width },
             { "height", height },
             { "right", right },
             { "bottom", bottom },
             { "center", pointRecordJson( left + width / 2, top + height / 2 ) } };
}


std::optional<nlohmann::json> placementCandidateRenderRegionJson(
        const AI_TOOL_STATE_SNAPSHOT& aToolState,
        int aCandidateX,
        int aCandidateY )
{
    std::optional<nlohmann::json> source =
            toolContextObjectField( aToolState, "cursor_region" );
    const char* basis = "cursor_region";

    if( !source )
    {
        source = toolContextObjectField( aToolState, "viewport" );
        basis = "viewport";
    }

    if( !source || !source->is_object() )
        return std::nullopt;

    int width = 0;
    int height = 0;

    if( !jsonNumberAsInt( *source, "width", width )
        || !jsonNumberAsInt( *source, "height", height )
        || width <= 0 || height <= 0 )
    {
        return std::nullopt;
    }

    return nlohmann::json{
        { "source", "placement_candidate_region" },
        { "basis", basis },
        { "mode", "placement_candidate_review" },
        { "bbox", bboxRecordJson( aCandidateX - width / 2,
                                  aCandidateY - height / 2,
                                  width, height ) } };
}


nlohmann::json placementCandidateFactJson(
        const AI_CONTEXT_ANCHOR& aAnchor,
        const AI_TOOL_STATE_SNAPSHOT& aToolState,
        const std::vector<AI_OBJECT_REF>& aVisibleObjects,
        int aReferenceX,
        int aReferenceY )
{
    const int dx = aAnchor.m_Position.x - aReferenceX;
    const int dy = aAnchor.m_Position.y - aReferenceY;
    const int absDx = std::abs( dx );
    const int absDy = std::abs( dy );
    nlohmann::json fact =
            { { "source", "cursor_attached_item_to_anchor" },
              { "anchor_id", toUtf8String( aAnchor.m_Id ) },
              { "anchor_kind", toUtf8String( aAnchor.KindAsString() ) },
              { "anchor_label", toUtf8String( aAnchor.m_Label ) },
              { "placeable_kind", placeableKindForToolState( aToolState.m_Kind ) },
              { "reference_position", pointRecordJson( aReferenceX, aReferenceY ) },
              { "candidate_position",
                pointRecordJson( aAnchor.m_Position.x, aAnchor.m_Position.y ) },
              { "dx", dx },
              { "dy", dy },
              { "abs_dx", absDx },
              { "abs_dy", absDy },
              { "manhattan_distance", absDx + absDy },
              { "confidence", aAnchor.m_Confidence },
              { "click_required_to_materialize", true },
              { "interaction_semantics",
                { { "mode", "active_interactive_placement" },
                  { "planning_target", "place_current_item" },
                  { "placement_anchor_source", "placement_anchor" },
                  { "cursor_attached_item", true },
                  { "manual_click_to_materialize", true },
                  { "manual_click_supersedes_attempt", true },
                  { "preview_must_be_rebased_after_click", true } } } };

    if( std::optional<nlohmann::json> renderRegion =
                placementCandidateRenderRegionJson( aToolState,
                                                    aAnchor.m_Position.x,
                                                    aAnchor.m_Position.y ) )
    {
        fact["suggested_render_region"] = std::move( *renderRegion );

        if( fact["suggested_render_region"].contains( "bbox" )
            && fact["suggested_render_region"]["bbox"].is_object() )
        {
            fact["candidate_obstacle_facts"] =
                    obstacleFactsInBBoxJson( aVisibleObjects,
                                             fact["suggested_render_region"]["bbox"],
                                             "placement_candidate_region" );
        }
    }

    nlohmann::json modeContext = objectFromJsonText( aToolState.m_ModeContextJson );

    if( fact.value( "placeable_kind", std::string() ) == "via"
        && modeContext.contains( "net" )
        && modeContext["net"].is_string()
        && modeContext.contains( "diameter" )
        && modeContext["diameter"].is_number_integer()
        && modeContext.contains( "drill" )
        && modeContext["drill"].is_number_integer()
        && modeContext.contains( "layer_pair" )
        && modeContext["layer_pair"].is_object() )
    {
        fact["suggested_tool_call"] =
                { { "name", "placement.repair_via" },
                  { "purpose", "hidden_attempt_repair_hint" },
                  { "arguments",
                    { { "position", fact["candidate_position"] },
                      { "net", modeContext["net"] },
                      { "diameter", modeContext["diameter"] },
                      { "drill", modeContext["drill"] },
                      { "layer_pair", modeContext["layer_pair"] } } } };
    }

    nlohmann::json details = objectFromJsonText( aAnchor.m_DetailsJson );

    if( details.is_object() )
    {
        copyJsonFieldIfPresent( fact, details, "role" );
        copyJsonFieldIfPresent( fact, details, "pitch" );
    }

    return fact;
}


nlohmann::json placementCandidateFactsJson(
        const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
        const AI_TOOL_STATE_SNAPSHOT& aToolState,
        const std::vector<AI_OBJECT_REF>& aVisibleObjects )
{
    int referenceX = 0;
    int referenceY = 0;

    if( aToolState.m_HasCursorBoardPosition )
    {
        referenceX = aToolState.m_CursorBoardPosition.x;
        referenceY = aToolState.m_CursorBoardPosition.y;
    }
    else
    {
        nlohmann::json modeContext = objectFromJsonText( aToolState.m_ModeContextJson );

        if( !modeContext.contains( "cursor" )
            || !jsonPointToInts( modeContext["cursor"], referenceX, referenceY ) )
        {
            return nlohmann::json::array();
        }
    }

    nlohmann::json facts = nlohmann::json::array();

    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( facts.size() >= 16 )
            break;

        if( !anchor.m_HasPosition || !isPlacementPacketAnchor( anchor.m_Kind ) )
            continue;

        facts.push_back(
                placementCandidateFactJson( anchor, aToolState, aVisibleObjects,
                                            referenceX, referenceY ) );
    }

    return facts;
}


wxString routingSegmentStyle( int aDx, int aDy )
{
    const int absDx = std::abs( aDx );
    const int absDy = std::abs( aDy );

    if( aDx == 0 && aDy == 0 )
        return wxS( "zero_length" );

    if( aDy == 0 )
        return wxS( "horizontal" );

    if( aDx == 0 )
        return wxS( "vertical" );

    if( absDx == absDy )
        return wxS( "forty_five" );

    return wxS( "free_angle" );
}


nlohmann::json routingSweptBBoxJson( int aStartX, int aStartY,
                                     int aEndX, int aEndY,
                                     const nlohmann::json& aModeContext )
{
    int width = 0;
    int padding = 0;

    if( jsonNumberAsInt( aModeContext, "width", width ) && width > 0 )
        padding = std::max( 1, width / 2 );

    const int left = std::min( aStartX, aEndX ) - padding;
    const int top = std::min( aStartY, aEndY ) - padding;
    const int right = std::max( aStartX, aEndX ) + padding;
    const int bottom = std::max( aStartY, aEndY ) + padding;

    return bboxRecordJson( left, top, right - left, bottom - top );
}


nlohmann::json routingCorridorFactJson(
        const AI_CONTEXT_ANCHOR& aAnchor,
        const nlohmann::json& aModeContext,
        const std::vector<AI_OBJECT_REF>& aVisibleObjects,
        int aStartX,
        int aStartY )
{
    const int dx = aAnchor.m_Position.x - aStartX;
    const int dy = aAnchor.m_Position.y - aStartY;
    const int absDx = std::abs( dx );
    const int absDy = std::abs( dy );
    const nlohmann::json sweptBBox =
            routingSweptBBoxJson( aStartX, aStartY, aAnchor.m_Position.x,
                                  aAnchor.m_Position.y, aModeContext );
    nlohmann::json fact =
            { { "source", "route_head_to_anchor" },
              { "anchor_id", toUtf8String( aAnchor.m_Id ) },
              { "anchor_kind", toUtf8String( aAnchor.KindAsString() ) },
              { "anchor_label", toUtf8String( aAnchor.m_Label ) },
              { "start", pointRecordJson( aStartX, aStartY ) },
              { "end", pointRecordJson( aAnchor.m_Position.x, aAnchor.m_Position.y ) },
              { "corridor_bbox",
                pointPairBoundingBoxJson( aStartX, aStartY, aAnchor.m_Position.x,
                                          aAnchor.m_Position.y ) },
              { "swept_bbox", sweptBBox },
              { "suggested_render_region",
                { { "source", "routing_corridor_swept_bbox" },
                  { "mode", "routing_corridor_review" },
                  { "bbox", sweptBBox } } },
              { "corridor_obstacle_facts",
                obstacleFactsInBBoxJson( aVisibleObjects, sweptBBox,
                                         "routing_corridor_swept_bbox" ) },
              { "dx", dx },
              { "dy", dy },
              { "abs_dx", absDx },
              { "abs_dy", absDy },
              { "manhattan_length", absDx + absDy },
              { "segment_style", toUtf8String( routingSegmentStyle( dx, dy ) ) },
              { "confidence", aAnchor.m_Confidence },
              { "manual_click_to_materialize", true },
              { "interaction_semantics",
                { { "mode", "active_interactive_routing" },
                  { "planning_target",
                    "next_landing_from_current_route_head" },
                  { "route_head_source", "mode_context.start" },
                  { "landing_anchor_source", "route_anchor" },
                  { "cursor_is_sorting_hint", true },
                  { "manual_click_to_materialize", true },
                  { "manual_click_supersedes_attempt", true },
                  { "preview_must_be_rebased_after_click", true } } } };

    if( aModeContext.contains( "net" ) && aModeContext["net"].is_string() )
        fact["net"] = aModeContext["net"];

    if( aModeContext.contains( "layer" ) && aModeContext["layer"].is_string() )
        fact["layer"] = aModeContext["layer"];

    if( aModeContext.contains( "width" ) && aModeContext["width"].is_number() )
        fact["width"] = aModeContext["width"];

    nlohmann::json toolArgs =
            { { "current_position", fact["start"] },
              { "target_position", fact["end"] } };

    if( fact.contains( "net" ) )
        toolArgs["net"] = fact["net"];

    if( fact.contains( "layer" ) )
        toolArgs["layer"] = fact["layer"];

    if( fact.contains( "width" ) )
        toolArgs["width"] = fact["width"];

    fact["suggested_tool_call"] =
            { { "name", "routing.repair_segment" },
              { "purpose", "hidden_attempt_repair_hint" },
              { "arguments", toolArgs } };

    int cursorX = 0;
    int cursorY = 0;

    if( aModeContext.contains( "cursor" )
        && jsonPointToInts( aModeContext["cursor"], cursorX, cursorY ) )
    {
        const int cursorDx = aAnchor.m_Position.x - cursorX;
        const int cursorDy = aAnchor.m_Position.y - cursorY;

        fact["cursor"] = pointRecordJson( cursorX, cursorY );
        fact["cursor_dx"] = cursorDx;
        fact["cursor_dy"] = cursorDy;
        fact["cursor_abs_dx"] = std::abs( cursorDx );
        fact["cursor_abs_dy"] = std::abs( cursorDy );
        fact["cursor_manhattan_distance"] = std::abs( cursorDx ) + std::abs( cursorDy );
        fact["cursor_is_sorting_hint"] = true;
    }

    nlohmann::json details = objectFromJsonText( aAnchor.m_DetailsJson );

    if( details.is_object() )
    {
        copyJsonFieldIfPresent( fact, details, "role" );
        copyJsonFieldIfPresent( fact, details, "target" );
    }

    return fact;
}


nlohmann::json routingCorridorFactsJson(
        const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
        const AI_TOOL_STATE_SNAPSHOT& aToolState,
        const std::vector<AI_OBJECT_REF>& aVisibleObjects )
{
    nlohmann::json modeContext = objectFromJsonText( aToolState.m_ModeContextJson );
    int            startX = 0;
    int            startY = 0;

    if( !modeContext.contains( "start" )
        || !jsonPointToInts( modeContext["start"], startX, startY ) )
    {
        return nlohmann::json::array();
    }

    nlohmann::json facts = nlohmann::json::array();

    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( facts.size() >= 16 )
            break;

        if( !anchor.m_HasPosition || !isRoutingPacketAnchor( anchor.m_Kind )
            || anchor.m_Kind == AI_CONTEXT_ANCHOR_KIND::RouteStart )
        {
            continue;
        }

        facts.push_back(
                routingCorridorFactJson( anchor, modeContext, aVisibleObjects,
                                         startX, startY ) );
    }

    return facts;
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

    nlohmann::json boardContext = boardContextSummaryJson( context );

    if( !boardContext.empty() )
        packet["board_context_summary"] = std::move( boardContext );

    nlohmann::json constraints = constraintSummaryJson( context );

    if( !constraints.empty() )
        packet["constraint_summary"] = std::move( constraints );

    nlohmann::json connectivity = connectivitySummaryJson( context );

    if( !connectivity.empty() )
        packet["connectivity_summary"] = std::move( connectivity );

    nlohmann::json activeNet = activeNetSummaryJson( context );
    nlohmann::json routingProgress =
            routingProgressFactsJson( activeNet, context.m_ToolState );
    nlohmann::json routingLayerReachability =
            routingLayerReachabilityFactsJson( activeNet, context.m_ToolState );
    nlohmann::json routingReachability =
            routingReachabilityFactsJson( activeNet, context.m_ToolState,
                                          context.m_VisibleObjects );

    if( !activeNet.empty() )
        packet["active_net_summary"] = std::move( activeNet );

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
        packet["interaction_semantics"] =
                { { "mode", "active_interactive_routing" },
                  { "planning_target",
                    "next_landing_from_current_route_head" },
                  { "route_head_source", "mode_context.start" },
                  { "cursor_is_sorting_hint", true },
                  { "manual_click_supersedes_attempt", true },
                  { "preview_must_be_rebased_after_click", true } };
        packet["planning_target"] =
                routingPlanningTargetJson( context.m_ToolState );
        packet["active_route_segment"] =
                routingActiveSegmentJson( context.m_ToolState );
        packet["route_anchor_ids"] = anchorIdArrayJson( context.m_Anchors );
        packet["route_anchors"] =
                anchorRecordsJson( context.m_Anchors, isRoutingPacketAnchor );
        packet["routing_corridor_facts"] =
                routingCorridorFactsJson( context.m_Anchors, context.m_ToolState,
                                          context.m_VisibleObjects );

        if( !routingProgress.empty() )
            packet["routing_progress_facts"] = std::move( routingProgress );

        if( !routingLayerReachability.empty() )
            packet["routing_layer_reachability_facts"] =
                    std::move( routingLayerReachability );

        if( !routingReachability.empty() )
            packet["routing_reachability_facts"] =
                    std::move( routingReachability );

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
        packet["interaction_semantics"] =
                { { "mode", "active_interactive_placement" },
                  { "planning_target", "place_current_item" },
                  { "cursor_attached_item", true },
                  { "manual_click_to_materialize", true },
                  { "manual_click_supersedes_attempt", true },
                  { "preview_must_be_rebased_after_click", true } };
        packet["planning_target"] =
                placementPlanningTargetJson( context.m_ToolState );
        packet["placement_anchor_ids"] = anchorIdArrayJson( context.m_Anchors );
        packet["placement_anchors"] =
                anchorRecordsJson( context.m_Anchors, isPlacementPacketAnchor );
        packet["placement_candidate_facts"] =
                placementCandidateFactsJson( context.m_Anchors, context.m_ToolState,
                                             context.m_VisibleObjects );
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
        packet["interaction_semantics"] =
                { { "mode", "focused_structured_surface" },
                  { "planning_target",
                    "auto_fill_or_refill_visible_surface" },
                  { "model_decides_patch_scope", true },
                  { "preview_artifact", "structured_surface_patch_overlay" },
                  { "accept_unit", "guarded_surface_patch" },
                  { "guarded_accept_required", true },
                  { "surface_revision_required", true },
                  { "selection_fingerprint_required", true } };

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
            packet["surface_guard_facts"] =
                    surfaceGuardFactsJson( *panel, state );
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
    nlohmann::json arguments = objectFromJsonText( aSuggestion.m_ArgumentsJson );

    if( arguments.contains( "source_tool" ) && arguments["source_tool"].is_string() )
        return arguments["source_tool"].get<std::string>();

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


nlohmann::json stableFingerprintJson( const nlohmann::json& aValue )
{
    if( aValue.is_array() )
    {
        nlohmann::json normalized = nlohmann::json::array();

        for( const nlohmann::json& item : aValue )
            normalized.push_back( stableFingerprintJson( item ) );

        return normalized;
    }

    if( !aValue.is_object() )
        return aValue;

    const bool looksLikeSessionHandle =
            aValue.contains( "session_id" ) && aValue.contains( "handle_id" )
            && aValue.contains( "generation" );

    if( looksLikeSessionHandle )
    {
        if( aValue.contains( "alias" ) && aValue["alias"].is_string() )
        {
            return { { "handle_alias", aValue["alias"] } };
        }

        if( aValue.contains( "status" ) && aValue["status"].is_string() )
        {
            return { { "handle_status", aValue["status"] } };
        }

        return { { "handle", "session_handle" } };
    }

    nlohmann::json normalized = nlohmann::json::object();

    for( auto it = aValue.begin(); it != aValue.end(); ++it )
        normalized[it.key()] = stableFingerprintJson( it.value() );

    return normalized;
}


wxString stableObjectFingerprintMaterial( const AI_OBJECT_REF& aObject )
{
    nlohmann::json details =
            nlohmann::json::parse( toUtf8String( aObject.m_DetailsJson ),
                                   nullptr, false );

    if( !details.is_discarded() && details.is_object()
        && details.value( "source", std::string() )
                   == "next_action_attempt_journal" )
    {
        nlohmann::json stable =
                { { "source", "next_action_attempt_journal" },
                  { "type", static_cast<int>( aObject.m_Type ) },
                  { "kind", details.value( "kind", std::string() ) },
                  { "arguments",
                    stableFingerprintJson(
                            details.value( "arguments",
                                           nlohmann::json::object() ) ) } };

        if( details.contains( "merged_from_tool" ) )
            stable["merged_from_tool"] = details["merged_from_tool"];

        return fromUtf8String( stable.dump() );
    }

    wxString material;
    material << static_cast<int>( aObject.m_Type )
             << wxS( ":" ) << aObject.m_Label
             << wxS( ":" ) << aObject.m_DetailsJson;
    return material;
}


wxString suggestionFingerprint( const AI_SUGGESTION_RECORD& aSuggestion,
                                const AI_NEXT_ACTION_CONTEXT_VERSION& aVersion )
{
    wxString material;
    material << aVersion.AsJsonText()
             << wxS( "|title=" ) << aSuggestion.m_Title
             << wxS( "|arguments=" ) << aSuggestion.m_ArgumentsJson
             << wxS( "|preview_objects=" ) << aSuggestion.m_PreviewObjects.size();

    for( const AI_OBJECT_REF& object : aSuggestion.m_PreviewObjects )
    {
        material << wxS( "|preview:" )
                 << stableObjectFingerprintMaterial( object );
    }

    material << wxS( "|edit_objects=" ) << aSuggestion.m_EditObjects.size();

    for( const AI_OBJECT_REF& object : aSuggestion.m_EditObjects )
    {
        material << wxS( "|edit:" )
                 << stableObjectFingerprintMaterial( object );
    }

    wxString fingerprint;
    fingerprint << wxS( "next-action|" ) << fnv1a64Fingerprint( material );
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
        const nlohmann::json arguments = objectFromJsonText( aCandidate.m_ArgumentsJson );
        const bool isParallelCandidate =
                arguments.value( "source_tool", std::string() )
                == "routing.generate_parallel_segment_candidates";
        const bool isBusCandidate =
                arguments.value( "source_tool", std::string() )
                == "routing.generate_bus_segment_candidates";

        return { { "kind", "routing_landing" },
                 { "source",
                   isBusCandidate
                           ? "bus_reference.offset"
                           : isParallelCandidate ? "parallel_reference.offset"
                                                 : "operation.end" },
                 { "point", pointJson( operation->m_End ) },
                 { "start", pointJson( operation->m_Start ) },
                 { "net", toUtf8String( operation->m_NetName ) },
                 { "layer", toUtf8String( operation->m_LayerName ) },
                 { "width", operation->m_Width } };
    }

    if( operation->IsMove() || operation->IsMoveSelected() )
    {
        const nlohmann::json arguments = objectFromJsonText( aCandidate.m_ArgumentsJson );

        if( arguments.value( "source_tool", std::string() )
                    == "placement.generate_footprint_transform_candidates"
            && arguments.contains( "footprint_transform_facts" )
            && arguments["footprint_transform_facts"].is_object() )
        {
            const nlohmann::json& facts = arguments["footprint_transform_facts"];
            int targetX = 0;
            int targetY = 0;

            if( jsonPointToInts( facts.value( "target_position",
                                              nlohmann::json::object() ),
                                 targetX, targetY ) )
            {
                return { { "kind", "placement_landing" },
                         { "source", "footprint_transform.target_position" },
                         { "position", pointRecordJson( targetX, targetY ) },
                         { "delta", pointJson( operation->m_MoveDelta ) },
                         { "transform_kind", "translate" } };
            }
        }
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

    if( arguments.contains( "parallel_facts" ) && arguments["parallel_facts"].is_object() )
        record["parallel_facts"] = arguments["parallel_facts"];

    if( arguments.contains( "bus_facts" ) && arguments["bus_facts"].is_object() )
        record["bus_facts"] = arguments["bus_facts"];

    if( arguments.contains( "footprint_transform_facts" )
        && arguments["footprint_transform_facts"].is_object() )
    {
        record["footprint_transform_facts"] =
                arguments["footprint_transform_facts"];
    }

    nlohmann::json landingFacts = candidateLandingFactsJson( aCandidate );

    if( !landingFacts.empty() )
        record["landing_facts"] = std::move( landingFacts );

    return record;
}


bool operationCanBeAttemptedDirectly(
        const AI_SUGGESTION_OPERATION& aOperation )
{
    return aOperation.IsPlaceViaPreview()
           || aOperation.IsRouteSegmentPreview()
           || aOperation.IsCreateShapePreview()
           || aOperation.IsCreateCopperZonePreview()
           || aOperation.IsPanelFillColumnPreview();
}


bool candidateArgumentsHaveExecutablePlan( const nlohmann::json& aArguments )
{
    if( !aArguments.is_object()
        || !aArguments.contains( "plan" )
        || !aArguments["plan"].is_object()
        || !aArguments["plan"].contains( "operations" )
        || !aArguments["plan"]["operations"].is_array()
        || aArguments["plan"]["operations"].empty()
        || aArguments["plan"]["operations"].size() > 32 )
    {
        return false;
    }

    for( const nlohmann::json& operation : aArguments["plan"]["operations"] )
    {
        if( !operation.is_object()
            || !operation.contains( "kind" )
            || !operation["kind"].is_string()
            || sessionOperationKindFromId( operation["kind"].get<std::string>() )
                       == AI_SESSION_OPERATION_KIND::Unknown )
        {
            return false;
        }

        if( operation.contains( "arguments" )
            && !operation["arguments"].is_object() )
        {
            return false;
        }
    }

    return true;
}


std::optional<AI_SUGGESTION_RECORD> candidateRecordFromToolResultJson(
        const nlohmann::json& aRecord,
        const AI_OBSERVATION_PACKET& aObservation )
{
    if( !aRecord.is_object()
        || aRecord.value( "publish_allowed", false )
        || !aRecord.contains( "arguments" )
        || !aRecord["arguments"].is_object() )
    {
        return std::nullopt;
    }

    nlohmann::json arguments = aRecord["arguments"];
    const std::string sourceTool =
            aRecord.value( "source_tool", std::string() );

    if( aRecord.contains( "plan" ) && aRecord["plan"].is_object()
        && ( !arguments.contains( "plan" )
             || !arguments["plan"].is_object() ) )
    {
        arguments["plan"] = aRecord["plan"];
    }

    if( !sourceTool.empty()
        && ( !arguments.contains( "source_tool" )
             || !arguments["source_tool"].is_string() ) )
    {
        arguments["source_tool"] = sourceTool;
    }

    const wxString argumentsJson = fromUtf8String( arguments.dump() );
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( argumentsJson );
    const bool hasExecutablePlan =
            candidateArgumentsHaveExecutablePlan( arguments );

    if( ( !operation || !operationCanBeAttemptedDirectly( *operation ) )
        && !hasExecutablePlan )
    {
        return std::nullopt;
    }

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = aObservation.m_ContextVersion.m_ContextVersion;
    suggestion.m_TriggerActivitySequence =
            aObservation.m_Activity.m_Sequence;
    suggestion.m_Title = fromUtf8String(
            aRecord.value( "title", std::string( "Tool generated candidate" ) ) );
    suggestion.m_Body = fromUtf8String(
            aRecord.value( "body", std::string() ) );
    suggestion.m_ContextKind = fromUtf8String(
            aRecord.value( "context_kind",
                           toUtf8String( aObservation.m_Kind ) ) );
    suggestion.m_ArgumentsJson = argumentsJson;
    suggestion.m_PreviewOnly = true;

    nlohmann::json details =
            { { "source", "decision_tool_result" },
              { "source_tool", sourceTool },
              { "tool_candidate_index",
                unsignedField( aRecord, "index", 0 ) },
              { "direct_attemptable", true } };
    suggestion.m_ContextDetailsJson = fromUtf8String( details.dump() );
    suggestion.m_Fingerprint << wxS( "decision-tool-candidate|" )
                             << aObservation.m_ContextVersion.AsJsonText()
                             << wxS( "|" )
                             << fromUtf8String( sourceTool )
                             << wxS( "|" ) << suggestion.m_ArgumentsJson;
    return suggestion;
}


std::vector<AI_SUGGESTION_RECORD> decisionToolGeneratedCandidates(
        const AI_NEXT_ACTION_LLM_DECISION& aDecision,
        const AI_OBSERVATION_PACKET& aObservation )
{
    std::vector<AI_SUGGESTION_RECORD> candidates;
    nlohmann::json decision = parseObjectBody( aDecision.m_RawJson );

    if( !decision.contains( "provider_tool_results" )
        || !decision["provider_tool_results"].is_array() )
    {
        return candidates;
    }

    for( const nlohmann::json& toolRecord : decision["provider_tool_results"] )
    {
        if( !toolRecord.is_object()
            || !toolRecord.value( "allowed", false )
            || !toolRecord.value( "executed", false )
            || !toolRecord.contains( "result" )
            || !toolRecord["result"].is_object() )
        {
            continue;
        }

        const nlohmann::json& result = toolRecord["result"];

        if( result.value( "status", std::string() )
                    != "candidates_generated"
            || !result.contains( "candidates" )
            || !result["candidates"].is_array() )
        {
            continue;
        }

        for( const nlohmann::json& candidateJson : result["candidates"] )
        {
            std::optional<AI_SUGGESTION_RECORD> candidate =
                    candidateRecordFromToolResultJson( candidateJson,
                                                       aObservation );

            if( candidate )
                candidates.push_back( std::move( *candidate ) );
        }
    }

    return candidates;
}


std::optional<std::string> stringField( const nlohmann::json& aObject,
                                        const char* aField )
{
    if( !aObject.is_object() || !aObject.contains( aField )
        || !aObject[aField].is_string() )
    {
        return std::nullopt;
    }

    return aObject[aField].get<std::string>();
}


std::optional<int> positiveIntField( const nlohmann::json& aObject,
                                     const char* aField )
{
    if( !aObject.is_object() || !aObject.contains( aField )
        || !aObject[aField].is_number_integer()
        || aObject[aField].get<int>() <= 0 )
    {
        return std::nullopt;
    }

    return aObject[aField].get<int>();
}


nlohmann::json placementFootprintTransformCandidatePayloadJson(
        const nlohmann::json& aArgs )
{
    int currentX = 0;
    int currentY = 0;
    int targetX = 0;
    int targetY = 0;

    if( !jsonPointToInts( aArgs.value( "current_position",
                                       nlohmann::json::object() ),
                          currentX, currentY )
        || !jsonPointToInts( aArgs.value( "target_position",
                                          nlohmann::json::object() ),
                             targetX, targetY ) )
    {
        return { { "tool", "placement.generate_footprint_transform_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "placement.generate_footprint_transform_candidates requires "
                   "current_position and target_position points." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    const int dx = targetX - currentX;
    const int dy = targetY - currentY;
    const std::string footprintRef =
            stringField( aArgs, "footprint_ref" ).value_or( "selected_footprint" );
    nlohmann::json transformFacts =
            { { "footprint_ref", footprintRef },
              { "current_position", pointRecordJson( currentX, currentY ) },
              { "target_position", pointRecordJson( targetX, targetY ) },
              { "delta", pointRecordJson( dx, dy ) },
              { "transform_kind", "translate" },
              { "rotation_supported", false },
              { "generation_strategy", "translate_current_to_target" } };
    nlohmann::json operation =
            { { "operation", "move_selected" },
              { "source_tool",
                "placement.generate_footprint_transform_candidates" },
              { "candidate_strategy", "translate_current_to_target" },
              { "dx", dx },
              { "dy", dy },
              { "footprint_transform_facts", transformFacts } };
    nlohmann::json candidate =
            { { "index", 0 },
              { "title", "Footprint placement transform" },
              { "body",
                "Candidate selected-footprint translation to a target position." },
              { "source_tool",
                "placement.generate_footprint_transform_candidates" },
              { "context_kind", "placement" },
              { "preview_object_count", 1 },
              { "edit_object_count", 1 },
              { "arguments", operation },
              { "operation", "move_selected" },
              { "footprint_transform_facts", transformFacts },
              { "landing_facts",
                { { "kind", "placement_landing" },
                  { "source", "footprint_transform.target_position" },
                  { "position", pointRecordJson( targetX, targetY ) },
                  { "delta", pointRecordJson( dx, dy ) },
                  { "transform_kind", "translate" } } },
              { "publish_allowed", false } };

    return { { "tool", "placement.generate_footprint_transform_candidates" },
             { "status", "candidates_generated" },
             { "candidate_count", 1 },
             { "candidates", nlohmann::json::array( { candidate } ) } };
}


nlohmann::json placementFootprintOrientationCandidatePayloadJson(
        const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "handles" ) || !aArgs["handles"].is_array()
        || aArgs["handles"].empty()
        || !aArgs.contains( "current_orientation_degrees" )
        || !aArgs["current_orientation_degrees"].is_number()
        || !aArgs.contains( "target_orientation_degrees" )
        || !aArgs["target_orientation_degrees"].is_number() )
    {
        return { { "tool",
                   "placement.generate_footprint_orientation_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "placement.generate_footprint_orientation_candidates "
                   "requires handles, current_orientation_degrees, and "
                   "target_orientation_degrees." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    const int currentOrientation =
            aArgs["current_orientation_degrees"].get<int>();
    const int targetOrientation = aArgs["target_orientation_degrees"].get<int>();
    const int orientationDelta = targetOrientation - currentOrientation;
    const std::string footprintRef =
            stringField( aArgs, "footprint_ref" ).value_or( "selected_footprint" );
    const std::optional<std::string> targetSide =
            stringField( aArgs, "target_side" );

    nlohmann::json typedProps =
            { { "orientation_degrees", targetOrientation },
              { "current_orientation_degrees", currentOrientation },
              { "orientation_delta_degrees", orientationDelta } };

    if( targetSide )
        typedProps["side"] = *targetSide;

    nlohmann::json orientationFacts =
            { { "footprint_ref", footprintRef },
              { "current_orientation_degrees", currentOrientation },
              { "target_orientation_degrees", targetOrientation },
              { "orientation_delta_degrees", orientationDelta },
              { "transform_kind", "orientation" },
              { "generation_strategy", "orient_selected_to_target" },
              { "requires_hidden_render", true },
              { "validation_hint",
                "run_validate_hidden_attempt_before_publish" } };

    if( targetSide )
        orientationFacts["target_side"] = *targetSide;

    nlohmann::json operationArguments =
            { { "handles", aArgs["handles"] },
              { "typed_props", typedProps },
              { "metadata",
                { { "source_tool",
                    "placement.generate_footprint_orientation_candidates" },
                  { "candidate_strategy", "orient_selected_to_target" },
                  { "footprint_ref", footprintRef } } } };

    nlohmann::json operation =
            { { "operation", "orient_selected_footprint" },
              { "source_tool",
                "placement.generate_footprint_orientation_candidates" },
              { "candidate_strategy", "orient_selected_to_target" },
              { "handles", aArgs["handles"] },
              { "current_orientation_degrees", currentOrientation },
              { "target_orientation_degrees", targetOrientation },
              { "orientation_delta_degrees", orientationDelta },
              { "footprint_orientation_facts", orientationFacts } };

    if( targetSide )
        operation["target_side"] = *targetSide;

    nlohmann::json candidate =
            { { "index", 0 },
              { "title", "Footprint orientation transform" },
              { "body",
                "Candidate selected-footprint orientation and side transform." },
              { "source_tool",
                "placement.generate_footprint_orientation_candidates" },
              { "context_kind", "placement" },
              { "preview_object_count", 1 },
              { "edit_object_count", 1 },
              { "arguments", operation },
              { "operation", "orient_selected_footprint" },
              { "footprint_orientation_facts", orientationFacts },
              { "landing_facts",
                { { "kind", "placement_landing" },
                  { "source", "footprint_orientation.target_orientation" },
                  { "orientation_degrees", targetOrientation },
                  { "orientation_delta_degrees", orientationDelta },
                  { "transform_kind", "orientation" } } },
              { "plan",
                { { "operations",
                    nlohmann::json::array(
                            { { { "kind", "pcb.set_item_properties" },
                                { "arguments", operationArguments } } } ) } } },
              { "publish_allowed", false } };

    if( targetSide )
        candidate["landing_facts"]["target_side"] = *targetSide;

    return { { "tool", "placement.generate_footprint_orientation_candidates" },
             { "status", "candidates_generated" },
             { "candidate_count", 1 },
             { "candidates", nlohmann::json::array( { candidate } ) } };
}


nlohmann::json routingParallelCandidatePayloadJson(
        const nlohmann::json& aArgs,
        const AI_OBSERVATION_PACKET& aObservation )
{
    int referenceStartX = 0;
    int referenceStartY = 0;
    int referenceEndX = 0;
    int referenceEndY = 0;
    int offsetX = 0;
    int offsetY = 0;

    if( !jsonPointToInts( aArgs.value( "reference_start",
                                       nlohmann::json::object() ),
                          referenceStartX, referenceStartY )
        || !jsonPointToInts( aArgs.value( "reference_end",
                                          nlohmann::json::object() ),
                             referenceEndX, referenceEndY )
        || !jsonPointToInts( aArgs.value( "offset", nlohmann::json::object() ),
                             offsetX, offsetY ) )
    {
        return { { "tool", "routing.generate_parallel_segment_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_parallel_segment_candidates requires "
                   "reference_start, reference_end, and offset points." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    const nlohmann::json modeContext =
            objectFromJsonText(
                    aObservation.m_ContextSnapshot.m_ToolState.m_ModeContextJson );
    std::optional<std::string> net = stringField( aArgs, "net" );
    std::optional<std::string> layer = stringField( aArgs, "layer" );
    std::optional<int>         width = positiveIntField( aArgs, "width" );

    if( !net )
        net = stringField( modeContext, "net" );

    if( !layer )
        layer = stringField( modeContext, "layer" );

    if( !width )
        width = positiveIntField( modeContext, "width" );

    if( !net || !layer || !width )
    {
        return { { "tool", "routing.generate_parallel_segment_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_parallel_segment_candidates requires net, "
                   "layer, and width from arguments or active routing context." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    const int startX = referenceStartX + offsetX;
    const int startY = referenceStartY + offsetY;
    const int endX = referenceEndX + offsetX;
    const int endY = referenceEndY + offsetY;
    nlohmann::json parallelFacts =
            { { "reference_start", pointRecordJson( referenceStartX, referenceStartY ) },
              { "reference_end", pointRecordJson( referenceEndX, referenceEndY ) },
              { "offset", pointRecordJson( offsetX, offsetY ) },
              { "candidate_start", pointRecordJson( startX, startY ) },
              { "candidate_end", pointRecordJson( endX, endY ) },
              { "segment_style",
                toUtf8String( routingSegmentStyle( referenceEndX - referenceStartX,
                                                   referenceEndY - referenceStartY ) ) },
              { "generation_strategy", "parallel_reference_offset" } };
    nlohmann::json operation =
            { { "operation", "route_segment_preview" },
              { "source_tool", "routing.generate_parallel_segment_candidates" },
              { "candidate_strategy", "parallel_reference_offset" },
              { "net", *net },
              { "layer", *layer },
              { "width", *width },
              { "start", pointRecordJson( startX, startY ) },
              { "end", pointRecordJson( endX, endY ) },
              { "parallel_facts", parallelFacts } };
    nlohmann::json candidate =
            { { "index", 0 },
              { "title", "Parallel route segment" },
              { "body", "Candidate route segment parallel to a reference segment." },
              { "source_tool", "routing.generate_parallel_segment_candidates" },
              { "context_kind", "routing" },
              { "preview_object_count", 1 },
              { "edit_object_count", 1 },
              { "arguments", operation },
              { "operation", "route_segment_preview" },
              { "parallel_facts", parallelFacts },
              { "landing_facts",
                { { "kind", "routing_landing" },
                  { "source", "parallel_reference.offset" },
                  { "point", pointRecordJson( endX, endY ) },
                  { "start", pointRecordJson( startX, startY ) },
                  { "net", *net },
                  { "layer", *layer },
                  { "width", *width } } },
              { "publish_allowed", false } };

    return { { "tool", "routing.generate_parallel_segment_candidates" },
             { "status", "candidates_generated" },
             { "candidate_count", 1 },
             { "candidates", nlohmann::json::array( { candidate } ) } };
}


nlohmann::json routingBusCandidatePayloadJson(
        const nlohmann::json& aArgs,
        const AI_OBSERVATION_PACKET& aObservation )
{
    int referenceStartX = 0;
    int referenceStartY = 0;
    int referenceEndX = 0;
    int referenceEndY = 0;

    if( !jsonPointToInts( aArgs.value( "reference_start",
                                       nlohmann::json::object() ),
                          referenceStartX, referenceStartY )
        || !jsonPointToInts( aArgs.value( "reference_end",
                                          nlohmann::json::object() ),
                             referenceEndX, referenceEndY )
        || !aArgs.contains( "lane_offsets" )
        || !aArgs["lane_offsets"].is_array()
        || aArgs["lane_offsets"].empty()
        || aArgs["lane_offsets"].size() > 16 )
    {
        return { { "tool", "routing.generate_bus_segment_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_bus_segment_candidates requires "
                   "reference_start, reference_end, and 1-16 lane_offsets." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    const nlohmann::json modeContext =
            objectFromJsonText(
                    aObservation.m_ContextSnapshot.m_ToolState.m_ModeContextJson );
    std::optional<std::string> layer = stringField( aArgs, "layer" );
    std::optional<int>         width = positiveIntField( aArgs, "width" );

    if( !layer )
        layer = stringField( modeContext, "layer" );

    if( !width )
        width = positiveIntField( modeContext, "width" );

    if( !layer || !width )
    {
        return { { "tool", "routing.generate_bus_segment_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_bus_segment_candidates requires layer "
                   "and width from arguments or active routing context." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    std::vector<std::string> nets;

    if( aArgs.contains( "nets" ) && aArgs["nets"].is_array() )
    {
        for( const nlohmann::json& netValue : aArgs["nets"] )
        {
            if( !netValue.is_string() )
            {
                return { { "tool", "routing.generate_bus_segment_candidates" },
                         { "status", "malformed_arguments" },
                         { "error_code", "malformed_arguments" },
                         { "message",
                           "routing.generate_bus_segment_candidates requires "
                           "string nets when nets is provided." },
                         { "candidate_count", 0 },
                         { "candidates", nlohmann::json::array() } };
            }

            nets.push_back( netValue.get<std::string>() );
        }
    }

    if( nets.empty() )
    {
        std::optional<std::string> activeNet = stringField( modeContext, "net" );

        if( !activeNet )
        {
            return { { "tool", "routing.generate_bus_segment_candidates" },
                     { "status", "malformed_arguments" },
                     { "error_code", "malformed_arguments" },
                     { "message",
                       "routing.generate_bus_segment_candidates requires nets "
                       "or active routing net context." },
                     { "candidate_count", 0 },
                     { "candidates", nlohmann::json::array() } };
        }

        nets.assign( aArgs["lane_offsets"].size(), *activeNet );
    }
    else if( nets.size() != aArgs["lane_offsets"].size() )
    {
        return { { "tool", "routing.generate_bus_segment_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_bus_segment_candidates requires nets "
                   "count to match lane_offsets count." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    nlohmann::json candidates = nlohmann::json::array();
    const int laneCount = static_cast<int>( aArgs["lane_offsets"].size() );

    for( int laneIndex = 0; laneIndex < laneCount; ++laneIndex )
    {
        int offsetX = 0;
        int offsetY = 0;

        if( !jsonPointToInts( aArgs["lane_offsets"].at( laneIndex ),
                              offsetX, offsetY ) )
        {
            return { { "tool", "routing.generate_bus_segment_candidates" },
                     { "status", "malformed_arguments" },
                     { "error_code", "malformed_arguments" },
                     { "message",
                       "routing.generate_bus_segment_candidates requires every "
                       "lane offset to be an x/y point." },
                     { "candidate_count", 0 },
                     { "candidates", nlohmann::json::array() } };
        }

        const int startX = referenceStartX + offsetX;
        const int startY = referenceStartY + offsetY;
        const int endX = referenceEndX + offsetX;
        const int endY = referenceEndY + offsetY;
        nlohmann::json busFacts =
                { { "reference_start",
                    pointRecordJson( referenceStartX, referenceStartY ) },
                  { "reference_end",
                    pointRecordJson( referenceEndX, referenceEndY ) },
                  { "offset", pointRecordJson( offsetX, offsetY ) },
                  { "candidate_start", pointRecordJson( startX, startY ) },
                  { "candidate_end", pointRecordJson( endX, endY ) },
                  { "lane_index", laneIndex },
                  { "lane_count", laneCount },
                  { "net", nets.at( laneIndex ) },
                  { "segment_style",
                    toUtf8String( routingSegmentStyle(
                            referenceEndX - referenceStartX,
                            referenceEndY - referenceStartY ) ) },
                  { "generation_strategy", "bus_reference_offsets" } };
        nlohmann::json operation =
                { { "operation", "route_segment_preview" },
                  { "source_tool", "routing.generate_bus_segment_candidates" },
                  { "candidate_strategy", "bus_reference_offsets" },
                  { "net", nets.at( laneIndex ) },
                  { "layer", *layer },
                  { "width", *width },
                  { "start", pointRecordJson( startX, startY ) },
                  { "end", pointRecordJson( endX, endY ) },
                  { "bus_facts", busFacts } };

        candidates.push_back(
                { { "index", laneIndex },
                  { "title",
                    std::string( "Bus route segment lane " )
                            + std::to_string( laneIndex + 1 ) },
                  { "body",
                    "Candidate bus lane segment parallel to a reference segment." },
                  { "source_tool", "routing.generate_bus_segment_candidates" },
                  { "context_kind", "routing" },
                  { "preview_object_count", 1 },
                  { "edit_object_count", 1 },
                  { "arguments", operation },
                  { "operation", "route_segment_preview" },
                  { "bus_facts", busFacts },
                  { "landing_facts",
                    { { "kind", "routing_landing" },
                      { "source", "bus_reference.offset" },
                      { "point", pointRecordJson( endX, endY ) },
                      { "start", pointRecordJson( startX, startY ) },
                      { "net", nets.at( laneIndex ) },
                      { "layer", *layer },
                      { "width", *width } } },
                  { "publish_allowed", false } } );
    }

    return { { "tool", "routing.generate_bus_segment_candidates" },
             { "status", "candidates_generated" },
             { "candidate_count", laneCount },
             { "candidates", candidates } };
}


nlohmann::json routingReplacePathCandidatePayloadJson(
        const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "replace_handles" )
        || !aArgs["replace_handles"].is_array()
        || aArgs["replace_handles"].empty()
        || !aArgs.contains( "replacement_points" )
        || !aArgs["replacement_points"].is_array()
        || aArgs["replacement_points"].size() < 2
        || !aArgs.contains( "net" )
        || !aArgs["net"].is_string()
        || !aArgs.contains( "layer" )
        || !aArgs["layer"].is_string()
        || !aArgs.contains( "width" )
        || !aArgs["width"].is_number_integer()
        || aArgs["width"].get<int>() <= 0 )
    {
        return { { "tool", "routing.generate_replace_path_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_replace_path_candidates requires "
                   "replace_handles, replacement_points, net, layer, and width." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    nlohmann::json replacementPoints = nlohmann::json::array();
    int            startX = 0;
    int            startY = 0;
    int            endX = 0;
    int            endY = 0;

    for( size_t index = 0; index < aArgs["replacement_points"].size(); ++index )
    {
        int x = 0;
        int y = 0;

        if( !jsonPointToInts( aArgs["replacement_points"].at( index ), x, y ) )
        {
            return { { "tool", "routing.generate_replace_path_candidates" },
                     { "status", "malformed_arguments" },
                     { "error_code", "malformed_arguments" },
                     { "message",
                       "routing.generate_replace_path_candidates requires every "
                       "replacement point to be an x/y point." },
                     { "candidate_count", 0 },
                     { "candidates", nlohmann::json::array() } };
        }

        if( index == 0 )
        {
            startX = x;
            startY = y;
        }

        if( index + 1 == aArgs["replacement_points"].size() )
        {
            endX = x;
            endY = y;
        }

        replacementPoints.push_back( pointRecordJson( x, y ) );
    }

    const std::string net = aArgs["net"].get<std::string>();
    const std::string layer = aArgs["layer"].get<std::string>();
    const int width = aArgs["width"].get<int>();
    nlohmann::json replaceFacts =
            { { "replace_handle_count", aArgs["replace_handles"].size() },
              { "point_count", replacementPoints.size() },
              { "start", pointRecordJson( startX, startY ) },
              { "end", pointRecordJson( endX, endY ) },
              { "net", net },
              { "layer", layer },
              { "width", width },
              { "generation_strategy", "delete_then_create_polyline" } };
    nlohmann::json plan =
            { { "operations",
                nlohmann::json::array(
                        { { { "kind", "pcb.delete_items" },
                            { "arguments",
                              { { "handles", aArgs["replace_handles"] },
                                { "alias", "replace_path_delete" },
                                { "metadata",
                                  { { "source_tool",
                                      "routing.generate_replace_path_candidates" } } } } } },
                          { { "kind", "pcb.create_track_polyline" },
                            { "arguments",
                              { { "points", replacementPoints },
                                { "layer", layer },
                                { "net", net },
                                { "width", width },
                                { "alias", "replace_path_polyline" },
                                { "metadata",
                                  { { "source_tool",
                                      "routing.generate_replace_path_candidates" } } } } } } } ) } };
    nlohmann::json candidate =
            { { "index", 0 },
              { "title", "Replace route path" },
              { "body",
                "Candidate bounded plan that deletes existing route items and "
                "creates a replacement polyline." },
              { "source_tool", "routing.generate_replace_path_candidates" },
              { "context_kind", "routing" },
              { "preview_object_count", 1 },
              { "edit_object_count", 2 },
              { "operation", "replace_path_plan" },
              { "plan", plan },
              { "arguments",
                { { "operation", "replace_path_plan" },
                  { "source_tool", "routing.generate_replace_path_candidates" },
                  { "candidate_strategy", "delete_then_create_polyline" },
                  { "plan", plan },
                  { "replace_path_facts", replaceFacts } } },
              { "replace_path_facts", replaceFacts },
              { "landing_facts",
                { { "kind", "routing_landing" },
                  { "source", "replace_path.replacement_points.end" },
                  { "point", pointRecordJson( endX, endY ) },
                  { "start", pointRecordJson( startX, startY ) },
                  { "net", net },
                  { "layer", layer },
                  { "width", width } } },
              { "publish_allowed", false } };

    return { { "tool", "routing.generate_replace_path_candidates" },
             { "status", "candidates_generated" },
             { "candidate_count", 1 },
             { "candidates", nlohmann::json::array( { candidate } ) } };
}


nlohmann::json routingConstraintAwareRerouteCandidatePayloadJson(
        const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "constraints" ) || !aArgs["constraints"].is_object() )
    {
        return { { "tool",
                   "routing.generate_constraint_aware_reroute_candidates" },
                 { "status", "malformed_arguments" },
                 { "error_code", "malformed_arguments" },
                 { "message",
                   "routing.generate_constraint_aware_reroute_candidates "
                   "requires constraints." },
                 { "candidate_count", 0 },
                 { "candidates", nlohmann::json::array() } };
    }

    nlohmann::json replacePayload =
            routingReplacePathCandidatePayloadJson( aArgs );

    if( replacePayload.value( "status", std::string() )
        == "malformed_arguments" )
    {
        replacePayload["tool"] =
                "routing.generate_constraint_aware_reroute_candidates";
        return replacePayload;
    }

    nlohmann::json& candidate = replacePayload["candidates"].at( 0 );
    nlohmann::json  facts = candidate["replace_path_facts"];
    facts["constraints"] = aArgs["constraints"];
    facts["generation_strategy"] = "constraint_aware_delete_then_create_polyline";
    facts["validation_hint"] = "run_validate_hidden_attempt_before_publish";

    candidate["source_tool"] =
            "routing.generate_constraint_aware_reroute_candidates";
    candidate["title"] = "Constraint-aware reroute";
    candidate["body"] =
            "Candidate bounded plan for a replacement route path with explicit "
            "constraint facts for model review.";
    candidate["operation"] = "constraint_aware_reroute_plan";
    candidate["constraint_aware_reroute_facts"] = facts;
    candidate["arguments"]["operation"] = "constraint_aware_reroute_plan";
    candidate["arguments"]["source_tool"] =
            "routing.generate_constraint_aware_reroute_candidates";
    candidate["arguments"]["candidate_strategy"] =
            "constraint_aware_delete_then_create_polyline";
    candidate["arguments"]["constraint_aware_reroute_facts"] = facts;
    candidate["plan"]["operations"].at( 0 )["arguments"]["metadata"]["source_tool"] =
            "routing.generate_constraint_aware_reroute_candidates";
    candidate["plan"]["operations"].at( 1 )["arguments"]["metadata"]["source_tool"] =
            "routing.generate_constraint_aware_reroute_candidates";
    candidate["landing_facts"]["source"] =
            "constraint_reroute.replacement_points.end";
    replacePayload["tool"] =
            "routing.generate_constraint_aware_reroute_candidates";

    return replacePayload;
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


KICAD_T editObjectTypeForJournalOperation( const std::string& aKind )
{
    const AI_SESSION_OPERATION_KIND kind = sessionOperationKindFromId( aKind );

    switch( kind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
        return PCB_VIA_T;

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
        return PCB_TRACE_T;

    case AI_SESSION_OPERATION_KIND::CreateZone:
    case AI_SESSION_OPERATION_KIND::RefillZones:
        return PCB_ZONE_T;

    case AI_SESSION_OPERATION_KIND::CreateShape:
        return PCB_SHAPE_T;

    default:
        return TYPE_NOT_INIT;
    }
}


wxString editObjectLabelForJournalOperation( const nlohmann::json& aOperation,
                                             const nlohmann::json& aArguments,
                                             size_t aFallbackIndex )
{
    const std::string kind = aOperation.value( "kind", std::string( "unknown" ) );
    const uint64_t operationId = unsignedField( aOperation, "operation_id",
                                                aFallbackIndex + 1 );

    if( aArguments.is_object() && aArguments.contains( "alias" )
        && aArguments["alias"].is_string() )
    {
        return fromUtf8String( "nextaction:" + kind + ":"
                               + aArguments["alias"].get<std::string>() );
    }

    return fromUtf8String( "nextaction:" + kind + ":"
                           + std::to_string( operationId ) );
}


std::vector<AI_OBJECT_REF> editObjectsFromAttemptJournal(
        const wxString& aJournalJson )
{
    std::vector<AI_OBJECT_REF> editObjects;
    nlohmann::json             journal = parseObjectBody( aJournalJson );

    if( !journal.is_object() || !journal.contains( "operations" )
        || !journal["operations"].is_array() )
    {
        return editObjects;
    }

    size_t fallbackIndex = 0;

    for( const nlohmann::json& operation : journal["operations"] )
    {
        if( !operation.is_object()
            || !operation.value( "is_mutation", false )
            || !operation.contains( "kind" )
            || !operation["kind"].is_string() )
        {
            continue;
        }

        const std::string kind = operation["kind"].get<std::string>();

        if( sessionOperationKindFromId( kind ) == AI_SESSION_OPERATION_KIND::Unknown )
            continue;

        nlohmann::json arguments =
                operation.contains( "arguments" ) && operation["arguments"].is_object()
                        ? operation["arguments"]
                        : nlohmann::json::object();

        nlohmann::json details =
                { { "source", "next_action_attempt_journal" },
                  { "kind", kind },
                  { "arguments", arguments },
                  { "operation_id", unsignedField( operation, "operation_id",
                                                   fallbackIndex + 1 ) },
                  { "step_id", unsignedField( operation, "step_id", 0 ) },
                  { "before_epoch", unsignedField( operation, "before_epoch", 0 ) },
                  { "after_epoch", unsignedField( operation, "after_epoch", 0 ) } };

        for( const char* key : { "merged_from_tool", "merged_from_tool_call_id" } )
        {
            if( operation.contains( key ) )
                details[key] = operation[key];
        }

        if( operation.contains( "resolved_handles" ) )
            details["resolved_handles"] = operation["resolved_handles"];

        if( operation.contains( "created_handles" ) )
            details["created_handles"] = operation["created_handles"];

        if( operation.contains( "result" ) )
            details["result"] = operation["result"];

        if( operation.contains( "warnings" ) )
            details["warnings"] = operation["warnings"];

        editObjects.emplace_back(
                KIID(), editObjectTypeForJournalOperation( kind ),
                editObjectLabelForJournalOperation( operation, arguments,
                                                    fallbackIndex ),
                fromUtf8String( details.dump() ) );
        ++fallbackIndex;
    }

    return editObjects;
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


const nlohmann::json* surfacePatchCellValue(
        const nlohmann::json& aSurfaceState, const std::string& aSurfaceId,
        const std::string& aTableId, const std::string& aRowId,
        const std::string& aColumnId )
{
    if( !aSurfaceState.contains( "surfaces" )
        || !aSurfaceState["surfaces"].is_object()
        || !aSurfaceState["surfaces"].contains( aSurfaceId ) )
    {
        return nullptr;
    }

    const nlohmann::json& surface = aSurfaceState["surfaces"][aSurfaceId];

    if( !surface.is_object() || !surface.contains( "tables" )
        || !surface["tables"].is_object()
        || !surface["tables"].contains( aTableId ) )
    {
        return nullptr;
    }

    const nlohmann::json& table = surface["tables"][aTableId];

    if( !table.is_object() || !table.contains( "rows" )
        || !table["rows"].is_object() || !table["rows"].contains( aRowId ) )
    {
        return nullptr;
    }

    const nlohmann::json& row = table["rows"][aRowId];

    if( !row.is_object() || !row.contains( "cells" )
        || !row["cells"].is_object() || !row["cells"].contains( aColumnId ) )
    {
        return nullptr;
    }

    return &row["cells"][aColumnId];
}


const nlohmann::json* surfacePatchFieldValue(
        const nlohmann::json& aSurfaceState, const std::string& aSurfaceId,
        const std::string& aFieldId )
{
    if( !aSurfaceState.contains( "surfaces" )
        || !aSurfaceState["surfaces"].is_object()
        || !aSurfaceState["surfaces"].contains( aSurfaceId ) )
    {
        return nullptr;
    }

    const nlohmann::json& surface = aSurfaceState["surfaces"][aSurfaceId];

    if( !surface.is_object() || !surface.contains( "fields" )
        || !surface["fields"].is_object()
        || !surface["fields"].contains( aFieldId ) )
    {
        return nullptr;
    }

    return &surface["fields"][aFieldId];
}


void annotateSurfacePatchValueDiff( nlohmann::json& aEntry,
                                    const nlohmann::json& aProposedValue,
                                    const nlohmann::json* aPreviousValue )
{
    aEntry["proposed_value"] = aProposedValue;

    if( aPreviousValue )
    {
        aEntry["previous_value_known"] = true;
        aEntry["previous_value"] = *aPreviousValue;
        aEntry["value_changed"] = *aPreviousValue != aProposedValue;
    }
    else
    {
        aEntry["previous_value_known"] = false;
    }
}


nlohmann::json surfacePatchDiffSummaryJson( const nlohmann::json& aEntries )
{
    size_t diffEntryCount = 0;
    size_t tableCellCount = 0;
    size_t fieldCount = 0;
    size_t changedValueCount = 0;
    size_t unchangedValueCount = 0;
    size_t unknownPreviousValueCount = 0;
    size_t visualTargetCount = 0;

    if( aEntries.is_array() )
    {
        for( const nlohmann::json& entry : aEntries )
        {
            if( !entry.is_object() )
                continue;

            ++diffEntryCount;

            const std::string kind = jsonStringOrEmpty( entry, "kind" );

            if( kind == "set_cell" )
                ++tableCellCount;
            else if( kind == "set_field" )
                ++fieldCount;

            if( entry.contains( "visual_target" )
                && entry["visual_target"].is_object() )
            {
                ++visualTargetCount;
            }

            if( entry.contains( "previous_value_known" )
                && entry["previous_value_known"].is_boolean()
                && !entry["previous_value_known"].get<bool>() )
            {
                ++unknownPreviousValueCount;
                continue;
            }

            if( entry.contains( "value_changed" )
                && entry["value_changed"].is_boolean() )
            {
                if( entry["value_changed"].get<bool>() )
                    ++changedValueCount;
                else
                    ++unchangedValueCount;
            }
        }
    }

    return { { "diff_entry_count", diffEntryCount },
             { "table_cell_count", tableCellCount },
             { "field_count", fieldCount },
             { "changed_value_count", changedValueCount },
             { "unchanged_value_count", unchangedValueCount },
             { "unknown_previous_value_count", unknownPreviousValueCount },
             { "visual_target_count", visualTargetCount } };
}


nlohmann::json surfacePatchDiffEntriesJson( const nlohmann::json& aArgs,
                                            nlohmann::json* aSurfaceState )
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

            nlohmann::json entry =
                    { { "kind", "set_cell" },
                      { "surface_id", surfaceId },
                      { "table_id", opTableId },
                      { "row_id", rowId },
                      { "column_id", columnId },
                      { "value", op["value"] },
                      { "target_path",
                        "surfaces." + surfaceId + ".tables." + opTableId
                                + ".rows." + rowId + ".cells." + columnId },
                      { "visual_target",
                        { { "kind", "table_cell" },
                          { "surface_id", surfaceId },
                          { "table_id", opTableId },
                          { "row_id", rowId },
                          { "column_id", columnId } } } };

            const nlohmann::json* previousValue =
                    aSurfaceState ? surfacePatchCellValue( *aSurfaceState,
                                                           surfaceId, opTableId,
                                                           rowId, columnId )
                                  : nullptr;
            annotateSurfacePatchValueDiff( entry, op["value"], previousValue );
            entries.push_back( std::move( entry ) );

            if( aSurfaceState )
            {
                ( *aSurfaceState )["surfaces"][surfaceId]["tables"][opTableId]
                                  ["rows"][rowId]["cells"][columnId] =
                        op["value"];
            }

            continue;
        }

        if( opName == "set_field" )
        {
            const std::string fieldId = jsonStringOrEmpty( op, "field_id" );

            if( surfaceId.empty() || fieldId.empty() || !op.contains( "value" ) )
                continue;

            nlohmann::json entry =
                    { { "kind", "set_field" },
                      { "surface_id", surfaceId },
                      { "field_id", fieldId },
                      { "value", op["value"] },
                      { "target_path",
                        "surfaces." + surfaceId + ".fields." + fieldId },
                      { "visual_target",
                        { { "kind", "field" },
                          { "surface_id", surfaceId },
                          { "field_id", fieldId } } } };

            const nlohmann::json* previousValue =
                    aSurfaceState ? surfacePatchFieldValue( *aSurfaceState,
                                                            surfaceId, fieldId )
                                  : nullptr;
            annotateSurfacePatchValueDiff( entry, op["value"], previousValue );
            entries.push_back( std::move( entry ) );

            if( aSurfaceState )
                ( *aSurfaceState )["surfaces"][surfaceId]["fields"][fieldId] =
                        op["value"];
        }
    }

    return entries;
}


nlohmann::json surfacePatchPreviewFactsJson(
        const AI_EXECUTION_SESSION& aSession )
{
    nlohmann::json previews = nlohmann::json::array();
    nlohmann::json surfaceState = nlohmann::json::object();

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

        for( const char* key : { "expected_surface_revision",
                                 "expected_schema_version",
                                 "expected_selection_fingerprint",
                                 "expected_overlap_set" } )
        {
            if( args.contains( key ) )
                preview[key] = args[key];
            else if( result.contains( key ) )
                preview[key] = result[key];
        }

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

            nlohmann::json diffEntries =
                    surfacePatchDiffEntriesJson( args, &surfaceState );

            if( !diffEntries.empty() )
            {
                preview["surface_patch_diff_entry_count"] = diffEntries.size();
                preview["surface_patch_diff_summary"] =
                        surfacePatchDiffSummaryJson( diffEntries );
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


AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT
AiValidateNextActionReplayTraceJson( const wxString& aReplayTraceJson )
{
    auto fail =
            []( const wxString& aErrorCode,
                const wxString& aMessage )
            {
                AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT result;
                result.m_Valid = false;
                result.m_ErrorCode = aErrorCode;
                result.m_Message = aMessage;
                return result;
            };

    auto pass =
            []()
            {
                AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT result;
                result.m_Valid = true;
                return result;
            };

    nlohmann::json trace =
            nlohmann::json::parse( toUtf8String( aReplayTraceJson ), nullptr, false );

    if( trace.is_discarded() || !trace.is_object() )
        return fail( wxS( "invalid_json" ), wxS( "Replay trace must be a JSON object." ) );

    if( !trace.contains( "schema" ) || !trace["schema"].is_object() )
        return fail( wxS( "missing_schema" ), wxS( "Replay trace schema is required." ) );

    const nlohmann::json& schema = trace["schema"];

    if( !schema.contains( "name" ) || !schema["name"].is_string()
        || schema["name"].get<std::string>() != "kisurf.next_action.replay_trace" )
    {
        return fail( wxS( "unsupported_schema_name" ),
                     wxS( "Replay trace schema name is unsupported." ) );
    }

    if( !schema.contains( "version" ) || !schema["version"].is_number_unsigned() )
    {
        return fail( wxS( "missing_schema_version" ),
                     wxS( "Replay trace schema version is required." ) );
    }

    if( schema["version"].get<unsigned>() != AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION )
    {
        return fail( wxS( "unsupported_schema_version" ),
                     wxS( "Replay trace schema version is unsupported." ) );
    }

    const std::vector<std::pair<const char*, const char*>> requiredObjectFields = {
        { "semantic_event", "semantic event" },
        { "observation_packet", "observation packet" },
        { "llm_decision", "LLM decision" },
        { "tool_results", "tool results" },
        { "llm_review_decision", "LLM review decision" }
    };

    for( const auto& [field, label] : requiredObjectFields )
    {
        if( !trace.contains( field ) || !trace[field].is_object() )
        {
            return fail( wxS( "missing_required_field" ),
                         wxString::Format( wxS( "Replay trace missing %s." ),
                                           fromUtf8String( label ) ) );
        }
    }

    if( !trace.contains( "runtime" ) || !trace["runtime"].is_string()
        || trace["runtime"].get<std::string>() != "next_action" )
    {
        return fail( wxS( "missing_required_field" ),
                     wxS( "Replay trace runtime must be next_action." ) );
    }

    if( !trace.contains( "runtime_step_id" )
        || !trace["runtime_step_id"].is_number_unsigned() )
    {
        return fail( wxS( "missing_required_field" ),
                     wxS( "Replay trace runtime_step_id is required." ) );
    }

    if( !trace.contains( "terminal_state" ) || !trace["terminal_state"].is_string() )
    {
        return fail( wxS( "missing_required_field" ),
                     wxS( "Replay trace terminal_state is required." ) );
    }

    if( !trace.contains( "attempts" ) || !trace["attempts"].is_array() )
    {
        return fail( wxS( "missing_required_field" ),
                     wxS( "Replay trace attempts array is required." ) );
    }

    return pass();
}


AI_NEXT_ACTION_REPLAY_TRACE_MIGRATION_RESULT
AiMigrateNextActionReplayTraceJson( const wxString& aReplayTraceJson,
                                    unsigned aTargetSchemaVersion )
{
    AI_NEXT_ACTION_REPLAY_TRACE_MIGRATION_RESULT result;
    result.m_TargetSchemaVersion = aTargetSchemaVersion;

    nlohmann::json trace =
            nlohmann::json::parse( toUtf8String( aReplayTraceJson ), nullptr, false );

    if( trace.is_object() && trace.contains( "schema" )
        && trace["schema"].is_object()
        && trace["schema"].contains( "version" )
        && trace["schema"]["version"].is_number_unsigned() )
    {
        result.m_SourceSchemaVersion = trace["schema"]["version"].get<unsigned>();
    }

    auto fail =
            [&result]( const wxString& aErrorCode,
                       const wxString& aMessage )
            {
                result.m_Valid = false;
                result.m_ErrorCode = aErrorCode;
                result.m_Message = aMessage;
                return result;
            };

    if( aTargetSchemaVersion != AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION )
    {
        return fail( wxS( "unsupported_target_schema_version" ),
                     wxS( "Replay trace target schema version is unsupported." ) );
    }

    AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT validation =
            AiValidateNextActionReplayTraceJson( aReplayTraceJson );

    if( !validation.m_Valid )
    {
        return fail( wxS( "invalid_source_replay_trace" ),
                     validation.m_Message );
    }

    if( result.m_SourceSchemaVersion != aTargetSchemaVersion )
    {
        return fail( wxS( "unsupported_source_schema_version" ),
                     wxS( "Replay trace source schema version has no migration path." ) );
    }

    result.m_Valid = true;
    result.m_Migrated = false;
    result.m_ReplayJson = aReplayTraceJson;
    return result;
}


bool containsObjectFieldRecursive( const nlohmann::json& aValue,
                                   const char* aField )
{
    if( aValue.is_object() )
    {
        if( aValue.contains( aField ) && aValue[aField].is_object() )
            return true;

        for( auto it = aValue.begin(); it != aValue.end(); ++it )
        {
            if( containsObjectFieldRecursive( it.value(), aField ) )
                return true;
        }
    }
    else if( aValue.is_array() )
    {
        for( const nlohmann::json& item : aValue )
        {
            if( containsObjectFieldRecursive( item, aField ) )
                return true;
        }
    }

    return false;
}


void incrementJsonCounter( nlohmann::json& aCounts, const std::string& aKey,
                           size_t aIncrement = 1 )
{
    if( aKey.empty() )
        return;

    if( !aCounts.is_object() )
        aCounts = nlohmann::json::object();

    size_t count = 0;

    if( aCounts.contains( aKey ) )
    {
        const nlohmann::json& value = aCounts[aKey];

        if( value.is_number_unsigned() )
            count = value.get<size_t>();
        else if( value.is_number_integer() && value.get<int64_t>() >= 0 )
            count = static_cast<size_t>( value.get<int64_t>() );
    }

    aCounts[aKey] = count + aIncrement;
}


void mergeJsonCounters( nlohmann::json& aTarget, const nlohmann::json& aSource )
{
    if( !aSource.is_object() )
        return;

    for( auto it = aSource.begin(); it != aSource.end(); ++it )
    {
        if( it.value().is_number_unsigned() )
        {
            incrementJsonCounter( aTarget, it.key(), it.value().get<size_t>() );
        }
        else if( it.value().is_number_integer() && it.value().get<int64_t>() >= 0 )
        {
            incrementJsonCounter( aTarget, it.key(),
                                  static_cast<size_t>(
                                          it.value().get<int64_t>() ) );
        }
    }
}


size_t jsonCounterValue( const nlohmann::json& aCounts,
                         const std::string& aKey )
{
    if( !aCounts.is_object() || !aCounts.contains( aKey ) )
        return 0;

    const nlohmann::json& value = aCounts[aKey];

    if( value.is_number_unsigned() )
        return value.get<size_t>();

    if( value.is_number_integer() && value.get<int64_t>() >= 0 )
        return static_cast<size_t>( value.get<int64_t>() );

    return 0;
}


wxString replayTraceWorkState( const nlohmann::json& aTrace )
{
    if( aTrace.contains( "observation_packet" )
        && aTrace["observation_packet"].is_object()
        && aTrace["observation_packet"].contains( "structured_facts" )
        && aTrace["observation_packet"]["structured_facts"].is_object() )
    {
        const nlohmann::json& facts =
                aTrace["observation_packet"]["structured_facts"];

        if( facts.contains( "work_state_packet" )
            && facts["work_state_packet"].is_object()
            && facts["work_state_packet"].contains( "kind" )
            && facts["work_state_packet"]["kind"].is_string() )
        {
            return fromUtf8String(
                    facts["work_state_packet"]["kind"].get<std::string>() );
        }

        if( facts.contains( "work_state" ) && facts["work_state"].is_string() )
            return fromUtf8String( facts["work_state"].get<std::string>() );
    }

    if( aTrace.contains( "llm_decision" )
        && aTrace["llm_decision"].is_object()
        && aTrace["llm_decision"].contains( "opportunity_type" )
        && aTrace["llm_decision"]["opportunity_type"].is_string() )
    {
        return fromUtf8String(
                aTrace["llm_decision"]["opportunity_type"].get<std::string>() );
    }

    return wxEmptyString;
}


AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT
AiEvaluateNextActionReplayTraceJson( const wxString& aReplayTraceJson )
{
    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT result;

    AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT validation =
            AiValidateNextActionReplayTraceJson( aReplayTraceJson );

    if( !validation.m_Valid )
    {
        result.m_Valid = false;
        result.m_ErrorCode = validation.m_ErrorCode;
        result.m_Message = validation.m_Message;
        return result;
    }

    nlohmann::json trace =
            nlohmann::json::parse( toUtf8String( aReplayTraceJson ), nullptr, false );

    result.m_Valid = true;
    result.m_SchemaVersion = trace["schema"]["version"].get<unsigned>();
    result.m_RuntimeStepId = trace["runtime_step_id"].get<uint64_t>();
    result.m_TerminalState =
            fromUtf8String( trace["terminal_state"].get<std::string>() );

    const std::string terminalState =
            trace["terminal_state"].get<std::string>();

    result.m_Published = trace.contains( "published_suggestion_id" )
                         && trace["published_suggestion_id"].is_number_unsigned();
    result.m_Accepted = terminalState == "accepted";
    result.m_Rejected = terminalState == "rejected";
    result.m_Expired = terminalState == "expired";
    result.m_Superseded = terminalState == "superseded";
    result.m_Abandoned = terminalState == "abandoned";

    nlohmann::json feedbackReasonCounts = nlohmann::json::object();
    nlohmann::json acceptGateReasonCounts = nlohmann::json::object();
    nlohmann::json validationIssueKindCounts = nlohmann::json::object();
    nlohmann::json validationIssueSeverityCounts = nlohmann::json::object();

    if( trace.contains( "tool_results" ) && trace["tool_results"].is_object() )
    {
        for( const char* phase : { "decision", "review" } )
        {
            if( trace["tool_results"].contains( phase )
                && trace["tool_results"][phase].is_array() )
            {
                const size_t phaseCount = trace["tool_results"][phase].size();
                result.m_ToolResultCount += phaseCount;

                if( std::string( phase ) == "decision" )
                    result.m_DecisionToolResultCount += phaseCount;
                else if( std::string( phase ) == "review" )
                    result.m_ReviewToolResultCount += phaseCount;

                for( const nlohmann::json& toolResult :
                     trace["tool_results"][phase] )
                {
                    if( toolResult.is_object()
                        && toolResult.contains( "tool_name" )
                        && toolResult["tool_name"].is_string()
                        && toolResult["tool_name"].get<std::string>()
                                   == "preview_gate_feedback" )
                    {
                        ++result.m_PreviewGateFeedbackCount;

                        if( toolResult.contains( "result" )
                            && toolResult["result"].is_object()
                            && toolResult["result"].contains(
                                    "preview_gate_result" )
                            && toolResult["result"]["preview_gate_result"].is_object()
                            && toolResult["result"]["preview_gate_result"].contains(
                                    "reasons" )
                            && toolResult["result"]["preview_gate_result"]["reasons"]
                                       .is_array() )
                        {
                            for( const nlohmann::json& reason :
                                 toolResult["result"]["preview_gate_result"]["reasons"] )
                            {
                                if( !reason.is_string() )
                                    continue;

                                const std::string key = reason.get<std::string>();
                                feedbackReasonCounts[key] =
                                        feedbackReasonCounts.value( key, 0 ) + 1;
                            }
                        }
                    }
                }
            }
        }
    }

    if( trace.contains( "publish_decision" ) && trace["publish_decision"].is_object()
        && trace["publish_decision"].contains( "preview_gate_result" )
        && trace["publish_decision"]["preview_gate_result"].is_object()
        && trace["publish_decision"]["preview_gate_result"].contains( "allowed" )
        && trace["publish_decision"]["preview_gate_result"]["allowed"].is_boolean() )
    {
        result.m_PreviewGateAllowed =
                trace["publish_decision"]["preview_gate_result"]["allowed"].get<bool>();
    }

    if( trace.contains( "publish_decision" )
        && trace["publish_decision"].is_object()
        && trace["publish_decision"].contains( "accept_gate_result" )
        && trace["publish_decision"]["accept_gate_result"].is_object() )
    {
        const nlohmann::json& gate =
                trace["publish_decision"]["accept_gate_result"];
        ++result.m_AcceptGateResultCount;

        if( gate.contains( "reasons" ) && gate["reasons"].is_array() )
        {
            for( const nlohmann::json& reason : gate["reasons"] )
            {
                if( reason.is_string() )
                    incrementJsonCounter( acceptGateReasonCounts,
                                          reason.get<std::string>() );
            }
        }
    }

    if( trace.contains( "observation_packet" )
        && trace["observation_packet"].is_object()
        && trace["observation_packet"].contains( "structured_facts" )
        && trace["observation_packet"]["structured_facts"].is_object()
        && trace["observation_packet"]["structured_facts"].contains(
                "work_state_packet" )
        && containsObjectFieldRecursive(
                trace["observation_packet"]["structured_facts"]
                     ["work_state_packet"],
                "interaction_semantics" ) )
    {
        result.m_WorkStateInteractionSemanticsPresent = true;
    }

    const nlohmann::json& attempts = trace["attempts"];
    result.m_AttemptCount = attempts.size();

    for( const nlohmann::json& attempt : attempts )
    {
        if( !attempt.is_object() )
            continue;

        if( attempt.contains( "hidden_attempt_journal" )
            && attempt["hidden_attempt_journal"].is_object()
            && attempt["hidden_attempt_journal"].contains( "operations" )
            && attempt["hidden_attempt_journal"]["operations"].is_array() )
        {
            result.m_HiddenOperationCount +=
                    attempt["hidden_attempt_journal"]["operations"].size();
        }

        if( attempt.contains( "budget_counters" )
            && attempt["budget_counters"].is_object() )
        {
            const nlohmann::json& budget = attempt["budget_counters"];
            result.m_BudgetToolRoundCount +=
                    jsonCounterValue( budget, "tool_round_count" );
            result.m_BudgetMutationCount +=
                    jsonCounterValue( budget, "mutation_count" );
            result.m_BudgetRenderCount +=
                    jsonCounterValue( budget, "render_count" );
            result.m_BudgetValidationCount +=
                    jsonCounterValue( budget, "validation_count" );
            result.m_BudgetCreatedObjectCount +=
                    jsonCounterValue( budget, "created_object_count" );
            result.m_BudgetTouchedObjectCount +=
                    jsonCounterValue( budget, "touched_object_count" );
        }

        if( attempt.contains( "render_outputs" )
            && attempt["render_outputs"].is_object()
            && !attempt["render_outputs"].empty() )
        {
            ++result.m_RenderResultCount;
        }

        if( attempt.contains( "validation_facts" )
            && attempt["validation_facts"].is_object()
            && !attempt["validation_facts"].empty() )
        {
            const nlohmann::json& validationFacts =
                    attempt["validation_facts"];
            ++result.m_ValidationResultCount;

            if( validationFacts.contains( "issues" )
                && validationFacts["issues"].is_array() )
            {
                for( const nlohmann::json& issue : validationFacts["issues"] )
                {
                    if( !issue.is_object() )
                        continue;

                    ++result.m_ValidationIssueCount;

                    if( issue.contains( "kind" ) && issue["kind"].is_string() )
                    {
                        incrementJsonCounter(
                                validationIssueKindCounts,
                                issue["kind"].get<std::string>() );
                    }

                    if( issue.contains( "severity" )
                        && issue["severity"].is_string() )
                    {
                        incrementJsonCounter(
                                validationIssueSeverityCounts,
                                issue["severity"].get<std::string>() );
                    }
                }
            }

            if( validationFactsBlockPreviewPublish(
                        fromUtf8String( attempt["validation_facts"].dump() ) ) )
            {
                result.m_HasBlockingValidationIssue = true;
            }
        }
    }

    result.m_PreviewGateFeedbackReasonCountsJson =
            fromUtf8String( feedbackReasonCounts.dump() );
    result.m_AcceptGateReasonCountsJson =
            fromUtf8String( acceptGateReasonCounts.dump() );
    result.m_ValidationIssueKindCountsJson =
            fromUtf8String( validationIssueKindCounts.dump() );
    result.m_ValidationIssueSeverityCountsJson =
            fromUtf8String( validationIssueSeverityCounts.dump() );

    nlohmann::json metrics =
            { { "schema_version", result.m_SchemaVersion },
              { "runtime_step_id", result.m_RuntimeStepId },
              { "terminal_state", terminalState },
              { "published", result.m_Published },
              { "accepted", result.m_Accepted },
              { "rejected", result.m_Rejected },
              { "expired", result.m_Expired },
              { "superseded", result.m_Superseded },
              { "abandoned", result.m_Abandoned },
              { "attempt_count", result.m_AttemptCount },
              { "hidden_operation_count", result.m_HiddenOperationCount },
              { "render_result_count", result.m_RenderResultCount },
              { "validation_result_count", result.m_ValidationResultCount },
              { "budget_tool_round_count", result.m_BudgetToolRoundCount },
              { "budget_mutation_count", result.m_BudgetMutationCount },
              { "budget_render_count", result.m_BudgetRenderCount },
              { "budget_validation_count", result.m_BudgetValidationCount },
              { "budget_created_object_count",
                result.m_BudgetCreatedObjectCount },
              { "budget_touched_object_count",
                result.m_BudgetTouchedObjectCount },
              { "tool_result_count", result.m_ToolResultCount },
              { "decision_tool_result_count",
                result.m_DecisionToolResultCount },
              { "review_tool_result_count",
                result.m_ReviewToolResultCount },
              { "preview_gate_feedback_count",
                result.m_PreviewGateFeedbackCount },
              { "preview_gate_feedback_reason_counts",
                feedbackReasonCounts },
              { "accept_gate_result_count",
                result.m_AcceptGateResultCount },
              { "accept_gate_reason_counts",
                acceptGateReasonCounts },
              { "validation_issue_count",
                result.m_ValidationIssueCount },
              { "validation_issue_kind_counts",
                validationIssueKindCounts },
              { "validation_issue_severity_counts",
                validationIssueSeverityCounts },
              { "preview_gate_allowed", result.m_PreviewGateAllowed },
              { "work_state_interaction_semantics_present",
                result.m_WorkStateInteractionSemanticsPresent },
              { "has_blocking_validation_issue",
                result.m_HasBlockingValidationIssue } };

    result.m_QualityMetricJson = fromUtf8String( metrics.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT
AiEvaluateNextActionReplayTraceBatch(
        const wxArrayString& aReplayTraceJsons )
{
    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT result;
    result.m_TotalTraceCount = aReplayTraceJsons.size();
    nlohmann::json feedbackReasonCounts = nlohmann::json::object();
    nlohmann::json acceptGateReasonCounts = nlohmann::json::object();
    nlohmann::json validationIssueKindCounts = nlohmann::json::object();
    nlohmann::json validationIssueSeverityCounts = nlohmann::json::object();

    for( size_t i = 0; i < aReplayTraceJsons.size(); ++i )
    {
        AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
                AiEvaluateNextActionReplayTraceJson( aReplayTraceJsons[i] );

        if( !evaluation.m_Valid )
        {
            ++result.m_InvalidTraceCount;

            if( result.m_FirstErrorCode.IsEmpty() )
            {
                result.m_FirstErrorCode = evaluation.m_ErrorCode;
                result.m_FirstErrorMessage = evaluation.m_Message;
            }

            continue;
        }

        ++result.m_ValidTraceCount;

        if( evaluation.m_Published )
            ++result.m_PublishedCount;

        if( evaluation.m_Accepted )
            ++result.m_AcceptedCount;

        if( evaluation.m_Rejected )
            ++result.m_RejectedCount;

        if( evaluation.m_Expired )
            ++result.m_ExpiredCount;

        if( evaluation.m_Superseded )
            ++result.m_SupersededCount;

        if( evaluation.m_Abandoned )
            ++result.m_AbandonedCount;

        if( evaluation.m_PreviewGateAllowed )
            ++result.m_PreviewGateAllowedCount;

        if( evaluation.m_WorkStateInteractionSemanticsPresent )
            ++result.m_WorkStateInteractionSemanticsPresentCount;

        if( evaluation.m_HasBlockingValidationIssue )
            ++result.m_BlockingValidationCount;

        result.m_AttemptCount += evaluation.m_AttemptCount;
        result.m_HiddenOperationCount += evaluation.m_HiddenOperationCount;
        result.m_RenderResultCount += evaluation.m_RenderResultCount;
        result.m_ValidationResultCount += evaluation.m_ValidationResultCount;
        result.m_BudgetToolRoundCount += evaluation.m_BudgetToolRoundCount;
        result.m_BudgetMutationCount += evaluation.m_BudgetMutationCount;
        result.m_BudgetRenderCount += evaluation.m_BudgetRenderCount;
        result.m_BudgetValidationCount += evaluation.m_BudgetValidationCount;
        result.m_BudgetCreatedObjectCount +=
                evaluation.m_BudgetCreatedObjectCount;
        result.m_BudgetTouchedObjectCount +=
                evaluation.m_BudgetTouchedObjectCount;
        result.m_ToolResultCount += evaluation.m_ToolResultCount;
        result.m_DecisionToolResultCount +=
                evaluation.m_DecisionToolResultCount;
        result.m_ReviewToolResultCount +=
                evaluation.m_ReviewToolResultCount;
        result.m_PreviewGateFeedbackCount +=
                evaluation.m_PreviewGateFeedbackCount;
        result.m_AcceptGateResultCount += evaluation.m_AcceptGateResultCount;
        result.m_ValidationIssueCount += evaluation.m_ValidationIssueCount;

        nlohmann::json traceFeedbackReasons =
                nlohmann::json::parse(
                        toUtf8String(
                                evaluation.m_PreviewGateFeedbackReasonCountsJson ),
                        nullptr, false );

        if( traceFeedbackReasons.is_object() )
        {
            for( auto it = traceFeedbackReasons.begin();
                 it != traceFeedbackReasons.end(); ++it )
            {
                if( it.value().is_number_integer()
                    || it.value().is_number_unsigned() )
                {
                    const std::string key = it.key();
                    feedbackReasonCounts[key] =
                            feedbackReasonCounts.value( key, 0 )
                            + it.value().get<size_t>();
                }
            }
        }

        nlohmann::json traceAcceptGateReasons =
                nlohmann::json::parse(
                        toUtf8String( evaluation.m_AcceptGateReasonCountsJson ),
                        nullptr, false );

        mergeJsonCounters( acceptGateReasonCounts, traceAcceptGateReasons );

        nlohmann::json traceValidationIssueKinds =
                nlohmann::json::parse(
                        toUtf8String(
                                evaluation.m_ValidationIssueKindCountsJson ),
                        nullptr, false );

        mergeJsonCounters( validationIssueKindCounts,
                           traceValidationIssueKinds );

        nlohmann::json traceValidationIssueSeverities =
                nlohmann::json::parse(
                        toUtf8String(
                                evaluation.m_ValidationIssueSeverityCountsJson ),
                        nullptr, false );

        mergeJsonCounters( validationIssueSeverityCounts,
                           traceValidationIssueSeverities );
    }

    result.m_Valid = result.m_InvalidTraceCount == 0;
    result.m_PreviewGateFeedbackReasonCountsJson =
            fromUtf8String( feedbackReasonCounts.dump() );
    result.m_AcceptGateReasonCountsJson =
            fromUtf8String( acceptGateReasonCounts.dump() );
    result.m_ValidationIssueKindCountsJson =
            fromUtf8String( validationIssueKindCounts.dump() );
    result.m_ValidationIssueSeverityCountsJson =
            fromUtf8String( validationIssueSeverityCounts.dump() );

    nlohmann::json summary =
            { { "total_trace_count", result.m_TotalTraceCount },
              { "valid_trace_count", result.m_ValidTraceCount },
              { "invalid_trace_count", result.m_InvalidTraceCount },
              { "published_count", result.m_PublishedCount },
              { "accepted_count", result.m_AcceptedCount },
              { "rejected_count", result.m_RejectedCount },
              { "expired_count", result.m_ExpiredCount },
              { "superseded_count", result.m_SupersededCount },
              { "abandoned_count", result.m_AbandonedCount },
              { "attempt_count", result.m_AttemptCount },
              { "hidden_operation_count", result.m_HiddenOperationCount },
              { "render_result_count", result.m_RenderResultCount },
              { "validation_result_count", result.m_ValidationResultCount },
              { "budget_tool_round_count", result.m_BudgetToolRoundCount },
              { "budget_mutation_count", result.m_BudgetMutationCount },
              { "budget_render_count", result.m_BudgetRenderCount },
              { "budget_validation_count", result.m_BudgetValidationCount },
              { "budget_created_object_count",
                result.m_BudgetCreatedObjectCount },
              { "budget_touched_object_count",
                result.m_BudgetTouchedObjectCount },
              { "tool_result_count", result.m_ToolResultCount },
              { "decision_tool_result_count",
                result.m_DecisionToolResultCount },
              { "review_tool_result_count",
                result.m_ReviewToolResultCount },
              { "preview_gate_feedback_count",
                result.m_PreviewGateFeedbackCount },
              { "preview_gate_feedback_reason_counts",
                feedbackReasonCounts },
              { "accept_gate_result_count",
                result.m_AcceptGateResultCount },
              { "accept_gate_reason_counts",
                acceptGateReasonCounts },
              { "validation_issue_count",
                result.m_ValidationIssueCount },
              { "validation_issue_kind_counts",
                validationIssueKindCounts },
              { "validation_issue_severity_counts",
                validationIssueSeverityCounts },
              { "preview_gate_allowed_count",
                result.m_PreviewGateAllowedCount },
              { "work_state_interaction_semantics_present_count",
                result.m_WorkStateInteractionSemanticsPresentCount },
              { "blocking_validation_count",
                result.m_BlockingValidationCount },
              { "valid", result.m_Valid } };

    if( !result.m_FirstErrorCode.IsEmpty() )
    {
        summary["first_error_code"] = toUtf8String( result.m_FirstErrorCode );
        summary["first_error_message"] =
                toUtf8String( result.m_FirstErrorMessage );
    }

    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT
AiEvaluateNextActionReplayTraceBatchJson( const wxString& aReplayBatchJson )
{
    auto finishBatchError =
            []( const wxString& aBatchId, const wxString& aErrorCode,
                const wxString& aMessage )
            {
                AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT result;
                result.m_Valid = false;
                result.m_FirstErrorCode = aErrorCode;
                result.m_FirstErrorMessage = aMessage;

                nlohmann::json summary =
                        { { "batch_id", toUtf8String( aBatchId ) },
                          { "valid", false },
                          { "first_error_code", toUtf8String( aErrorCode ) },
                          { "first_error_message", toUtf8String( aMessage ) },
                          { "total_trace_count", 0 },
                          { "valid_trace_count", 0 },
                          { "invalid_trace_count", 0 },
                          { "published_count", 0 },
                          { "accepted_count", 0 },
                          { "rejected_count", 0 },
                          { "expired_count", 0 },
                          { "superseded_count", 0 },
                          { "abandoned_count", 0 } };

                result.m_SummaryJson = fromUtf8String( summary.dump() );
                return result;
            };

    nlohmann::json batch =
            nlohmann::json::parse( toUtf8String( aReplayBatchJson ), nullptr,
                                   false );

    if( batch.is_discarded() || !batch.is_object() )
    {
        return finishBatchError( wxEmptyString, wxS( "invalid_batch_json" ),
                                 wxS( "Replay batch must be a JSON object." ) );
    }

    wxString batchId;

    if( batch.contains( "id" ) && batch["id"].is_string() )
        batchId = fromUtf8String( batch["id"].get<std::string>() );

    if( !batch.contains( "schema" ) || !batch["schema"].is_object() )
    {
        return finishBatchError( batchId, wxS( "missing_schema" ),
                                 wxS( "Replay batch schema is required." ) );
    }

    const nlohmann::json& schema = batch["schema"];

    if( !schema.contains( "name" ) || !schema["name"].is_string()
        || schema["name"].get<std::string>() != "kisurf.next_action.replay_batch" )
    {
        return finishBatchError( batchId, wxS( "unsupported_schema_name" ),
                                 wxS( "Replay batch schema name is unsupported." ) );
    }

    if( !schema.contains( "version" ) || !schema["version"].is_number_unsigned() )
    {
        return finishBatchError( batchId, wxS( "missing_schema_version" ),
                                 wxS( "Replay batch schema version is required." ) );
    }

    if( schema["version"].get<unsigned>()
        != AI_NEXT_ACTION_REPLAY_BATCH_SCHEMA_VERSION )
    {
        return finishBatchError( batchId, wxS( "unsupported_schema_version" ),
                                 wxS( "Replay batch schema version is unsupported." ) );
    }

    if( !batch.contains( "traces" ) || !batch["traces"].is_array() )
    {
        return finishBatchError( batchId, wxS( "missing_traces" ),
                                 wxS( "Replay batch traces array is required." ) );
    }

    wxArrayString traces;

    for( const nlohmann::json& trace : batch["traces"] )
    {
        if( !trace.is_object() )
        {
            return finishBatchError( batchId, wxS( "invalid_trace_entry" ),
                                     wxS( "Replay batch trace entries must be JSON objects." ) );
        }

        traces.Add( fromUtf8String( trace.dump() ) );
    }

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT result =
            AiEvaluateNextActionReplayTraceBatch( traces );

    nlohmann::json summary =
            nlohmann::json::parse( toUtf8String( result.m_SummaryJson ), nullptr,
                                   false );

    if( summary.is_discarded() || !summary.is_object() )
        summary = nlohmann::json::object();

    summary["batch_id"] = toUtf8String( batchId );
    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT
AiEvaluateNextActionReplayGoldenRecordJson( const wxString& aGoldenRecordJson )
{
    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT result;

    auto finish =
            [&result]( bool aValid, bool aPassed,
                       const wxString& aErrorCode,
                       const wxString& aMessage,
                       const AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT* aTraceEval )
            {
                result.m_Valid = aValid;
                result.m_Passed = aPassed;
                result.m_ErrorCode = aErrorCode;
                result.m_Message = aMessage;

                nlohmann::json summary =
                        { { "record_id", toUtf8String( result.m_RecordId ) },
                          { "valid", result.m_Valid },
                          { "passed", result.m_Passed } };

                if( !aErrorCode.IsEmpty() )
                    summary["error_code"] = toUtf8String( aErrorCode );

                if( !aMessage.IsEmpty() )
                    summary["message"] = toUtf8String( aMessage );

                if( !result.m_WorkState.IsEmpty() )
                    summary["work_state"] = toUtf8String( result.m_WorkState );

                if( aTraceEval )
                {
                    summary["trace_terminal_state"] =
                            toUtf8String( aTraceEval->m_TerminalState );
                    summary["trace_published"] = aTraceEval->m_Published;
                    summary["trace_hidden_operation_count"] =
                            aTraceEval->m_HiddenOperationCount;
                    summary["trace_attempt_count"] = aTraceEval->m_AttemptCount;
                    summary["trace_budget_tool_round_count"] =
                            aTraceEval->m_BudgetToolRoundCount;
                    summary["trace_budget_mutation_count"] =
                            aTraceEval->m_BudgetMutationCount;
                    summary["trace_budget_render_count"] =
                            aTraceEval->m_BudgetRenderCount;
                    summary["trace_budget_validation_count"] =
                            aTraceEval->m_BudgetValidationCount;
                    summary["trace_budget_created_object_count"] =
                            aTraceEval->m_BudgetCreatedObjectCount;
                    summary["trace_budget_touched_object_count"] =
                            aTraceEval->m_BudgetTouchedObjectCount;
                    summary["trace_decision_tool_result_count"] =
                            aTraceEval->m_DecisionToolResultCount;
                    summary["trace_review_tool_result_count"] =
                            aTraceEval->m_ReviewToolResultCount;
                    summary["trace_preview_gate_feedback_count"] =
                            aTraceEval->m_PreviewGateFeedbackCount;
                    summary["trace_preview_gate_allowed"] =
                            aTraceEval->m_PreviewGateAllowed;
                }

                result.m_SummaryJson = fromUtf8String( summary.dump() );
                return result;
            };

    nlohmann::json golden =
            nlohmann::json::parse( toUtf8String( aGoldenRecordJson ), nullptr, false );

    if( golden.is_discarded() || !golden.is_object() )
    {
        return finish( false, false, wxS( "invalid_golden_json" ),
                       wxS( "Golden record must be a JSON object." ), nullptr );
    }

    if( golden.contains( "id" ) && golden["id"].is_string() )
        result.m_RecordId = fromUtf8String( golden["id"].get<std::string>() );

    if( !golden.contains( "schema" ) || !golden["schema"].is_object() )
    {
        return finish( false, false, wxS( "missing_schema" ),
                       wxS( "Golden record schema is required." ), nullptr );
    }

    const nlohmann::json& schema = golden["schema"];

    if( !schema.contains( "name" ) || !schema["name"].is_string()
        || schema["name"].get<std::string>() != "kisurf.next_action.golden_trace" )
    {
        return finish( false, false, wxS( "unsupported_schema_name" ),
                       wxS( "Golden record schema name is unsupported." ), nullptr );
    }

    if( !schema.contains( "version" ) || !schema["version"].is_number_unsigned() )
    {
        return finish( false, false, wxS( "missing_schema_version" ),
                       wxS( "Golden record schema version is required." ), nullptr );
    }

    if( schema["version"].get<unsigned>()
        != AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION )
    {
        return finish( false, false, wxS( "unsupported_schema_version" ),
                       wxS( "Golden record schema version is unsupported." ), nullptr );
    }

    if( result.m_RecordId.IsEmpty() )
    {
        return finish( false, false, wxS( "missing_record_id" ),
                       wxS( "Golden record id is required." ), nullptr );
    }

    if( !golden.contains( "replay_trace" )
        || !golden["replay_trace"].is_object() )
    {
        return finish( false, false, wxS( "missing_replay_trace" ),
                       wxS( "Golden record replay_trace object is required." ), nullptr );
    }

    if( !golden.contains( "expected" ) || !golden["expected"].is_object() )
    {
        return finish( false, false, wxS( "missing_expected" ),
                       wxS( "Golden record expected object is required." ), nullptr );
    }

    result.m_WorkState = replayTraceWorkState( golden["replay_trace"] );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT traceEval =
            AiEvaluateNextActionReplayTraceJson(
                    fromUtf8String( golden["replay_trace"].dump() ) );

    if( !traceEval.m_Valid )
    {
        return finish( false, false, wxS( "invalid_replay_trace" ),
                       traceEval.m_Message, &traceEval );
    }

    const nlohmann::json& expected = golden["expected"];

    if( expected.contains( "work_state" )
        && expected["work_state"].is_string()
        && expected["work_state"].get<std::string>()
           != toUtf8String( result.m_WorkState ) )
    {
        return finish( true, false, wxS( "work_state_mismatch" ),
                       wxS( "Replay work_state did not match expected." ),
                       &traceEval );
    }

    if( expected.contains( "terminal_state" )
        && expected["terminal_state"].is_string()
        && expected["terminal_state"].get<std::string>()
           != toUtf8String( traceEval.m_TerminalState ) )
    {
        return finish( true, false, wxS( "terminal_state_mismatch" ),
                       wxS( "Replay terminal_state did not match expected." ),
                       &traceEval );
    }

    if( expected.contains( "published" ) && expected["published"].is_boolean()
        && expected["published"].get<bool>() != traceEval.m_Published )
    {
        return finish( true, false, wxS( "published_mismatch" ),
                       wxS( "Replay published state did not match expected." ),
                       &traceEval );
    }

    if( expected.contains( "min_hidden_operation_count" )
        && expected["min_hidden_operation_count"].is_number_unsigned()
        && traceEval.m_HiddenOperationCount
           < expected["min_hidden_operation_count"].get<size_t>() )
    {
        return finish( true, false,
                       wxS( "hidden_operation_count_below_minimum" ),
                       wxS( "Replay hidden operation count is below expected minimum." ),
                       &traceEval );
    }

    auto checkMinimum =
            [&]( const char* aExpectedKey, size_t aActualValue,
                 const wxString& aErrorCode, const wxString& aMessage )
            -> std::optional<AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT>
            {
                if( expected.contains( aExpectedKey )
                    && expected[aExpectedKey].is_number_unsigned()
                    && aActualValue < expected[aExpectedKey].get<size_t>() )
                {
                    return finish( true, false, aErrorCode, aMessage,
                                   &traceEval );
                }

                return std::nullopt;
            };

    if( auto checked = checkMinimum(
                "min_budget_tool_round_count",
                traceEval.m_BudgetToolRoundCount,
                wxS( "budget_tool_round_count_below_minimum" ),
                wxS( "Replay budget tool-round count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( auto checked = checkMinimum(
                "min_budget_mutation_count",
                traceEval.m_BudgetMutationCount,
                wxS( "budget_mutation_count_below_minimum" ),
                wxS( "Replay budget mutation count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( auto checked = checkMinimum(
                "min_budget_render_count",
                traceEval.m_BudgetRenderCount,
                wxS( "budget_render_count_below_minimum" ),
                wxS( "Replay budget render count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( auto checked = checkMinimum(
                "min_budget_validation_count",
                traceEval.m_BudgetValidationCount,
                wxS( "budget_validation_count_below_minimum" ),
                wxS( "Replay budget validation count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( auto checked = checkMinimum(
                "min_budget_created_object_count",
                traceEval.m_BudgetCreatedObjectCount,
                wxS( "budget_created_object_count_below_minimum" ),
                wxS( "Replay budget created-object count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( auto checked = checkMinimum(
                "min_budget_touched_object_count",
                traceEval.m_BudgetTouchedObjectCount,
                wxS( "budget_touched_object_count_below_minimum" ),
                wxS( "Replay budget touched-object count is below expected minimum." ) ) )
    {
        return *checked;
    }

    if( expected.contains( "min_decision_tool_result_count" )
        && expected["min_decision_tool_result_count"].is_number_unsigned()
        && traceEval.m_DecisionToolResultCount
           < expected["min_decision_tool_result_count"].get<size_t>() )
    {
        return finish( true, false,
                       wxS( "decision_tool_result_count_below_minimum" ),
                       wxS( "Replay decision tool result count is below expected minimum." ),
                       &traceEval );
    }

    if( expected.contains( "min_review_tool_result_count" )
        && expected["min_review_tool_result_count"].is_number_unsigned()
        && traceEval.m_ReviewToolResultCount
           < expected["min_review_tool_result_count"].get<size_t>() )
    {
        return finish( true, false,
                       wxS( "review_tool_result_count_below_minimum" ),
                       wxS( "Replay review tool result count is below expected minimum." ),
                       &traceEval );
    }

    if( expected.contains( "min_preview_gate_feedback_count" )
        && expected["min_preview_gate_feedback_count"].is_number_unsigned()
        && traceEval.m_PreviewGateFeedbackCount
           < expected["min_preview_gate_feedback_count"].get<size_t>() )
    {
        return finish( true, false,
                       wxS( "preview_gate_feedback_count_below_minimum" ),
                       wxS( "Replay preview gate feedback count is below expected minimum." ),
                       &traceEval );
    }

    if( expected.contains( "min_accept_gate_result_count" )
        && expected["min_accept_gate_result_count"].is_number_unsigned()
        && traceEval.m_AcceptGateResultCount
           < expected["min_accept_gate_result_count"].get<size_t>() )
    {
        return finish( true, false,
                       wxS( "accept_gate_result_count_below_minimum" ),
                       wxS( "Replay accept gate result count is below expected minimum." ),
                       &traceEval );
    }

    if( expected.contains( "min_accept_gate_reason_counts" )
        && expected["min_accept_gate_reason_counts"].is_object() )
    {
        nlohmann::json acceptGateReasonCounts =
                nlohmann::json::parse(
                        toUtf8String( traceEval.m_AcceptGateReasonCountsJson ),
                        nullptr, false );

        if( acceptGateReasonCounts.is_discarded()
            || !acceptGateReasonCounts.is_object() )
        {
            acceptGateReasonCounts = nlohmann::json::object();
        }

        for( auto it = expected["min_accept_gate_reason_counts"].begin();
             it != expected["min_accept_gate_reason_counts"].end(); ++it )
        {
            if( !( it.value().is_number_unsigned()
                   || ( it.value().is_number_integer()
                        && it.value().get<int64_t>() >= 0 ) ) )
            {
                continue;
            }

            const size_t minimum =
                    it.value().is_number_unsigned()
                            ? it.value().get<size_t>()
                            : static_cast<size_t>(
                                      it.value().get<int64_t>() );

            if( jsonCounterValue( acceptGateReasonCounts, it.key() )
                < minimum )
            {
                return finish(
                        true, false,
                        wxS( "accept_gate_reason_count_below_minimum" ),
                        wxS( "Replay accept gate reason count is below expected minimum." ),
                        &traceEval );
            }
        }
    }

    return finish( true, true, wxEmptyString, wxEmptyString, &traceEval );
}


AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT
AiEvaluateNextActionReplayGoldenDataset( const wxArrayString& aGoldenRecordJsons )
{
    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT result;
    result.m_TotalRecordCount = aGoldenRecordJsons.size();
    nlohmann::json workStateCounts = nlohmann::json::object();
    nlohmann::json errorCodeCounts = nlohmann::json::object();

    for( size_t i = 0; i < aGoldenRecordJsons.size(); ++i )
    {
        AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT record =
                AiEvaluateNextActionReplayGoldenRecordJson( aGoldenRecordJsons[i] );

        if( !record.m_Valid )
        {
            ++result.m_InvalidRecordCount;

            if( !record.m_ErrorCode.IsEmpty() )
                incrementJsonCounter( errorCodeCounts,
                                      toUtf8String( record.m_ErrorCode ) );

            if( result.m_FirstErrorCode.IsEmpty() )
            {
                result.m_FirstErrorCode = record.m_ErrorCode;
                result.m_FirstErrorMessage = record.m_Message;
                result.m_FirstFailedRecordId = record.m_RecordId;
            }

            continue;
        }

        ++result.m_ValidRecordCount;

        if( !record.m_WorkState.IsEmpty() )
            incrementJsonCounter( workStateCounts,
                                  toUtf8String( record.m_WorkState ) );

        if( record.m_Passed )
        {
            ++result.m_PassedRecordCount;
            continue;
        }

        ++result.m_FailedRecordCount;

        if( !record.m_ErrorCode.IsEmpty() )
            incrementJsonCounter( errorCodeCounts,
                                  toUtf8String( record.m_ErrorCode ) );

        if( result.m_FirstErrorCode.IsEmpty() )
        {
            result.m_FirstErrorCode = record.m_ErrorCode;
            result.m_FirstErrorMessage = record.m_Message;
            result.m_FirstFailedRecordId = record.m_RecordId;
        }
    }

    result.m_Valid = result.m_InvalidRecordCount == 0;
    result.m_Passed = result.m_Valid && result.m_FailedRecordCount == 0;

    nlohmann::json summary =
            { { "valid", result.m_Valid },
              { "passed", result.m_Passed },
              { "total_record_count", result.m_TotalRecordCount },
              { "valid_record_count", result.m_ValidRecordCount },
              { "invalid_record_count", result.m_InvalidRecordCount },
              { "passed_record_count", result.m_PassedRecordCount },
              { "failed_record_count", result.m_FailedRecordCount },
              { "work_state_counts", workStateCounts },
              { "error_code_counts", errorCodeCounts } };

    if( !result.m_FirstErrorCode.IsEmpty() )
    {
        summary["first_error_code"] = toUtf8String( result.m_FirstErrorCode );
        summary["first_error_message"] =
                toUtf8String( result.m_FirstErrorMessage );
        summary["first_failed_record_id"] =
                toUtf8String( result.m_FirstFailedRecordId );
    }

    result.m_WorkStateCountsJson = fromUtf8String( workStateCounts.dump() );
    result.m_ErrorCodeCountsJson = fromUtf8String( errorCodeCounts.dump() );
    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT
AiEvaluateNextActionReplayGoldenDatasetJson( const wxString& aGoldenDatasetJson )
{
    auto finishDatasetError =
            []( const wxString& aDatasetId, const wxString& aErrorCode,
                const wxString& aMessage )
            {
                AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT result;
                result.m_Valid = false;
                result.m_Passed = false;
                result.m_FirstErrorCode = aErrorCode;
                result.m_FirstErrorMessage = aMessage;
                nlohmann::json errorCodeCounts = nlohmann::json::object();
                incrementJsonCounter( errorCodeCounts, toUtf8String( aErrorCode ) );

                nlohmann::json summary =
                        { { "dataset_id", toUtf8String( aDatasetId ) },
                          { "valid", false },
                          { "passed", false },
                          { "first_error_code", toUtf8String( aErrorCode ) },
                          { "first_error_message", toUtf8String( aMessage ) },
                          { "total_record_count", 0 },
                          { "valid_record_count", 0 },
                          { "invalid_record_count", 0 },
                          { "passed_record_count", 0 },
                          { "failed_record_count", 0 },
                          { "error_code_counts", errorCodeCounts } };

                result.m_ErrorCodeCountsJson =
                        fromUtf8String( errorCodeCounts.dump() );
                result.m_SummaryJson = fromUtf8String( summary.dump() );
                return result;
            };

    nlohmann::json dataset =
            nlohmann::json::parse( toUtf8String( aGoldenDatasetJson ), nullptr,
                                   false );

    if( dataset.is_discarded() || !dataset.is_object() )
    {
        return finishDatasetError( wxEmptyString, wxS( "invalid_dataset_json" ),
                                   wxS( "Golden dataset must be a JSON object." ) );
    }

    wxString datasetId;

    if( dataset.contains( "id" ) && dataset["id"].is_string() )
        datasetId = fromUtf8String( dataset["id"].get<std::string>() );

    if( !dataset.contains( "schema" ) || !dataset["schema"].is_object() )
    {
        return finishDatasetError( datasetId, wxS( "missing_schema" ),
                                   wxS( "Golden dataset schema is required." ) );
    }

    const nlohmann::json& schema = dataset["schema"];

    if( !schema.contains( "name" ) || !schema["name"].is_string()
        || schema["name"].get<std::string>() != "kisurf.next_action.golden_dataset" )
    {
        return finishDatasetError( datasetId, wxS( "unsupported_schema_name" ),
                                   wxS( "Golden dataset schema name is unsupported." ) );
    }

    if( !schema.contains( "version" ) || !schema["version"].is_number_unsigned() )
    {
        return finishDatasetError( datasetId, wxS( "missing_schema_version" ),
                                   wxS( "Golden dataset schema version is required." ) );
    }

    if( schema["version"].get<unsigned>()
        != AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_SCHEMA_VERSION )
    {
        return finishDatasetError( datasetId, wxS( "unsupported_schema_version" ),
                                   wxS( "Golden dataset schema version is unsupported." ) );
    }

    if( !dataset.contains( "records" ) || !dataset["records"].is_array() )
    {
        return finishDatasetError( datasetId, wxS( "missing_records" ),
                                   wxS( "Golden dataset records array is required." ) );
    }

    wxArrayString records;

    for( const nlohmann::json& record : dataset["records"] )
        records.Add( fromUtf8String( record.dump() ) );

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT result =
            AiEvaluateNextActionReplayGoldenDataset( records );

    nlohmann::json summary =
            nlohmann::json::parse( toUtf8String( result.m_SummaryJson ), nullptr,
                                   false );

    if( summary.is_discarded() || !summary.is_object() )
        summary = nlohmann::json::object();

    summary["dataset_id"] = toUtf8String( datasetId );
    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT
AiEvaluateNextActionReplayGoldenDatasetFile( const wxString& aGoldenDatasetPath )
{
    auto finishFileError =
            []( const wxString& aDatasetPath, const wxString& aErrorCode,
                const wxString& aMessage )
            {
                AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT result;
                result.m_Valid = false;
                result.m_Passed = false;
                result.m_FirstErrorCode = aErrorCode;
                result.m_FirstErrorMessage = aMessage;
                nlohmann::json errorCodeCounts = nlohmann::json::object();
                incrementJsonCounter( errorCodeCounts, toUtf8String( aErrorCode ) );

                nlohmann::json summary =
                        { { "dataset_path", toUtf8String( aDatasetPath ) },
                          { "valid", false },
                          { "passed", false },
                          { "first_error_code", toUtf8String( aErrorCode ) },
                          { "first_error_message", toUtf8String( aMessage ) },
                          { "total_record_count", 0 },
                          { "valid_record_count", 0 },
                          { "invalid_record_count", 0 },
                          { "passed_record_count", 0 },
                          { "failed_record_count", 0 },
                          { "error_code_counts", errorCodeCounts } };

                result.m_ErrorCodeCountsJson =
                        fromUtf8String( errorCodeCounts.dump() );
                result.m_SummaryJson = fromUtf8String( summary.dump() );
                return result;
            };

    wxFFile datasetFile( aGoldenDatasetPath, wxS( "rb" ) );

    if( !datasetFile.IsOpened() )
    {
        return finishFileError( aGoldenDatasetPath, wxS( "dataset_file_unreadable" ),
                                wxS( "Golden dataset file could not be opened." ) );
    }

    wxString datasetJson;

    if( !datasetFile.ReadAll( &datasetJson ) )
    {
        return finishFileError( aGoldenDatasetPath, wxS( "dataset_file_read_failed" ),
                                wxS( "Golden dataset file could not be read." ) );
    }

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT result =
            AiEvaluateNextActionReplayGoldenDatasetJson( datasetJson );

    nlohmann::json summary =
            nlohmann::json::parse( toUtf8String( result.m_SummaryJson ), nullptr,
                                   false );

    if( summary.is_discarded() || !summary.is_object() )
        summary = nlohmann::json::object();

    summary["dataset_path"] = toUtf8String( aGoldenDatasetPath );
    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
}


AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_BATCH_EVALUATION_RESULT
AiEvaluateNextActionReplayGoldenDatasetFiles( const wxArrayString& aGoldenDatasetPaths )
{
    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_BATCH_EVALUATION_RESULT result;
    result.m_TotalDatasetCount = aGoldenDatasetPaths.size();

    nlohmann::json datasetSummaries = nlohmann::json::array();
    nlohmann::json workStateCounts = nlohmann::json::object();
    nlohmann::json errorCodeCounts = nlohmann::json::object();

    for( size_t i = 0; i < aGoldenDatasetPaths.size(); ++i )
    {
        const wxString& datasetPath = aGoldenDatasetPaths[i];

        AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT dataset =
                AiEvaluateNextActionReplayGoldenDatasetFile( datasetPath );

        result.m_TotalRecordCount += dataset.m_TotalRecordCount;
        result.m_ValidRecordCount += dataset.m_ValidRecordCount;
        result.m_InvalidRecordCount += dataset.m_InvalidRecordCount;
        result.m_PassedRecordCount += dataset.m_PassedRecordCount;
        result.m_FailedRecordCount += dataset.m_FailedRecordCount;

        if( dataset.m_Valid )
            ++result.m_ValidDatasetCount;
        else
            ++result.m_InvalidDatasetCount;

        if( dataset.m_Passed )
            ++result.m_PassedDatasetCount;
        else
            ++result.m_FailedDatasetCount;

        if( !dataset.m_Passed && result.m_FirstErrorCode.IsEmpty() )
        {
            result.m_FirstErrorCode = dataset.m_FirstErrorCode;
            result.m_FirstErrorMessage = dataset.m_FirstErrorMessage;
            result.m_FirstFailedDatasetPath = datasetPath;
        }

        nlohmann::json datasetSummary =
                nlohmann::json::parse( toUtf8String( dataset.m_SummaryJson ),
                                       nullptr, false );

        if( datasetSummary.is_discarded() || !datasetSummary.is_object() )
            datasetSummary = nlohmann::json::object();

        nlohmann::json datasetWorkStateCounts =
                nlohmann::json::parse( toUtf8String( dataset.m_WorkStateCountsJson ),
                                       nullptr, false );

        if( ( datasetWorkStateCounts.is_discarded()
              || !datasetWorkStateCounts.is_object() )
            && datasetSummary.contains( "work_state_counts" )
            && datasetSummary["work_state_counts"].is_object() )
        {
            datasetWorkStateCounts = datasetSummary["work_state_counts"];
        }

        mergeJsonCounters( workStateCounts, datasetWorkStateCounts );

        nlohmann::json datasetErrorCodeCounts =
                nlohmann::json::parse( toUtf8String( dataset.m_ErrorCodeCountsJson ),
                                       nullptr, false );

        if( ( datasetErrorCodeCounts.is_discarded()
              || !datasetErrorCodeCounts.is_object() )
            && datasetSummary.contains( "error_code_counts" )
            && datasetSummary["error_code_counts"].is_object() )
        {
            datasetErrorCodeCounts = datasetSummary["error_code_counts"];
        }

        if( ( datasetErrorCodeCounts.is_discarded()
              || !datasetErrorCodeCounts.is_object()
              || datasetErrorCodeCounts.empty() )
            && !dataset.m_FirstErrorCode.IsEmpty() )
        {
            datasetErrorCodeCounts = nlohmann::json::object();
            incrementJsonCounter( datasetErrorCodeCounts,
                                  toUtf8String( dataset.m_FirstErrorCode ) );
        }

        mergeJsonCounters( errorCodeCounts, datasetErrorCodeCounts );

        datasetSummary["dataset_path"] = toUtf8String( datasetPath );
        datasetSummaries.push_back( datasetSummary );
    }

    result.m_Valid = result.m_InvalidDatasetCount == 0;
    result.m_Passed = result.m_Valid && result.m_FailedDatasetCount == 0;
    result.m_DatasetPassRate =
            result.m_TotalDatasetCount == 0
                    ? 0.0
                    : static_cast<double>( result.m_PassedDatasetCount )
                              / static_cast<double>( result.m_TotalDatasetCount );
    result.m_RecordPassRate =
            result.m_TotalRecordCount == 0
                    ? 0.0
                    : static_cast<double>( result.m_PassedRecordCount )
                              / static_cast<double>( result.m_TotalRecordCount );

    nlohmann::json summary =
            { { "valid", result.m_Valid },
              { "passed", result.m_Passed },
              { "total_dataset_count", result.m_TotalDatasetCount },
              { "valid_dataset_count", result.m_ValidDatasetCount },
              { "invalid_dataset_count", result.m_InvalidDatasetCount },
              { "passed_dataset_count", result.m_PassedDatasetCount },
              { "failed_dataset_count", result.m_FailedDatasetCount },
              { "total_record_count", result.m_TotalRecordCount },
              { "valid_record_count", result.m_ValidRecordCount },
              { "invalid_record_count", result.m_InvalidRecordCount },
              { "passed_record_count", result.m_PassedRecordCount },
              { "failed_record_count", result.m_FailedRecordCount },
              { "dataset_pass_rate", result.m_DatasetPassRate },
              { "record_pass_rate", result.m_RecordPassRate },
              { "work_state_counts", workStateCounts },
              { "error_code_counts", errorCodeCounts },
              { "datasets", datasetSummaries } };

    if( !result.m_FirstErrorCode.IsEmpty() )
    {
        summary["first_error_code"] = toUtf8String( result.m_FirstErrorCode );
        summary["first_error_message"] =
                toUtf8String( result.m_FirstErrorMessage );
        summary["first_failed_dataset_path"] =
                toUtf8String( result.m_FirstFailedDatasetPath );
    }

    result.m_WorkStateCountsJson = fromUtf8String( workStateCounts.dump() );
    result.m_ErrorCodeCountsJson = fromUtf8String( errorCodeCounts.dump() );
    result.m_SummaryJson = fromUtf8String( summary.dump() );
    return result;
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
              { { "name", "placement.generate_footprint_transform_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "placement" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_footprint_transform_library" },
                { "can_publish", false } },
              { { "name", "placement.generate_footprint_orientation_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "placement" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_footprint_orientation_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_segment_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_routing_segment_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_parallel_segment_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_parallel_routing_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_bus_segment_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_bus_routing_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_replace_path_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source", "internal_replace_path_library" },
                { "can_publish", false } },
              { { "name", "routing.generate_constraint_aware_reroute_candidates" },
                { "layer", "integrated" },
                { "role", "candidate_generation" },
                { "work_state", "routing" },
                { "side_effect", "read_only" },
                { "candidate_source",
                  "internal_constraint_aware_reroute_library" },
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
                { "argument_contract",
                  { { "net", "empty string for NoNet" } } },
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
              { { "name", "placement.repair_footprint_orientation" },
                { "layer", "integrated" },
                { "role", "placement_repair" },
                { "work_state", "placement" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.set_item_properties" },
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
              { { "name", "routing.repair_bus_segments" },
                { "layer", "integrated" },
                { "role", "routing_repair" },
                { "work_state", "routing" },
                { "side_effect", "shadow_mutation" },
                { "can_publish", false },
                { "raw_board_access", false },
                { "direct_publish", false },
                { "requires_checkpoint", true },
                { "requires_journal", true },
                { "lowers_to", "pcb.create_track_segment[]" },
                { "max_steps", 16 } },
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

    auto namespaceForTool =
            []( const std::string& aName )
            {
                if( aName.rfind( "placement.", 0 ) == 0 )
                    return std::string( "placement" );

                if( aName.rfind( "routing.", 0 ) == 0 )
                    return std::string( "routing" );

                if( aName.rfind( "surface.", 0 ) == 0 )
                    return std::string( "surface" );

                if( aName.rfind( "script.", 0 ) == 0 )
                    return std::string( "script" );

                if( aName.rfind( "repair.", 0 ) == 0 )
                    return std::string( "repair" );

                return std::string( "runtime" );
            };

    for( nlohmann::json& tool : tools )
    {
        if( tool.is_object() )
            tool["namespace"] = namespaceForTool(
                    tool.value( "name", std::string() ) );
    }

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

                auto pointSchema =
                        []( const char* aDescription )
                        {
                            return nlohmann::json{
                                { "type", "object" },
                                { "additionalProperties", false },
                                { "description", aDescription },
                                { "properties",
                                  { { "x",
                                      { { "type", "integer" },
                                        { "description",
                                          "Internal-coordinate x value." } } },
                                    { "y",
                                      { { "type", "integer" },
                                        { "description",
                                          "Internal-coordinate y value." } } } } },
                                { "required", nlohmann::json::array( { "x", "y" } ) }
                            };
                        };

                auto pointArraySchema =
                        [&]( const char* aDescription, int aMinItems = 1 )
                        {
                            return nlohmann::json{
                                { "type", "array" },
                                { "description", aDescription },
                                { "items", pointSchema( "Internal-coordinate x/y point." ) },
                                { "minItems", aMinItems }
                            };
                        };

                auto handleSchema =
                        []()
                        {
                            return nlohmann::json{
                                { "type", "object" },
                                { "additionalProperties", false },
                                { "description",
                                  "Session handle returned by a prior hidden attempt tool." },
                                { "properties",
                                  { { "session_id",
                                      { { "type", "integer" },
                                        { "minimum", 1 },
                                        { "description",
                                          "Optional session id for the handle." } } },
                                    { "handle_id",
                                      { { "type", "integer" },
                                        { "minimum", 1 },
                                        { "description",
                                          "Session-local handle id." } } },
                                    { "generation",
                                      { { "type", "integer" },
                                        { "minimum", 1 },
                                        { "description",
                                          "Optional handle generation for stale-handle checks." } } },
                                    { "alias",
                                      { { "type", "string" },
                                        { "description",
                                          "Optional model-readable handle alias." } } } } },
                                { "required", nlohmann::json::array( { "handle_id" } ) }
                            };
                        };

                auto handleArraySchema =
                        [&]( const char* aDescription )
                        {
                            return nlohmann::json{
                                { "type", "array" },
                                { "description", aDescription },
                                { "items", handleSchema() },
                                { "minItems", 1 }
                            };
                        };

                auto busSegmentSchema =
                        [&]()
                        {
                            return nlohmann::json{
                                { "type", "object" },
                                { "additionalProperties", false },
                                { "properties",
                                  { { "start",
                                      pointSchema( "Start point for this bus lane segment." ) },
                                    { "end",
                                      pointSchema( "End point for this bus lane segment." ) },
                                    { "net",
                                      { { "type", "string" },
                                        { "description",
                                          "Net assigned to this bus lane segment." } } },
                                    { "layer",
                                      { { "type", "string" },
                                        { "description",
                                          "Optional layer override for this bus lane segment." } } },
                                    { "width",
                                      { { "type", "integer" },
                                        { "minimum", 1 },
                                        { "description",
                                          "Optional width override for this bus lane segment." } } },
                                    { "alias",
                                      { { "type", "string" },
                                        { "description",
                                          "Optional model-readable alias for this lane." } } },
                                    { "metadata",
                                      { { "type", "object" },
                                        { "description",
                                          "Optional provenance metadata for this lane." } } } } },
                                { "required", nlohmann::json::array(
                                              { "start", "end", "net" } ) }
                            };
                        };

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
                else if( aName == "render.hidden_attempt" )
                {
                    parameters["properties"]["scope"] =
                            { { "type", "string" },
                              { "enum",
                                nlohmann::json::array( { "session",
                                                          "affected_area",
                                                          "selection",
                                                          "region" } ) },
                              { "description",
                                "Render scope requested for the hidden attempt." } };
                    parameters["properties"]["mode"] =
                            { { "type", "string" },
                              { "description",
                                "Render mode requested for model review, for example visual_review." } };
                    parameters["properties"]["region"] =
                            { { "type", "object" },
                              { "additionalProperties", true },
                              { "description",
                                "Optional board or viewport region to render." } };
                    parameters["properties"]["layer_mask"] =
                            { { "type", "array" },
                              { "items", { { "type", "string" } } },
                              { "description",
                                "Optional layer names to include in the rendered view." } };
                }
                else if( aName == "validate.hidden_attempt" )
                {
                    parameters["properties"]["scope"] =
                            { { "type", "string" },
                              { "enum",
                                nlohmann::json::array( { "session",
                                                          "affected_area",
                                                          "selection",
                                                          "region" } ) },
                              { "description",
                                "Validation scope requested for the hidden attempt." } };
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
                else if( isPlacementFootprintTransformCandidateTool( aName ) )
                {
                    parameters["properties"]["footprint_ref"] =
                            { { "type", "string" },
                              { "description",
                                "Optional footprint reference designator for facts." } };
                    parameters["properties"]["current_position"] =
                            pointSchema( "Current x/y position of the footprint being placed." );
                    parameters["properties"]["target_position"] =
                            pointSchema( "Target x/y position for the placement candidate." );
                    parameters["required"] = nlohmann::json::array(
                            { "current_position", "target_position" } );
                }
                else if( isPlacementFootprintOrientationCandidateTool( aName ) )
                {
                    parameters["properties"]["handles"] =
                            handleArraySchema(
                                    "Session handles for the footprint or selected placement items to orient." );
                    parameters["properties"]["footprint_ref"] =
                            { { "type", "string" },
                              { "description",
                                "Optional footprint reference designator for facts." } };
                    parameters["properties"]["current_orientation_degrees"] =
                            { { "type", "number" },
                              { "description",
                                "Current footprint orientation in degrees." } };
                    parameters["properties"]["target_orientation_degrees"] =
                            { { "type", "number" },
                              { "description",
                                "Target footprint orientation in degrees." } };
                    parameters["properties"]["target_side"] =
                            { { "type", "string" },
                              { "description",
                                "Optional target board side or layer family, for "
                                "example F.Cu or B.Cu." } };
                    parameters["required"] = nlohmann::json::array(
                            { "handles", "current_orientation_degrees",
                              "target_orientation_degrees" } );
                }
                else if( isRoutingParallelCandidateTool( aName ) )
                {
                    parameters["properties"]["reference_start"] =
                            pointSchema( "Start point of the already-routed reference segment." );
                    parameters["properties"]["reference_end"] =
                            pointSchema( "End point of the already-routed reference segment." );
                    parameters["properties"]["offset"] =
                            pointSchema( "Integer x/y offset to translate the reference segment into a parallel route candidate." );
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description",
                                "Optional target net; defaults to the active routing net." } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description",
                                "Optional target layer; defaults to the active routing layer." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description",
                                "Optional target width; defaults to the active routing width." } };
                    parameters["required"] = nlohmann::json::array(
                            { "reference_start", "reference_end", "offset" } );
                }
                else if( isRoutingBusCandidateTool( aName ) )
                {
                    parameters["properties"]["reference_start"] =
                            pointSchema( "Start point of the already-routed reference segment." );
                    parameters["properties"]["reference_end"] =
                            pointSchema( "End point of the already-routed reference segment." );
                    parameters["properties"]["lane_offsets"] =
                            pointArraySchema( "Array of integer x/y offsets, one per bus lane." );
                    parameters["properties"]["nets"] =
                            { { "type", "array" },
                              { "description",
                                "Optional target net names; when provided, count must match lane_offsets." } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description",
                                "Optional target layer; defaults to the active routing layer." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description",
                                "Optional target width; defaults to the active routing width." } };
                    parameters["required"] = nlohmann::json::array(
                            { "reference_start", "reference_end", "lane_offsets" } );
                }
                else if( isRoutingReplacePathCandidateTool( aName ) )
                {
                    parameters["properties"]["replace_handles"] =
                            handleArraySchema(
                                    "Existing session handles to delete as part of the replacement path candidate." );
                    parameters["properties"]["replacement_points"] =
                            pointArraySchema( "Replacement polyline points, at least start and end.", 2 );
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description", "Target net for the replacement path." } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description", "Target routing layer for the replacement path." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Replacement track width." } };
                    parameters["required"] = nlohmann::json::array(
                            { "replace_handles", "replacement_points", "net",
                              "layer", "width" } );
                }
                else if( isRoutingConstraintAwareRerouteCandidateTool( aName ) )
                {
                    parameters["properties"]["replace_handles"] =
                            handleArraySchema(
                                    "Existing session handles to delete as part of the constraint-aware reroute candidate." );
                    parameters["properties"]["replacement_points"] =
                            pointArraySchema( "Replacement polyline points, at least start and end.", 2 );
                    parameters["properties"]["constraints"] =
                            { { "type", "object" },
                              { "description",
                                "Constraint facts, for example clearance, keepout, "
                                "or DRC-lite source facts used by the model." } };
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description", "Target net for the reroute." } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description", "Target routing layer for the reroute." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description", "Replacement track width." } };
                    parameters["required"] = nlohmann::json::array(
                            { "replace_handles", "replacement_points",
                              "constraints", "net", "layer", "width" } );
                }
                else if( isPlacementRepairViaTool( aName ) )
                {
                    parameters["properties"]["position"] =
                            pointSchema( "Board position for the repaired via, using integer internal coordinates." );
                    parameters["properties"]["net"] =
                            { { "type", "string" },
                              { "description",
                                "Net assigned to the repaired via; use empty string for NoNet." } };
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
                            handleArraySchema(
                                    "Session handles for hidden placement items to move. Use handles returned by earlier hidden attempt tools." );
                    parameters["properties"]["delta"] =
                            pointSchema( "Integer internal-coordinate movement delta with x and y." );
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
                else if( isPlacementRepairFootprintOrientationTool( aName ) )
                {
                    parameters["properties"]["handles"] =
                            handleArraySchema(
                                    "Session handles for the footprint or selected placement items to orient in the hidden attempt." );
                    parameters["properties"]["target_orientation_degrees"] =
                            { { "type", "number" },
                              { "description",
                                "Target footprint orientation in degrees." } };
                    parameters["properties"]["current_orientation_degrees"] =
                            { { "type", "number" },
                              { "description",
                                "Optional current orientation fact used for audit." } };
                    parameters["properties"]["target_side"] =
                            { { "type", "string" },
                              { "description",
                                "Optional target board side or layer family." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias for this orientation repair." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "handles", "target_orientation_degrees" } );
                }
                else if( isRoutingRepairSegmentTool( aName ) )
                {
                    parameters["properties"]["start"] =
                            pointSchema( "Start point for the repaired route segment, using integer internal coordinates." );
                    parameters["properties"]["end"] =
                            pointSchema( "End point for the repaired route segment, using integer internal coordinates." );
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
                            pointArraySchema( "Ordered route polyline points using integer internal coordinates.", 2 );
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
                else if( isRoutingRepairBusSegmentsTool( aName ) )
                {
                    parameters["properties"]["segments"] =
                            { { "type", "array" },
                              { "description",
                                "Ordered bus lane segment objects. Each segment "
                                "must provide start, end, and net; layer and "
                                "width may be supplied per segment or at top level." },
                              { "items", busSegmentSchema() },
                              { "minItems", 1 },
                              { "maxItems", 16 } };
                    parameters["properties"]["layer"] =
                            { { "type", "string" },
                              { "description",
                                "Default routing layer for all bus segments." } };
                    parameters["properties"]["width"] =
                            { { "type", "integer" },
                              { "minimum", 1 },
                              { "description",
                                "Default routing width for all bus segments." } };
                    parameters["properties"]["alias"] =
                            { { "type", "string" },
                              { "description",
                                "Optional model-readable alias prefix for the bus repair." } };
                    parameters["properties"]["metadata"] =
                            { { "type", "object" },
                              { "description", "Optional provenance metadata." } };
                    parameters["required"] = nlohmann::json::array(
                            { "segments" } );
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
                    parameters["properties"]["write_policy"] =
                            { { "type", "string" },
                              { "enum",
                                nlohmann::json::array(
                                        { "fill_empty_only", "allow_overwrite" } ) },
                              { "description",
                                "Structured surface write policy. Use fill_empty_only "
                                "for ambient Auto-fill or Refill so accept replay "
                                "cannot overwrite user-authored non-empty values." } };
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
                + ", namespace="
                + tool.value( "namespace", std::string( "unknown" ) )
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
        const AI_SUGGESTION_RECORD& aCandidate,
        const wxString& aRequestedRenderArgsJson ) const
{
    nlohmann::json surfacePatchPreviews =
            surfacePatchPreviewFactsJson( aSession );
    nlohmann::json requestedArgs = nlohmann::json::parse(
            toUtf8String( aRequestedRenderArgsJson ), nullptr, false );
    nlohmann::json renderArgs =
            { { "scope", "session" }, { "mode", "hidden_attempt" } };

    if( requestedArgs.is_object() )
    {
        const std::string scope =
                requestedArgs.value( "scope", std::string() );

        if( scope == "session" || scope == "affected_area"
            || scope == "selection" || scope == "region" )
        {
            renderArgs["scope"] = scope;
        }

        const std::string mode =
                requestedArgs.value( "mode", std::string() );

        if( !mode.empty() )
            renderArgs["mode"] = mode;

        for( const char* key : { "region", "layer_mask", "view_mode" } )
        {
            if( requestedArgs.contains( key ) )
                renderArgs[key] = requestedArgs[key];
        }
    }

    const wxString renderArgsJson = fromUtf8String( renderArgs.dump() );

    if( m_PreviewService )
    {
        AI_SESSION_PREVIEW_RESULT result =
                m_PreviewService->RenderPreview( aSession, renderArgsJson );
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
                  { "render_args", renderArgs },
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
        const AI_EXECUTION_SESSION& aSession, const AI_SUGGESTION_RECORD&,
        const wxString& aRequestedValidationArgsJson ) const
{
    nlohmann::json requestedArgs = nlohmann::json::parse(
            toUtf8String( aRequestedValidationArgsJson ), nullptr, false );
    nlohmann::json validationArgs =
            { { "scope", "session" }, { "level", "drc_lite" } };

    if( requestedArgs.is_object() )
    {
        const std::string level =
                requestedArgs.value( "level", std::string() );

        if( level == "geometry" || level == "drc_lite"
            || level == "full_drc" )
        {
            validationArgs["level"] = level;
        }

        const std::string scope =
                requestedArgs.value( "scope", std::string() );

        if( scope == "session" || scope == "affected_area"
            || scope == "selection" || scope == "region" )
        {
            validationArgs["scope"] = scope;
        }

        for( const char* key : { "region", "handles", "gate" } )
        {
            if( requestedArgs.contains( key ) )
                validationArgs[key] = requestedArgs[key];
        }
    }

    const wxString validationArgsJson =
            fromUtf8String( validationArgs.dump() );

    if( m_ValidationService )
    {
        AI_SESSION_VALIDATION_RESULT result =
                m_ValidationService->RunValidation( aSession, validationArgsJson,
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
                    validationArgs },
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

        if( serviceResult.contains( "validation" )
            && serviceResult["validation"].is_object() )
        {
            const nlohmann::json& nativeValidation =
                    serviceResult["validation"];
            nlohmann::json summary = nlohmann::json::object();

            for( const char* key : { "status", "backend", "scope", "level",
                                     "grade", "exactness", "issue_count",
                                     "preview_state_exact",
                                     "accept_validation_sufficient" } )
            {
                if( nativeValidation.contains( key ) )
                    summary[key] = nativeValidation[key];
            }

            for( const char* key : { "rule_load", "connectivity", "refill" } )
            {
                if( nativeValidation.contains( key ) )
                    summary[key] = nativeValidation[key];
            }

            if( !summary.empty() )
                validation["validation_summary"] = std::move( summary );
        }

        if( serviceResult.contains( "validation" )
            && serviceResult["validation"].is_object()
            && serviceResult["validation"].contains( "issues" )
            && serviceResult["validation"]["issues"].is_array() )
        {
            nlohmann::json issueGeometryFacts = nlohmann::json::array();
            bool           truncated = false;

            for( const nlohmann::json& issue : serviceResult["validation"]["issues"] )
            {
                if( !issue.is_object() )
                    continue;

                if( !issue.contains( "geometry" ) && !issue.contains( "bbox" )
                    && !issue.contains( "region" )
                    && !issue.contains( "position" )
                    && !issue.contains( "main_item_bbox" )
                    && !issue.contains( "aux_item_bbox" ) )
                {
                    continue;
                }

                nlohmann::json fact = nlohmann::json::object();

                for( const char* key : { "source", "kind", "key", "title",
                                         "severity", "message", "code", "rule",
                                         "net", "layer", "layer_name",
                                         "blocking", "blocks_publish",
                                         "main_item_uuid", "aux_item_uuid",
                                         "main_item_type", "aux_item_type" } )
                {
                    if( issue.contains( key ) )
                        fact[key] = issue[key];
                }

                if( issue.contains( "geometry" ) )
                    fact["geometry"] = issue["geometry"];

                if( issue.contains( "bbox" ) )
                    fact["bbox"] = issue["bbox"];

                if( issue.contains( "region" ) )
                    fact["region"] = issue["region"];

                if( issue.contains( "position" ) )
                    fact["position"] = issue["position"];

                if( issue.contains( "main_item_bbox" ) )
                    fact["main_item_bbox"] = issue["main_item_bbox"];

                if( issue.contains( "aux_item_bbox" ) )
                    fact["aux_item_bbox"] = issue["aux_item_bbox"];

                addMainAuxBBoxRelationFact( issue, fact );

                issueGeometryFacts.push_back( std::move( fact ) );

                if( issueGeometryFacts.size() >= 8 )
                {
                    truncated = true;
                    break;
                }
            }

            if( !issueGeometryFacts.empty() )
            {
                validation["issue_geometry_fact_count"] = issueGeometryFacts.size();
                validation["issue_geometry_facts"] = std::move( issueGeometryFacts );

                if( truncated )
                    validation["issue_geometry_facts_truncated"] = true;
            }
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

                if( name == "placement_generate_footprint_transform_candidates"
                    || name == "placement.generate_footprint_transform_candidates" )
                {
                    return std::string(
                            "placement.generate_footprint_transform_candidates" );
                }

                if( name == "placement_generate_footprint_orientation_candidates"
                    || name == "placement.generate_footprint_orientation_candidates" )
                {
                    return std::string(
                            "placement.generate_footprint_orientation_candidates" );
                }

                if( name == "routing_generate_segment_candidates"
                    || name == "routing.generate_segment_candidates" )
                {
                    return std::string( "routing.generate_segment_candidates" );
                }

                if( name == "routing_generate_parallel_segment_candidates"
                    || name == "routing.generate_parallel_segment_candidates" )
                {
                    return std::string(
                            "routing.generate_parallel_segment_candidates" );
                }

                if( name == "routing_generate_bus_segment_candidates"
                    || name == "routing.generate_bus_segment_candidates" )
                {
                    return std::string(
                            "routing.generate_bus_segment_candidates" );
                }

                if( name == "routing_generate_replace_path_candidates"
                    || name == "routing.generate_replace_path_candidates" )
                {
                    return std::string(
                            "routing.generate_replace_path_candidates" );
                }

                if( name == "routing_generate_constraint_aware_reroute_candidates"
                    || name == "routing.generate_constraint_aware_reroute_candidates" )
                {
                    return std::string(
                            "routing.generate_constraint_aware_reroute_candidates" );
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

                if( name == "placement_repair_footprint_orientation"
                    || name == "placement.repair_footprint_orientation" )
                {
                    return std::string(
                            "placement.repair_footprint_orientation" );
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

                if( name == "routing_repair_bus_segments"
                    || name == "routing.repair_bus_segments" )
                {
                    return std::string( "routing.repair_bus_segments" );
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

    if( toolName == "placement.generate_footprint_transform_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                placementFootprintTransformCandidatePayloadJson( args );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "placement.generate_footprint_transform_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
    }

    if( toolName == "placement.generate_footprint_orientation_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                placementFootprintOrientationCandidatePayloadJson( args );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "placement.generate_footprint_orientation_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
    }

    if( toolName == "routing.generate_parallel_segment_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                routingParallelCandidatePayloadJson( args, aObservation );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "routing.generate_parallel_segment_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
    }

    if( toolName == "routing.generate_bus_segment_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                routingBusCandidatePayloadJson( args, aObservation );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "routing.generate_bus_segment_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
    }

    if( toolName == "routing.generate_replace_path_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                routingReplacePathCandidatePayloadJson( args );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "routing.generate_replace_path_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
    }

    if( toolName == "routing.generate_constraint_aware_reroute_candidates" )
    {
        nlohmann::json args = objectFromJsonText( aToolCall.m_ArgumentsJson );
        nlohmann::json payload =
                routingConstraintAwareRerouteCandidatePayloadJson( args );
        const bool malformed =
                payload.value( "status", std::string() ) == "malformed_arguments";
        const wxString errorCode =
                malformed ? wxString( wxS( "malformed_arguments" ) ) : wxString();
        const wxString message =
                malformed
                        ? wxString( wxS( "routing.generate_constraint_aware_reroute_candidates "
                                         "received malformed arguments." ) )
                        : wxString( wxS( "Candidates generated." ) );

        return makeResult(
                !malformed, !malformed, errorCode, message,
                std::move( payload ) );
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

        if( isRoutingRepairBusSegmentsTool( toolName ) )
        {
            if( !args.contains( "segments" ) || !args["segments"].is_array()
                || args["segments"].empty() || args["segments"].size() > 16 )
            {
                return makeResult(
                        false, false, wxS( "malformed_arguments" ),
                        wxS( "routing.repair_bus_segments requires 1-16 segments." ),
                        { { "tool", boundedPlanToolName },
                          { "status", "malformed_arguments" } } );
            }

            auto widthFrom =
                    []( const nlohmann::json& aObject ) -> std::optional<int>
                    {
                        if( aObject.is_object() && aObject.contains( "width" )
                            && aObject["width"].is_number_integer()
                            && aObject["width"].get<int>() > 0 )
                        {
                            return aObject["width"].get<int>();
                        }

                        return std::nullopt;
                    };

            nlohmann::json operations = nlohmann::json::array();
            const std::optional<std::string> defaultLayer =
                    stringField( args, "layer" );
            const std::optional<int> defaultWidth = widthFrom( args );
            const std::optional<std::string> aliasPrefix =
                    stringField( args, "alias" );

            size_t laneIndex = 0;

            for( const nlohmann::json& segment : args["segments"] )
            {
                std::optional<std::string> layer = stringField( segment, "layer" );
                std::optional<std::string> net = stringField( segment, "net" );
                std::optional<int> width = widthFrom( segment );

                if( !layer )
                    layer = defaultLayer;

                if( !width )
                    width = defaultWidth;

                if( !segment.is_object()
                    || !segment.contains( "start" )
                    || !segment["start"].is_object()
                    || !segment.contains( "end" )
                    || !segment["end"].is_object()
                    || !net || !layer || !width )
                {
                    return makeResult(
                            false, false, wxS( "malformed_arguments" ),
                            wxS( "routing.repair_bus_segments requires every "
                                 "segment to resolve start, end, net, layer, and width." ),
                            { { "tool", boundedPlanToolName },
                              { "status", "malformed_arguments" },
                              { "lane_index", laneIndex } } );
                }

                nlohmann::json operationArgs =
                        { { "start", segment["start"] },
                          { "end", segment["end"] },
                          { "layer", *layer },
                          { "net", *net },
                          { "width", *width } };

                if( segment.contains( "metadata" ) && segment["metadata"].is_object() )
                    operationArgs["metadata"] = segment["metadata"];
                else if( args.contains( "metadata" ) && args["metadata"].is_object() )
                    operationArgs["metadata"] = args["metadata"];

                if( aliasPrefix )
                {
                    operationArgs["alias"] = *aliasPrefix + ":lane:"
                                             + std::to_string( laneIndex );
                }
                else if( segment.contains( "alias" ) && segment["alias"].is_string() )
                {
                    operationArgs["alias"] = segment["alias"];
                }

                operations.push_back(
                        { { "kind", "pcb.create_track_segment" },
                          { "arguments", std::move( operationArgs ) } } );
                ++laneIndex;
            }

            args =
                    { { "plan", { { "operations", std::move( operations ) } } },
                      { "max_steps", laneIndex } };
        }
        else if( isRepairWrapperTool( toolName ) )
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
            else if( isPlacementRepairFootprintOrientationTool( toolName ) )
            {
                operationKind = "pcb.set_item_properties";
                malformed = !args.contains( "handles" )
                             || !args["handles"].is_array()
                             || args["handles"].empty()
                             || !args.contains( "target_orientation_degrees" )
                             || !args["target_orientation_degrees"].is_number();
                malformedMessage =
                        wxS( "placement.repair_footprint_orientation requires "
                             "handles and target_orientation_degrees." );

                if( !malformed )
                {
                    nlohmann::json typedProps =
                            { { "orientation_degrees",
                                args["target_orientation_degrees"] } };

                    if( args.contains( "current_orientation_degrees" )
                        && args["current_orientation_degrees"].is_number() )
                    {
                        typedProps["current_orientation_degrees"] =
                                args["current_orientation_degrees"];
                    }

                    if( args.contains( "target_side" )
                        && args["target_side"].is_string() )
                    {
                        typedProps["side"] = args["target_side"];
                    }

                    args["typed_props"] = std::move( typedProps );
                }
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

            if( operationKind == "surface.apply_patch"
                && !args.contains( "write_policy" )
                && !( args.contains( "patch" ) && args["patch"].is_object()
                      && args["patch"].contains( "write_policy" ) ) )
            {
                args["write_policy"] = "fill_empty_only";
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

        const ATTEMPT_BUDGET_POLICY mutationPolicy =
                attemptPolicyForWorkState( budgetPolicyWorkStateForCandidate(
                        aAttempt->m_Candidate ) );
        const uint64_t currentMutationCount =
                aAttempt->m_BudgetCounters.m_MutationCount;
        const uint64_t requestedOperationCount =
                static_cast<uint64_t>( operations.size() );
        const uint64_t remainingMutationBudget =
                currentMutationCount >= mutationPolicy.m_MaxMutations
                        ? 0
                        : mutationPolicy.m_MaxMutations - currentMutationCount;

        if( requestedOperationCount > remainingMutationBudget )
        {
            return makeResult(
                    false, false, wxS( "mutation_budget_exceeded" ),
                    wxS( "hidden mutation batch would exceed the attempt "
                         "mutation budget." ),
                    { { "tool", boundedPlanToolName },
                      { "status", "mutation_budget_exceeded" },
                      { "operation_count", requestedOperationCount },
                      { "attempt_mutation_count", currentMutationCount },
                      { "remaining_mutation_budget", remainingMutationBudget },
                      { "max_mutations", mutationPolicy.m_MaxMutations } } );
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

            if( kindName == "surface.apply_patch"
                && !operationArgs.contains( "write_policy" )
                && !( operationArgs.contains( "patch" )
                      && operationArgs["patch"].is_object()
                      && operationArgs["patch"].contains( "write_policy" ) ) )
            {
                operationArgs["write_policy"] = "fill_empty_only";
            }

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
                record.m_ResultJson =
                        atomicFailureResultJson( execution,
                                                 "script_operation_failed" );
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
        bool                   rolledBackFailedPlan = false;

        if( failed )
        {
            rolledBackFailedPlan = session->RollbackTo( checkpointId );
            observation.m_Epoch = session->Epoch();
            observation.m_OperationCount =
                    session->Journal().Operations().size();
            observation.m_Summary =
                    rolledBackFailedPlan
                            ? wxS( "bounded_script_plan.rollback_on_failure" )
                            : wxS( "bounded_script_plan.rollback_failed" );
        }

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

            if( !failed || !rolledBackFailedPlan )
            {
                keptBatches.push_back(
                        { { "tool", boundedPlanToolName },
                          { "tool_call_id", toolCallId },
                          { "operation_count", mergedOperationCount },
                          { "script_step_id", stepId },
                          { "checkpoint_id", checkpointId },
                          { "active_attempt_frame", true } } );
            }

            if( !keptBatches.empty() )
                journal["merged_tool_batches"] = std::move( keptBatches );

            nlohmann::json rolledBackToolCallIds =
                    activeJournal.contains( "rolled_back_tool_call_ids" )
                    && activeJournal["rolled_back_tool_call_ids"].is_array()
                            ? activeJournal["rolled_back_tool_call_ids"]
                            : nlohmann::json::array();

            if( failed && rolledBackFailedPlan )
            {
                bool alreadyTrackedRollback = false;

                for( const nlohmann::json& id : rolledBackToolCallIds )
                {
                    if( id.is_string() && id.get<std::string>() == toolCallId )
                    {
                        alreadyTrackedRollback = true;
                        break;
                    }
                }

                if( !alreadyTrackedRollback )
                    rolledBackToolCallIds.push_back( toolCallId );
            }

            if( !rolledBackToolCallIds.empty() )
            {
                journal["rolled_back_tool_call_ids"] =
                        std::move( rolledBackToolCallIds );
            }

            aAttempt->m_JournalJson = fromUtf8String( journal.dump() );
            syncAttemptMutationCountersFromJournal( *aAttempt );

            nlohmann::json provenance =
                    parseObjectBody( aAttempt->m_ProvenanceJson );

            if( !provenance.is_object() )
                provenance = nlohmann::json::object();

            provenance["session_journal"] = journal;

            if( failed && rolledBackFailedPlan )
            {
                nlohmann::json rollback =
                        { { "tool", boundedPlanToolName },
                          { "status", "rolled_back" },
                          { "checkpoint_id", checkpointId },
                          { "rolled_back", true },
                          { "rollback_scope", "active_attempt_frame" },
                          { "rolled_back_tool_call_id", toolCallId },
                          { "partial_mutation_discarded", true },
                          { "attempt_session_journal", journal },
                          { "publish_allowed", false } };

                nlohmann::json rollbackHistory =
                        provenance.contains( "rollback_history" )
                        && provenance["rollback_history"].is_array()
                                ? provenance["rollback_history"]
                                : nlohmann::json::array();
                rollbackHistory.push_back( rollback );
                provenance["rollback_history"] = std::move( rollbackHistory );
                provenance["last_rollback"] = std::move( rollback );
                provenance["partial_mutation_discarded"] = true;
            }

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
            payload["rolled_back"] = rolledBackFailedPlan;
            payload["rollback_checkpoint_id"] = checkpointId;
            payload["rollback_scope"] =
                    usingActiveAttemptFrame ? "active_attempt_frame"
                                            : "isolated_script_session";
            payload["partial_mutation_discarded"] = rolledBackFailedPlan;
        }

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

        const ATTEMPT_BUDGET_POLICY renderPolicy =
                attemptPolicyForWorkState( budgetPolicyWorkStateForCandidate(
                        aAttempt->m_Candidate ) );

        if( aAttempt->m_BudgetCounters.m_RenderCount
            >= renderPolicy.m_MaxRenderCount )
        {
            return makeResult(
                    false, false, wxS( "render_budget_exceeded" ),
                    wxS( "render.hidden_attempt would exceed the attempt "
                         "render budget." ),
                    { { "tool", "render.hidden_attempt" },
                      { "status", "render_budget_exceeded" },
                      { "attempt_render_count",
                        aAttempt->m_BudgetCounters.m_RenderCount },
                      { "remaining_render_budget", 0 },
                      { "max_render_count", renderPolicy.m_MaxRenderCount } } );
        }

        std::unique_ptr<AI_EXECUTION_SESSION> fallbackSession;
        AI_EXECUTION_SESSION* session = aAttemptSession;

        if( !session )
        {
            fallbackSession = attemptSessionFromJournal( *aAttempt );
            session = fallbackSession.get();
        }

        aAttempt->m_RenderOutputsJson =
                RenderAttempt( *session, aAttempt->m_Candidate,
                               aToolCall.m_ArgumentsJson );
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

        const ATTEMPT_BUDGET_POLICY validationPolicy =
                attemptPolicyForWorkState( budgetPolicyWorkStateForCandidate(
                        aAttempt->m_Candidate ) );

        if( aAttempt->m_BudgetCounters.m_ValidationCount
            >= validationPolicy.m_MaxValidationCount )
        {
            return makeResult(
                    false, false, wxS( "validation_budget_exceeded" ),
                    wxS( "validate.hidden_attempt would exceed the attempt "
                         "validation budget." ),
                    { { "tool", "validate.hidden_attempt" },
                      { "status", "validation_budget_exceeded" },
                      { "attempt_validation_count",
                        aAttempt->m_BudgetCounters.m_ValidationCount },
                      { "remaining_validation_budget", 0 },
                      { "max_validation_count",
                        validationPolicy.m_MaxValidationCount } } );
        }

        std::unique_ptr<AI_EXECUTION_SESSION> fallbackSession;
        AI_EXECUTION_SESSION* session = aAttemptSession;

        if( !session )
        {
            fallbackSession = attemptSessionFromJournal( *aAttempt );
            session = fallbackSession.get();
        }

        aAttempt->m_ValidationFactsJson =
                ValidateAttempt( *session, aAttempt->m_Candidate,
                                 aToolCall.m_ArgumentsJson );
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

    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls = aRequest.m_ToolResults;
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

    if( !response.m_ToolCalls.empty()
        && toolRounds >= aRequest.m_MaxToolRounds )
    {
        std::vector<AI_TOOL_CALL_RECORD> unhandledToolCalls =
                std::move( response.m_ToolCalls );

        for( AI_TOOL_CALL_RECORD& toolCall : unhandledToolCalls )
        {
            toolCall.m_RequestId = aRequest.m_RequestId;
            toolCall.m_Allowed = false;
            toolCall.m_Executed = false;
            toolCall.m_ErrorCode = wxS( "tool_round_budget_exceeded" );
            toolCall.m_Message = wxS( "Next Action tool round budget was "
                                      "exhausted before this tool call could run." );

            nlohmann::json result =
                    { { "tool", toUtf8String( toolCall.m_ToolName ) },
                      { "provider_tool_name", toUtf8String( toolCall.m_ToolName ) },
                      { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
                      { "status", "tool_round_budget_exceeded" },
                      { "allowed", false },
                      { "executed", false },
                      { "publish_allowed", false },
                      { "error_code", "tool_round_budget_exceeded" },
                      { "message",
                        "Next Action tool round budget was exhausted before "
                        "this tool call could run." },
                      { "max_tool_rounds", aRequest.m_MaxToolRounds },
                      { "completed_tool_rounds", toolRounds } };

            toolCall.m_ResultJson = fromUtf8String( result.dump() );
        }

        handledToolCalls.insert(
                handledToolCalls.end(),
                std::make_move_iterator( unhandledToolCalls.begin() ),
                std::make_move_iterator( unhandledToolCalls.end() ) );

        if( aAttempt )
        {
            const nlohmann::json toolTrace =
                    { { "provider_tool_results",
                        toolCallRecordsJson( handledToolCalls ) } };
            attachReviewProviderToolResultsToAttempt(
                    *aAttempt, fromUtf8String( toolTrace.dump() ) );
        }
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
    step.m_SemanticEventJson = fromUtf8String( semanticEventJson( *event ).dump() );

    AI_OBSERVATION_PACKET observation = buildObservationPacket( *event );
    step.m_ObservationPacketId = observation.m_Id;
    step.m_ObservationPacketJson = observation.AsJsonText();

    AI_NEXT_ACTION_LLM_DECISION decision = runDecisionTurn( step, observation );
    step.m_LlmDecisionJson = decision.m_RawJson;
    step.m_LlmDecisionToolResultsJson = decision.m_ToolResultsJson;

    if( !decision.WantsAttempt() )
    {
        step.m_Status = AI_NEXT_ACTION_STEP_STATUS::Abandoned;
        m_Steps.push_back( step );
        return std::nullopt;
    }

    std::vector<AI_SUGGESTION_RECORD> candidates =
            decisionToolGeneratedCandidates( decision, observation );
    std::vector<AI_SUGGESTION_RECORD> workStateCandidates =
            m_Tools.GenerateCandidates( observation );

    candidates.insert( candidates.end(),
                       std::make_move_iterator( workStateCandidates.begin() ),
                       std::make_move_iterator( workStateCandidates.end() ) );

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
    const ATTEMPT_POLICY_SIGNALS policySignals =
            attemptPolicySignalsForWorkState( m_Suggestions, m_Attempts,
                                              m_Steps, observation.m_Kind );
    const size_t attemptLimit = explicitCandidateSelected
                                        ? 1
                                        : attemptLimitForWorkState( observation.m_Kind,
                                                                    policySignals );

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

        std::vector<AI_TOOL_CALL_RECORD> reviewInitialToolResults;
        size_t                           gateFeedbackRounds = 0;
        bool                             tryNextCandidate = false;
        bool                             stopAttemptLoop = false;

        for( ;; )
        {
            AI_NEXT_ACTION_REVIEW_DECISION review =
                    runReviewTurn( step, observation, attempt,
                                   reviewInitialToolResults );
            step.m_ReviewDecisionJson = review.m_RawJson;
            step.m_ReviewToolResultsJson = review.m_ToolResultsJson;
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
                    if( previewGateFailureCanReenterReview( publish.m_GateResult,
                                                            publish.m_RawJson )
                        && gateFeedbackRounds < 2
                        && attemptHasReviewToolRoundsRemaining( attempt ) )
                    {
                        reviewInitialToolResults =
                                toolCallRecordsFromReviewJson( publish.m_RawJson );
                        reviewInitialToolResults.push_back(
                                previewGateFeedbackToolResult(
                                        step.m_Id * 10 + 2, publish,
                                        ++gateFeedbackRounds ) );
                        continue;
                    }

                    tryNextCandidate = ii + 1 < candidates.size();
                    stopAttemptLoop = true;
                    break;
                }

                AI_SUGGESTION_RECORD suggestion =
                        publishAttempt( step, attempt, publish );
                std::optional<AI_SUGGESTION_RECORD> stored =
                        storeSuggestion( suggestion );

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
                stopAttemptLoop = true;
                break;
            }

            if( !m_Attempts.empty() && m_Attempts.back().m_Id == attempt.m_Id )
                m_Attempts.back().m_RollbackJson =
                        m_Tools.RollbackAttempt( attempt.m_BaseCheckpointId );

            break;
        }

        if( tryNextCandidate )
            continue;

        if( stopAttemptLoop )
            break;
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


std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> AI_NEXT_ACTION_RUNTIME::ReplayTraceRecords() const
{
    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> records;

    for( const AI_NEXT_ACTION_RUNTIME_STEP& step : m_Steps )
    {
        const AI_SUGGESTION_RECORD* publishedSuggestion = nullptr;

        if( step.m_PublishedSuggestionId != 0 )
        {
            for( const AI_SUGGESTION_RECORD& suggestion : m_Suggestions )
            {
                if( suggestion.m_Id == step.m_PublishedSuggestionId )
                {
                    publishedSuggestion = &suggestion;
                    break;
                }
            }
        }

        const std::string terminalState =
                publishedSuggestion
                        ? suggestionStatusJsonName( publishedSuggestion->m_Status )
                        : nextActionStepStatusJsonName( step.m_Status );

        nlohmann::json attempts = nlohmann::json::array();

        for( uint64_t attemptId : step.m_AttemptIds )
        {
            auto attemptIt =
                    std::find_if( m_Attempts.begin(), m_Attempts.end(),
                                  [attemptId]( const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt )
                                  {
                                      return aAttempt.m_Id == attemptId;
                                  } );

            if( attemptIt != m_Attempts.end() )
                attempts.push_back( attemptReplayJson( *attemptIt ) );
        }

        nlohmann::json llmDecision = parseObjectBody( step.m_LlmDecisionJson );
        nlohmann::json llmReview = parseObjectBody( step.m_ReviewDecisionJson );
        nlohmann::json publishDecision = llmReview;

        if( publishedSuggestion )
        {
            nlohmann::json provenance =
                    parseObjectBody( publishedSuggestion->m_RuntimeProvenanceJson );

            if( provenance.contains( "preview_lease" ) )
                publishDecision["preview_lease"] = provenance["preview_lease"];

            if( provenance.contains( "accept_token" ) )
                publishDecision["accept_token"] = provenance["accept_token"];

            if( provenance.contains( "preview_gate_result" ) )
                publishDecision["preview_gate_result"] = provenance["preview_gate_result"];

            if( provenance.contains( "accept_gate_result" ) )
                publishDecision["accept_gate_result"] = provenance["accept_gate_result"];
        }

        nlohmann::json trace =
                { { "schema",
                    { { "name", "kisurf.next_action.replay_trace" },
                      { "version", AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION } } },
                  { "runtime", "next_action" },
                  { "runtime_step_id", step.m_Id },
                  { "suggestion_stream_id", toUtf8String( step.m_SuggestionStreamId ) },
                  { "status", nextActionStepStatusJsonName( step.m_Status ) },
                  { "terminal_state", terminalState },
                  { "context_version",
                    parseObjectBody( step.m_ContextVersion.AsJsonText() ) },
                  { "semantic_event", parseObjectBody( step.m_SemanticEventJson ) },
                  { "observation_packet", parseObjectBody( step.m_ObservationPacketJson ) },
                  { "llm_decision", llmDecision },
                  { "tool_results",
                    { { "decision",
                        parseJsonText( step.m_LlmDecisionToolResultsJson,
                                       nlohmann::json::array() ) },
                      { "review",
                        parseJsonText( step.m_ReviewToolResultsJson,
                                       nlohmann::json::array() ) } } },
                  { "attempt_ids", step.m_AttemptIds },
                  { "attempts", attempts },
                  { "llm_review_decision", llmReview } };

        if( step.m_PublishedSuggestionId != 0 )
        {
            trace["published_suggestion_id"] = step.m_PublishedSuggestionId;
            trace["publish_decision"] = publishDecision;
        }

        AI_NEXT_ACTION_REPLAY_TRACE_RECORD record;
        record.m_Sequence = step.m_Id;
        record.m_RuntimeStepId = step.m_Id;
        record.m_Status = fromUtf8String( terminalState );
        record.m_ReplayJson = fromUtf8String( trace.dump() );
        records.push_back( std::move( record ) );
    }

    return records;
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
                || hasPreviewableOperation( *suggestion )
                || hasNextActionRuntimePreviewArtifact( *suggestion ) );
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
                    || hasActionPreviewOperation( *suggestion )
                    || hasNextActionRuntimePreviewArtifact( *suggestion ) );
    }

    return !suggestion->m_PreviewOnly
           && ( !suggestion->m_EditObjects.empty()
                || hasActionPreviewOperation( *suggestion ) )
           && suggestion->m_RuntimeProvenanceJson.IsEmpty();
}


wxString surfacePatchOverlayValueLabel( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return fromUtf8String( aValue.get<std::string>() );

    if( aValue.is_null() )
        return wxS( "<null>" );

    return fromUtf8String( aValue.dump() );
}


wxString surfacePatchOverlayItemLabel( const nlohmann::json& aEntry,
                                       size_t aFallbackIndex )
{
    if( aEntry.contains( "target_path" ) && aEntry["target_path"].is_string() )
        return fromUtf8String( aEntry["target_path"].get<std::string>() );

    return wxString::Format( wxS( "surface_patch_diff_%zu" ),
                             aFallbackIndex );
}


wxString surfacePatchOverlayMessage( const nlohmann::json& aEntry )
{
    const std::string kind = jsonStringOrEmpty( aEntry, "kind" );
    const nlohmann::json& visualTarget =
            aEntry.contains( "visual_target" )
            && aEntry["visual_target"].is_object()
                    ? aEntry["visual_target"]
                    : nlohmann::json::object();
    wxString proposed = wxS( "<unknown>" );

    if( aEntry.contains( "proposed_value" ) )
        proposed = surfacePatchOverlayValueLabel( aEntry["proposed_value"] );
    else if( aEntry.contains( "value" ) )
        proposed = surfacePatchOverlayValueLabel( aEntry["value"] );

    if( kind == "set_cell" )
    {
        return wxString::Format(
                wxS( "SurfacePatch cell %s/%s -> %s" ),
                fromUtf8String( jsonStringOrEmpty( visualTarget, "row_id" ) ),
                fromUtf8String( jsonStringOrEmpty( visualTarget, "column_id" ) ),
                proposed );
    }

    if( kind == "set_field" )
    {
        return wxString::Format(
                wxS( "SurfacePatch field %s -> %s" ),
                fromUtf8String( jsonStringOrEmpty( visualTarget, "field_id" ) ),
                proposed );
    }

    return wxString::Format( wxS( "SurfacePatch %s -> %s" ),
                             fromUtf8String( kind ), proposed );
}


void showSurfacePatchPreviewOverlaysFromToolRecords(
        const nlohmann::json& aToolRecords,
        AI_PREVIEW_MANAGER& aPreviewManager )
{
    if( !aToolRecords.is_array() )
        return;

    size_t fallbackIndex = 0;

    for( const nlohmann::json& toolRecord : aToolRecords )
    {
        if( !toolRecord.is_object() || !toolRecord.contains( "result" )
            || !toolRecord["result"].is_object() )
        {
            continue;
        }

        const nlohmann::json& result = toolRecord["result"];

        if( !result.contains( "surface_patch_previews" )
            || !result["surface_patch_previews"].is_array() )
        {
            continue;
        }

        for( const nlohmann::json& preview : result["surface_patch_previews"] )
        {
            if( !preview.is_object()
                || !preview.contains( "surface_patch_diff_entries" )
                || !preview["surface_patch_diff_entries"].is_array() )
            {
                continue;
            }

            for( const nlohmann::json& entry :
                 preview["surface_patch_diff_entries"] )
            {
                if( !entry.is_object() )
                    continue;

                aPreviewManager.ShowItemOverlay(
                        surfacePatchOverlayItemLabel( entry, fallbackIndex++ ),
                        wxS( "structured_surface_patch" ), wxS( "preview" ),
                        surfacePatchOverlayMessage( entry ),
                        fromUtf8String( entry.dump() ) );
            }
        }
    }
}


void showSurfacePatchPreviewOverlays(
        const AI_SUGGESTION_RECORD& aSuggestion,
        AI_PREVIEW_MANAGER& aPreviewManager )
{
    nlohmann::json provenance =
            parseObjectBody( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.is_object() )
        return;

    if( provenance.contains( "provider_tool_results" ) )
    {
        showSurfacePatchPreviewOverlaysFromToolRecords(
                provenance["provider_tool_results"], aPreviewManager );
    }

    if( provenance.contains( "attempt" ) && provenance["attempt"].is_object()
        && provenance["attempt"].contains( "provider_tool_results" ) )
    {
        showSurfacePatchPreviewOverlaysFromToolRecords(
                provenance["attempt"]["provider_tool_results"],
                aPreviewManager );
    }
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

    showSurfacePatchPreviewOverlays( *suggestion, aPreviewManager );

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

    const ATTEMPT_POLICY_SIGNALS policySignals =
            attemptPolicySignalsForWorkState( m_Suggestions, m_Attempts,
                                              m_Steps, aEvent.m_Kind );

    nlohmann::json facts =
            { { "slot_id", toUtf8String( aEvent.m_SlotId ) },
              { "work_state", toUtf8String( aEvent.m_Kind ) },
              { "attempt_policy", attemptPolicyJson( aEvent.m_Kind,
                                                     policySignals ) },
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
    request.m_MaxToolRounds =
            attemptPolicyForWorkState( aObservation.m_Kind ).m_MaxToolRounds;
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
    decision.m_ToolResultsJson =
            fromUtf8String( toolCallRecordsJson( response.m_ToolCalls ).dump() );
    return decision;
}


AI_NEXT_ACTION_REVIEW_DECISION AI_NEXT_ACTION_RUNTIME::runReviewTurn(
        const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
        const AI_OBSERVATION_PACKET& aObservation,
        AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
        const std::vector<AI_TOOL_CALL_RECORD>& aInitialToolResults )
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
                  { "hidden_mutation_requires_fresh_render", true },
                  { "hidden_mutation_requires_fresh_validation", true },
                  { "rollback_clears_pending_hidden_mutation_evidence",
                    true },
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
        reviewInput["previous_attempts"] = previousAttempts;

    if( !aStep.m_ReviewDecisionJson.IsEmpty() )
        reviewInput["previous_review_decision"] =
                parseObjectBody( aStep.m_ReviewDecisionJson );

    reviewInput["internal_tool_catalog"] =
            nlohmann::json::parse( toUtf8String( m_Tools.ToolCatalogJson() ) );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = aStep.m_Id * 10 + 2;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionReview;
    request.m_EditorKind = aObservation.m_ContextSnapshot.m_EditorKind;
    request.m_ContextVersion = aObservation.m_ContextVersion.m_ContextVersion;
    request.m_ContextSnapshot = aObservation.m_ContextSnapshot;
    request.m_UserText = fromUtf8String( reviewInput.dump() );
    request.m_ToolResults = aInitialToolResults;
    request.m_SystemPromptOverride =
            wxS( "You are reviewing a hidden KiSurf Next Action attempt. Decide "
                 "whether to publish, retry, rollback_retry, or abandon. "
                 "When previous_attempts are present, use their render, validation, "
                 "rollback, and review feedback to avoid repeating failed work. "
                 "Publishing requires review_basis.render_valid, "
                 "review_basis.validation_passed, review_basis.budget_within_limits, "
                 "and review_basis.self_review_passed to be true. "
                 "If you execute hidden mutation tools during review, call "
                 "render_hidden_attempt and validate_hidden_attempt after the final "
                 "hidden mutation before requesting publish. Return JSON only." );
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
    review.m_ToolResultsJson =
            fromUtf8String( toolCallRecordsJson( response.m_ToolCalls ).dump() );
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

    const wxString argumentsJson = aCandidate.m_ArgumentsJson.IsEmpty()
                                           ? wxString( wxS( "{}" ) )
                                           : aCandidate.m_ArgumentsJson;
    const nlohmann::json candidateArguments =
            objectFromJsonText( argumentsJson );

    auto appendFailedOperation =
            [&]( AI_SESSION_OPERATION_KIND aKind,
                 const wxString& aArgumentsJson,
                 const AI_ATOMIC_EXECUTION_RESULT& aExecution )
            {
                AI_SESSION_OPERATION_RECORD operation;
                operation.m_Kind = aKind;
                operation.m_ArgumentsJson = aArgumentsJson;
                operation.m_ResultJson =
                        atomicFailureResultJson( aExecution,
                                                 "operation_failed" );
                operation.m_Warnings = aExecution.m_Warnings;
                session->AppendOperation( std::move( operation ) );
            };

    if( candidateArgumentsHaveExecutablePlan( candidateArguments ) )
    {
        for( const nlohmann::json& operation :
             candidateArguments["plan"]["operations"] )
        {
            const AI_SESSION_OPERATION_KIND operationKind =
                    sessionOperationKindFromId(
                            operation["kind"].get<std::string>() );
            const nlohmann::json operationArguments =
                    operation.contains( "arguments" )
                    && operation["arguments"].is_object()
                            ? operation["arguments"]
                            : nlohmann::json::object();
            const wxString operationArgumentsJson =
                    fromUtf8String( operationArguments.dump() );
            AI_ATOMIC_EXECUTION_RESULT execution =
                    AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                            *session, operationKind, operationArgumentsJson );

            if( !execution.m_Ok )
            {
                appendFailedOperation( operationKind, operationArgumentsJson,
                                       execution );
                break;
            }
        }
    }
    else
    {
        const AI_SESSION_OPERATION_KIND operationKind =
                sessionOperationKindForCandidate( aCandidate );
        AI_ATOMIC_EXECUTION_RESULT execution =
                AI_ATOMIC_OPERATION_EXECUTOR::Execute( *session, operationKind,
                                                       argumentsJson );

        if( !execution.m_Ok )
            appendFailedOperation( operationKind, argumentsJson, execution );
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

    if( !decisionSurfaceTargetScopeMatchesRenderedPatchPreviews(
                aStep.m_LlmDecisionJson, aReview.m_RawJson ) )
    {
        appendGateReason( publish.m_GateResult,
                          wxS( "surface_patch_target_scope_failed" ) );
    }

    if( !reviewBasisAllowsPreviewPublish( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult, wxS( "review_basis_failed" ) );

    if( reviewProviderToolResultsBlockPreviewPublish( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult,
                          wxS( "provider_tool_result_failed" ) );

    if( !reviewHiddenMutationRenderFreshnessSatisfied( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult,
                          wxS( "render_freshness_failed" ) );

    if( !reviewRenderHintsSatisfied( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult,
                          wxS( "render_hint_not_satisfied" ) );

    if( !reviewHiddenMutationValidationFreshnessSatisfied( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult,
                          wxS( "validation_freshness_failed" ) );

    if( !reviewValidationHintsSatisfied( aReview.m_RawJson ) )
        appendGateReason( publish.m_GateResult,
                          wxS( "validation_hint_not_satisfied" ) );

    if( !attemptBudgetWithinPolicy( aAttempt ) )
        appendGateReason( publish.m_GateResult, wxS( "budget_policy_failed" ) );

    if( sessionJournalBlocksPreviewPublish( aAttempt.m_JournalJson ) )
        appendGateReason( publish.m_GateResult, wxS( "journal_gate_failed" ) );

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

    std::vector<AI_OBJECT_REF> journalEditObjects =
            editObjectsFromAttemptJournal( aAttempt.m_JournalJson );

    if( !journalEditObjects.empty() )
    {
        suggestion.m_PreviewObjects = journalEditObjects;
        suggestion.m_EditObjects = std::move( journalEditObjects );
    }

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
