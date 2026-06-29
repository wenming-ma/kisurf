/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf_ai_pcb_session_validation_service.h>

#include <board.h>
#include <board_design_settings.h>
#include <board_item.h>
#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <kisurf_ai_pcb_session_apply_adapter.h>
#include <netinfo.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <richio.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <wx/filename.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


nlohmann::json parseObjectJson( const wxString& aText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


std::string stringFieldOr( const nlohmann::json& aJson, const char* aKey,
                           const char* aFallback )
{
    if( aJson.contains( aKey ) && aJson[aKey].is_string() )
        return aJson[aKey].get_ref<const std::string&>();

    return aFallback;
}


std::string severityName( SEVERITY aSeverity )
{
    switch( aSeverity )
    {
    case RPT_SEVERITY_ERROR:     return "error";
    case RPT_SEVERITY_WARNING:   return "warning";
    case RPT_SEVERITY_EXCLUSION: return "exclusion";
    case RPT_SEVERITY_IGNORE:    return "ignore";
    case RPT_SEVERITY_INFO:      return "info";
    case RPT_SEVERITY_ACTION:    return "action";
    case RPT_SEVERITY_DEBUG:     return "debug";
    case RPT_SEVERITY_UNDEFINED:
    default:                     return "undefined";
    }
}


nlohmann::json bboxJson( const BOX2I& aBox )
{
    return {
        { "x", aBox.GetX() },
        { "y", aBox.GetY() },
        { "width", aBox.GetWidth() },
        { "height", aBox.GetHeight() }
    };
}


nlohmann::json worldBoundsJson( const BOX2I& aBox )
{
    return {
        { "left", aBox.GetLeft() },
        { "top", aBox.GetTop() },
        { "right", aBox.GetRight() },
        { "bottom", aBox.GetBottom() }
    };
}


void mergeIssueWorldBounds( std::optional<BOX2I>& aWorldBounds,
                            const BOX2I& aItemBounds )
{
    if( aWorldBounds )
        aWorldBounds->Merge( aItemBounds );
    else
        aWorldBounds = aItemBounds;
}


std::optional<BOX2I> addIssueItemGeometryFacts( const BOARD& aBoard,
                                                const KIID& aItemId,
                                                const char* aPrefix,
                                                nlohmann::json& aIssue )
{
    BOARD_ITEM* item = aBoard.ResolveItem( aItemId, true );

    if( !item )
        return std::nullopt;

    const std::string prefix( aPrefix );
    const BOX2I       itemBounds = item->GetBoundingBox();

    aIssue[prefix + "_item_bbox"] = bboxJson( itemBounds );
    aIssue[prefix + "_item_type"] = static_cast<int>( item->Type() );

    return itemBounds;
}


size_t sessionMutationCount( const AI_EXECUTION_SESSION& aSession )
{
    size_t count = 0;

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RunValidation )
            continue;

        if( operation.IsMutation() )
            ++count;
    }

    return count;
}


void ensureValidationPayloadShape( nlohmann::json& aPayload, const nlohmann::json& aArgs )
{
    if( !aPayload.is_object() )
        aPayload = nlohmann::json::object();

    if( !aPayload.contains( "status" ) )
        aPayload["status"] = "validation_completed";

    if( !aPayload.contains( "validation" ) || !aPayload["validation"].is_object() )
        aPayload["validation"] = nlohmann::json::object();

    nlohmann::json& validation = aPayload["validation"];

    if( !validation.contains( "scope" ) )
        validation["scope"] = stringFieldOr( aArgs, "scope", "session" );

    if( !validation.contains( "level" ) )
        validation["level"] = stringFieldOr( aArgs, "level", "geometry" );

    if( !validation.contains( "issues" ) || !validation["issues"].is_array() )
        validation["issues"] = nlohmann::json::array();

    if( !validation.contains( "warnings" ) || !validation["warnings"].is_array() )
        validation["warnings"] = nlohmann::json::array();
}


void addWarning( nlohmann::json& aPayload, const std::string& aWarning )
{
    aPayload["validation"]["warnings"].push_back( aWarning );
}


void syncMissingNets( const BOARD& aSourceBoard, BOARD& aPreviewBoard )
{
    for( NETINFO_ITEM* net : aSourceBoard.GetNetInfo() )
    {
        if( !net || net->GetNetCode() == NETINFO_LIST::UNCONNECTED
            || net->GetNetname().IsEmpty()
            || aPreviewBoard.FindNet( net->GetNetname() ) )
        {
            continue;
        }

        aPreviewBoard.Add( new NETINFO_ITEM( &aPreviewBoard, net->GetNetname(),
                                             net->GetNetCode() ) );
    }
}


std::unique_ptr<BOARD> cloneBoardThroughKiCadSexpr( BOARD& aBoard )
{
    PCB_IO_KICAD_SEXPR io;
    STRING_FORMATTER   formatter;

    io.FormatBoardToFormatter( &formatter, &aBoard, nullptr );

    STRING_LINE_READER reader( formatter.GetString(),
                               wxS( "KiSurf AI preview validation board" ) );

    std::unique_ptr<BOARD> clone(
            io.DoLoad( reader, nullptr, nullptr, nullptr, 0 ) );

    if( clone )
    {
        clone->SetFileName( aBoard.GetFileName() );
        syncMissingNets( aBoard, *clone );
    }

    return clone;
}


