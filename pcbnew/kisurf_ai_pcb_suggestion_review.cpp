#include <kisurf_ai_pcb_suggestion_review.h>

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf_ai_pcb_move_edit_adapter.h>
#include <kisurf_ai_pcb_operation_edit_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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


nlohmann::json parseObject( const wxString& aJsonText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aJsonText ), nullptr, false );

    return parsed.is_object() ? parsed : nlohmann::json::object();
}


wxString jsonText( const nlohmann::json& aJson )
{
    return fromUtf8String( aJson.dump() );
}


std::optional<AI_SESSION_OPERATION_KIND> operationKindFromId(
        const std::string& aKind )
{
    static constexpr std::array<AI_SESSION_OPERATION_KIND, 27> kinds = { {
        AI_SESSION_OPERATION_KIND::Checkpoint,
        AI_SESSION_OPERATION_KIND::RollbackTo,
        AI_SESSION_OPERATION_KIND::QueryBoardSummary,
        AI_SESSION_OPERATION_KIND::QueryItems,
        AI_SESSION_OPERATION_KIND::QueryItem,
        AI_SESSION_OPERATION_KIND::QuerySelection,
        AI_SESSION_OPERATION_KIND::QueryNets,
        AI_SESSION_OPERATION_KIND::QueryLayers,
        AI_SESSION_OPERATION_KIND::QueryDesignRules,
        AI_SESSION_OPERATION_KIND::QueryViewport,
        AI_SESSION_OPERATION_KIND::QueryActivityTimeline,
        AI_SESSION_OPERATION_KIND::RenderPreview,
        AI_SESSION_OPERATION_KIND::ObserveStep,
        AI_SESSION_OPERATION_KIND::CreateVia,
        AI_SESSION_OPERATION_KIND::CreateTrackSegment,
        AI_SESSION_OPERATION_KIND::CreateTrackPolyline,
        AI_SESSION_OPERATION_KIND::CreateZone,
        AI_SESSION_OPERATION_KIND::CreateShape,
        AI_SESSION_OPERATION_KIND::MoveItems,
        AI_SESSION_OPERATION_KIND::DeleteItems,
        AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
        AI_SESSION_OPERATION_KIND::SetItemNet,
        AI_SESSION_OPERATION_KIND::SetItemLayer,
        AI_SESSION_OPERATION_KIND::SetItemProperties,
        AI_SESSION_OPERATION_KIND::SetMetadata,
        AI_SESSION_OPERATION_KIND::RefillZones,
        AI_SESSION_OPERATION_KIND::RebuildConnectivity
    } };

    for( AI_SESSION_OPERATION_KIND kind : kinds )
    {
        if( toUtf8String( AiSessionOperationKindId( kind ) ) == aKind )
            return kind;
    }

    if( toUtf8String( AiSessionOperationKindId(
                AI_SESSION_OPERATION_KIND::RunValidation ) ) == aKind )
    {
        return AI_SESSION_OPERATION_KIND::RunValidation;
    }

    return std::nullopt;
}


std::vector<wxString> warningsFromJson( const nlohmann::json& aWarnings )
{
    std::vector<wxString> warnings;

    if( !aWarnings.is_array() )
        return warnings;

    for( const nlohmann::json& warning : aWarnings )
    {
        if( warning.is_string() )
            warnings.push_back( fromUtf8String( warning.get<std::string>() ) );
    }

    return warnings;
}


std::optional<AI_SESSION_HANDLE> handleFromJson( const nlohmann::json& aHandle )
{
    if( !aHandle.is_object() )
        return std::nullopt;

    AI_SESSION_HANDLE handle;
    handle.m_SessionId = aHandle.value( "session_id", uint64_t( 0 ) );
    handle.m_HandleId = aHandle.value( "handle_id", uint64_t( 0 ) );
    handle.m_Generation = aHandle.value( "generation", uint64_t( 0 ) );

    if( aHandle.contains( "alias" ) && aHandle["alias"].is_string() )
        handle.m_Alias = fromUtf8String( aHandle["alias"].get<std::string>() );

    if( !handle.IsValid() )
        return std::nullopt;

    return handle;
}


std::vector<AI_SESSION_HANDLE> handlesFromJson( const nlohmann::json& aHandles )
{
    std::vector<AI_SESSION_HANDLE> handles;

    if( !aHandles.is_array() )
        return handles;

    for( const nlohmann::json& handleJson : aHandles )
    {
        std::optional<AI_SESSION_HANDLE> handle = handleFromJson( handleJson );
        if( handle )
            handles.push_back( *handle );
    }

    return handles;
}


