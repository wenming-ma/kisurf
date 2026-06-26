#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_python_local_worker.h>
#include <kisurf/ai/ai_python_worker.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_shadow_board.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <utility>
#include <vector>
#include <wx/filefn.h>
#include <wx/filename.h>

namespace
{
AI_PROVIDER_REQUEST requestWithContext()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 71;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextVersion.m_DocumentRevision = 10;
    request.m_ContextVersion.m_SelectionRevision = 2;
    request.m_ContextVersion.m_ViewRevision = 4;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;
    return request;
}


AI_PROVIDER_REQUEST requestWithRichContext()
{
    AI_PROVIDER_REQUEST request = requestWithContext();
    request.m_ContextSnapshot.m_Summary = wxS( "PCB canvas has an active route preview." );
    request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ),
                           wxS( "{\"net\":\"GND\",\"layer\":\"F.Cu\"}" ) ) );
    request.m_ContextSnapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_VIA_T, wxS( "via:/GND" ),
                           wxS( "{\"net\":\"GND\",\"layers\":[\"F.Cu\",\"B.Cu\"]}" ) ) );

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 42;
    activity.m_Kind = AI_ACTIVITY_KIND::UserAction;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "pcbnew.InteractiveRoute" );
    activity.m_Message = wxS( "User started dragging a route." );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    request.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    request.m_ContextSnapshot.m_ToolState.m_ContextVersion = request.m_ContextVersion;
    request.m_ContextSnapshot.m_ToolState.m_ActiveActionName =
            wxS( "pcbnew.InteractiveRoute" );
    request.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    request.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 400, 800 );
    request.m_ContextSnapshot.m_ToolState.m_SharedContextJson =
            wxS( "{\"viewport\":{\"center\":{\"x\":1000,\"y\":2000},\"zoom\":3.5},"
                 "\"design_rules\":{\"clearance_min\":150000}}" );

    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 1280;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 720;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 2048;
    return request;
}


bool schemaRequiresXY( const nlohmann::json& aSchema )
{
    return aSchema.is_object() && aSchema.contains( "required" )
           && aSchema["required"].is_array()
           && std::find( aSchema["required"].begin(), aSchema["required"].end(),
                         "x" ) != aSchema["required"].end()
           && std::find( aSchema["required"].begin(), aSchema["required"].end(),
                         "y" ) != aSchema["required"].end();
}


bool boxSchemaSupportsCanonicalForms( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
        || !aSchema["anyOf"].is_array() )
    {
        return false;
    }

    bool sawOriginSizeBox = false;
    bool sawMinMaxBox = false;

    for( const nlohmann::json& variant : aSchema["anyOf"] )
    {
        if( !variant.is_object() || !variant.contains( "properties" )
            || !variant["properties"].is_object() )
        {
            continue;
        }

        const nlohmann::json& properties = variant["properties"];

        sawOriginSizeBox = sawOriginSizeBox
                           || ( properties.contains( "x" )
                                && properties.contains( "y" )
                                && properties.contains( "width" )
                                && properties.contains( "height" ) );

        sawMinMaxBox = sawMinMaxBox
                       || ( properties.contains( "min" )
                            && properties.contains( "max" )
                            && schemaRequiresXY( properties["min"] )
                            && schemaRequiresXY( properties["max"] ) );
    }

    return sawOriginSizeBox && sawMinMaxBox;
}


bool stringEnumContainsAll( const nlohmann::json& aSchema,
                            std::initializer_list<const char*> aValues )
{
    if( !aSchema.is_object() || aSchema.value( "type", std::string() ) != "string"
        || !aSchema.contains( "enum" ) || !aSchema["enum"].is_array() )
    {
        return false;
    }

    for( const char* value : aValues )
    {
        if( std::find( aSchema["enum"].begin(), aSchema["enum"].end(), value )
            == aSchema["enum"].end() )
        {
            return false;
        }
    }

    return true;
}


bool queryFilterSchemaSupportsShadowFilters( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || aSchema.value( "type", std::string() ) != "object"
        || aSchema.value( "additionalProperties", true ) != false
        || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "type" )
           && properties["type"].value( "type", std::string() ) == "string"
           && properties.contains( "net" )
           && properties["net"].value( "type", std::string() ) == "string"
           && properties.contains( "layer" )
           && properties["layer"].value( "type", std::string() ) == "string"
           && properties.contains( "alias" )
           && properties["alias"].value( "type", std::string() ) == "string"
           && properties.contains( "selection" )
           && properties["selection"].value( "type", std::string() ) == "boolean"
           && properties.contains( "bbox" )
           && boxSchemaSupportsCanonicalForms( properties["bbox"] )
           && properties.contains( "handle" )
           && properties["handle"].contains( "anyOf" );
}


bool queryHandleSchemaSupportsTypedReferences( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
        || !aSchema["anyOf"].is_array() )
    {
        return false;
    }

    bool sawAlias = false;
    bool sawHandleId = false;
    bool sawHandleObject = false;

    for( const nlohmann::json& variant : aSchema["anyOf"] )
    {
        if( !variant.is_object() )
            continue;

        const std::string type = variant.value( "type", std::string() );

        if( type == "string" )
            sawAlias = true;

        if( type == "integer" )
            sawHandleId = true;

        if( type == "object" && variant.value( "additionalProperties", true ) == false
            && variant.contains( "properties" )
            && variant["properties"].contains( "handle_id" )
            && variant["properties"].contains( "generation" )
            && variant["properties"].contains( "alias" )
            && variant.contains( "required" )
            && std::find( variant["required"].begin(), variant["required"].end(),
                          "handle_id" ) != variant["required"].end() )
        {
            sawHandleObject = true;
        }
    }

    return sawAlias && sawHandleId && sawHandleObject;
}


bool handleArraySchemaSupportsTypedReferences( const nlohmann::json& aSchema )
{
    return aSchema.is_object() && aSchema.value( "type", std::string() ) == "array"
           && aSchema.contains( "items" )
           && queryHandleSchemaSupportsTypedReferences( aSchema["items"] );
}


bool renderPreviewSchemaDeclaresTypedObservationArgs( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "region" )
           && boxSchemaSupportsCanonicalForms( properties["region"] )
           && properties.contains( "layer_mask" )
           && properties["layer_mask"].value( "type", std::string() ) == "array"
           && properties["layer_mask"].contains( "items" )
           && properties["layer_mask"]["items"].value( "type", std::string() ) == "string"
           && properties.contains( "view_mode" )
           && properties["view_mode"].value( "type", std::string() ) == "string";
}


bool validationSchemaDeclaresTypedObservationArgs( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "scope" )
           && stringEnumContainsAll( properties["scope"],
                                     { "session", "affected_area", "selection",
                                       "region" } )
           && properties.contains( "level" )
           && stringEnumContainsAll( properties["level"],
                                     { "geometry", "drc_lite", "full_drc" } )
           && properties.contains( "region" )
           && boxSchemaSupportsCanonicalForms( properties["region"] )
           && properties.contains( "handles" )
           && handleArraySchemaSupportsTypedReferences( properties["handles"] )
           && properties.contains( "gate" )
           && stringEnumContainsAll( properties["gate"],
                                     { "preview", "accept" } );
}


AI_TOOL_CALL_RECORD toolCall( const wxString& aToolName, const wxString& aArguments )
{
    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 71;
    call.m_ToolCallId = wxS( "call_session" );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = aArguments;
    return call;
}


wxString pythonSdkRootPath()
{
    wxString sdkRoot = AI_PYTHON_LOCAL_WORKER::DefaultSdkRootPath();
    wxFileName workerPy( sdkRoot, wxS( "worker.py" ) );
    workerPy.PrependDir( wxS( "kisurf_ai" ) );

    if( !sdkRoot.IsEmpty() && workerPy.FileExists() )
        return sdkRoot;

    wxFileName fallback( wxGetCwd(), wxEmptyString );
    fallback.AppendDir( wxS( "common" ) );
    fallback.AppendDir( wxS( "kisurf" ) );
    fallback.AppendDir( wxS( "ai" ) );
    fallback.AppendDir( wxS( "python" ) );
    return fallback.GetPath();
}