bool replaySessionToPreviewBoard( const AI_EXECUTION_SESSION& aSession,
                                  BOARD& aPreviewBoard, wxString& aError )
{
    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( aPreviewBoard );

    if( !adapter.BeginTransaction( aSession, aError ) )
        return false;

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        if( !operation.IsMutation() )
            continue;

        if( !adapter.ApplyOperation( operation, aError ) )
        {
            adapter.AbortTransaction();
            return false;
        }
    }

    if( !adapter.CommitTransaction( aError ) )
    {
        adapter.AbortTransaction();
        return false;
    }

    return true;
}
} // namespace


KISURF_AI_PCB_SESSION_VALIDATION_SERVICE::KISURF_AI_PCB_SESSION_VALIDATION_SERVICE(
        BOARD& aBoard ) :
        m_BoardProvider( [&aBoard]() { return &aBoard; } )
{
}


KISURF_AI_PCB_SESSION_VALIDATION_SERVICE::KISURF_AI_PCB_SESSION_VALIDATION_SERVICE(
        BOARD_PROVIDER aBoardProvider ) :
        m_BoardProvider( std::move( aBoardProvider ) )
{
}


AI_SESSION_VALIDATION_RESULT KISURF_AI_PCB_SESSION_VALIDATION_SERVICE::RunValidation(
        const AI_EXECUTION_SESSION& aSession, const wxString& aArgumentsJson,
        const wxString& aCurrentResultJson )
{
    const nlohmann::json args = parseObjectJson( aArgumentsJson );
    nlohmann::json payload = parseObjectJson( aCurrentResultJson );
    ensureValidationPayloadShape( payload, args );

    AI_SESSION_VALIDATION_RESULT result;

    const std::string level =
            stringFieldOr( payload["validation"], "level",
                           stringFieldOr( args, "level", "geometry" ).c_str() );

    if( level != "drc_lite" && level != "full_drc" )
    {
        payload["validation"]["native_backend"] = "not_requested";
        result.m_ResultJson = fromJson( payload );
        return result;
    }

    payload["validation"]["native_backend"] = "pcbnew.drc_engine";

    BOARD* board = m_BoardProvider ? m_BoardProvider() : nullptr;

    if( !board )
    {
        payload["validation"]["status"] = "native_unavailable";
        addWarning( payload, "PCB board is not connected to native validation service." );
        result.m_ResultJson = fromJson( payload );
        result.m_Warnings.push_back(
                wxS( "PCB board is not connected to native validation service." ) );
        return result;
    }

    payload["validation"]["warnings"] = nlohmann::json::array();
    nlohmann::json& issues = payload["validation"]["issues"];
    const size_t sessionMutations = sessionMutationCount( aSession );

    std::unique_ptr<BOARD> previewBoard;
    BOARD* validationBoard = board;

    if( sessionMutations > 0 )
    {
        try
        {
            previewBoard = cloneBoardThroughKiCadSexpr( *board );

            if( !previewBoard )
            {
                payload["validation"]["status"] = "preview_board_unavailable";
                payload["validation"]["validated_state"] = "semantic_shadow_board";
                payload["validation"]["preview_state_exact"] = true;
                payload["validation"]["session_mutation_count"] = sessionMutations;
                payload["validation"]["accept_validation_sufficient"] = false;
                payload["validation"]["accept_validation_reason"] =
                        "preview_board_native_drc_failed";
                addWarning( payload,
                            "Preview board could not be reconstructed for native DRC." );
                result.m_Warnings.push_back(
                        wxS( "Preview board could not be reconstructed for native DRC." ) );
                result.m_ResultJson = fromJson( payload );
                return result;
            }

            wxString replayError;

            if( !replaySessionToPreviewBoard( aSession, *previewBoard, replayError ) )
            {
                payload["validation"]["status"] = "preview_replay_failed";
                payload["validation"]["validated_state"] = "preview_board";
                payload["validation"]["preview_state_exact"] = false;
                payload["validation"]["session_mutation_count"] = sessionMutations;
                payload["validation"]["accept_validation_sufficient"] = false;
                payload["validation"]["accept_validation_reason"] =
                        "preview_board_native_drc_failed";

                const std::string warning =
                        "Preview board replay failed before native DRC: "
                        + toUtf8String( replayError );
                addWarning( payload, warning );
                result.m_Warnings.push_back( wxString::FromUTF8( warning.c_str() ) );
                result.m_ResultJson = fromJson( payload );
                return result;
            }

            validationBoard = previewBoard.get();
        }
        catch( const std::exception& e )
        {
            payload["validation"]["status"] = "preview_board_unavailable";
            payload["validation"]["validated_state"] = "semantic_shadow_board";
            payload["validation"]["preview_state_exact"] = false;
            payload["validation"]["session_mutation_count"] = sessionMutations;
            payload["validation"]["accept_validation_sufficient"] = false;
            payload["validation"]["accept_validation_reason"] =
                    "preview_board_native_drc_failed";
            addWarning( payload,
                        std::string( "Preview board native DRC setup failed: " )
                                + e.what() );

            for( const nlohmann::json& warning : payload["validation"]["warnings"] )
            {
                if( warning.is_string() )
                {
                    result.m_Warnings.push_back( wxString::FromUTF8(
                            warning.get_ref<const std::string&>().c_str() ) );
                }
            }

            result.m_ResultJson = fromJson( payload );
            return result;
        }
    }

    payload["validation"]["validated_state"] =
            sessionMutations == 0 ? "live_board" : "preview_board";
    payload["validation"]["preview_state_exact"] = true;
    payload["validation"]["session_mutation_count"] = sessionMutations;
    payload["validation"]["accept_validation_sufficient"] = true;
    payload["validation"]["accept_validation_reason"] =
            "native_drc_matches_preview_state";

    BOARD_DESIGN_SETTINGS& settings = validationBoard->GetDesignSettings();

    if( !settings.m_DRCEngine )
        settings.m_DRCEngine = std::make_shared<DRC_ENGINE>( validationBoard, &settings );

    std::shared_ptr<DRC_ENGINE> drcEngine = settings.m_DRCEngine;
    drcEngine->SetBoard( validationBoard );
    drcEngine->SetDesignSettings( &settings );

    try
    {
        validationBoard->BuildConnectivity();
        drcEngine->InitEngine( wxFileName( validationBoard->GetDesignRulesPath() ) );

        drcEngine->SetViolationHandler(
                [&]( const std::shared_ptr<DRC_ITEM>& aItem, const VECTOR2I& aPos,
                     int aLayer,
                     const std::function<void( PCB_MARKER* )>& aPathGenerator )
                {
                    wxUnusedVar( aPathGenerator );

                    if( !aItem )
                        return;

                    nlohmann::json issue = {
                        { "source", "pcbnew.drc_engine" },
                        { "code", aItem->GetErrorCode() },
                        { "key", toUtf8String( aItem->GetSettingsKey() ) },
                        { "title", toUtf8String( aItem->GetErrorText( false ) ) },
                        { "message", toUtf8String( aItem->GetErrorMessage( false ) ) },
                        { "severity", severityName( settings.GetSeverity(
                                              aItem->GetErrorCode() ) ) },
                        { "position", { { "x", aPos.x }, { "y", aPos.y } } },
                        { "layer", aLayer }
                    };

                    if( aLayer >= 0 && aLayer < PCB_LAYER_ID_COUNT )
                    {
                        issue["layer_name"] = toUtf8String(
                                validationBoard->GetLayerName(
                                        static_cast<PCB_LAYER_ID>( aLayer ) ) );
                    }

                    std::optional<BOX2I> issueWorldBounds;

                    if( aItem->GetMainItemID() != niluuid )
                    {
                        issue["main_item_uuid"] =
                                toUtf8String( aItem->GetMainItemID().AsString() );

                        if( std::optional<BOX2I> itemBounds =
                                    addIssueItemGeometryFacts(
                                            *validationBoard,
                                            aItem->GetMainItemID(), "main",
                                            issue ) )
                        {
                            mergeIssueWorldBounds( issueWorldBounds,
                                                   *itemBounds );
                        }
                    }

                    if( aItem->GetAuxItemID() != niluuid )
                    {
                        issue["aux_item_uuid"] =
                                toUtf8String( aItem->GetAuxItemID().AsString() );

                        if( std::optional<BOX2I> itemBounds =
                                    addIssueItemGeometryFacts(
                                            *validationBoard,
                                            aItem->GetAuxItemID(), "aux",
                                            issue ) )
                        {
                            mergeIssueWorldBounds( issueWorldBounds,
                                                   *itemBounds );
                        }
                    }

                    if( issueWorldBounds )
                    {
                        issue["world_bounds"] = worldBoundsJson( *issueWorldBounds );
                        issue["bbox"] = bboxJson( *issueWorldBounds );
                    }

                    issues.push_back( std::move( issue ) );
                } );

        drcEngine->RunTests( EDA_UNITS::MM, true, false );
        drcEngine->ClearViolationHandler();
        payload["validation"]["status"] = "native_checked";
    }
    catch( const std::exception& e )
    {
        drcEngine->ClearViolationHandler();
        payload["validation"]["status"] = "native_failed";
        payload["validation"]["accept_validation_sufficient"] = false;
        payload["validation"]["accept_validation_reason"] = "native_drc_failed";
        addWarning( payload, std::string( "Native DRC failed: " ) + e.what() );
    }

    payload["validation"]["issue_count"] = issues.size();

    for( const nlohmann::json& warning : payload["validation"]["warnings"] )
    {
        if( warning.is_string() )
        {
            result.m_Warnings.push_back( wxString::FromUTF8(
                    warning.get_ref<const std::string&>().c_str() ) );
        }
    }

    result.m_ResultJson = fromJson( payload );
    return result;
}