wxString jsonValueText( const nlohmann::json& aValue,
                        const wxString& aDefault = wxS( "{}" ) )
{
    if( aValue.is_discarded() || aValue.is_null() )
        return aDefault;

    if( aValue.is_string() )
        return fromUtf8String( aValue.get<std::string>() );

    return jsonText( aValue );
}


std::vector<wxString> stringArrayFromJson( const nlohmann::json& aValues )
{
    std::vector<wxString> values;

    if( !aValues.is_array() )
        return values;

    for( const nlohmann::json& value : aValues )
    {
        if( value.is_string() )
            values.push_back( fromUtf8String( value.get<std::string>() ) );
    }

    return values;
}


std::map<wxString, wxString> metadataFromJson( const nlohmann::json& aMetadata )
{
    std::map<wxString, wxString> metadata;

    if( !aMetadata.is_object() )
        return metadata;

    for( auto it = aMetadata.begin(); it != aMetadata.end(); ++it )
    {
        metadata[fromUtf8String( it.key() )] =
                it.value().is_string()
                        ? fromUtf8String( it.value().get<std::string>() )
                        : jsonText( it.value() );
    }

    return metadata;
}


std::optional<AI_SHADOW_ITEM> shadowItemFromJson(
        const nlohmann::json& aItem, wxString& aError )
{
    if( !aItem.is_object() )
    {
        aError = wxS( "Next Action session journal contains an invalid shadow item." );
        return std::nullopt;
    }

    std::optional<AI_SESSION_HANDLE> handle =
            handleFromJson( aItem.value( "handle", nlohmann::json::object() ) );

    if( !handle )
    {
        aError = wxS( "Next Action session journal contains a shadow item without a valid handle." );
        return std::nullopt;
    }

    AI_SHADOW_ITEM item;
    item.m_Handle = *handle;

    if( aItem.contains( "created_by" ) && aItem["created_by"].is_string() )
    {
        std::optional<AI_SESSION_OPERATION_KIND> createdBy =
                operationKindFromId( aItem["created_by"].get<std::string>() );

        if( createdBy )
            item.m_CreatedBy = *createdBy;
    }

    if( aItem.contains( "type" ) && aItem["type"].is_string() )
        item.m_Type = fromUtf8String( aItem["type"].get<std::string>() );

    if( aItem.contains( "alias" ) && aItem["alias"].is_string() )
        item.m_Alias = fromUtf8String( aItem["alias"].get<std::string>() );

    if( aItem.contains( "net" ) && aItem["net"].is_string() )
        item.m_Net = fromUtf8String( aItem["net"].get<std::string>() );

    if( aItem.contains( "layer" ) && aItem["layer"].is_string() )
        item.m_Layer = fromUtf8String( aItem["layer"].get<std::string>() );

    item.m_Layers = stringArrayFromJson( aItem.value( "layers", nlohmann::json::array() ) );
    item.m_GeometryJson = jsonValueText( aItem.value( "geometry", nlohmann::json::object() ) );
    item.m_PropertiesJson = jsonValueText( aItem.value( "properties", nlohmann::json::object() ) );
    item.m_Metadata = metadataFromJson( aItem.value( "metadata", nlohmann::json::object() ) );
    item.m_CreatedEpoch = aItem.value( "created_epoch", uint64_t( 0 ) );
    item.m_UpdatedEpoch = aItem.value( "updated_epoch", uint64_t( 0 ) );
    item.m_Deleted = aItem.value( "deleted", false );
    return item;
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
    nlohmann::json provenance = parseObject( aSuggestion.m_RuntimeProvenanceJson );

    if( provenance.value( "runtime", std::string() ) != "next_action" )
        return true;

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

    return jsonText( fingerprint );
}


bool runtimeAcceptTokenMatchesDependency(
        const AI_SUGGESTION_RECORD& aSuggestion,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    nlohmann::json provenance = parseObject( aSuggestion.m_RuntimeProvenanceJson );

    if( !provenance.contains( "accept_token" )
        || !provenance["accept_token"].is_object() )
    {
        return false;
    }

    return provenance["accept_token"].value( "dependency_fingerprint",
                                             std::string() )
           == toUtf8String( dependencyFingerprint( aCurrentContextVersion ) );
}


bool sameContextVersion( const AI_CONTEXT_VERSION& aLeft,
                         const AI_CONTEXT_VERSION& aRight )
{
    return aLeft.m_DocumentRevision == aRight.m_DocumentRevision
           && aLeft.m_SelectionRevision == aRight.m_SelectionRevision
           && aLeft.m_ViewRevision == aRight.m_ViewRevision;
}