wxString jsonText( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


class SCRIPTED_PYTHON_WORKER : public AI_PYTHON_WORKER
{
public:
    explicit SCRIPTED_PYTHON_WORKER( AI_PYTHON_CELL_RESULT aResult ) :
            m_Result( std::move( aResult ) )
    {
    }

    bool IsConnected() const override { return true; }

    void SetEventSink( AI_PYTHON_EVENT_SINK* aSink ) override
    {
        m_EventSink = aSink;
    }

    AI_PYTHON_CELL_RESULT RunCell( const AI_EXECUTION_SESSION& aSession,
                                   const AI_PYTHON_CELL_REQUEST& aRequest ) override
    {
        ++m_RunCount;
        m_LastSessionId = aSession.SessionId();
        m_LastCellId = aRequest.m_CellId;
        m_LastCellText = aRequest.m_CellText;

        if( m_EventSink )
        {
            for( const AI_PYTHON_EVENT& event : m_StreamEvents )
                m_EventSink->OnPythonEvent( event );
        }

        AI_PYTHON_CELL_RESULT result = m_Result;

        if( !result.m_HasSessionContext )
        {
            result.m_HasSessionContext = true;
            result.m_SessionId = aRequest.m_SessionId;
            result.m_BoardId = aRequest.m_BoardId;
            result.m_BaseHash = aRequest.m_BaseHash;
            result.m_Epoch = aRequest.m_Epoch;
        }

        return result;
    }

    void Cancel() override { ++m_CancelCount; }
    void HardKill() override { ++m_HardKillCount; }

    AI_PYTHON_CELL_RESULT m_Result;
    int                   m_RunCount = 0;
    int                   m_CancelCount = 0;
    int                   m_HardKillCount = 0;
    uint64_t              m_LastSessionId = 0;
    wxString              m_LastCellId;
    wxString              m_LastCellText;
    AI_PYTHON_EVENT_SINK* m_EventSink = nullptr;
    std::vector<AI_PYTHON_EVENT> m_StreamEvents;
};


class SCRIPTED_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit SCRIPTED_NEXT_ACTION_PROVIDER( std::deque<wxString> aBodies ) :
            m_Bodies( std::move( aBodies ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;

        if( m_Bodies.empty() )
            response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        else
        {
            response.m_Body = m_Bodies.front();
            m_Bodies.pop_front();
        }

        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    std::deque<wxString>             m_Bodies;
};


AI_OBJECT_REF nextActionViaRef( int aX, int aY )
{
    return AI_OBJECT_REF(
            KIID(), PCB_VIA_T,
            wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
            wxString::Format(
                    wxS( "{\"kind\":\"via\",\"position\":{\"x\":%d,\"y\":%d},"
                         "\"diameter\":600000,\"net_name\":\"GND\"}" ),
                    aX, aY ) );
}


AI_SUGGESTION_TRIGGER nextActionViaTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 12;
    trigger.m_ContextVersion.m_ViewRevision = 5;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( nextActionViaRef( 100, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( nextActionViaRef( 200, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( nextActionViaRef( 300, 50 ) );
    trigger.m_Activity.m_Sequence = 44;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.placeVia" );
    trigger.m_Reason = wxS( "cursor paused" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


class QUEUED_PYTHON_WORKER : public AI_PYTHON_WORKER
{
public:
    explicit QUEUED_PYTHON_WORKER( std::vector<AI_PYTHON_CELL_RESULT> aResults ) :
            m_Results( std::move( aResults ) )
    {
    }

    bool IsConnected() const override { return true; }

    AI_PYTHON_CELL_RESULT RunCell( const AI_EXECUTION_SESSION& aSession,
                                   const AI_PYTHON_CELL_REQUEST& aRequest ) override
    {
        ++m_RunCount;

        AI_PYTHON_CELL_RESULT result =
                m_RunCount <= m_Results.size() ? m_Results[m_RunCount - 1]
                                               : AI_PYTHON_CELL_RESULT();

        if( !result.m_HasSessionContext )
        {
            result.m_HasSessionContext = true;
            result.m_SessionId = aSession.SessionId();
            result.m_BoardId = aRequest.m_BoardId;
            result.m_BaseHash = aRequest.m_BaseHash;
            result.m_Epoch = aRequest.m_Epoch;
        }

        return result;
    }

    void Cancel() override {}
    void HardKill() override {}

    std::vector<AI_PYTHON_CELL_RESULT> m_Results;
    size_t                             m_RunCount = 0;
};


class RECORDING_SESSION_ACCEPT_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override
    {
        wxUnusedVar( aError );
        ++m_BeginCount;
        m_SessionId = aSession.SessionId();
        return true;
    }

    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override
    {
        wxUnusedVar( aError );
        m_OperationIds.push_back( aOperation.m_Id );
        m_OperationKinds.push_back( aOperation.m_Kind );
        return true;
    }

    bool CommitTransaction( wxString& aError ) override
    {
        wxUnusedVar( aError );
        ++m_CommitCount;
        return true;
    }

    bool HasBoardChanges() const override { return m_HasBoardChanges; }

    uint64_t m_SessionId = 0;
    int      m_BeginCount = 0;
    int      m_CommitCount = 0;
    bool     m_HasBoardChanges = true;
    std::vector<uint64_t> m_OperationIds;
    std::vector<AI_SESSION_OPERATION_KIND> m_OperationKinds;
};


class RECORDING_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) override
    {
        ++m_RenderCount;
        m_LastSessionId = aSession.SessionId();
        m_LastArgumentsJson = aArgumentsJson;
        m_RenderedItemCounts.push_back( aSession.ShadowBoard().LiveItemCount() );

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = true;
        result.m_PreviewId = 77;
        result.m_RenderedItemCount = aSession.ShadowBoard().LiveItemCount();
        result.m_ResultJson = wxS(
                "{\"status\":\"preview_rendered\",\"preview_id\":77,"
                "\"native_preview\":true}" );
        return result;
    }

    void ClearPreview( uint64_t aSessionId ) override
    {
        ++m_ClearCount;
        m_LastClearedSessionId = aSessionId;
    }

    int      m_RenderCount = 0;
    int      m_ClearCount = 0;
    uint64_t m_LastSessionId = 0;
    uint64_t m_LastClearedSessionId = 0;
    wxString m_LastArgumentsJson;
    std::vector<size_t> m_RenderedItemCounts;
};


class BLOCKING_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) override
    {
        ++m_RenderCount;
        m_LastSessionId = aSession.SessionId();
        m_LastArgumentsJson = aArgumentsJson;

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "render_failed" );
        result.m_Message = wxS( "Native preview renderer failed." );
        result.m_ResultJson =
                wxS( "{\"status\":\"render_failed\",\"render_valid\":false,"
                     "\"reason\":\"native preview renderer failed\"}" );
        return result;
    }

    void ClearPreview( uint64_t aSessionId ) override
    {
        ++m_ClearCount;
        m_LastClearedSessionId = aSessionId;
    }

    int      m_RenderCount = 0;
    int      m_ClearCount = 0;
    uint64_t m_LastSessionId = 0;
    uint64_t m_LastClearedSessionId = 0;
    wxString m_LastArgumentsJson;
};


class RECORDING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson,
            const wxString& aCurrentResultJson ) override
    {
        ++m_RunCount;
        m_LastSessionId = aSession.SessionId();
        m_LastArgumentsJson = aArgumentsJson;
        m_LastCurrentResultJson = aCurrentResultJson;

        nlohmann::json payload =
                nlohmann::json::parse( aCurrentResultJson.ToStdString() );
        payload["validation"]["native_backend"] = "recording";
        payload["validation"]["status"] = "native_checked";
        payload["validation"]["issue_count"] = 1;
        payload["validation"]["issues"] = nlohmann::json::array(
                { { { "code", "recording_native_issue" },
                    { "severity", "warning" },
                    { "message", "Native validation service ran." } } } );
        payload["validation"]["warnings"] = nlohmann::json::array(
                { "recording native validation warning" } );

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = true;
        result.m_Message = wxS( "Native validation service ran." );
        result.m_ResultJson = jsonText( payload );
        result.m_Warnings.push_back( wxS( "recording native validation warning" ) );
        return result;
    }

    int      m_RunCount = 0;
    uint64_t m_LastSessionId = 0;
    wxString m_LastArgumentsJson;
    wxString m_LastCurrentResultJson;
};


class BLOCKING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION&,
            const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "preview_gate_blocked" );
        result.m_Message = wxS( "Blocking native validation issue." );
        result.m_ResultJson =
                wxS( "{\"validation\":{\"status\":\"blocked\","
                     "\"issue_count\":1,\"issues\":[{\"severity\":\"error\","
                     "\"code\":\"drc_overlap\",\"message\":\"Overlap.\"}]}}" );
        result.m_Warnings.push_back( wxS( "blocking native validation warning" ) );
        return result;
    }

    int m_RunCount = 0;
};
} // namespace


BOOST_AUTO_TEST_SUITE( AiSessionToolCallHandler )


BOOST_AUTO_TEST_CASE( SessionToolCatalogDeclaresLayeredAtomicScriptContract )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;
    const nlohmann::json catalog =
            nlohmann::json::parse( handler.ToolCatalogJson().ToStdString() );

    BOOST_REQUIRE( catalog.is_array() );

    auto catalogTool =
            [&]( const std::string& aName ) -> const nlohmann::json*
            {
                for( const nlohmann::json& tool : catalog )
                {
                    if( tool.is_object()
                        && tool.value( "name", std::string() ) == aName )
                    {
                        return &tool;
                    }
                }

                return nullptr;
            };

    BOOST_REQUIRE( catalogTool( "kisurf_run_cell" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_run_cell" )->value( "layer", std::string() ),
                       "script" );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_run_cell" )->value( "side_effect",
                                                                std::string() ),
                       "shadow_mutation" );
    BOOST_CHECK( !catalogTool( "kisurf_run_cell" )->value( "raw_board_access", true ) );
    BOOST_CHECK( !catalogTool( "kisurf_run_cell" )->value( "direct_publish", true ) );
    BOOST_REQUIRE( catalogTool( "kisurf_run_cell" )->contains( "lowers_to_atomic_ops" ) );

    const nlohmann::json& atomicOps =
            ( *catalogTool( "kisurf_run_cell" ) )["lowers_to_atomic_ops"];
    BOOST_REQUIRE( atomicOps.is_array() );

    for( const std::string& opName :
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
           "surface.apply_patch" } )
    {
        BOOST_CHECK( std::find( atomicOps.begin(), atomicOps.end(), opName )
                     != atomicOps.end() );
    }

    BOOST_REQUIRE( catalogTool( "kisurf_run_atomic_operation" ) );
    BOOST_REQUIRE( catalogTool( "kisurf_run_atomic_operation" )
                           ->contains( "operation_contracts" ) );
    const nlohmann::json& operationContracts =
            ( *catalogTool( "kisurf_run_atomic_operation" ) )["operation_contracts"];
    BOOST_REQUIRE( operationContracts.contains( "pcb.create_via" ) );
    BOOST_REQUIRE( operationContracts.contains( "pcb.create_track_segment" ) );
    BOOST_REQUIRE( operationContracts.contains( "pcb.move_items" ) );
    BOOST_REQUIRE( operationContracts.contains( "surface.apply_patch" ) );
    BOOST_CHECK( operationContracts["pcb.create_via"]["required"].dump().find(
                         "position" ) != std::string::npos );
    BOOST_CHECK( operationContracts["pcb.create_track_segment"]["properties"]
                         .contains( "width" ) );
    BOOST_CHECK( operationContracts["pcb.move_items"]["properties"].contains(
            "target_positions" ) );
    BOOST_REQUIRE( operationContracts.contains( "pcb.create_zone" ) );
    BOOST_REQUIRE( operationContracts["pcb.create_zone"]["properties"].contains(
            "outline" ) );
    const nlohmann::json& zoneOutlineContract =
            operationContracts["pcb.create_zone"]["properties"]["outline"];
    BOOST_REQUIRE( zoneOutlineContract.contains( "properties" ) );
    BOOST_REQUIRE( zoneOutlineContract["properties"].contains( "points" ) );
    const nlohmann::json& zoneOutlinePointsContract =
            zoneOutlineContract["properties"]["points"];
    BOOST_CHECK_EQUAL( zoneOutlinePointsContract.value( "minItems", 0 ), 3 );
    BOOST_REQUIRE( zoneOutlinePointsContract.contains( "items" ) );
    BOOST_REQUIRE( zoneOutlinePointsContract["items"].contains( "required" ) );
    BOOST_CHECK( std::find( zoneOutlinePointsContract["items"]["required"].begin(),
                            zoneOutlinePointsContract["items"]["required"].end(),
                            "x" )
                 != zoneOutlinePointsContract["items"]["required"].end() );
    BOOST_CHECK( std::find( zoneOutlinePointsContract["items"]["required"].begin(),
                            zoneOutlinePointsContract["items"]["required"].end(),
                            "y" )
                 != zoneOutlinePointsContract["items"]["required"].end() );
    BOOST_CHECK( operationContracts["surface.apply_patch"]["required"].dump().find(
                         "surface_id" ) != std::string::npos );
    BOOST_CHECK( operationContracts["surface.apply_patch"]["properties"]
                         ["expected_surface_revision"].is_object() );
    BOOST_CHECK( operationContracts["surface.apply_patch"]["properties"]
                         ["expected_schema_version"].is_object() );
    BOOST_REQUIRE( operationContracts.contains( "pcb.create_shape" ) );

    const nlohmann::json& createShapeContract =
            operationContracts["pcb.create_shape"];
    BOOST_REQUIRE( createShapeContract["properties"].contains( "shape_type" ) );
    BOOST_REQUIRE( createShapeContract["properties"]["shape_type"].contains( "enum" ) );
    BOOST_CHECK( std::find(
                         createShapeContract["properties"]["shape_type"]["enum"].begin(),
                         createShapeContract["properties"]["shape_type"]["enum"].end(),
                         "polygon" )
                 != createShapeContract["properties"]["shape_type"]["enum"].end() );

    BOOST_REQUIRE( createShapeContract["properties"].contains( "geometry" ) );
    const nlohmann::json& geometryContract =
            createShapeContract["properties"]["geometry"];
    BOOST_REQUIRE( geometryContract.contains( "properties" ) );
    BOOST_REQUIRE( geometryContract["properties"].contains( "points" ) );
    const nlohmann::json& pointsContract =
            geometryContract["properties"]["points"];
    BOOST_CHECK_EQUAL( pointsContract.value( "minItems", 0 ), 3 );
    BOOST_REQUIRE( pointsContract.contains( "items" ) );
    BOOST_REQUIRE( pointsContract["items"].contains( "required" ) );
    BOOST_CHECK( std::find( pointsContract["items"]["required"].begin(),
                            pointsContract["items"]["required"].end(), "x" )
                 != pointsContract["items"]["required"].end() );
    BOOST_CHECK( std::find( pointsContract["items"]["required"].begin(),
                            pointsContract["items"]["required"].end(), "y" )
                 != pointsContract["items"]["required"].end() );

    BOOST_REQUIRE( operationContracts.contains( "pcb.update_item_geometry" ) );
    const nlohmann::json& updateGeometryContract =
            operationContracts["pcb.update_item_geometry"];
    BOOST_REQUIRE( updateGeometryContract["properties"].contains(
            "geometry_patch" ) );
    const nlohmann::json& geometryPatchContract =
            updateGeometryContract["properties"]["geometry_patch"];
    BOOST_REQUIRE( geometryPatchContract.contains( "properties" ) );
    BOOST_CHECK( geometryPatchContract["properties"].contains( "start" ) );
    BOOST_CHECK( geometryPatchContract["properties"].contains( "end" ) );
    BOOST_CHECK( geometryPatchContract["properties"].contains( "center" ) );
    BOOST_CHECK( geometryPatchContract["properties"].contains( "mid" ) );
    BOOST_CHECK( geometryPatchContract["properties"].contains( "radius" ) );
    BOOST_REQUIRE( geometryPatchContract["properties"].contains( "points" ) );
    const nlohmann::json& patchPointsContract =
            geometryPatchContract["properties"]["points"];
    BOOST_CHECK_EQUAL( patchPointsContract.value( "minItems", 0 ), 3 );
    BOOST_REQUIRE( patchPointsContract.contains( "items" ) );
    BOOST_REQUIRE( patchPointsContract["items"].contains( "required" ) );
    BOOST_CHECK( std::find( patchPointsContract["items"]["required"].begin(),
                            patchPointsContract["items"]["required"].end(), "x" )
                 != patchPointsContract["items"]["required"].end() );
    BOOST_CHECK( std::find( patchPointsContract["items"]["required"].begin(),
                            patchPointsContract["items"]["required"].end(), "y" )
                 != patchPointsContract["items"]["required"].end() );

    BOOST_REQUIRE( operationContracts.contains( "pcb.set_item_properties" ) );
    BOOST_REQUIRE( operationContracts["pcb.set_item_properties"]["properties"].contains(
            "typed_props" ) );
    const nlohmann::json& typedPropsContract =
            operationContracts["pcb.set_item_properties"]["properties"]["typed_props"];
    BOOST_REQUIRE( typedPropsContract.contains( "properties" ) );
    for( const std::string& propName :
         { "diameter", "drill", "width", "fill", "clearance", "priority",
           "fill_mode", "reference", "value", "side", "orientation_degrees" } )
    {
        BOOST_CHECK( typedPropsContract["properties"].contains( propName ) );
    }
    BOOST_CHECK_EQUAL( typedPropsContract["properties"]["fill"]["type"], "boolean" );
    BOOST_CHECK_EQUAL( typedPropsContract["properties"]["reference"]["type"], "string" );
    BOOST_CHECK_EQUAL( typedPropsContract["properties"]["side"]["type"], "string" );
    BOOST_REQUIRE( typedPropsContract["properties"]["fill_mode"].contains( "enum" ) );
    BOOST_CHECK( std::find( typedPropsContract["properties"]["fill_mode"]["enum"].begin(),
                            typedPropsContract["properties"]["fill_mode"]["enum"].end(),
                            "hatch_pattern" )
                 != typedPropsContract["properties"]["fill_mode"]["enum"].end() );

    BOOST_REQUIRE( operationContracts.contains( "pcb.refill_zones" ) );
    BOOST_REQUIRE( operationContracts["pcb.refill_zones"]["properties"].contains(
            "affected_area" ) );
    BOOST_CHECK( boxSchemaSupportsCanonicalForms(
            operationContracts["pcb.refill_zones"]["properties"]["affected_area"] ) );

    BOOST_REQUIRE( operationContracts.contains( "pcb.rebuild_connectivity" ) );
    BOOST_REQUIRE( operationContracts["pcb.rebuild_connectivity"]["properties"].contains(
            "scope" ) );
    BOOST_CHECK( stringEnumContainsAll(
            operationContracts["pcb.rebuild_connectivity"]["properties"]["scope"],
            { "session", "affected_area", "selection", "region" } ) );
    BOOST_REQUIRE( operationContracts.contains( "pcb.run_validation" ) );
    BOOST_REQUIRE( operationContracts["pcb.run_validation"]["properties"].contains(
            "scope" ) );
    BOOST_CHECK( stringEnumContainsAll(
            operationContracts["pcb.run_validation"]["properties"]["scope"],
            { "session", "affected_area", "selection", "region" } ) );
    BOOST_CHECK( stringEnumContainsAll(
            operationContracts["pcb.run_validation"]["properties"]["level"],
            { "geometry", "drc_lite", "full_drc" } ) );

    BOOST_REQUIRE( catalogTool( "kisurf_query_items" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_query_items" )->value( "layer",
                                                                  std::string() ),
                       "atomic" );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_query_items" )->value( "side_effect",
                                                                  std::string() ),
                       "read_only" );
    BOOST_REQUIRE( catalogTool( "kisurf_query_items" )->contains(
            "filter_contract" ) );
    BOOST_CHECK( queryFilterSchemaSupportsShadowFilters(
            ( *catalogTool( "kisurf_query_items" ) )["filter_contract"] ) );
    BOOST_REQUIRE( catalogTool( "kisurf_query_item" ) );
    BOOST_REQUIRE( catalogTool( "kisurf_query_item" )->contains(
            "handle_contract" ) );
    BOOST_CHECK( queryHandleSchemaSupportsTypedReferences(
            ( *catalogTool( "kisurf_query_item" ) )["handle_contract"] ) );
    BOOST_REQUIRE( catalogTool( "kisurf_render_preview" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_render_preview" )->value( "side_effect",
                                                                     std::string() ),
                       "render" );
    BOOST_REQUIRE( catalogTool( "kisurf_render_preview" )->contains(
            "argument_contract" ) );
    BOOST_CHECK( renderPreviewSchemaDeclaresTypedObservationArgs(
            ( *catalogTool( "kisurf_render_preview" ) )["argument_contract"] ) );
    BOOST_REQUIRE( catalogTool( "kisurf_run_validation" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_run_validation" )->value( "side_effect",
                                                                     std::string() ),
                       "validate" );
    BOOST_REQUIRE( catalogTool( "kisurf_run_validation" )->contains(
            "argument_contract" ) );
    BOOST_CHECK( validationSchemaDeclaresTypedObservationArgs(
            ( *catalogTool( "kisurf_run_validation" ) )["argument_contract"] ) );
    BOOST_REQUIRE( catalogTool( "kisurf_run_cell" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_run_cell" )->value( "layer",
                                                               std::string() ),
                       "script" );
    BOOST_CHECK( !catalogTool( "kisurf_run_cell" )->value( "can_publish", true ) );
    BOOST_CHECK( !catalogTool( "kisurf_run_cell" )->value( "direct_publish", true ) );
    BOOST_REQUIRE( catalogTool( "kisurf_run_cell" )->contains(
            "script_budget" ) );
    BOOST_CHECK_EQUAL(
            ( *catalogTool( "kisurf_run_cell" ) )["script_budget"]
                    ["default_max_operation_count"].get<int>(),
            256 );
    BOOST_REQUIRE( catalogTool( "kisurf_accept_session" ) );
    BOOST_CHECK_EQUAL( catalogTool( "kisurf_accept_session" )->value( "layer",
                                                                     std::string() ),
                       "runtime_gate" );
    BOOST_CHECK( !catalogTool( "kisurf_accept_session" )->value( "can_publish", true ) );
    BOOST_CHECK( !catalogTool( "kisurf_accept_session" )->value( "direct_publish", true ) );

    const wxString catalogText = handler.ToolCatalogJson();
    BOOST_CHECK( !catalogText.Contains( wxS( "script_run_operation_bundle" ) ) );
    BOOST_CHECK( !catalogText.Contains( wxS( "pcb_fill_via_matrix" ) ) );
    BOOST_CHECK( !catalogText.Contains( wxS( "\"can_publish\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( UnknownToolFallsThroughDispatcherContract )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(), toolCall( wxS( "pcb_create_via" ), wxS( "{}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "unknown_tool" ) ) );
}


BOOST_AUTO_TEST_CASE( RunCellAutoOpensSessionButDoesNotMutateBoard )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-1\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->SessionId(), 1 );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_received" );
    BOOST_CHECK_EQUAL( payload["python_worker"].get<std::string>(), "not_connected" );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["session"]["base_hash"].get<std::string>(),
                       "doc=10;sel=2;view=4" );
}


BOOST_AUTO_TEST_CASE( SessionBaseHashPrefersEditorBoardHashContext )
{
    AI_PROVIDER_REQUEST request = requestWithContext();
    request.m_ContextSnapshot.m_ToolState.m_SharedContextJson =
            wxS( "{\"board_hash\":\"pcb-content-hash-42\"}" );

    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.query_board_summary()\","
                           "\"cell_id\":\"cell-board-hash\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["session"]["base_hash"].get<std::string>(),
                       "pcb-content-hash-42" );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->BaseHash(),
                       wxString( wxS( "pcb-content-hash-42" ) ) );
}