bool hasNextActionSessionJournal( const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObject( aSuggestion.m_RuntimeProvenanceJson );

    return provenance.value( "runtime", std::string() ) == "next_action"
           && provenance.contains( "attempt" )
           && provenance["attempt"].is_object()
           && provenance["attempt"].contains( "session_journal" )
           && provenance["attempt"]["session_journal"].is_object();
}


bool isNextActionRuntimeSuggestion( const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json provenance = parseObject( aSuggestion.m_RuntimeProvenanceJson );
    return provenance.value( "runtime", std::string() ) == "next_action";
}


std::unique_ptr<AI_EXECUTION_SESSION> replaySessionFromProvenance(
        const AI_SUGGESTION_RECORD& aSuggestion,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion,
        wxString& aError )
{
    nlohmann::json provenance = parseObject( aSuggestion.m_RuntimeProvenanceJson );

    if( provenance.value( "runtime", std::string() ) != "next_action"
        || !provenance.contains( "attempt" )
        || !provenance["attempt"].is_object()
        || !provenance["attempt"].contains( "session_journal" )
        || !provenance["attempt"]["session_journal"].is_object() )
    {
        aError = wxS( "Next Action suggestion does not contain a session journal." );
        return nullptr;
    }

    const nlohmann::json& journal = provenance["attempt"]["session_journal"];

    if( !journal.contains( "operations" ) || !journal["operations"].is_array() )
    {
        aError = wxS( "Next Action session journal has no operation list." );
        return nullptr;
    }

    wxString baseHash;

    if( journal.contains( "base_hash" ) && journal["base_hash"].is_string() )
        baseHash = fromUtf8String( journal["base_hash"].get<std::string>() );

    if( baseHash.IsEmpty() )
    {
        aError = wxS( "Next Action session journal has no base hash." );
        return nullptr;
    }

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = journal.value( "session_id", uint64_t( 1 ) );
    options.m_BoardId = wxS( "next-action-replay" );

    if( journal.contains( "board_id" ) && journal["board_id"].is_string() )
        options.m_BoardId = fromUtf8String( journal["board_id"].get<std::string>() );
    options.m_BaseHash = baseHash;
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion = aCurrentContextVersion.m_ContextVersion;

    auto session = std::make_unique<AI_EXECUTION_SESSION>( options );

    if( journal.contains( "shadow_items" ) )
    {
        if( !journal["shadow_items"].is_array() )
        {
            aError = wxS( "Next Action session journal shadow item list is invalid." );
            return nullptr;
        }

        for( const nlohmann::json& shadowItemJson : journal["shadow_items"] )
        {
            std::optional<AI_SHADOW_ITEM> shadowItem =
                    shadowItemFromJson( shadowItemJson, aError );

            if( !shadowItem )
                return nullptr;

            session->ShadowBoard().UpsertItem( std::move( *shadowItem ) );
        }
    }

    for( const nlohmann::json& operationJson : journal["operations"] )
    {
        if( !operationJson.is_object()
            || !operationJson.contains( "kind" )
            || !operationJson["kind"].is_string() )
        {
            aError = wxS( "Next Action session journal contains an invalid operation." );
            return nullptr;
        }

        std::optional<AI_SESSION_OPERATION_KIND> kind =
                operationKindFromId( operationJson["kind"].get<std::string>() );

        if( !kind )
        {
            aError = wxString::Format(
                    wxS( "Next Action session journal contains unsupported operation '%s'." ),
                    fromUtf8String( operationJson["kind"].get<std::string>() ) );
            return nullptr;
        }

        AI_SESSION_OPERATION_RECORD operation;
        operation.m_Kind = *kind;
        operation.m_ArgumentsJson = wxS( "{}" );
        operation.m_ResultJson = wxS( "{}" );

        if( operationJson.contains( "arguments" ) )
            operation.m_ArgumentsJson = jsonText( operationJson["arguments"] );

        if( operationJson.contains( "result" ) )
            operation.m_ResultJson = jsonText( operationJson["result"] );
        operation.m_ResolvedHandles =
                handlesFromJson( operationJson.value(
                        "resolved_handles", nlohmann::json::array() ) );
        operation.m_CreatedHandles =
                handlesFromJson( operationJson.value(
                        "created_handles", nlohmann::json::array() ) );
        operation.m_Warnings =
                warningsFromJson( operationJson.value(
                        "warnings", nlohmann::json::array() ) );

        session->AppendOperation( std::move( operation ) );
    }

    return session;
}


wxString currentBaseHashForAccept(
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    if( !aCurrentContextVersion.m_BoardBaseHash.IsEmpty() )
        return aCurrentContextVersion.m_BoardBaseHash;

    return aCurrentContextVersion.m_ContextVersion.AsString();
}