BOOST_AUTO_TEST_CASE( RunCellWithConnectedWorkerAppliesLoweredAtomicOps )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python placed via" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"py-via-0\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    auto workerOwner = std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult );
    SCRIPTED_PYTHON_WORKER* worker = workerOwner.get();
    AI_SESSION_TOOL_CALL_HANDLER handler( std::move( workerOwner ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...) && observe\","
                           "\"cell_id\":\"cell-py-1\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( worker->m_RunCount, 1 );
    BOOST_CHECK_EQUAL( worker->m_LastSessionId, handler.ActiveSession()->SessionId() );
    BOOST_CHECK_EQUAL( worker->m_LastCellId, wxString( wxS( "cell-py-1" ) ) );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["python_worker"].get<std::string>(), "connected" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_CHECK( payload["shadow_board_mutated"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );

    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 1 );
    BOOST_REQUIRE( handler.ActiveSession()->ResolveAlias( wxS( "py-via-0" ) ).has_value() );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsOperationBatchOverCallerLimit )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "too many operations" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"budget-via-a\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"budget-via-b\",\"net\":\"GND\","
                   "\"position\":{\"x\":35,\"y\":60}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    nlohmann::json args = {
        { "cell_text", "session.create_via(...); session.create_via(...)" },
        { "cell_id", "cell-budget" },
        { "max_operation_count", 1 }
    };

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(), toolCall( wxS( "kisurf_run_cell" ), jsonText( args ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "budget-via-a" ) ).has_value() );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "budget-via-b" ) ).has_value() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "script_operation_budget_exceeded" );
    BOOST_CHECK_EQUAL( payload["operation_count"].get<size_t>(), 2 );
    BOOST_CHECK_EQUAL( payload["max_operation_count"].get<size_t>(), 1 );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsCallerOperationLimitAboveRuntimeCap )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    nlohmann::json args = {
        { "cell_text", "pass" },
        { "max_operation_count", 257 }
    };

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(), toolCall( wxS( "kisurf_run_cell" ), jsonText( args ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( !handler.ActiveSession() );
}


BOOST_AUTO_TEST_CASE( DirectAtomicOperationAppliesToShadowSessionOnly )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_atomic_operation" ),
                      wxS( "{\"kind\":\"pcb.create_via\",\"arguments\":"
                           "{\"alias\":\"direct-via\",\"net\":\"GND\","
                           "\"position\":{\"x\":25,\"y\":50}}}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "atomic_operation_executed" );
    BOOST_CHECK_EQUAL( payload["kind"].get<std::string>(), "pcb.create_via" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_CHECK( payload["shadow_board_mutated"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
    BOOST_REQUIRE( payload["operation_ids"].is_array() );
    BOOST_REQUIRE_EQUAL( payload["operation_ids"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["operation_ids"][0].get<uint64_t>(), 1 );

    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[0].m_Kind
                 == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 1 );
    BOOST_REQUIRE( handler.ActiveSession()->ResolveAlias( wxS( "direct-via" ) ).has_value() );
}


BOOST_AUTO_TEST_CASE( DirectAtomicOperationRejectsRawBoardAccessBeforeMutation )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_atomic_operation" ),
                      wxS( "{\"kind\":\"pcb.create_via\",\"arguments\":"
                           "{\"alias\":\"raw-board-via\",\"net\":\"GND\","
                           "\"position\":{\"x\":25,\"y\":50},"
                           "\"raw_board_access\":true}}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "forbidden_runtime_capability" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"forbidden_field\":\"arguments.raw_board_access\"" ) ) );
    BOOST_CHECK( !handler.ActiveSession() );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsChangedSelectionRevisionBeforePythonWorkerRuns )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"blocked-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":11,\"y\":12}}" ) } );

    auto worker = std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult );
    SCRIPTED_PYTHON_WORKER* rawWorker = worker.get();
    AI_SESSION_TOOL_CALL_HANDLER handler( std::move( worker ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ),
                      wxS( "{\"board_id\":\"pcb-a\",\"base_hash\":\"hash-a\"}" ) ) )
                           .m_Allowed );

    AI_PROVIDER_REQUEST changedRequest = requestWithContext();
    changedRequest.m_ContextVersion.m_SelectionRevision = 5;
    changedRequest.m_ContextSnapshot.m_Version = changedRequest.m_ContextVersion;
    changedRequest.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            changedRequest.m_ContextVersion;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            changedRequest,
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-selection-conflict\"}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "selection_conflict" ) ) );
    BOOST_CHECK_EQUAL( rawWorker->m_RunCount, 0 );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK( handler.ActiveSession()->Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( RunCellAutoRendersPreviewAndValidationAfterMutationStep )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python placed visible via" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"auto-preview-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    RECORDING_SESSION_VALIDATION_SERVICE validationService;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ), nullptr,
            &previewService, nullptr, &validationService );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-auto-step-feedback\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_REQUIRE_EQUAL( previewService.m_RenderCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.m_RenderedItemCounts.size(), 1 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts.back(), 1 );
    BOOST_REQUIRE_EQUAL( validationService.m_RunCount, 1 );

    const auto& journal = handler.ActiveSession()->Journal().Operations();
    BOOST_REQUIRE_EQUAL( journal.size(), 3 );
    BOOST_CHECK( journal[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( journal[1].m_Kind == AI_SESSION_OPERATION_KIND::RunValidation );
    BOOST_CHECK( journal[2].m_Kind == AI_SESSION_OPERATION_KIND::RenderPreview );
    BOOST_CHECK_EQUAL( journal[0].m_StepId, journal[1].m_StepId );
    BOOST_CHECK_EQUAL( journal[0].m_StepId, journal[2].m_StepId );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( payload.contains( "step_results" ) );
    BOOST_REQUIRE_EQUAL( payload["step_results"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["step_results"][0]["kind"].get<std::string>(),
                       "pcb.run_validation" );
    BOOST_CHECK_EQUAL( payload["step_results"][0]["status"].get<std::string>(),
                       "validation_completed" );
    BOOST_CHECK_EQUAL( payload["step_results"][0]["validation"]["native_backend"]
                               .get<std::string>(),
                       "recording" );
    BOOST_CHECK_EQUAL( payload["step_results"][1]["kind"].get<std::string>(),
                       "render.preview" );
    BOOST_CHECK_EQUAL( payload["step_results"][1]["status"].get<std::string>(),
                       "preview_rendered" );
    BOOST_CHECK_EQUAL( payload["step_results"][1]["rendered_item_count"].get<size_t>(),
                       1 );
}


BOOST_AUTO_TEST_CASE( RunCellReturnsWorkerEventsForAgentInspection )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python emitted events" );
    workerResult.m_Events.push_back(
            { wxS( "progress" ), wxS( "placed guard ring" ),
              wxS( "{\"step\":\"zone\",\"count\":1}" ) } );
    workerResult.m_Events.push_back(
            { wxS( "inspection" ), wxS( "needs clearance review" ),
              wxS( "{\"severity\":\"warning\"}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.event(...)\","
                           "\"cell_id\":\"cell-events\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_REQUIRE( payload.contains( "events" ) );
    BOOST_REQUIRE_EQUAL( payload["events"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["events"][0]["kind"].get<std::string>(), "progress" );
    BOOST_CHECK_EQUAL( payload["events"][0]["message"].get<std::string>(),
                       "placed guard ring" );
    BOOST_CHECK_EQUAL( payload["events"][0]["payload"]["step"].get<std::string>(),
                       "zone" );
    BOOST_CHECK_EQUAL( payload["events"][1]["payload"]["severity"].get<std::string>(),
                       "warning" );
}


BOOST_AUTO_TEST_CASE( RunCellRecordsStreamedWorkerEventsForTimeline )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_Events.push_back(
            { wxS( "progress" ), wxS( "routed first segment" ),
              wxS( "{\"segment\":1}" ) } );

    auto worker = std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult );
    worker->m_StreamEvents.push_back(
            { wxS( "progress" ), wxS( "routed first segment" ),
              wxS( "{\"segment\":1}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler( std::move( worker ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.event(...)\","
                           "\"cell_id\":\"cell-streamed-events\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( payload.contains( "recorded_events" ) );
    BOOST_REQUIRE_EQUAL( payload["recorded_events"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["recorded_events"][0]["kind"].get<std::string>(),
                       "progress" );
    BOOST_CHECK_EQUAL( payload["recorded_events"][0]["source"].get<std::string>(),
                       "stream" );
    BOOST_CHECK_EQUAL( payload["recorded_events"][0]["cell_id"].get<std::string>(),
                       "cell-streamed-events" );
    BOOST_CHECK_EQUAL( payload["recorded_events"][0]["sequence"].get<uint64_t>(), 1 );

    AI_TOOL_INVOCATION_RESULT timelineResult = handler.HandleToolCall(
            requestWithRichContext(),
            toolCall( wxS( "kisurf_query_activity_timeline" ), wxS( "{}" ) ) );

    BOOST_REQUIRE( timelineResult.m_Allowed );
    nlohmann::json timeline =
            nlohmann::json::parse( timelineResult.m_ResultJson.ToStdString() );
    BOOST_REQUIRE_EQUAL( timeline["python_event_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( timeline["python_events"].size(), 1 );
    BOOST_CHECK_EQUAL( timeline["python_events"][0]["message"].get<std::string>(),
                       "routed first segment" );
    BOOST_CHECK_EQUAL( timeline["python_events"][0]["payload"]["segment"].get<int>(), 1 );
}


BOOST_AUTO_TEST_CASE( RunCellReturnsPythonSdkRuntimeMetadata )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_SdkName = wxS( "kisurf-ai-session-sdk" );
    workerResult.m_SdkVersion = wxS( "0.1.0" );
    workerResult.m_SdkProtocol = wxS( "kisurf.ai.session.v1" );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"pass\","
                           "\"cell_id\":\"cell-runtime\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( payload.contains( "sdk" ) );
    BOOST_CHECK_EQUAL( payload["sdk"]["name"].get<std::string>(),
                       "kisurf-ai-session-sdk" );
    BOOST_CHECK_EQUAL( payload["sdk"]["version"].get<std::string>(), "0.1.0" );
    BOOST_CHECK_EQUAL( payload["sdk"]["protocol"].get<std::string>(),
                       "kisurf.ai.session.v1" );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsMismatchedWorkerSessionBeforeApplyingOps )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_HasSessionContext = true;
    workerResult.m_SessionId = 999;
    workerResult.m_BoardId = wxS( "active-pcb" );
    workerResult.m_BaseHash = wxS( "doc=10;sel=2;view=4" );
    workerResult.m_Epoch = 0;
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"wrong-session-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-wrong-session\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "stale_python_cell_result" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 0 );
    BOOST_CHECK( !payload["shadow_board_mutated"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunCellReturnsObservationResultsFromPythonOperations )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python inspect placement" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"observe-via-0\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QueryBoardSummary, wxS( "{}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QueryItems,
              wxS( "{\"filter\":{\"type\":\"via\",\"net\":\"GND\"}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...); session.query_items()\","
                           "\"cell_id\":\"cell-observe\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_REQUIRE( payload.contains( "operation_results" ) );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["kind"].get<std::string>(),
                       "query.board_summary" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["summary"]["vias"].get<size_t>(),
                       1 );
    BOOST_CHECK( payload["operation_results"][0]["journaled"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["operation_ids"][0].get<int>(),
                       2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["kind"].get<std::string>(),
                       "query.items" );
    BOOST_REQUIRE_EQUAL( payload["operation_results"][1]["items"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["items"][0]["alias"].get<std::string>(),
                       "observe-via-0" );
    BOOST_CHECK( payload["operation_results"][1]["journaled"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["operation_ids"][0].get<int>(),
                       3 );

    const auto& journal = handler.ActiveSession()->Journal().Operations();
    BOOST_REQUIRE_EQUAL( journal.size(), 3 );
    BOOST_CHECK( journal[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( journal[1].m_Kind == AI_SESSION_OPERATION_KIND::QueryBoardSummary );
    BOOST_CHECK( journal[2].m_Kind == AI_SESSION_OPERATION_KIND::QueryItems );

    nlohmann::json queryRecord =
            nlohmann::json::parse( journal[2].m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( queryRecord["status"].get<std::string>(), "items" );
    BOOST_REQUIRE_EQUAL( queryRecord["items"].size(), 1 );
    BOOST_CHECK_EQUAL( queryRecord["items"][0]["alias"].get<std::string>(),
                       "observe-via-0" );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 1 );
}


BOOST_AUTO_TEST_CASE( RunCellExecutesPythonCheckpointAndRollbackRequests )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "trial then rollback" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::Checkpoint,
              wxS( "{\"name\":\"stable before trial\"}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"trial-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RollbackTo,
              wxS( "{\"checkpoint_id\":2}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.checkpoint(...);"
                           " session.create_via(...); session.rollback_to(...)\","
                           "\"cell_id\":\"cell-control\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "trial-via" ) ).has_value() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["kind"].get<std::string>(),
                       "session.checkpoint" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["status"].get<std::string>(),
                       "checkpoint_created" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["checkpoint_id"].get<uint64_t>(),
                       2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["kind"].get<std::string>(),
                       "session.rollback_to" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["status"].get<std::string>(),
                       "rolled_back" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["checkpoint_id"].get<uint64_t>(),
                       2 );
    BOOST_CHECK( !payload["operation_results"][1]["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunCellRollsBackToNamedPythonCheckpoint )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "named checkpoint rollback" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::Checkpoint,
              wxS( "{\"name\":\"before correction\"}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"wrong-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RollbackTo,
              wxS( "{\"checkpoint_name\":\"before correction\"}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.checkpoint('before correction');"
                           " session.create_via(...);"
                           " session.rollback_to(name='before correction')\","
                           "\"cell_id\":\"cell-named-rollback\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "wrong-via" ) ).has_value() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["checkpoint_id"].get<uint64_t>(),
                       2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["kind"].get<std::string>(),
                       "session.rollback_to" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["status"].get<std::string>(),
                       "rolled_back" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["checkpoint_name"].get<std::string>(),
                       "before correction" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["checkpoint_id"].get<uint64_t>(),
                       2 );
}


BOOST_AUTO_TEST_CASE( RunCellCanContinueAfterPythonRollbackInSameCell )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "rollback then correct" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::Checkpoint,
              wxS( "{\"name\":\"before correction\"}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"wrong-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RollbackTo,
              wxS( "{\"checkpoint_id\":2}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"corrected-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":75,\"y\":100}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.checkpoint(...);"
                           " session.create_via(...); session.rollback_to(...);"
                           " session.create_via(...)\","
                           "\"cell_id\":\"cell-control-continue\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 1 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "wrong-via" ) ).has_value() );
    BOOST_REQUIRE( handler.ActiveSession()->ResolveAlias( wxS( "corrected-via" ) ).has_value() );
    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[0].m_Kind
                 == AI_SESSION_OPERATION_KIND::CreateVia );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["step_id"].get<uint64_t>(), 2 );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 2 );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 2 );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["status"].get<std::string>(),
                       "rolled_back" );
}


BOOST_AUTO_TEST_CASE( QueryActivityTimelineIncludesSessionJournalOperations )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python checks own timeline" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"timeline-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QueryActivityTimeline, wxS( "{}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRichContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.query_activity_timeline()\","
                           "\"cell_id\":\"cell-own-timeline\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 1 );
    const nlohmann::json& timeline = payload["operation_results"][0];
    BOOST_CHECK_EQUAL( timeline["status"].get<std::string>(), "activity_timeline" );
    BOOST_CHECK_EQUAL( timeline["events"][0]["action"].get<std::string>(),
                       "pcbnew.InteractiveRoute" );
    BOOST_CHECK_EQUAL( timeline["session_operation_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( timeline["session_operations"].size(), 1 );
    BOOST_CHECK_EQUAL( timeline["session_operations"][0]["kind"].get<std::string>(),
                       "pcb.create_via" );
    BOOST_CHECK_EQUAL( timeline["session_operations"][0]["created_handles"][0]["alias"]
                               .get<std::string>(),
                       "timeline-via" );

    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 2 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[1].m_Kind
                 == AI_SESSION_OPERATION_KIND::QueryActivityTimeline );
}


BOOST_AUTO_TEST_CASE( LocalWorkerScriptCreatesAnnularZoneAndViaRingInShadowBoard )
{
    auto worker = std::make_unique<AI_PYTHON_LOCAL_WORKER>(
            wxS( "python" ), pythonSdkRootPath() );
    BOOST_REQUIRE( worker->IsConnected() );

    AI_SESSION_TOOL_CALL_HANDLER handler( std::move( worker ) );

    const char* cellText =
            "with session.step('annular copper with via stitching'):\n"
            "    session.create_annular_zone(\n"
            "        center={'x': 1000, 'y': 2000},\n"
            "        inner_radius=250,\n"
            "        outer_radius=500,\n"
            "        segments=16,\n"
            "        layer_set=['F.Cu'],\n"
            "        net='GND',\n"
            "        alias='guard-ring')\n"
            "    session.create_via_ring(\n"
            "        center={'x': 1000, 'y': 2000},\n"
            "        radius=650,\n"
            "        count=8,\n"
            "        net='GND',\n"
            "        alias_prefix='stitch')\n";

    nlohmann::json args = {
        { "cell_id", "guard-ring-script" },
        { "cell_text", cellText }
    };

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(), toolCall( wxS( "kisurf_run_cell" ), jsonText( args ) ) );

    BOOST_REQUIRE( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_REQUIRE_MESSAGE( result.m_Executed, payload.dump() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 9 );
    BOOST_CHECK_EQUAL( payload["operation_ids"].size(), 9 );

    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 9 );

    nlohmann::json summary = nlohmann::json::parse(
            handler.ActiveSession()->ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["zones"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["vias"].get<size_t>(), 8 );

    std::vector<AI_SHADOW_ITEM> zones =
            handler.ActiveSession()->ShadowBoard().QueryItems(
                    wxS( "{\"type\":\"zone\"}" ) );
    BOOST_REQUIRE_EQUAL( zones.size(), 1 );
    BOOST_CHECK_EQUAL( zones.front().m_Alias, wxString( wxS( "guard-ring" ) ) );

    nlohmann::json zoneGeometry =
            nlohmann::json::parse( zones.front().m_GeometryJson.ToStdString() );
    BOOST_CHECK_EQUAL( zoneGeometry["type"].get<std::string>(), "annulus" );
    BOOST_CHECK_EQUAL( zoneGeometry["outer"].size(), 16 );
    BOOST_CHECK_EQUAL( zoneGeometry["inner"].size(), 16 );

    std::vector<AI_SHADOW_ITEM> vias =
            handler.ActiveSession()->ShadowBoard().QueryItems(
                    wxS( "{\"type\":\"via\",\"net\":\"GND\"}" ) );
    BOOST_REQUIRE_EQUAL( vias.size(), 8 );
    BOOST_CHECK_EQUAL( vias.front().m_Alias, wxString( wxS( "stitch_0" ) ) );
}


BOOST_AUTO_TEST_CASE( RunCellReturnsValidationResultFromPythonOperation )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python validate placement" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"validate-via-0\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RunValidation,
              wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.run_validation(level='geometry')\","
                           "\"cell_id\":\"cell-validate\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 2 );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["kind"].get<std::string>(),
                       "pcb.run_validation" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["status"].get<std::string>(),
                       "validation_completed" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["validation"]["level"].get<std::string>(),
                       "geometry" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["validation"]["issue_count"].get<size_t>(),
                       0 );

    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 2 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[1].m_Kind
                 == AI_SESSION_OPERATION_KIND::RunValidation );
}


BOOST_AUTO_TEST_CASE( RunCellWorkerFailureRollsBackPartialOps )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = false;
    workerResult.m_ErrorCode = wxS( "python_exception" );
    workerResult.m_Message = wxS( "spacing variable was undefined" );
    workerResult.m_StepLabel = wxS( "bad python cell" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"bad-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":5,\"y\":10}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"raise NameError('spacing')\","
                           "\"cell_id\":\"bad-cell\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["python_worker"].get<std::string>(), "connected" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(), "python_exception" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );

    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations().empty() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias( wxS( "bad-via" ) ).has_value() );
}


BOOST_AUTO_TEST_CASE( RunCellFailureRollsBackObservationJournalRecords )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python observes then fails" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QuerySelection, wxS( "{}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::Unknown, wxS( "{}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRichContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.query_selection();"
                           " session.emit('unknown')\","
                           "\"cell_id\":\"cell-observation-rollback\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "unsupported_operation" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsMalformedPythonOperationArguments )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "malformed python operation args" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia, wxS( "{" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.emit('pcb.create_via')\","
                           "\"cell_id\":\"cell-bad-operation-json\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "malformed_operation_arguments" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunCellRejectsForbiddenPythonOperationCapability )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "forbidden python operation capability" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"forbidden-cell-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50},"
                   "\"direct_publish\":true}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-forbidden-capability\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK( !handler.ActiveSession()->ResolveAlias(
            wxS( "forbidden-cell-via" ) ).has_value() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "forbidden_runtime_capability" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["status"].get<std::string>(),
                       "forbidden_runtime_capability" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["forbidden_field"].get<std::string>(),
                       "operation[0].arguments.direct_publish" );
}


BOOST_AUTO_TEST_CASE( StepLifecycleAndCheckpointAreStateful )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ),
                      wxS( "{\"board_id\":\"pcb-a\",\"base_hash\":\"hash-a\"}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT beginResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_begin_step" ),
                      wxS( "{\"label\":\"place ring vias\"}" ) ) );

    BOOST_CHECK( beginResult.m_Allowed );
    nlohmann::json beginPayload =
            nlohmann::json::parse( beginResult.m_ResultJson.ToStdString() );
    const uint64_t stepId = beginPayload["step_id"].get<uint64_t>();
    BOOST_CHECK_EQUAL( stepId, 1 );

    AI_TOOL_INVOCATION_RESULT checkpointResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_checkpoint" ),
                      wxS( "{\"name\":\"before validation tweak\"}" ) ) );

    BOOST_CHECK( checkpointResult.m_Allowed );
    nlohmann::json checkpointPayload =
            nlohmann::json::parse( checkpointResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( checkpointPayload["checkpoint_id"].get<uint64_t>(), 1 );

    AI_TOOL_INVOCATION_RESULT endResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_end_step" ),
                      wxString::Format( wxS( "{\"step_id\":%llu}" ),
                                        static_cast<unsigned long long>( stepId ) ) ) );

    BOOST_CHECK( endResult.m_Allowed );
    nlohmann::json endPayload =
            nlohmann::json::parse( endResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( endPayload["status"].get<std::string>(), "step_completed" );
    BOOST_CHECK_EQUAL( endPayload["observation"]["step_id"].get<uint64_t>(), stepId );

    AI_TOOL_INVOCATION_RESULT rollbackResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_rollback_to" ),
                      wxS( "{\"checkpoint_id\":1}" ) ) );

    BOOST_CHECK( rollbackResult.m_Allowed );
    nlohmann::json rollbackPayload =
            nlohmann::json::parse( rollbackResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( rollbackPayload["status"].get<std::string>(), "rolled_back" );
}


BOOST_AUTO_TEST_CASE( RollbackToolAcceptsCheckpointName )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT open = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( open.m_Allowed );

    AI_TOOL_INVOCATION_RESULT checkpoint = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_checkpoint" ),
                      wxS( "{\"name\":\"before direct correction\"}" ) ) );
    BOOST_REQUIRE( checkpoint.m_Allowed );

    AI_TOOL_INVOCATION_RESULT rollback = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_rollback_to" ),
                      wxS( "{\"checkpoint_name\":\"before direct correction\"}" ) ) );

    BOOST_CHECK( rollback.m_Allowed );
    BOOST_CHECK( rollback.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( rollback.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "rolled_back" );
    BOOST_CHECK_EQUAL( payload["checkpoint_name"].get<std::string>(),
                       "before direct correction" );
    BOOST_CHECK_EQUAL( payload["checkpoint_id"].get<uint64_t>(), 1 );
}


BOOST_AUTO_TEST_CASE( ObserveStepDoesNotCloseOpenStep )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ),
                      wxS( "{\"board_id\":\"pcb-a\",\"base_hash\":\"hash-a\"}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT beginResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_begin_step" ),
                      wxS( "{\"label\":\"inspect intermediate preview\"}" ) ) );
    BOOST_REQUIRE( beginResult.m_Allowed );

    AI_TOOL_INVOCATION_RESULT observeResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_observe_step" ), wxS( "{\"step_id\":1}" ) ) );
    BOOST_REQUIRE( observeResult.m_Allowed );
    nlohmann::json observePayload =
            nlohmann::json::parse( observeResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( observePayload["status"].get<std::string>(), "step_observed" );
    BOOST_CHECK_EQUAL( observePayload["observation"]["summary"].get<std::string>(),
                       "Step 1 open with 0 operation(s)." );

    AI_TOOL_INVOCATION_RESULT endResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_end_step" ), wxS( "{\"step_id\":1}" ) ) );
    BOOST_REQUIRE( endResult.m_Allowed );
    nlohmann::json endPayload =
            nlohmann::json::parse( endResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( endPayload["status"].get<std::string>(), "step_completed" );
    BOOST_CHECK_EQUAL( endPayload["observation"]["summary"].get<std::string>(),
                       "Step 1 completed with 0 operation(s)." );
}