bool runAcceptGateValidation( AI_EXECUTION_SESSION& aSession,
                              AI_SESSION_VALIDATION_SERVICE& aValidationService,
                              wxString& aError )
{
    const wxString validationArgs =
            wxS( "{\"scope\":\"session\",\"level\":\"full_drc\","
                 "\"gate\":\"accept\"}" );

    const uint64_t stepId = aSession.BeginStep( wxS( "next action accept gate validation" ) );

    if( stepId == 0 )
    {
        aError = wxS( "Accept validation could not open a session step." );
        return false;
    }

    AI_ATOMIC_EXECUTION_RESULT commonValidation =
            AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                    aSession, AI_SESSION_OPERATION_KIND::RunValidation,
                    validationArgs );

    if( !commonValidation.m_Ok )
    {
        aError = commonValidation.m_Message;

        if( aError.IsEmpty() )
            aError = wxS( "Accept validation could not be recorded." );

        aSession.FailStep( stepId, aError );
        return false;
    }

    AI_SESSION_VALIDATION_RESULT nativeValidation =
            aValidationService.RunValidation( aSession, validationArgs,
                                              commonValidation.m_ResultJson );

    if( !commonValidation.m_OperationIds.empty() )
    {
        aSession.UpdateOperationResult( commonValidation.m_OperationIds.back(),
                                        nativeValidation.m_ResultJson,
                                        nativeValidation.m_Warnings );
    }

    aSession.EndStep( stepId );

    if( !nativeValidation.m_Ok )
    {
        aError = nativeValidation.m_Message;

        if( aError.IsEmpty() )
            aError = wxS( "Accept validation service failed." );

        return false;
    }

    nlohmann::json validation =
            nlohmann::json::parse( toUtf8String( nativeValidation.m_ResultJson ),
                                   nullptr, false );

    if( validation.is_discarded() || !validationAcceptGradeSufficient( validation ) )
    {
        aError = wxS( "Accept validation did not reach accept grade." );
        return false;
    }

    return true;
}
} // namespace

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            AI_ACCEPT_APPLY_ADAPTER& aSessionApplyAdapter,
                            AI_SESSION_VALIDATION_SERVICE& aValidationService,
                            const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            aModel.FindSuggestion( aSuggestionId );

    if( !suggestion || !hasNextActionSessionJournal( *suggestion ) )
        return false;

    if( !aModel.CanAcceptSuggestion( aSuggestionId )
        || !sameContextVersion( suggestion->m_ContextVersion,
                                aCurrentContextVersion.m_ContextVersion )
        || !runtimeAcceptTokenMatchesDependency( *suggestion,
                                                 aCurrentContextVersion )
        || !runtimeAttemptAcceptValidationSufficient( *suggestion ) )
    {
        aModel.ExpireSuggestion( aSuggestionId );
        return false;
    }

    wxString error;
    std::unique_ptr<AI_EXECUTION_SESSION> session =
            replaySessionFromProvenance( *suggestion, aCurrentContextVersion,
                                         error );

    if( !session )
        return false;

    if( !runAcceptGateValidation( *session, aValidationService, error ) )
    {
        aModel.ExpireSuggestion( aSuggestionId );
        return false;
    }

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            *session, currentBaseHashForAccept( aCurrentContextVersion ),
            aCurrentContextVersion.m_ContextVersion, aSessionApplyAdapter );

    if( !result.m_Ok )
    {
        aModel.ExpireSuggestion( aSuggestionId );
        return false;
    }

    return aModel.MarkSuggestionAccepted( aSuggestionId );
}

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                            COMMIT& aCommit,
                            const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            aModel.FindSuggestion( aSuggestionId );

    if( !suggestion )
        return false;

    if( isNextActionRuntimeSuggestion( *suggestion ) )
        return false;

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    if( !operation )
        return false;

    if( operation->IsMove() || operation->IsMoveSelected() )
    {
        KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( aResolver, aCommit,
                                                 operation->m_MoveDelta );
        AI_EDIT_SESSION                 session( adapter );
        return aModel.AcceptSuggestion( aSuggestionId, session,
                                        aCurrentContextVersion );
    }

    if( operation->IsRouteSegmentPreview() || operation->IsPlaceViaPreview()
        || operation->IsCreateShapePreview()
        || operation->IsCreateCopperZonePreview() )
    {
        KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( aResolver, aCommit );
        AI_EDIT_SESSION                      session( adapter );
        return aModel.AcceptSuggestion( aSuggestionId, session,
                                        aCurrentContextVersion );
    }

    return false;
}