BOOST_AUTO_TEST_CASE( QueryToolsExposeShadowBoardWithoutMutatingLiveBoard )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT summaryResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_board_summary" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( summaryResult.m_Allowed );
    BOOST_CHECK( !summaryResult.m_Executed );

    nlohmann::json summaryPayload =
            nlohmann::json::parse( summaryResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( summaryPayload["status"].get<std::string>(), "board_summary" );
    BOOST_CHECK_EQUAL( summaryPayload["summary"]["items_total"].get<size_t>(), 0 );
    BOOST_CHECK( !summaryPayload["board_mutated"].get<bool>() );

    AI_TOOL_INVOCATION_RESULT itemsResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_items" ),
                      wxS( "{\"filter\":{\"type\":\"via\"}}" ) ) );
    BOOST_REQUIRE( itemsResult.m_Allowed );

    nlohmann::json itemsPayload =
            nlohmann::json::parse( itemsResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( itemsPayload["status"].get<std::string>(), "items" );
    BOOST_CHECK_EQUAL( itemsPayload["items"].size(), 0 );
    BOOST_CHECK( !itemsPayload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( QueryItemToolResolvesSessionAliasWithoutMutatingLiveBoard )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "create query target" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"query-target\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT itemResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_item" ),
                      wxS( "{\"alias\":\"query-target\"}" ) ) );

    BOOST_REQUIRE( itemResult.m_Allowed );
    BOOST_CHECK( !itemResult.m_Executed );
    nlohmann::json payload = nlohmann::json::parse( itemResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "item" );
    BOOST_CHECK( payload["found"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["item"]["alias"].get<std::string>(), "query-target" );
    BOOST_CHECK_EQUAL( payload["item"]["net"].get<std::string>(), "GND" );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( UpdateItemGeometryPatchesWithoutDroppingExistingGeometry )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "patch via geometry" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"patch-target\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50},\"diameter\":500000}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
              wxS( "{\"handle\":\"patch-target\","
                   "\"geometry_patch\":{\"diameter\":700000}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.update_item_geometry(...)\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT itemResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_item" ),
                      wxS( "{\"alias\":\"patch-target\"}" ) ) );

    BOOST_REQUIRE( itemResult.m_Allowed );
    nlohmann::json payload = nlohmann::json::parse( itemResult.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( payload["found"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["item"]["geometry"]["position"]["x"].get<int>(), 25 );
    BOOST_CHECK_EQUAL( payload["item"]["geometry"]["position"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( payload["item"]["geometry"]["diameter"].get<int>(), 700000 );
}


BOOST_AUTO_TEST_CASE( UpdateItemGeometryRejectsInvalidPatchWithoutJournalMutation )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                           .m_Allowed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_begin_step" ),
                      wxS( "{\"label\":\"create patch target\"}" ) ) )
                           .m_Allowed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_atomic_operation" ),
                      wxS( "{\"kind\":\"pcb.create_via\","
                           "\"arguments\":{\"alias\":\"bad-patch-target\","
                           "\"position\":{\"x\":25,\"y\":50}}}" ) ) )
                           .m_Executed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_end_step" ), wxS( "{\"step_id\":1}" ) ) )
                           .m_Allowed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_begin_step" ),
                      wxS( "{\"label\":\"invalid geometry patch\"}" ) ) )
                           .m_Allowed );

    const uint64_t epochBefore = handler.ActiveSession()->Epoch();
    const size_t operationCountBefore =
            handler.ActiveSession()->Journal().Operations().size();

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_atomic_operation" ),
                      wxS( "{\"kind\":\"pcb.update_item_geometry\","
                           "\"arguments\":{\"handle\":\"bad-patch-target\","
                           "\"geometry_patch\":\"not an object\"}}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), epochBefore );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(),
                       operationCountBefore );
}


BOOST_AUTO_TEST_CASE( QueryItemToolReturnsCreateZoneTypedProperties )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "create property-rich zone" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateZone,
              wxS( "{\"alias\":\"zone-props\",\"net\":\"GND\","
                   "\"layer_set\":[\"F.Cu\"],\"clearance\":250000,"
                   "\"priority\":7,\"fill_mode\":\"hatch_pattern\","
                   "\"outline\":{\"points\":[{\"x\":0,\"y\":0},"
                   "{\"x\":2000,\"y\":0},{\"x\":2000,\"y\":2000},"
                   "{\"x\":0,\"y\":2000}]}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_zone(...)\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT itemResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_item" ),
                      wxS( "{\"alias\":\"zone-props\"}" ) ) );

    BOOST_REQUIRE( itemResult.m_Allowed );
    nlohmann::json payload = nlohmann::json::parse( itemResult.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( payload["found"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["item"]["properties"]["clearance"].get<int>(), 250000 );
    BOOST_CHECK_EQUAL( payload["item"]["properties"]["priority"].get<int>(), 7 );
    BOOST_CHECK_EQUAL( payload["item"]["properties"]["fill_mode"].get<std::string>(),
                       "hatch_pattern" );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( QuerySelectionIncludesShadowBoardSelectedHandles )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "create selected query target" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"selected-query-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::SetMetadata,
              wxS( "{\"handle\":\"selected-query-via\","
                   "\"key_values\":{\"selected\":\"true\"}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.set_metadata(...)\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT selectionResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_query_selection" ), wxS( "{}" ) ) );

    BOOST_REQUIRE( selectionResult.m_Allowed );
    BOOST_CHECK( !selectionResult.m_Executed );

    nlohmann::json payload =
            nlohmann::json::parse( selectionResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "selection" );
    BOOST_CHECK_EQUAL( payload["selected_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( payload["selected_shadow_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( payload["selected_shadow_items"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["selected_shadow_items"][0]["alias"].get<std::string>(),
                       "selected-query-via" );
    BOOST_REQUIRE_EQUAL( payload["selected_handles"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["selected_handles"][0]["alias"].get<std::string>(),
                       "selected-query-via" );
}


BOOST_AUTO_TEST_CASE( QuerySelectionReportsLiveSelectionRevisionConflict )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;
    AI_PROVIDER_REQUEST openRequest = requestWithContext();

    BOOST_REQUIRE( handler.HandleToolCall(
            openRequest, toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                           .m_Allowed );

    AI_PROVIDER_REQUEST changedRequest = requestWithRichContext();
    changedRequest.m_ContextVersion.m_SelectionRevision = 5;
    changedRequest.m_ContextSnapshot.m_Version = changedRequest.m_ContextVersion;
    changedRequest.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            changedRequest.m_ContextVersion;

    AI_TOOL_INVOCATION_RESULT selectionResult = handler.HandleToolCall(
            changedRequest,
            toolCall( wxS( "kisurf_query_selection" ), wxS( "{}" ) ) );

    BOOST_REQUIRE( selectionResult.m_Allowed );
    nlohmann::json payload =
            nlohmann::json::parse( selectionResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "selection" );
    BOOST_CHECK( payload["selection_revision"]["changed"].get<bool>() );
    BOOST_CHECK( payload["selection_revision"]["conflict"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["session"].get<uint64_t>(), 2 );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["current"].get<uint64_t>(), 5 );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["policy"].get<std::string>(),
                       "selection_handles_are_pinned_to_session_open" );
}


BOOST_AUTO_TEST_CASE( QueryItemsSelectionFilterReportsRevisionConflict )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "create selected query item" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"selected-filter-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::SetMetadata,
              wxS( "{\"handle\":\"selected-filter-via\","
                   "\"key_values\":{\"selected\":\"true\"}}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_PROVIDER_REQUEST openRequest = requestWithContext();
    BOOST_REQUIRE( handler.HandleToolCall(
            openRequest,
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.set_metadata(...)\"}" ) ) )
                           .m_Executed );

    AI_PROVIDER_REQUEST changedRequest = requestWithRichContext();
    changedRequest.m_ContextVersion.m_SelectionRevision = 5;
    changedRequest.m_ContextSnapshot.m_Version = changedRequest.m_ContextVersion;
    changedRequest.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            changedRequest.m_ContextVersion;

    AI_TOOL_INVOCATION_RESULT itemsResult = handler.HandleToolCall(
            changedRequest,
            toolCall( wxS( "kisurf_query_items" ),
                      wxS( "{\"filter\":{\"selection\":true}}" ) ) );

    BOOST_REQUIRE( itemsResult.m_Allowed );
    nlohmann::json payload =
            nlohmann::json::parse( itemsResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "items" );
    BOOST_REQUIRE_EQUAL( payload["items"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["items"][0]["alias"].get<std::string>(),
                       "selected-filter-via" );
    BOOST_CHECK( payload["selection_revision"]["changed"].get<bool>() );
    BOOST_CHECK( payload["selection_revision"]["conflict"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["session"].get<uint64_t>(), 2 );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["current"].get<uint64_t>(), 5 );
    BOOST_CHECK_EQUAL( payload["selection_revision"]["policy"].get<std::string>(),
                       "selection_handles_are_pinned_to_session_open" );
}


BOOST_AUTO_TEST_CASE( QueryToolsExposeCurrentEditorObservationContext )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;
    AI_PROVIDER_REQUEST request = requestWithRichContext();

    BOOST_CHECK( handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT selectionResult = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_query_selection" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( selectionResult.m_Allowed );
    BOOST_CHECK( !selectionResult.m_Executed );
    nlohmann::json selectionPayload =
            nlohmann::json::parse( selectionResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( selectionPayload["status"].get<std::string>(), "selection" );
    BOOST_CHECK_EQUAL( selectionPayload["selected_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( selectionPayload["selected_objects"][0]["label"].get<std::string>(),
                       "U1.1" );
    BOOST_CHECK( !selectionPayload["board_mutated"].get<bool>() );

    AI_TOOL_INVOCATION_RESULT netsResult = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_query_nets" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( netsResult.m_Allowed );
    nlohmann::json netsPayload =
            nlohmann::json::parse( netsResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( netsPayload["status"].get<std::string>(), "nets" );
    BOOST_REQUIRE_EQUAL( netsPayload["nets"].size(), 1 );
    BOOST_CHECK_EQUAL( netsPayload["nets"][0].get<std::string>(), "GND" );

    AI_TOOL_INVOCATION_RESULT layersResult = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_query_layers" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( layersResult.m_Allowed );
    nlohmann::json layersPayload =
            nlohmann::json::parse( layersResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( layersPayload["status"].get<std::string>(), "layers" );
    BOOST_CHECK( std::find( layersPayload["layers"].begin(), layersPayload["layers"].end(),
                            "F.Cu" )
                 != layersPayload["layers"].end() );
    BOOST_CHECK( std::find( layersPayload["layers"].begin(), layersPayload["layers"].end(),
                            "B.Cu" )
                 != layersPayload["layers"].end() );

    AI_TOOL_INVOCATION_RESULT viewportResult = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_query_viewport" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( viewportResult.m_Allowed );
    nlohmann::json viewportPayload =
            nlohmann::json::parse( viewportResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( viewportPayload["status"].get<std::string>(), "viewport" );
    BOOST_CHECK_EQUAL( viewportPayload["viewport"]["zoom"].get<double>(), 3.5 );
    BOOST_CHECK_EQUAL( viewportPayload["cursor"]["x"].get<int>(), 400 );
    BOOST_CHECK_EQUAL( viewportPayload["visual"]["source"].get<std::string>(),
                       "pcbnew.canvas" );

    AI_TOOL_INVOCATION_RESULT activityResult = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_query_activity_timeline" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( activityResult.m_Allowed );
    nlohmann::json activityPayload =
            nlohmann::json::parse( activityResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( activityPayload["status"].get<std::string>(),
                       "activity_timeline" );
    BOOST_REQUIRE_EQUAL( activityPayload["events"].size(), 1 );
    BOOST_CHECK_EQUAL( activityPayload["events"][0]["action"].get<std::string>(),
                       "pcbnew.InteractiveRoute" );

    AI_TOOL_INVOCATION_RESULT rulesResult = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_query_design_rules" ), wxS( "{}" ) ) );
    BOOST_REQUIRE( rulesResult.m_Allowed );
    nlohmann::json rulesPayload =
            nlohmann::json::parse( rulesResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( rulesPayload["status"].get<std::string>(), "design_rules" );
    BOOST_CHECK_EQUAL( rulesPayload["design_rules"]["clearance_min"].get<int>(), 150000 );
}


BOOST_AUTO_TEST_CASE( RunValidationToolRecordsSessionValidationResult )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) ) );
    BOOST_CHECK_EQUAL( validationResult.m_ErrorCode, wxString( wxS( "" ) ) );
    BOOST_REQUIRE( validationResult.m_Allowed );
    BOOST_CHECK( validationResult.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload =
            nlohmann::json::parse( validationResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "validation_completed" );
    BOOST_CHECK_EQUAL( payload["validation"]["scope"].get<std::string>(), "session" );
    BOOST_CHECK_EQUAL( payload["validation"]["level"].get<std::string>(), "geometry" );
    BOOST_CHECK_EQUAL( payload["validation"]["issue_count"].get<size_t>(), 0 );
    BOOST_REQUIRE_EQUAL( payload["operation_ids"].size(), 1 );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );

    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[0].m_Kind
                 == AI_SESSION_OPERATION_KIND::RunValidation );
}


BOOST_AUTO_TEST_CASE( RunValidationProjectsIssuesToShadowItemMetadata )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateTrackSegment,
              wxS( "{\"alias\":\"bad-track\",\"net\":\"GND\",\"layer\":\"F.Cu\","
                   "\"start\":{\"x\":100,\"y\":200},"
                   "\"end\":{\"x\":100,\"y\":200},\"width\":100000}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_track_segment(...)\"}" ) ) )
                           .m_Allowed );
    BOOST_REQUIRE( handler.ActiveSession() );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) ) );

    BOOST_REQUIRE( validationResult.m_Allowed );
    nlohmann::json payload =
            nlohmann::json::parse( validationResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["validation"]["issue_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( payload["validation"]["projected_overlay_count"].get<size_t>(), 1 );

    std::optional<AI_SESSION_HANDLE> handle =
            handler.ActiveSession()->ResolveAlias( wxS( "bad-track" ) );
    BOOST_REQUIRE( handle.has_value() );
    const AI_SHADOW_ITEM* item =
            handler.ActiveSession()->ShadowBoard().FindItem( *handle );
    BOOST_REQUIRE( item != nullptr );
    BOOST_CHECK_EQUAL( item->m_Metadata.at( wxS( "validation_status" ) ),
                       wxString( wxS( "error" ) ) );
    BOOST_CHECK_EQUAL( item->m_Metadata.at( wxS( "validation_issue_code" ) ),
                       wxString( wxS( "zero_length_track_segment" ) ) );
    BOOST_CHECK( item->m_Metadata.at( wxS( "validation_message" ) )
                         .Contains( wxS( "Track segment" ) ) );
}


BOOST_AUTO_TEST_CASE( RunValidationToolRecordsDrcLiteWarningInJournalAndPayload )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" ) ) );

    BOOST_REQUIRE( validationResult.m_Allowed );
    BOOST_CHECK( validationResult.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload =
            nlohmann::json::parse( validationResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "validation_completed" );
    BOOST_CHECK_EQUAL( payload["validation"]["level"].get<std::string>(), "drc_lite" );
    BOOST_CHECK( !payload["validation"]["accept_validation_sufficient"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["validation"]["accept_validation_reason"].get<std::string>(),
                       "native_validation_service_not_connected" );
    BOOST_REQUIRE_EQUAL( payload["validation"]["warnings"].size(), 1 );
    BOOST_CHECK( payload["validation"]["warnings"][0].get<std::string>().find( "native DRC" )
                 != std::string::npos );

    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    nlohmann::json recordPayload = nlohmann::json::parse(
            handler.ActiveSession()->Journal().Operations()[0].m_ResultJson.ToStdString() );
    BOOST_CHECK( !recordPayload["validation"]["accept_validation_sufficient"].get<bool>() );
    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations()[0].m_Warnings.size(),
                         1 );
    BOOST_CHECK( handler.ActiveSession()->Journal().Operations()[0].m_Warnings[0].Contains(
            wxS( "native DRC" ) ) );
}


BOOST_AUTO_TEST_CASE( RunValidationToolUsesInjectedNativeValidationService )
{
    RECORDING_SESSION_VALIDATION_SERVICE validationService;
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, nullptr, nullptr, nullptr,
                                          &validationService );

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" ) ) );

    BOOST_REQUIRE( validationResult.m_Allowed );
    BOOST_CHECK( validationResult.m_Executed );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_REQUIRE( handler.ActiveSession() );

    nlohmann::json payload =
            nlohmann::json::parse( validationResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["validation"]["native_backend"].get<std::string>(),
                       "recording" );
    BOOST_CHECK_EQUAL( payload["validation"]["status"].get<std::string>(),
                       "native_checked" );
    BOOST_CHECK_EQUAL( payload["validation"]["issue_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( payload["validation"]["warnings"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["validation"]["warnings"][0].get<std::string>(),
                       "recording native validation warning" );

    BOOST_REQUIRE_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 1 );
    const AI_SESSION_OPERATION_RECORD& record =
            handler.ActiveSession()->Journal().Operations().front();
    nlohmann::json recordPayload =
            nlohmann::json::parse( record.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( recordPayload["validation"]["native_backend"].get<std::string>(),
                       "recording" );
    BOOST_REQUIRE_EQUAL( record.m_Warnings.size(), 1 );
    BOOST_CHECK_EQUAL( record.m_Warnings[0],
                       wxString( wxS( "recording native validation warning" ) ) );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimeValidationFactsUseInjectedSessionValidationService )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    RECORDING_SESSION_VALIDATION_SERVICE validationService;
    RECORDING_SESSION_PREVIEW_SERVICE    previewService;

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_REQUIRE( runtime.Update( nextActionViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK_NE( validationService.m_LastSessionId, 0 );
    BOOST_CHECK( validationService.m_LastArgumentsJson.Contains( wxS( "drc_lite" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "recording" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "recording native validation warning" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 2 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "recording" ) ) );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimePreviewGateBlocksPublishWhenValidationFails )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    BLOCKING_SESSION_VALIDATION_SERVICE validationService;

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( nextActionViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "validation_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimePreviewGateBlocksPublishWhenRenderFails )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    BLOCKING_SESSION_PREVIEW_SERVICE previewService;

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    nullptr, &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( nextActionViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 1 );
    BOOST_CHECK_NE( previewService.m_LastSessionId, 0 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_RenderOutputsJson.Contains(
            wxS( "render_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RunValidationToolRejectsUnsupportedLevelWithoutJournalMutation )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"magic\"}" ) ) );

    BOOST_CHECK( !validationResult.m_Allowed );
    BOOST_CHECK( !validationResult.m_Executed );
    BOOST_CHECK_EQUAL( validationResult.m_ErrorCode,
                       wxString( wxS( "unsupported_validation_level" ) ) );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );
}


BOOST_AUTO_TEST_CASE( RunValidationToolRejectsMalformedGateWithoutJournalMutation )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT validationResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_validation" ),
                      wxS( "{\"scope\":\"session\",\"level\":\"geometry\","
                           "\"gate\":\"publish\"}" ) ) );

    BOOST_CHECK( !validationResult.m_Allowed );
    BOOST_CHECK( !validationResult.m_Executed );
    BOOST_CHECK_EQUAL( validationResult.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Journal().Operations().size(), 0 );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->Epoch(), 0 );
}


BOOST_AUTO_TEST_CASE( RunCellObservationOpsExposeCurrentEditorContext )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "python inspects current editor state" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QuerySelection, wxS( "{}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QueryViewport, wxS( "{}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::QueryActivityTimeline, wxS( "{}" ) } );

    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ) );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRichContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.query_selection();"
                           " session.query_viewport();"
                           " session.query_activity_timeline()\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 3 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["selected_objects"][0]["label"]
                               .get<std::string>(),
                       "U1.1" );
    BOOST_CHECK_EQUAL( payload["operation_results"][1]["viewport"]["zoom"].get<double>(),
                       3.5 );
    BOOST_CHECK_EQUAL( payload["operation_results"][2]["events"][0]["sequence"].get<int>(),
                       42 );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );

    BOOST_REQUIRE( handler.ActiveSession() );
    const auto& journal = handler.ActiveSession()->Journal().Operations();
    BOOST_REQUIRE_EQUAL( journal.size(), 3 );
    BOOST_CHECK( journal[0].m_Kind == AI_SESSION_OPERATION_KIND::QuerySelection );

    nlohmann::json selectionRecord =
            nlohmann::json::parse( journal[0].m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( selectionRecord["status"].get<std::string>(), "selection" );
    BOOST_CHECK_EQUAL(
            selectionRecord["selected_objects"][0]["label"].get<std::string>(),
            "U1.1" );
    BOOST_CHECK( payload["operation_results"][0]["journaled"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["operation_ids"][0].get<int>(),
                       1 );
}


BOOST_AUTO_TEST_CASE( RenderPreviewRejectsUnknownArgumentBeforePreviewService )
{
    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, nullptr, &previewService );

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT previewResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_render_preview" ),
                      wxS( "{\"unexpected\":true}" ) ) );

    BOOST_CHECK( !previewResult.m_Allowed );
    BOOST_CHECK( !previewResult.m_Executed );
    BOOST_CHECK_EQUAL( previewResult.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 0 );
}


BOOST_AUTO_TEST_CASE( RenderPreviewUsesInjectedSessionPreviewService )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "preview via" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"preview-via-0\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ),
            nullptr, &previewService );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-preview\"}" ) ) )
                           .m_Allowed );

    AI_TOOL_INVOCATION_RESULT previewResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_render_preview" ),
                      wxS( "{\"region\":{\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
                           "\"layer_mask\":[\"F.Cu\"],\"mode\":\"native\"}" ) ) );

    BOOST_REQUIRE( previewResult.m_Allowed );
    BOOST_CHECK( previewResult.m_Executed );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 2 );
    BOOST_REQUIRE_EQUAL( previewService.m_RenderedItemCounts.size(), 2 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[0], 1 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[1], 1 );
    BOOST_CHECK_EQUAL( previewService.m_LastSessionId,
                       handler.ActiveSession()->SessionId() );

    nlohmann::json args =
            nlohmann::json::parse( previewService.m_LastArgumentsJson.ToStdString() );
    BOOST_CHECK_EQUAL( args["mode"].get<std::string>(), "native" );

    nlohmann::json payload =
            nlohmann::json::parse( previewResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "preview_rendered" );
    BOOST_CHECK_EQUAL( payload["preview_id"].get<uint64_t>(), 77 );
    BOOST_CHECK_EQUAL( payload["rendered_item_count"].get<size_t>(), 1 );
    BOOST_CHECK( payload["native_preview"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_reject_session" ), wxS( "{}" ) ) )
                         .m_Allowed );
    BOOST_CHECK_EQUAL( previewService.m_ClearCount, 1 );
    BOOST_CHECK_EQUAL( previewService.m_LastClearedSessionId,
                       previewService.m_LastSessionId );
}


BOOST_AUTO_TEST_CASE( RunCellRenderPreviewUsesInjectedSessionPreviewService )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "preview from python" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"cell-preview-via-0\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RenderPreview,
              wxS( "{\"region\":{\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
                   "\"layer_mask\":[\"F.Cu\"],\"mode\":\"cell\"}" ) } );

    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ),
            nullptr, &previewService );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.render_preview(mode='cell')\","
                           "\"cell_id\":\"cell-preview-op\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 1 );
    BOOST_CHECK_EQUAL( previewService.m_LastSessionId,
                       handler.ActiveSession()->SessionId() );

    nlohmann::json args =
            nlohmann::json::parse( previewService.m_LastArgumentsJson.ToStdString() );
    BOOST_CHECK_EQUAL( args["mode"].get<std::string>(), "cell" );

    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_executed" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( payload["operation_results"].size(), 1 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["status"].get<std::string>(),
                       "preview_rendered" );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["preview_id"].get<uint64_t>(), 77 );
    BOOST_CHECK_EQUAL( payload["operation_results"][0]["rendered_item_count"].get<size_t>(),
                       1 );
    BOOST_CHECK( payload["operation_results"][0]["native_preview"].get<bool>() );
    BOOST_CHECK( !payload["operation_results"][0]["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RollbackRestoresLastPreviewAgainstCheckpointShadowBoard )
{
    AI_PYTHON_CELL_RESULT firstCell;
    firstCell.m_Ok = true;
    firstCell.m_StepLabel = wxS( "first preview item" );
    firstCell.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"restore-preview-a\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );

    AI_PYTHON_CELL_RESULT secondCell;
    secondCell.m_Ok = true;
    secondCell.m_StepLabel = wxS( "second preview item" );
    secondCell.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"restore-preview-b\",\"net\":\"GND\","
                   "\"position\":{\"x\":75,\"y\":100}}" ) } );

    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<QUEUED_PYTHON_WORKER>(
                    std::vector<AI_PYTHON_CELL_RESULT>{ firstCell, secondCell } ),
            nullptr, &previewService );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-preview-a\"}" ) ) )
                           .m_Executed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_render_preview" ), wxS( "{\"mode\":\"native\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT checkpointResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_checkpoint" ),
                      wxS( "{\"name\":\"stable-preview\"}" ) ) );
    BOOST_REQUIRE( checkpointResult.m_Allowed );

    const uint64_t checkpointId =
            nlohmann::json::parse( checkpointResult.m_ResultJson.ToStdString() )
                    ["checkpoint_id"].get<uint64_t>();

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\","
                           "\"cell_id\":\"cell-preview-b\"}" ) ) )
                           .m_Executed );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_render_preview" ), wxS( "{\"mode\":\"native\"}" ) ) )
                           .m_Executed );

    BOOST_REQUIRE_EQUAL( previewService.m_RenderedItemCounts.size(), 4 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[0], 1 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[1], 1 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[2], 2 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[3], 2 );

    AI_TOOL_INVOCATION_RESULT rollbackResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_rollback_to" ),
                      jsonText( { { "checkpoint_id", checkpointId } } ) ) );

    BOOST_REQUIRE( rollbackResult.m_Allowed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 1 );
    BOOST_CHECK_EQUAL( previewService.m_ClearCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.m_RenderedItemCounts.size(), 5 );
    BOOST_CHECK_EQUAL( previewService.m_RenderedItemCounts[4], 1 );

    nlohmann::json payload =
            nlohmann::json::parse( rollbackResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "rolled_back" );
    BOOST_CHECK( payload["preview_restored"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["preview"]["rendered_item_count"].get<size_t>(), 1 );
}


BOOST_AUTO_TEST_CASE( RunCellFailureAfterPreviewClearsPreviewOnRollback )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "preview then fail" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"rollback-preview-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":25,\"y\":50}}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RenderPreview,
              wxS( "{\"mode\":\"cell\"}" ) } );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::MoveItems,
              wxS( "{\"handles\":[\"missing-alias\"],\"delta\":{\"x\":1,\"y\":1}}" ) } );

    RECORDING_SESSION_PREVIEW_SERVICE previewService;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ),
            nullptr, &previewService );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...);"
                           " session.render_preview(); session.move_items(...)\","
                           "\"cell_id\":\"cell-preview-rollback\"}" ) ) );

    BOOST_REQUIRE( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK_EQUAL( handler.ActiveSession()->ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 1 );
    BOOST_CHECK_EQUAL( previewService.m_ClearCount, 1 );
    BOOST_CHECK_EQUAL( previewService.m_LastClearedSessionId,
                       previewService.m_LastSessionId );

    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "cell_failed" );
    BOOST_CHECK( payload["rolled_back"].get<bool>() );
    BOOST_CHECK( !payload["shadow_board_mutated"].get<bool>() );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( AcceptRejectsStaleBaseAndClosesOnMatchingBase )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ),
                      wxS( "{\"board_id\":\"pcb-a\",\"base_hash\":\"hash-a\"}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT staleResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_accept_session" ),
                      wxS( "{\"base_hash\":\"hash-b\"}" ) ) );

    BOOST_CHECK( !staleResult.m_Allowed );
    BOOST_CHECK_EQUAL( staleResult.m_ErrorCode, wxString( wxS( "stale_session" ) ) );
    BOOST_REQUIRE( handler.ActiveSession() );

    AI_TOOL_INVOCATION_RESULT acceptResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_accept_session" ),
                      wxS( "{\"base_hash\":\"hash-a\"}" ) ) );

    BOOST_CHECK( acceptResult.m_Allowed );
    nlohmann::json payload =
            nlohmann::json::parse( acceptResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "accepted" );
    BOOST_CHECK_EQUAL( payload["accept_replay"].get<std::string>(), "not_connected" );
    BOOST_CHECK( !handler.ActiveSession() );
}


BOOST_AUTO_TEST_CASE( AcceptRejectsChangedSelectionRevision )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ),
                      wxS( "{\"board_id\":\"pcb-a\",\"base_hash\":\"hash-a\"}" ) ) )
                           .m_Allowed );

    AI_PROVIDER_REQUEST changedRequest = requestWithContext();
    changedRequest.m_ContextVersion.m_SelectionRevision = 5;
    changedRequest.m_ContextSnapshot.m_Version = changedRequest.m_ContextVersion;
    changedRequest.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            changedRequest.m_ContextVersion;

    AI_TOOL_INVOCATION_RESULT conflictResult = handler.HandleToolCall(
            changedRequest,
            toolCall( wxS( "kisurf_accept_session" ),
                      wxS( "{\"base_hash\":\"hash-a\"}" ) ) );

    BOOST_CHECK( !conflictResult.m_Allowed );
    BOOST_CHECK_EQUAL( conflictResult.m_ErrorCode,
                       wxString( wxS( "selection_conflict" ) ) );
    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_CHECK( handler.ActiveSession()->Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( AcceptWithAdapterReplaysJournalBeforeClosingSession )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "accepted via" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::CreateVia,
              wxS( "{\"alias\":\"accepted-via\",\"net\":\"GND\","
                   "\"position\":{\"x\":11,\"y\":12}}" ) } );

    RECORDING_SESSION_ACCEPT_ADAPTER adapter;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ), &adapter );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.create_via(...)\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT acceptResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_accept_session" ),
                      wxS( "{\"base_hash\":\"doc=10;sel=2;view=4\"}" ) ) );

    BOOST_REQUIRE( acceptResult.m_Allowed );
    BOOST_CHECK( acceptResult.m_Executed );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationIds.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_OperationIds.front(), 1 );
    BOOST_CHECK( adapter.m_OperationKinds.front() == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( !handler.ActiveSession() );

    nlohmann::json payload =
            nlohmann::json::parse( acceptResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "accepted" );
    BOOST_CHECK_EQUAL( payload["accept_replay"].get<std::string>(), "applied" );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_CHECK( payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RejectClearsActiveSessionWithoutBoardMutation )
{
    AI_SESSION_TOOL_CALL_HANDLER handler;

    BOOST_CHECK( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                         .m_Allowed );

    AI_TOOL_INVOCATION_RESULT rejectResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_reject_session" ),
                      wxS( "{\"reason\":\"bad spacing\"}" ) ) );

    BOOST_CHECK( rejectResult.m_Allowed );
    BOOST_CHECK( !handler.ActiveSession() );

    nlohmann::json payload =
            nlohmann::json::parse( rejectResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "rejected" );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( AcceptWithAdapterReportsValidationOnlyReplayAsNotBoardMutated )
{
    AI_PYTHON_CELL_RESULT workerResult;
    workerResult.m_Ok = true;
    workerResult.m_StepLabel = wxS( "validation only" );
    workerResult.m_Operations.push_back(
            { AI_SESSION_OPERATION_KIND::RunValidation,
              wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) } );

    RECORDING_SESSION_ACCEPT_ADAPTER adapter;
    adapter.m_HasBoardChanges = false;
    AI_SESSION_TOOL_CALL_HANDLER handler(
            std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult ), &adapter );

    BOOST_REQUIRE( handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_run_cell" ),
                      wxS( "{\"cell_text\":\"session.run_validation()\"}" ) ) )
                           .m_Executed );

    AI_TOOL_INVOCATION_RESULT acceptResult = handler.HandleToolCall(
            requestWithContext(),
            toolCall( wxS( "kisurf_accept_session" ),
                      wxS( "{\"base_hash\":\"doc=10;sel=2;view=4\"}" ) ) );

    BOOST_REQUIRE( acceptResult.m_Allowed );
    nlohmann::json payload =
            nlohmann::json::parse( acceptResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "accepted" );
    BOOST_CHECK_EQUAL( payload["accept_replay"].get<std::string>(), "applied" );
    BOOST_CHECK( !payload["board_mutated"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["applied_operation_count"].get<size_t>(), 1 );
    BOOST_CHECK( !handler.ActiveSession() );
}


BOOST_AUTO_TEST_CASE( SessionTerminalActionsStopPerSessionPythonWorker )
{
    auto makeWorker = []()
    {
        AI_PYTHON_CELL_RESULT workerResult;
        workerResult.m_Ok = true;
        return std::make_unique<SCRIPTED_PYTHON_WORKER>( workerResult );
    };

    {
        auto workerOwner = makeWorker();
        SCRIPTED_PYTHON_WORKER* worker = workerOwner.get();
        AI_SESSION_TOOL_CALL_HANDLER handler( std::move( workerOwner ) );

        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_close_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_CHECK_EQUAL( worker->m_CancelCount, 1 );
        BOOST_CHECK_EQUAL( worker->m_HardKillCount, 0 );
    }

    {
        auto workerOwner = makeWorker();
        SCRIPTED_PYTHON_WORKER* worker = workerOwner.get();
        AI_SESSION_TOOL_CALL_HANDLER handler( std::move( workerOwner ) );

        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_reject_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_CHECK_EQUAL( worker->m_CancelCount, 1 );
        BOOST_CHECK_EQUAL( worker->m_HardKillCount, 0 );
    }

    {
        auto workerOwner = makeWorker();
        SCRIPTED_PYTHON_WORKER* worker = workerOwner.get();
        AI_SESSION_TOOL_CALL_HANDLER handler( std::move( workerOwner ) );

        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_cancel_session" ), wxS( "{}" ) ) )
                               .m_Allowed );
        BOOST_CHECK_EQUAL( worker->m_CancelCount, 1 );
        BOOST_CHECK_EQUAL( worker->m_HardKillCount, 0 );
    }

    {
        auto workerOwner = makeWorker();
        SCRIPTED_PYTHON_WORKER* worker = workerOwner.get();
        AI_SESSION_TOOL_CALL_HANDLER handler( std::move( workerOwner ) );

        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_open_session" ),
                          wxS( "{\"base_hash\":\"doc=10;sel=2;view=4\"}" ) ) )
                               .m_Allowed );
        BOOST_REQUIRE( handler.HandleToolCall(
                requestWithContext(),
                toolCall( wxS( "kisurf_accept_session" ),
                          wxS( "{\"base_hash\":\"doc=10;sel=2;view=4\"}" ) ) )
                               .m_Allowed );
        BOOST_CHECK_EQUAL( worker->m_CancelCount, 1 );
        BOOST_CHECK_EQUAL( worker->m_HardKillCount, 0 );
    }
}


BOOST_AUTO_TEST_SUITE_END()
