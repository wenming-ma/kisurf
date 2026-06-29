#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_chat_session_store.h>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_next_action_session_store.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <algorithm>
#include <memory>
#include <deque>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx
#include <wx/filefn.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/utils.h>

namespace
{
wxString uniquePanelPromptTracePath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "kst" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_panel_prompt_trace_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


wxString uniquePanelArtifactManifestPath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksa" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_panel_artifacts_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


wxString uniquePanelChatSessionDirectory( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksc" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_panel_chat_sessions_%s_%lu" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxDirExists( path.GetFullPath() ) )
        wxFileName::Rmdir( path.GetFullPath(), wxPATH_RMDIR_RECURSIVE );

    return path.GetFullPath();
}


wxString uniquePanelNextActionSessionDirectory( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksn" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_panel_next_action_sessions_%s_%lu" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxDirExists( path.GetFullPath() ) )
        wxFileName::Rmdir( path.GetFullPath(), wxPATH_RMDIR_RECURSIVE );

    return path.GetFullPath();
}


AI_PROVIDER_REQUEST sessionRequestWithContext()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 701;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextVersion.m_DocumentRevision = 10;
    request.m_ContextVersion.m_SelectionRevision = 2;
    request.m_ContextVersion.m_ViewRevision = 4;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;
    return request;
}


AI_TOOL_CALL_RECORD sessionToolCall( const wxString& aToolName,
                                     const wxString& aArguments )
{
    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 701;
    call.m_ToolCallId = wxS( "call_panel_session" );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = aArguments;
    return call;
}


void writePanelTextFile( const wxString& aPath, const wxString& aText )
{
    wxFFile file( aPath, wxS( "wb" ) );
    BOOST_REQUIRE( file.IsOpened() );
    file.Write( aText );
}


class CAPTURING_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "captured" );
        response.m_Body = aRequest.m_ContextSnapshot.AsPromptText();
        return response;
    }

    AI_PROVIDER_REQUEST m_LastRequest;
    int                 m_CallCount = 0;
};


class TOOL_CALL_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "tool call" );
        response.m_Body = wxS( "Tool call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_panel" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }

    int m_CallCount = 0;
};


class TWO_ROUND_TOOL_CALL_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "two round tool call" );

        if( aRequest.m_ToolResults.size() >= 2 )
        {
            response.m_Body = wxS( "Two tool results received." );
            return response;
        }

        const size_t round = aRequest.m_ToolResults.size() + 1;
        response.m_Body = wxString::Format( wxS( "Tool round %llu requested." ),
                                            static_cast<unsigned long long>( round ) );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxString::Format( wxS( "call_panel_%llu" ),
                                              static_cast<unsigned long long>( round ) );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
        response.m_ToolCalls.push_back( call );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SESSION_MUTATION_CHAT_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "session mutation chat" );

        if( aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Creating a via through the session runtime." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxS( "call_create_via" );
            call.m_ToolName = wxS( "kisurf_run_atomic_operation" );
            call.m_ArgumentsJson =
                    wxS( "{\"kind\":\"pcb.create_via\","
                         "\"arguments\":{\"alias\":\"chat-auto-via\","
                         "\"position\":{\"x\":25,\"y\":50}}}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        response.m_Body = wxS( "Placed the via." );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class RUNTIME_ACTIVITY_CAPTURE_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "runtime activity capture" );

        if( m_CallCount == 1 )
        {
            response.m_Body = wxS( "Tool call requested." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxS( "call_panel" );
            call.m_ToolName = wxS( "kisurf_run_action" );
            call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Tool result received." );
            return response;
        }

        m_LastRequest = aRequest;
        response.m_Body = wxS( "Captured later request." );
        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
};


class LARGE_TOOL_RESULT_PANEL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "large tool result provider" );

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Tool result received." );
            return response;
        }

        response.m_Body = wxS( "Tool call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_panel_large" );
        call.m_ToolName = wxS( "kisurf_run_cell" );
        call.m_ArgumentsJson = wxS( "{\"cell_id\":\"panel-large\"}" );
        response.m_ToolCalls.push_back( call );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class ERROR_AFTER_PANEL_TOOL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "panel recovery provider" );

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Title = wxS( "AI Provider Error" );
            response.m_Body = wxS( "Provider failed after panel tool execution." );
            response.m_ProviderTraceJson =
                    wxS( "{\"retry_history\":[{\"reason\":\"transient_gateway\","
                         "\"action\":\"failed_after_retry\"}]}" );
            return response;
        }

        response.m_Body = wxS( "Tool call before provider recovery failure." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_panel_recovery" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
        response.m_ToolCalls.push_back( call );
        return response;
    }

    int m_CallCount = 0;
};


class FAKE_PANEL_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = wxS( "common.Control.showAgentPanel" );
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "panel handler executed" );
        result.m_ResultJson = wxS( "{\"status\":\"panel-executed\"}" );
        return result;
    }

    int m_CallCount = 0;
};


class RECOVERABLE_PANEL_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = aToolCall.m_ToolName;
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "recoverable panel mutation" );
        result.m_ResultJson =
                wxS( "{\"status\":\"executed\","
                     "\"session_id\":\"panel-session-9\","
                     "\"checkpoint_id\":77,"
                     "\"session_journal\":{\"operations\":["
                     "{\"kind\":\"pcb.create_via\"}]}}" );
        return result;
    }

    int m_CallCount = 0;
};


class RECORDING_PANEL_RECOVERY_ACCEPT_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override
    {
        ++m_BeginCount;
        m_SessionOperationCount = aSession.Journal().Operations().size();
        aError.clear();
        return true;
    }

    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override
    {
        m_OperationKinds.push_back( aOperation.m_Kind );
        aError.clear();
        return true;
    }

    bool CommitTransaction( wxString& aError ) override
    {
        ++m_CommitCount;
        aError.clear();
        return true;
    }

    bool HasBoardChanges() const override { return true; }

    int                                    m_BeginCount = 0;
    int                                    m_CommitCount = 0;
    size_t                                 m_SessionOperationCount = 0;
    std::vector<AI_SESSION_OPERATION_KIND> m_OperationKinds;
};


class LARGE_PANEL_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = aToolCall.m_ToolName;
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "large panel result" );

        wxString payload;

        for( int i = 0; i < 5000; ++i )
            payload << wxS( "p" );

        result.m_ResultJson = wxS( "{\"stdout\":\"" ) + payload + wxS( "\"}" );
        m_LastResultJson = result.m_ResultJson;
        return result;
    }

    wxString m_LastResultJson;
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
        response.m_Title = wxS( "scripted next action" );

        if( m_Bodies.empty() )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
            return response;
        }

        response.m_Body = m_Bodies.front();
        m_Bodies.pop_front();
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    std::deque<wxString>             m_Bodies;
};


class REVIEW_TOOL_ERROR_AFTER_EXECUTED_PANEL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Title = wxS( "decision before panel next action recovery" );
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"panel_recovery_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Title = wxS( "review requests hidden mutation" );
                response.m_Body = wxS( "Need hidden mutation facts before failure." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId =
                        wxS( "call_panel_next_action_script_before_failure" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,"
                             "\"y\":2400000},\"net\":\"GND\",\"diameter\":600000,"
                             "\"drill\":300000,\"layer_pair\":{\"start\":\"F.Cu\","
                             "\"end\":\"B.Cu\"},\"alias\":"
                             "\"panel_next_action_before_failure\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Title = wxS( "AI Provider Error" );
            response.m_Body = wxS( "AI provider request failed after hidden mutation." );
            response.m_ProviderTraceJson =
                    wxS( "{\"retry_history\":[{\"reason\":\"transient_gateway\","
                         "\"action\":\"failed_after_retry\"}]}" );
            return response;
        }

        response.m_Title = wxS( "unexpected" );
        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override { m_BeginId = aPreviewId; }

    void ShowObject( uint64_t, const AI_OBJECT_REF& aObject ) override
    {
        m_Shown.push_back( aObject.m_Label );
    }

    void ShowOperation( uint64_t, const AI_SUGGESTION_OPERATION& aOperation ) override
    {
        m_Operations.push_back( aOperation );
    }

    void ClearPreview( uint64_t ) override {}

    uint64_t                             m_BeginId = 0;
    std::vector<wxString>                m_Shown;
    std::vector<AI_SUGGESTION_OPERATION> m_Operations;
};


class BLOCKING_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& ) override
    {
        ++m_RenderCount;
        m_LastSessionId = aSession.SessionId();

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "render_failed" );
        result.m_Message = wxS( "Native preview renderer failed." );
        result.m_ResultJson =
                wxS( "{\"status\":\"render_failed\",\"render_valid\":false}" );
        return result;
    }

    void ClearPreview( uint64_t ) override {}

    int      m_RenderCount = 0;
    uint64_t m_LastSessionId = 0;
};


class PASSING_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION&, const wxString& ) override
    {
        ++m_RenderCount;

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = true;
        result.m_PreviewId = 101;
        result.m_RenderedItemCount = 1;
        result.m_ResultJson =
                wxS( "{\"status\":\"preview_rendered\","
                     "\"render_valid\":true,"
                     "\"native_preview\":true}" );
        return result;
    }

    void ClearPreview( uint64_t ) override {}

    int m_RenderCount = 0;
};


class PASSING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION&, const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = true;
        result.m_ResultJson =
                wxS( "{\"validation\":{\"status\":\"validated\","
                     "\"issue_count\":0}}" );
        return result;
    }

    int m_RunCount = 0;
};


class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return true;
    }

    std::vector<wxString> m_Applied;
};


AI_CONTEXT_SNAPSHOT makeSuggestionContext( uint64_t aDocRevision = 1 )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = aDocRevision;
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return snapshot;
}


wxString viaDetails( int aX, int aY, const wxString& aNetName,
                     int aDiameter = 600000 )
{
    wxString details;
    details << wxS( "{\"kind\":\"via\",\"position\":{\"x\":" ) << aX
            << wxS( ",\"y\":" ) << aY << wxS( "},\"diameter\":" )
            << aDiameter << wxS( ",\"net_name\":\"" ) << aNetName << wxS( "\"}" );
    return details;
}


AI_OBJECT_REF viaRef( int aX, int aY, const wxString& aNetName = wxS( "GND" ) )
{
    return AI_OBJECT_REF( KIID(), PCB_VIA_T,
                          wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
                          viaDetails( aX, aY, aNetName ) );
}


AI_CONTEXT_SNAPSHOT makeViaNextActionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 9;
    snapshot.m_Version.m_ViewRevision = 1;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    snapshot.m_ToolState.m_ContextVersion = snapshot.m_Version;
    snapshot.m_VisibleObjects.push_back( viaRef( 100, 50 ) );
    snapshot.m_VisibleObjects.push_back( viaRef( 200, 50 ) );
    snapshot.m_VisibleObjects.push_back( viaRef( 300, 50 ) );
    return snapshot;
}


AI_CONTEXT_SNAPSHOT makeViewportBoundViaNextActionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot = makeViaNextActionContext();
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 1200, 2200 );
    snapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"viewport\":{\"center\":{\"x\":1000,\"y\":2000},"
                 "\"zoom\":3.5,\"width\":640,\"height\":480},"
                 "\"cursor_region\":{\"x\":1000,\"y\":2000,"
                 "\"width\":500,\"height\":500}}" );
    return snapshot;
}


AI_CONTEXT_SNAPSHOT makeViewportDriftedViaNextActionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot = makeViewportBoundViaNextActionContext();
    snapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"viewport\":{\"center\":{\"x\":1500,\"y\":2500},"
                 "\"zoom\":3.5,\"width\":640,\"height\":480},"
                 "\"cursor_region\":{\"x\":1500,\"y\":2500,"
                 "\"width\":500,\"height\":500}}" );
    return snapshot;
}


wxString panelTableStateJson()
{
    return wxS( "{\"tables\":[{\"id\":\"clearance.rules\","
                "\"title\":\"Clearance rules\","
                "\"focused_cell\":{\"row_id\":\"row.default\","
                "\"column_id\":\"clearance\"},"
                "\"columns\":[{\"id\":\"clearance\","
                "\"label\":\"Clearance\"}],\"rows\":["
                "{\"id\":\"row.default\",\"label\":\"Default\","
                "\"cells\":{\"clearance\":{\"value\":\"0.20 mm\"}}},"
                "{\"id\":\"row.power\",\"label\":\"Power\","
                "\"cells\":{\"clearance\":\"\"}},"
                "{\"id\":\"row.signal\",\"label\":\"Signal\","
                "\"cells\":{\"clearance\":\"\"}}]}]}" );
}


AI_CONTEXT_SNAPSHOT makePanelTableContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 14;
    snapshot.m_Version.m_ViewRevision = 2;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Unknown;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.rules.row.default.clearance" );
    panel.m_FocusedControlLabel = wxS( "Clearance" );
    panel.m_StateJson = panelTableStateJson();
    snapshot.m_PanelStates.push_back( panel );
    return snapshot;
}


AI_ACTIVITY_RECORD makeSuggestionActivity( uint64_t aSequence = 1 )
{
    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = aSequence;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    return activity;
}


AI_NEXT_ACTION_CONTEXT_VERSION nextActionContextForSuggestion(
        const AI_SUGGESTION_RECORD& aSuggestion )
{
    AI_NEXT_ACTION_CONTEXT_VERSION context;
    context.m_ContextVersion = aSuggestion.m_ContextVersion;
    return context;
}


AI_SUGGESTION_RECORD makeModelSuggestion( const wxString& aTitle = wxS( "Review U1.1" ) )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = aTitle;
    suggestion.m_Body = wxS( "Preview before edit." );
    suggestion.m_PreviewObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return suggestion;
}


AI_SUGGESTION_RECORD makePanelFillOperationSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Fill clearance column" );
    suggestion.m_Body = wxS( "Preview panel table fill before committing it." );
    suggestion.m_ContextKind = wxS( "panel" );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"panel_fill_column_preview\","
            "\"panel_id\":\"board_setup.clearance\","
            "\"table_id\":\"clearance.rules\","
            "\"column_id\":\"clearance\","
            "\"value\":\"0.20 mm\","
            "\"target_row_ids\":[\"row.power\",\"row.signal\"]}" );
    return suggestion;
}


AI_SUGGESTION_RECORD makeAnchorFocusOperationSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Preview anchor focus" );
    suggestion.m_Body = wxS( "Preview semantic anchor focus before routing." );
    suggestion.m_ContextKind = wxS( "routing" );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"anchor_focus_preview\","
            "\"anchor_id\":\"tool.routing.orthogonal.horizontal\","
            "\"position\":{\"x\":500,\"y\":200},"
            "\"focus_layer\":\"F.Cu\","
            "\"focus_net\":\"/GPIO\","
            "\"dim_unfocused_layers\":true}" );
    return suggestion;
}


AI_SUGGESTION_RECORD makeActionPreviewSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Preview action" );
    suggestion.m_Body = wxS( "Run this action only after acceptance." );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"action_preview\","
            "\"action\":\"common.Control.showAgentPanel\"}" );
    return suggestion;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentPanelModel )


BOOST_AUTO_TEST_CASE( EmptyInputCannotSend )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( !model.CanSend( wxEmptyString ) );
    BOOST_CHECK( !model.CanSend( wxS( "   " ) ) );
    BOOST_CHECK( model.CanSend( wxS( "inspect board" ) ) );
}


BOOST_AUTO_TEST_CASE( SendAppendsUserAndAgentMessages )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_RESPONSE response = model.SendUserText( wxS( "inspect board" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Role, wxString( wxS( "user" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Text, wxString( wxS( "inspect board" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 1 ).m_Role, wxString( wxS( "assistant" ) ) );
    BOOST_CHECK( model.Messages().at( 1 ).m_Text.Contains( wxS( "inspect board" ) ) );
}


BOOST_AUTO_TEST_CASE( PreparedChatRequestAppendsUserBeforeProviderRuns )
{
    auto provider = std::make_unique<CAPTURING_AI_PROVIDER>();
    CAPTURING_AI_PROVIDER* providerPtr = provider.get();
    AI_AGENT_PANEL_MODEL model( std::move( provider ) );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CHAT_REQUEST_STATE state = model.PrepareUserTextRequest(
            wxS( "inspect this board" ), AI_EDITOR_KIND::Pcb, snapshot );

    BOOST_CHECK_EQUAL( providerPtr->m_CallCount, 0 );
    BOOST_REQUIRE_EQUAL( model.Messages().size(), 1 );
    BOOST_CHECK_EQUAL( model.Messages().front().m_Role, wxString( wxS( "user" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().front().m_Text,
                       wxString( wxS( "inspect this board" ) ) );
    BOOST_CHECK( state.m_DocumentWriteOwned );
    BOOST_CHECK_EQUAL( state.m_Request.m_UserText,
                       wxString( wxS( "inspect this board" ) ) );

    AI_PROVIDER_RESPONSE response = model.ExecutePreparedChatRequest( state );
    BOOST_CHECK_EQUAL( providerPtr->m_CallCount, 1 );

    model.FinishPreparedChatRequest( std::move( state ), response );

    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( model.Messages().back().m_Role,
                       wxString( wxS( "assistant" ) ) );
}


BOOST_AUTO_TEST_CASE( PreparedChatRequestDoesNotDuplicateCurrentUserInRecentTurns )
{
    auto provider = std::make_unique<CAPTURING_AI_PROVIDER>();
    AI_AGENT_PANEL_MODEL model( std::move( provider ) );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    model.SendUserText( wxS( "prior constraint" ), AI_EDITOR_KIND::Pcb, snapshot );

    AI_CHAT_REQUEST_STATE state = model.PrepareUserTextRequest(
            wxS( "current task" ), AI_EDITOR_KIND::Pcb, snapshot );

    BOOST_REQUIRE_EQUAL( model.Messages().size(), 3 );
    BOOST_CHECK_EQUAL( model.Messages().back().m_Role, wxString( wxS( "user" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().back().m_Text, wxString( wxS( "current task" ) ) );

    bool sawRecentTurns = false;

    for( const AI_PROVIDER_INPUT_BLOCK& block : state.m_Request.m_ProviderInputBlocks )
    {
        if( block.m_Id != wxS( "chat.recent_turns" ) )
            continue;

        sawRecentTurns = true;
        BOOST_CHECK( block.m_Text.Contains( wxS( "prior constraint" ) ) );
        BOOST_CHECK( !block.m_Text.Contains( wxS( "current task" ) ) );
    }

    BOOST_CHECK( sawRecentTurns );
}


BOOST_AUTO_TEST_CASE( PreparedChatRequestStreamsRuntimeProgressEvents )
{
    auto* provider = new RUNTIME_ACTIVITY_CAPTURE_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CHAT_REQUEST_STATE state = model.PrepareUserTextRequest(
            wxS( "show panel" ), AI_EDITOR_KIND::Pcb, snapshot );

    std::vector<AI_RUNTIME_STREAM_EVENT> events;
    AI_PROVIDER_RESPONSE response =
            model.ExecutePreparedChatRequest(
                    state,
                    [&]( const AI_RUNTIME_STREAM_EVENT& aEvent )
                    {
                        events.push_back( aEvent );
                    } );

    BOOST_CHECK_EQUAL( response.m_Body,
                       wxString( wxS( "Tool result received." ) ) );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );

    const auto toolStarted = std::find_if(
            events.begin(), events.end(),
            []( const AI_RUNTIME_STREAM_EVENT& aEvent )
            {
                return aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::ToolCallStarted;
            } );

    BOOST_REQUIRE( toolStarted != events.end() );
    BOOST_CHECK_EQUAL( toolStarted->m_ToolCall.m_ToolName,
                       wxString( wxS( "kisurf_run_action" ) ) );
    BOOST_CHECK_EQUAL( static_cast<int>( events.back().m_Kind ),
                       static_cast<int>( AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse ) );

    model.FinishPreparedChatRequest( std::move( state ), response );
    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( model.Messages().back().m_Text,
                       wxString( wxS( "Tool result received." ) ) );
}


BOOST_AUTO_TEST_CASE( StartNewChatCreatesModelConversationBoundary )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    const uint64_t firstSession = model.ActiveChatSessionId();

    model.SendUserText( wxS( "old raw transcript should stay archived" ),
                        AI_EDITOR_KIND::Pcb );

    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ConversationId, firstSession );

    AI_ACTIVITY_RECORD oldActivity;
    oldActivity.m_ActionName = wxS( "old.chat.tool" );
    oldActivity.m_Message = wxS( "OLD_ACTIVITY_NEEDLE" );
    model.RecordActivity( oldActivity );

    model.StartNewChat();

    BOOST_CHECK( model.Messages().empty() );
    BOOST_CHECK_GT( model.ActiveChatSessionId(), firstSession );

    model.SendUserText( wxS( "new task" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ConversationId,
                       model.ActiveChatSessionId() );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "old raw transcript" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "OLD_ACTIVITY_NEEDLE" ) ) );
}


BOOST_AUTO_TEST_CASE( StartNewChatExpiresActiveNextActionPreviewBoundary )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );

    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    model.StartNewChat();

    std::optional<AI_SUGGESTION_RECORD> stored =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK( !model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( StartNewChatCancelsActiveExecutionSessionBoundary )
{
    AI_AGENT_PANEL_MODEL        model( std::make_unique<AI_STUB_PROVIDER>() );
    AI_SESSION_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    BOOST_REQUIRE( handler.HandleToolCall(
            sessionRequestWithContext(),
            sessionToolCall( wxS( "kisurf_open_session" ), wxS( "{}" ) ) )
                           .m_Allowed );
    BOOST_REQUIRE( handler.HandleToolCall(
            sessionRequestWithContext(),
            sessionToolCall( wxS( "kisurf_run_atomic_operation" ),
                             wxS( "{\"kind\":\"pcb.create_via\","
                                  "\"arguments\":{\"alias\":\"new-chat-via\","
                                  "\"position\":{\"x\":25,\"y\":50}}}" ) ) )
                           .m_Allowed );

    BOOST_REQUIRE( handler.ActiveSession() );
    BOOST_REQUIRE( handler.HasPendingSessionPreview() );

    model.StartNewChat();

    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( !handler.HasPendingSessionPreview() );
}


BOOST_AUTO_TEST_CASE( SendUserTextKeepsCompletedChatExecutionSessionPendingAcceptance )
{
    auto* provider = new SESSION_MUTATION_CHAT_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    RECORDING_PANEL_RECOVERY_ACCEPT_ADAPTER acceptAdapter;
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &acceptAdapter );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 10;
    snapshot.m_Version.m_SelectionRevision = 2;
    snapshot.m_Version.m_ViewRevision = 4;

    AI_PROVIDER_RESPONSE response =
            model.SendUserText( wxS( "place one via" ), AI_EDITOR_KIND::Pcb,
                                snapshot );

    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Placed the via." ) ) );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK_EQUAL( acceptAdapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( acceptAdapter.m_CommitCount, 0 );
    BOOST_CHECK( acceptAdapter.m_OperationKinds.empty() );
    BOOST_CHECK( handler.ActiveSession() );
    BOOST_CHECK( handler.HasPendingSessionPreview() );

    const std::vector<AI_ACTIVITY_RECORD> activity = model.ActivityRecords();
    BOOST_CHECK(
            std::none_of( activity.begin(), activity.end(),
                          []( const AI_ACTIVITY_RECORD& aRecord )
                          {
                              return aRecord.m_ToolCallId
                                             == wxS( "chat_session_auto_accept" );
                          } ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextIncludesCurrentSessionRecentTurns )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    model.SendUserText( wxS( "remember that net USB_DP needs 0.20mm clearance" ),
                        AI_EDITOR_KIND::Pcb );

    model.SendUserText( wxS( "what did I just tell you?" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "Previous chat turns" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "USB_DP needs 0.20mm clearance" ) ) );

    model.StartNewChat();
    model.SendUserText( wxS( "new unrelated task" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "USB_DP needs 0.20mm clearance" ) ) );
}


BOOST_AUTO_TEST_CASE( StartNewChatArchivesPreviousTranscriptArtifact )
{
    wxString path = uniquePanelArtifactManifestPath( wxS( "chat_transcript" ) );

    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetArtifactStore( &artifactStore );
    const uint64_t conversationId = model.ActiveChatSessionId();

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-chat" );
    snapshot.m_DocumentId = wxS( "board-chat" );

    model.SendUserText( wxS( "archive this design note" ), AI_EDITOR_KIND::Pcb,
                        snapshot );

    model.StartNewChat();

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind,
                       wxString( wxS( "chat_transcript" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId,
                       wxString( wxS( "project-chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId,
                       wxString( wxS( "board-chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_Source, wxString( wxS( "chat.new" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_RetentionClass,
                       wxString( wxS( "session_archive" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxString::Format( wxS( "\"conversation_id\":%llu" ),
                              static_cast<unsigned long long>( conversationId ) ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxS( "\"message_count\":2" ) ) );

    wxString payload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              payload, error ) );
    BOOST_CHECK( payload.Contains( wxS( "archive this design note" ) ) );
    BOOST_CHECK( payload.Contains( wxS( "\"role\":\"user\"" ) ) );
    BOOST_CHECK( payload.Contains( wxS( "\"role\":\"assistant\"" ) ) );

    model.StartNewChat();

    BOOST_CHECK_EQUAL( artifactStore.LoadAll( error ).size(), 1 );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ChatSessionStoreWritesCurrentSessionJsonFile )
{
    wxString directory = uniquePanelChatSessionDirectory( wxS( "current_json" ) );

    AI_CHAT_SESSION_STORE store( directory );
    AI_AGENT_PANEL_MODEL  model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetChatSessionStore( &store );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-current-chat" );
    snapshot.m_DocumentId = wxS( "board-current-chat" );

    model.SendUserText( wxS( "remember current session" ), AI_EDITOR_KIND::Pcb,
                        snapshot );

    wxString error;
    AI_CHAT_SESSION_RECORD firstSession =
            store.LoadSession( model.ActiveChatSessionId(), error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( wxFileExists( store.SessionPath( model.ActiveChatSessionId() ) ) );
    BOOST_CHECK_EQUAL( firstSession.m_ConversationId, model.ActiveChatSessionId() );
    BOOST_CHECK_EQUAL( firstSession.m_ProjectId,
                       wxString( wxS( "project-current-chat" ) ) );
    BOOST_CHECK_EQUAL( firstSession.m_DocumentId,
                       wxString( wxS( "board-current-chat" ) ) );
    BOOST_REQUIRE_EQUAL( firstSession.m_Messages.size(), 2 );
    BOOST_CHECK_EQUAL( firstSession.m_Messages.front().m_Role,
                       wxString( wxS( "user" ) ) );
    BOOST_CHECK( firstSession.m_Messages.front().m_Text.Contains(
            wxS( "remember current session" ) ) );

    const wxString firstPath = store.SessionPath( model.ActiveChatSessionId() );
    model.SendUserText( wxS( "append to same file" ), AI_EDITOR_KIND::Pcb,
                        snapshot );

    AI_CHAT_SESSION_RECORD appended =
            store.LoadSession( model.ActiveChatSessionId(), error );

    BOOST_CHECK_EQUAL( store.SessionPath( model.ActiveChatSessionId() ), firstPath );
    BOOST_REQUIRE_EQUAL( appended.m_Messages.size(), 4 );
    BOOST_CHECK( appended.m_Messages.at( 2 ).m_Text.Contains(
            wxS( "append to same file" ) ) );

    const uint64_t firstConversationId = model.ActiveChatSessionId();
    model.StartNewChat();
    model.SendUserText( wxS( "new chat file" ), AI_EDITOR_KIND::Pcb, snapshot );

    BOOST_CHECK_NE( store.SessionPath( model.ActiveChatSessionId() ), firstPath );
    BOOST_CHECK( wxFileExists( firstPath ) );
    BOOST_CHECK( wxFileExists( store.SessionPath( model.ActiveChatSessionId() ) ) );

    AI_CHAT_SESSION_RECORD previous = store.LoadSession( firstConversationId, error );
    AI_CHAT_SESSION_RECORD current =
            store.LoadSession( model.ActiveChatSessionId(), error );

    BOOST_REQUIRE_EQUAL( previous.m_Messages.size(), 4 );
    BOOST_REQUIRE_EQUAL( current.m_Messages.size(), 2 );
    BOOST_CHECK( current.m_Messages.front().m_Text.Contains(
            wxS( "new chat file" ) ) );

    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( PreparedChatRequestPersistsUserMessageBeforeProviderRuns )
{
    wxString directory =
            uniquePanelChatSessionDirectory( wxS( "prepared_user" ) );

    AI_CHAT_SESSION_STORE store( directory );
    AI_AGENT_PANEL_MODEL  model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetChatSessionStore( &store );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-prepared-chat" );
    snapshot.m_DocumentId = wxS( "board-prepared-chat" );

    AI_CHAT_REQUEST_STATE state = model.PrepareUserTextRequest(
            wxS( "persist before provider runs" ), AI_EDITOR_KIND::Pcb,
            snapshot );

    wxString error;
    AI_CHAT_SESSION_RECORD session =
            store.LoadSession( model.ActiveChatSessionId(), error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_REQUIRE_EQUAL( session.m_Messages.size(), 1 );
    BOOST_CHECK_EQUAL( session.m_Messages.front().m_Role,
                       wxString( wxS( "user" ) ) );
    BOOST_CHECK( session.m_Messages.front().m_Text.Contains(
            wxS( "persist before provider runs" ) ) );
    BOOST_CHECK_EQUAL( session.m_ProjectId,
                       wxString( wxS( "project-prepared-chat" ) ) );
    BOOST_CHECK_EQUAL( session.m_DocumentId,
                       wxString( wxS( "board-prepared-chat" ) ) );

    AI_PROVIDER_RESPONSE response = model.ExecutePreparedChatRequest( state );
    model.FinishPreparedChatRequest( std::move( state ), std::move( response ) );

    AI_CHAT_SESSION_RECORD finished =
            store.LoadSession( model.ActiveChatSessionId(), error );
    BOOST_CHECK( error.IsEmpty() );
    BOOST_REQUIRE_EQUAL( finished.m_Messages.size(), 2 );
    BOOST_CHECK_EQUAL( finished.m_Messages.back().m_Role,
                       wxString( wxS( "assistant" ) ) );

    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( ChatSessionStoreStartsAfterExistingSessionFiles )
{
    wxString directory = uniquePanelChatSessionDirectory( wxS( "existing_json" ) );

    AI_CHAT_SESSION_STORE store( directory );

    AI_CHAT_SESSION_RECORD existing;
    existing.m_ConversationId = 1;
    existing.m_ProjectId = wxS( "project-existing" );
    existing.m_DocumentId = wxS( "board-existing" );
    existing.m_Messages.push_back(
            { wxS( "user" ), wxS( "DO_NOT_OVERWRITE_EXISTING_SESSION" ) } );

    wxString error;
    BOOST_REQUIRE( store.WriteSession( existing, error ) );
    BOOST_CHECK( error.IsEmpty() );

    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetChatSessionStore( &store );

    BOOST_CHECK_EQUAL( model.ActiveChatSessionId(), 2 );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-new" );
    snapshot.m_DocumentId = wxS( "board-new" );

    model.SendUserText( wxS( "start without overwriting old chat" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_CHAT_SESSION_RECORD previous = store.LoadSession( 1, error );
    BOOST_CHECK( error.IsEmpty() );
    BOOST_REQUIRE_EQUAL( previous.m_Messages.size(), 1 );
    BOOST_CHECK( previous.m_Messages.front().m_Text.Contains(
            wxS( "DO_NOT_OVERWRITE_EXISTING_SESSION" ) ) );

    AI_CHAT_SESSION_RECORD current =
            store.LoadSession( model.ActiveChatSessionId(), error );
    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK_EQUAL( current.m_ConversationId, 2 );
    BOOST_REQUIRE_EQUAL( current.m_Messages.size(), 2 );
    BOOST_CHECK( current.m_Messages.front().m_Text.Contains(
            wxS( "start without overwriting old chat" ) ) );

    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( ChatSessionStoreWritesToolCallsInCurrentSessionJsonFile )
{
    wxString directory = uniquePanelChatSessionDirectory( wxS( "tool_calls" ) );

    AI_CHAT_SESSION_STORE store( directory );
    AI_AGENT_PANEL_MODEL  model( std::make_unique<TWO_ROUND_TOOL_CALL_AI_PROVIDER>() );
    model.SetChatSessionStore( &store );

    model.SendUserText( wxS( "run the visible tool workflow" ), AI_EDITOR_KIND::Pcb );

    wxFFile file( store.SessionPath( model.ActiveChatSessionId() ), wxS( "rb" ) );
    BOOST_REQUIRE( file.IsOpened() );

    wxString content;
    BOOST_REQUIRE( file.ReadAll( &content, wxConvUTF8 ) );

    BOOST_CHECK( content.Contains( wxS( "\"tool_calls\"" ) ) );
    BOOST_CHECK( content.Contains( wxS( "call_panel_1" ) ) );
    BOOST_CHECK( content.Contains( wxS( "call_panel_2" ) ) );
    BOOST_CHECK( content.Contains( wxS( "no_tool_handler" ) ) );

    file.Close();
    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( SendUserTextIncludesRecentToolCallSummary )
{
    AI_AGENT_PANEL_MODEL model(
            std::make_unique<TWO_ROUND_TOOL_CALL_AI_PROVIDER>() );

    model.SendUserText( wxS( "run the panel action twice" ), AI_EDITOR_KIND::Pcb );

    auto* capturingProvider = new CAPTURING_AI_PROVIDER();
    model.SetProvider( std::unique_ptr<AI_PROVIDER>( capturingProvider ) );

    model.SendUserText( wxS( "what tools did you just use?" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( capturingProvider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "Previous chat tool calls" ) ) );
    BOOST_CHECK( capturingProvider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "call_panel_1" ) ) );
    BOOST_CHECK( capturingProvider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "call_panel_2" ) ) );
    BOOST_CHECK( capturingProvider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "kisurf_run_action" ) ) );
    BOOST_CHECK( capturingProvider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "no_tool_handler" ) ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextDropsOlderChatTurnsFromProviderInput )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    model.SendUserText( wxS( "EARLIEST_CONTEXT_NEEDLE keep JTAG away from antenna" ),
                        AI_EDITOR_KIND::Pcb );

    for( int i = 0; i < 8; ++i )
    {
        model.SendUserText(
                wxString::Format( wxS( "intermediate chat turn %d" ), i ),
                AI_EDITOR_KIND::Pcb );
    }

    model.SendUserText( wxS( "RECENT_CONTEXT_NEEDLE keep this visible" ),
                        AI_EDITOR_KIND::Pcb );

    model.SendUserText( wxS( "continue from the earlier constraints" ),
                        AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "Previous chat turns" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "RECENT_CONTEXT_NEEDLE" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "Earlier chat summary" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "EARLIEST_CONTEXT_NEEDLE" ) ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextTracesTruncatedChatTurnsInProviderInput )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    model.SendUserText( wxS( "older message one should be omitted" ),
                        AI_EDITOR_KIND::Pcb );
    model.SendUserText( wxS( "older message two stays near boundary" ),
                        AI_EDITOR_KIND::Pcb );

    wxString longText = wxS( "LONG_CONTEXT_HEAD " );

    for( int i = 0; i < 1600; ++i )
        longText << wxS( "x" );

    longText << wxS( " LONG_CONTEXT_TAIL_SHOULD_NOT_BE_SENT" );

    model.SendUserText( longText, AI_EDITOR_KIND::Pcb );

    for( int i = 0; i < 2; ++i )
    {
        model.SendUserText(
                wxString::Format( wxS( "short recent turn %d" ), i ),
                AI_EDITOR_KIND::Pcb );
    }

    model.SendUserText( wxS( "continue after long context" ),
                        AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "LONG_CONTEXT_HEAD" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "[truncated chat turn]" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "LONG_CONTEXT_TAIL_SHOULD_NOT_BE_SENT" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "truncated_chat_turn_count" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "older_message_count" ) ) );
}


BOOST_AUTO_TEST_CASE( ReloadDefaultProvidersPreservesPanelModelState )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    model.SendUserText( wxS( "inspect board" ), AI_EDITOR_KIND::Pcb );

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    AI_ACTIVITY_RECORD recorded = model.RecordActivity( activity );

    AI_AGENT_WORKSPACE_CONTEXT_STATE routingState;
    routingState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;
    routingState.m_Title = wxS( "Routing" );
    routingState.m_StateJson = wxS( "{\"net\":\"GND\"}" );
    routingState.m_LastActivitySequence = recorded.m_Sequence;
    model.SaveWorkspaceContextState( routingState );
    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );

    model.ReloadDefaultProviders();

    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_REQUIRE_EQUAL( model.ActivityRecords().size(), 1 );
    BOOST_CHECK_EQUAL( model.ActivityRecords().front().m_ActionName,
                       wxString( wxS( "common.Interactive.selected" ) ) );
    BOOST_CHECK( model.ActiveWorkspaceContext() == AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );
    BOOST_CHECK_EQUAL( model.ActiveWorkspaceContextState().m_StateJson,
                       wxString( wxS( "{\"net\":\"GND\"}" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelModelUsesDefaultPromptTraceStorePath )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK_EQUAL( model.PromptTraceStorePath(),
                       AI_PROMPT_TRACE_STORE::DefaultPath() );
}


BOOST_AUTO_TEST_CASE( PanelModelUsesDefaultMemoryStorePath )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK_EQUAL( model.MemoryStorePath(), AI_MEMORY_STORE::DefaultPath() );
}


BOOST_AUTO_TEST_CASE( PanelModelUsesDefaultArtifactStorePath )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK_EQUAL( model.ArtifactStorePath(),
                       AI_ARTIFACT_STORE::DefaultManifestPath() );
}


BOOST_AUTO_TEST_CASE( SendUserTextRetrievesDurableMemoryForCurrentDocument )
{
    wxString path = uniquePanelPromptTracePath( wxS( "memory_retrieval" ) );

    AI_MEMORY_STORE memoryStore( path );
    wxString        error;

    AI_MEMORY_RECORD matching;
    matching.m_Id = wxS( "rule-usb" );
    matching.m_ProjectId = wxS( "project-a" );
    matching.m_DocumentId = wxS( "board-1" );
    matching.m_Type = wxS( "rule_memory" );
    matching.m_Text = wxS( "USB differential pair clearance is 0.20 mm" );
    matching.m_Source = wxS( "design_rules" );
    matching.m_ProvenanceJson = wxS( "{\"rule_id\":\"usb-clearance\"}" );
    matching.m_AcceptanceState = wxS( "accepted" );
    matching.m_TrustLevel = 90;
    matching.m_Sequence = 10;
    BOOST_REQUIRE( memoryStore.Append( matching, error ) );

    AI_MEMORY_RECORD wrongDocument = matching;
    wrongDocument.m_Id = wxS( "wrong-document" );
    wrongDocument.m_DocumentId = wxS( "board-2" );
    wrongDocument.m_Text = wxS( "WRONG_DOCUMENT_NEEDLE" );
    wrongDocument.m_Sequence = 11;
    BOOST_REQUIRE( memoryStore.Append( wrongDocument, error ) );

    AI_MEMORY_RECORD lowTrust = matching;
    lowTrust.m_Id = wxS( "low-trust" );
    lowTrust.m_Text = wxS( "LOW_TRUST_NEEDLE" );
    lowTrust.m_TrustLevel = 20;
    lowTrust.m_Sequence = 12;
    BOOST_REQUIRE( memoryStore.Append( lowTrust, error ) );

    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    model.SetMemoryStore( &memoryStore );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );

    model.SendUserText( wxS( "route USB pair" ), AI_EDITOR_KIND::Pcb, snapshot );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "USB differential pair clearance" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "rule-usb" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "design_rules" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "WRONG_DOCUMENT_NEEDLE" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "LOW_TRUST_NEEDLE" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( SendUserTextRetrievesLocalTextMemoryForCurrentDocument )
{
    AI_LOCAL_TEXT_MEMORY_INDEX index;

    AI_LOCAL_TEXT_MEMORY_RECORD matching;
    matching.m_Id = wxS( "local-file:usb-routing" );
    matching.m_ProjectId = wxS( "project-a" );
    matching.m_DocumentId = wxS( "board-1" );
    matching.m_AgentKind = wxS( "shared" );
    matching.m_Type = wxS( "project_research" );
    matching.m_Text =
            wxS( "LOCAL_TEXT_RETRIEVAL_NEEDLE USB routing should keep DP/DM "
                 "parallel through the connector escape." );
    matching.m_Source = wxS( "local_file" );
    matching.m_ProvenanceJson = wxS( "{\"kind\":\"local_text_file\"}" );
    matching.m_AcceptanceState = wxS( "accepted" );
    matching.m_TrustLevel = 80;
    matching.m_Sequence = 30;
    index.AddRecord( matching );

    AI_LOCAL_TEXT_MEMORY_RECORD wrongDocument = matching;
    wrongDocument.m_Id = wxS( "local-file:wrong-document" );
    wrongDocument.m_DocumentId = wxS( "board-2" );
    wrongDocument.m_Text = wxS( "LOCAL_TEXT_WRONG_DOCUMENT_NEEDLE USB routing" );
    wrongDocument.m_Sequence = 31;
    index.AddRecord( wrongDocument );

    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    model.SetLocalTextMemoryIndex( &index );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );

    model.SendUserText( wxS( "route USB DP DM escape" ), AI_EDITOR_KIND::Pcb,
                        snapshot );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "LOCAL_TEXT_RETRIEVAL_NEEDLE" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "local-file:usb-routing" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "local_text_file" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "LOCAL_TEXT_WRONG_DOCUMENT_NEEDLE" ) ) );
}


BOOST_AUTO_TEST_CASE( LoadResearchFolderFeedsDefaultLocalTextRetrieval )
{
    wxString root = wxFileName::CreateTempFileName( wxS( "kisurf_panel_research" ) );
    wxRemoveFile( root );
    BOOST_REQUIRE( wxFileName::Mkdir( root ) );

    wxFileName researchFile( root, wxS( "routing-notes.md" ) );
    writePanelTextFile( researchFile.GetFullPath(),
                        wxS( "PANEL_RESEARCH_FOLDER_NEEDLE route USB pair with "
                             "matched 45 degree breakout." ) );

    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    wxString error;
    BOOST_REQUIRE( model.LoadLocalTextResearchDirectory(
            root,
            wxS( "project-a" ),
            wxS( "board-1" ),
            error ) );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );

    model.SendUserText( wxS( "route USB breakout" ), AI_EDITOR_KIND::Pcb,
                        snapshot );

    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "PANEL_RESEARCH_FOLDER_NEEDLE" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            wxS( "research_folder" ) ) );

    wxFileName::Rmdir( root, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( ConfiguredResearchFolderReloadsBeforeChatRequest )
{
    wxString root = wxFileName::CreateTempFileName(
            wxS( "kisurf_panel_research_reload" ) );
    wxRemoveFile( root );
    BOOST_REQUIRE( wxFileName::Mkdir( root ) );

    wxFileName firstFile( root, wxS( "initial.md" ) );
    writePanelTextFile( firstFile.GetFullPath(),
                        wxS( "PANEL_RESEARCH_INITIAL_NEEDLE route USB pair." ) );

    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    model.SetLocalTextResearchDirectory( root );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );

    model.SendUserText( wxS( "route USB pair" ), AI_EDITOR_KIND::Pcb, snapshot );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "PANEL_RESEARCH_INITIAL_NEEDLE" ) ) );

    wxFileName secondFile( root, wxS( "updated.md" ) );
    writePanelTextFile( secondFile.GetFullPath(),
                        wxS( "PANEL_RESEARCH_AUTO_RELOAD_NEEDLE keep skew low." ) );

    model.SendUserText( wxS( "keep USB skew low" ), AI_EDITOR_KIND::Pcb, snapshot );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "PANEL_RESEARCH_AUTO_RELOAD_NEEDLE" ) ) );

    wxFileName::Rmdir( root, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( PanelModelExposesConfiguredResearchDirectoryForAsyncNextActionWorker )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<CAPTURING_AI_PROVIDER>() );

    model.SetLocalTextResearchDirectory( wxS( "  C:/tmp/kisurf-research  " ) );

    BOOST_CHECK_EQUAL( model.LocalTextResearchDirectory(),
                       wxString( wxS( "C:/tmp/kisurf-research" ) ) );
}


BOOST_AUTO_TEST_CASE( ChatRuntimeWritesScriptOutputArtifactsThroughPanelModelStore )
{
    wxString path = uniquePanelArtifactManifestPath( wxS( "chat_script_output" ) );

    auto* provider = new LARGE_TOOL_RESULT_PANEL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    LARGE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );
    model.SetArtifactStore( &artifactStore );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );

    model.SendUserText( wxS( "run verbose script" ), AI_EDITOR_KIND::Pcb, snapshot );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind, wxString( wxS( "script_output" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_Source, wxString( wxS( "kisurf_run_cell" ) ) );

    BOOST_REQUIRE_GE( provider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.back().m_ToolResults.size(), 1 );
    nlohmann::json compressed = nlohmann::json::parse(
            provider->m_Requests.back().m_ToolResults.front().m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "script_output" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["uri"].get<std::string>(),
                       artifacts.front().m_Uri.ToStdString() );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK_EQUAL( archivedPayload, handler.m_LastResultJson );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelExposesLatestChatProviderRecoveryPolicy )
{
    wxString path = uniquePanelArtifactManifestPath( wxS( "provider_recovery" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_POLICY policy =
            model.LatestChatProviderRecoveryPolicy();

    BOOST_CHECK( policy.m_Available );
    BOOST_CHECK( !policy.m_BlindToolReplayAllowed );
    BOOST_CHECK( policy.m_CheckpointOrJournalResumeRequired );
    BOOST_CHECK_EQUAL( policy.m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( policy.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( policy.m_Reason,
                       wxString( wxS( "post_side_effect_ambiguity" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains( wxS( "panel-session-9" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains( wxS( "checkpoint_id" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsChatProviderRecoveryResumePacket )
{
    wxString path =
            uniquePanelArtifactManifestPath( wxS( "provider_recovery_resume" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_RESUME_PACKET packet =
            model.LatestChatProviderRecoveryResumePacket();

    BOOST_CHECK( packet.m_Available );
    BOOST_CHECK( !packet.m_BlindToolReplayAllowed );
    BOOST_CHECK( packet.m_CheckpointOrJournalResumeRequired );
    BOOST_CHECK_EQUAL( packet.m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( packet.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( packet.m_ResumeAction,
                       wxString( wxS( "resume_from_checkpoint_or_journal" ) ) );
    BOOST_CHECK( packet.m_ArtifactUri.StartsWith(
            wxS( "kisurf-artifact://provider-recovery/" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains(
            wxS( "kisurf.ai.provider_recovery_resume" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains(
            wxS( "\"blind_tool_replay_allowed\":false" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains( wxS( "panel-session-9" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains( wxS( "session_journal" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsChatProviderRecoveryResumePlan )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_resume_plan" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            model.LatestChatProviderRecoveryResumePlan();

    BOOST_CHECK( plan.m_Available );
    BOOST_CHECK( !plan.m_BlindToolReplayAllowed );
    BOOST_CHECK( plan.m_UserReviewRequired );
    BOOST_CHECK( plan.m_CheckpointOrJournalReplayRequired );
    BOOST_CHECK_EQUAL( plan.m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( plan.m_RequestId, 1 );
    BOOST_CHECK( plan.m_PlanJson.Contains(
            wxS( "kisurf.ai.provider_recovery_plan" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains(
            wxS( "\"blind_tool_replay_allowed\":false" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains(
            wxS( "\"checkpoint_or_journal_replay_required\":true" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains(
            wxS( "verify_live_document_version_matches_recovery_basis" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains( wxS( "replay_candidates" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains( wxS( "panel-session-9" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( plan.m_PlanJson.Contains( wxS( "session_journal" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelPreflightsChatProviderRecoveryAgainstDocumentRevision )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_preflight" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT staleContext;
    staleContext.m_DocumentRevision = 41;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT stale =
            model.LatestChatProviderRecoveryPreflight( staleContext );

    BOOST_CHECK( !stale.m_Allowed );
    BOOST_CHECK_EQUAL( stale.m_Reason,
                       wxString( wxS( "document_revision_mismatch" ) ) );
    BOOST_CHECK_EQUAL( stale.m_ExpectedDocumentRevision, 42 );
    BOOST_CHECK_EQUAL( stale.m_CurrentDocumentRevision, 41 );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT ready =
            model.LatestChatProviderRecoveryPreflight( currentContext );

    BOOST_CHECK( ready.m_Allowed );
    BOOST_CHECK_EQUAL( ready.m_Reason,
                       wxString( wxS( "ready_for_replay_review" ) ) );
    BOOST_CHECK_EQUAL( ready.m_ReplayCandidateCount, 1 );
    BOOST_CHECK( ready.m_ResultJson.Contains(
            wxS( "kisurf.ai.provider_recovery_preflight" ) ) );
    BOOST_CHECK( ready.m_ResultJson.Contains(
            wxS( "review_effectful_tool_results_before_replay" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsChatProviderRecoveryReplayRequest )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_replay_request" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_REPLAY_REQUEST replay =
            model.LatestChatProviderRecoveryReplayRequest( currentContext );

    BOOST_CHECK( replay.m_Available );
    BOOST_CHECK( replay.m_Allowed );
    BOOST_CHECK( replay.m_UserReviewRequired );
    BOOST_CHECK_EQUAL( replay.m_Reason,
                       wxString( wxS( "ready_for_user_review" ) ) );
    BOOST_CHECK_EQUAL( replay.m_ToolCallId,
                       wxString( wxS( "call_panel_recovery" ) ) );
    BOOST_CHECK_EQUAL( replay.m_CheckpointId, 77 );
    BOOST_CHECK_EQUAL( replay.m_JournalOperationCount, 1 );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "kisurf.ai.provider_recovery_replay_request" ) ) );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsAutomaticChatProviderRecoveryEpisode )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_episode" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_EPISODE episode =
            model.LatestChatProviderRecoveryEpisode( currentContext );

    BOOST_CHECK( episode.m_Available );
    BOOST_CHECK( episode.m_PreflightAllowed );
    BOOST_CHECK( episode.m_ReplayRequestAvailable );
    BOOST_CHECK( episode.m_ReadyForUserReview );
    BOOST_CHECK( episode.m_UserReviewRequired );
    BOOST_CHECK( !episode.m_AutomaticExecutionAllowed );
    BOOST_CHECK( !episode.m_BlindToolReplayAllowed );
    BOOST_CHECK_EQUAL( episode.m_Status,
                       wxString( wxS( "ready_for_user_review" ) ) );
    BOOST_CHECK_EQUAL( episode.m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( episode.m_RequestId, 1 );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "kisurf.ai.provider_recovery_episode" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"automatic_execution_allowed\":false" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"user_review_required\":true" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains( wxS( "preflight_result" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains( wxS( "replay_request" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "call_panel_recovery" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelAutomaticProviderRecoveryEpisodeBlocksStaleContext )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_episode_stale" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT staleContext;
    staleContext.m_DocumentRevision = 41;

    AI_PROVIDER_RECOVERY_EPISODE episode =
            model.LatestChatProviderRecoveryEpisode( staleContext );

    BOOST_CHECK( episode.m_Available );
    BOOST_CHECK( !episode.m_PreflightAllowed );
    BOOST_CHECK( !episode.m_ReplayRequestAvailable );
    BOOST_CHECK( !episode.m_ReadyForUserReview );
    BOOST_CHECK( episode.m_UserReviewRequired );
    BOOST_CHECK( !episode.m_AutomaticExecutionAllowed );
    BOOST_CHECK_EQUAL( episode.m_Status,
                       wxString( wxS( "blocked_by_preflight" ) ) );
    BOOST_CHECK_EQUAL( episode.m_Reason,
                       wxString( wxS( "document_revision_mismatch" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "kisurf.ai.provider_recovery_episode" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"status\":\"blocked_by_preflight\"" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"reason\":\"document_revision_mismatch\"" ) ) );
    BOOST_CHECK( !episode.m_EpisodeJson.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelExecutesReviewedChatProviderRecoveryReplayRequest )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "provider_recovery_replay_execute" ) );

    auto* provider = new ERROR_AFTER_PANEL_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-a" );
    snapshot.m_DocumentId = wxS( "board-1" );
    snapshot.m_Version.m_DocumentRevision = 42;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = 42;

    RECORDING_PANEL_RECOVERY_ACCEPT_ADAPTER adapter;
    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS options;

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT blocked =
            model.ExecuteChatProviderRecoveryReplayRequest(
                    currentContext, options, adapter );

    BOOST_CHECK( !blocked.m_Ok );
    BOOST_CHECK_EQUAL( blocked.m_ErrorCode,
                       wxString( wxS( "user_review_required" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );

    options.m_UserReviewed = true;
    options.m_Reviewer = wxS( "engineer" );

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT applied =
            model.ExecuteChatProviderRecoveryReplayRequest(
                    currentContext, options, adapter );

    BOOST_CHECK( applied.m_Ok );
    BOOST_CHECK( applied.m_BoardMutated );
    BOOST_CHECK_EQUAL( applied.m_AppliedOperationCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationKinds.size(), 1 );
    BOOST_CHECK( adapter.m_OperationKinds.front()
                 == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK_EQUAL( adapter.m_SessionOperationCount, 1 );
    BOOST_CHECK( applied.m_ResultJson.Contains(
            wxS( "kisurf.ai.provider_recovery_replay_execution" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelExposesLatestNextActionProviderRecoveryPolicy )
{
    wxString path =
            uniquePanelArtifactManifestPath( wxS( "next_action_provider_recovery" ) );

    auto* provider =
            new REVIEW_TOOL_ERROR_AFTER_EXECUTED_PANEL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::make_unique<AI_STUB_PROVIDER>() };
    model.SetArtifactStore( &artifactStore );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( provider ) );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-next-action" );
    context.m_DocumentId = wxS( "board-next-action" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );

    AI_PROVIDER_RECOVERY_POLICY policy =
            model.LatestNextActionProviderRecoveryPolicy();

    BOOST_CHECK( policy.m_Available );
    BOOST_CHECK( !policy.m_BlindToolReplayAllowed );
    BOOST_CHECK( policy.m_CheckpointOrJournalResumeRequired );
    BOOST_CHECK_EQUAL( policy.m_AgentKind, wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_GT( policy.m_RequestId, 0 );
    BOOST_CHECK_EQUAL( policy.m_Reason,
                       wxString( wxS( "post_side_effect_ambiguity" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains(
            wxS( "call_panel_next_action_script_before_failure" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains( wxS( "session_journal" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsNextActionProviderRecoveryResumePacket )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "next_action_provider_recovery_resume" ) );

    auto* provider =
            new REVIEW_TOOL_ERROR_AFTER_EXECUTED_PANEL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::make_unique<AI_STUB_PROVIDER>() };
    model.SetArtifactStore( &artifactStore );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( provider ) );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-next-action" );
    context.m_DocumentId = wxS( "board-next-action" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );

    AI_PROVIDER_RECOVERY_RESUME_PACKET packet =
            model.LatestNextActionProviderRecoveryResumePacket();

    BOOST_CHECK( packet.m_Available );
    BOOST_CHECK( !packet.m_BlindToolReplayAllowed );
    BOOST_CHECK( packet.m_CheckpointOrJournalResumeRequired );
    BOOST_CHECK_EQUAL( packet.m_AgentKind, wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_GT( packet.m_RequestId, 0 );
    BOOST_CHECK_EQUAL( packet.m_ResumeAction,
                       wxString( wxS( "resume_from_checkpoint_or_journal" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains(
            wxS( "kisurf.ai.provider_recovery_resume" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains(
            wxS( "do_not_blindly_reexecute_tools" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains(
            wxS( "call_panel_next_action_script_before_failure" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( packet.m_ResumePacketJson.Contains( wxS( "session_journal" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( PanelModelBuildsAutomaticNextActionProviderRecoveryEpisode )
{
    wxString path = uniquePanelArtifactManifestPath(
            wxS( "next_action_provider_recovery_episode" ) );

    auto* provider =
            new REVIEW_TOOL_ERROR_AFTER_EXECUTED_PANEL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{ std::make_unique<AI_STUB_PROVIDER>() };
    model.SetArtifactStore( &artifactStore );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( provider ) );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-next-action" );
    context.m_DocumentId = wxS( "board-next-action" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = context.m_Version.m_DocumentRevision;

    AI_PROVIDER_RECOVERY_EPISODE episode =
            model.LatestNextActionProviderRecoveryEpisode( currentContext );

    BOOST_CHECK( episode.m_Available );
    BOOST_CHECK( episode.m_PreflightAllowed );
    BOOST_CHECK( episode.m_ReplayRequestAvailable );
    BOOST_CHECK( episode.m_ReadyForUserReview );
    BOOST_CHECK( episode.m_UserReviewRequired );
    BOOST_CHECK( !episode.m_AutomaticExecutionAllowed );
    BOOST_CHECK( !episode.m_BlindToolReplayAllowed );
    BOOST_CHECK_EQUAL( episode.m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_EQUAL( episode.m_Status,
                       wxString( wxS( "ready_for_user_review" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "kisurf.ai.provider_recovery_episode" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "\"automatic_execution_allowed\":false" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains(
            wxS( "call_panel_next_action_script_before_failure" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains( wxS( "preflight_result" ) ) );
    BOOST_CHECK( episode.m_EpisodeJson.Contains( wxS( "replay_request" ) ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ChatRuntimeWritesPromptTraceThroughPanelModelStore )
{
    wxString path = uniquePanelPromptTracePath( wxS( "chat" ) );

    AI_PROMPT_TRACE_STORE store( path );
    AI_AGENT_PANEL_MODEL  model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetPromptTraceStore( &store );
    const uint64_t conversationId = model.ActiveChatSessionId();

    model.SendUserText( wxS( "trace this chat request" ), AI_EDITOR_KIND::Pcb );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK_EQUAL( entries.front().m_ConversationId, conversationId );
    BOOST_CHECK_EQUAL( entries.front().m_ProviderStatus,
                       wxString( wxS( "provider_response" ) ) );
    BOOST_CHECK( entries.front().m_PromptTraceJson.Contains( wxS( "user.request" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimeWritesPromptTraceThroughPanelModelStore )
{
    wxString path = uniquePanelPromptTracePath( wxS( "next_action" ) );

    AI_PROMPT_TRACE_STORE store( path );
    AI_AGENT_PANEL_MODEL  model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetPromptTraceStore( &store );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( makeViaNextActionContext(),
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK_EQUAL( static_cast<int>( entries.at( 0 ).m_RequestKind ),
                       static_cast<int>( AI_PROVIDER_REQUEST_KIND::NextActionDecision ) );
    BOOST_CHECK_EQUAL( static_cast<int>( entries.at( 1 ).m_RequestKind ),
                       static_cast<int>( AI_PROVIDER_REQUEST_KIND::NextActionReview ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimeWritesSessionJsonThroughPanelModelStore )
{
    wxString directory =
            uniquePanelNextActionSessionDirectory( wxS( "next_action" ) );

    AI_NEXT_ACTION_SESSION_STORE store( directory );
    AI_AGENT_PANEL_MODEL         model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetNextActionSessionStore( &store );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"panel_session_probe\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-panel-next-action-session" );
    context.m_DocumentId = wxS( "board-panel-next-action-session" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( nextActionProvider->m_Requests.size(), 1 );

    const uint64_t conversationId =
            nextActionProvider->m_Requests.front().m_ConversationId;
    BOOST_CHECK( wxFileExists( store.SessionPath( conversationId ) ) );

    wxString error;
    AI_NEXT_ACTION_SESSION_RECORD record =
            store.LoadSession( conversationId, error );
    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK_EQUAL( record.m_ConversationId, conversationId );
    BOOST_CHECK_EQUAL( record.m_SessionType, wxString( wxS( "placement" ) ) );
    BOOST_CHECK_EQUAL( record.m_ProjectId,
                       wxString( wxS( "project-panel-next-action-session" ) ) );
    BOOST_CHECK_EQUAL( record.m_DocumentId,
                       wxString( wxS( "board-panel-next-action-session" ) ) );
    BOOST_REQUIRE_EQUAL( record.m_Steps.size(), 1 );
    BOOST_CHECK_EQUAL( record.m_Steps.front().m_Status,
                       wxString( wxS( "published" ) ) );
    BOOST_CHECK( record.m_Steps.front().m_LlmDecisionJson.Contains(
            wxS( "panel_session_probe" ) ) );

    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( NextActionActiveToolStateChangeCreatesProviderConversationBoundary )
{
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"observe_placement\"}" ),
              wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"observe_routing\"}" ) } );

    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT placement = makeViaNextActionContext();
    placement.m_ProjectId = wxS( "project-next-session" );
    placement.m_DocumentId = wxS( "board-next-session" );

    model.UpdateSuggestionsIfBackgroundEnabled( placement,
                                                makeSuggestionActivity( 1 ),
                                                wxS( "activity" ) );

    AI_CONTEXT_SNAPSHOT routing = placement;
    routing.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    routing.m_ToolState.m_ModeContextJson =
            wxS( "{\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":100}}" );

    model.UpdateSuggestionsIfBackgroundEnabled( routing,
                                                makeSuggestionActivity( 2 ),
                                                wxS( "activity" ) );

    BOOST_REQUIRE_EQUAL( nextActionProvider->m_Requests.size(), 2 );
    BOOST_CHECK_NE( nextActionProvider->m_Requests.at( 0 ).m_ConversationId,
                    nextActionProvider->m_Requests.at( 1 ).m_ConversationId );
    BOOST_CHECK_GT( nextActionProvider->m_Requests.at( 1 ).m_ConversationId,
                    nextActionProvider->m_Requests.at( 0 ).m_ConversationId );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimeRetrievesDurableMemoryForCurrentDocument )
{
    wxString path = uniquePanelPromptTracePath( wxS( "next_action_memory" ) );

    AI_MEMORY_STORE memoryStore( path );
    wxString        error;

    AI_MEMORY_RECORD matching;
    matching.m_Id = wxS( "next-action-rule" );
    matching.m_ProjectId = wxS( "project-a" );
    matching.m_DocumentId = wxS( "board-1" );
    matching.m_AgentKind = wxS( "nextaction" );
    matching.m_Type = wxS( "layout_preference" );
    matching.m_Text = wxS( "NEXT_ACTION_MEMORY_NEEDLE place decouplers inside the regulator keepout edge" );
    matching.m_Source = wxS( "accepted_next_action" );
    matching.m_AcceptanceState = wxS( "accepted" );
    matching.m_TrustLevel = 95;
    matching.m_Sequence = 21;
    BOOST_REQUIRE( memoryStore.Append( matching, error ) );

    AI_MEMORY_RECORD wrongDocument = matching;
    wrongDocument.m_Id = wxS( "next-action-wrong-document" );
    wrongDocument.m_DocumentId = wxS( "board-2" );
    wrongDocument.m_Text = wxS( "NEXT_ACTION_WRONG_DOCUMENT_NEEDLE" );
    wrongDocument.m_Sequence = 22;
    BOOST_REQUIRE( memoryStore.Append( wrongDocument, error ) );

    AI_MEMORY_RECORD lowTrust = matching;
    lowTrust.m_Id = wxS( "next-action-low-trust" );
    lowTrust.m_Text = wxS( "NEXT_ACTION_LOW_TRUST_NEEDLE" );
    lowTrust.m_TrustLevel = 20;
    lowTrust.m_Sequence = 23;
    BOOST_REQUIRE( memoryStore.Append( lowTrust, error ) );

    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetMemoryStore( &memoryStore );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-a" );
    context.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( nextActionProvider->m_Requests.size(), 2 );

    for( const AI_PROVIDER_REQUEST& request : nextActionProvider->m_Requests )
    {
        BOOST_REQUIRE_EQUAL( request.m_RetrievedMemoryBlocks.size(), 1 );
        BOOST_CHECK_EQUAL( request.m_RetrievedMemoryBlocks.front().m_Id,
                           wxString( wxS( "memory.next-action-rule" ) ) );
        BOOST_CHECK( request.m_RetrievedMemoryBlocks.front().m_Text.Contains(
                wxS( "NEXT_ACTION_MEMORY_NEEDLE" ) ) );
        BOOST_CHECK( !request.m_RetrievedMemoryBlocks.front().m_Text.Contains(
                wxS( "NEXT_ACTION_WRONG_DOCUMENT_NEEDLE" ) ) );
        BOOST_CHECK( !request.m_RetrievedMemoryBlocks.front().m_Text.Contains(
                wxS( "NEXT_ACTION_LOW_TRUST_NEEDLE" ) ) );
    }

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( NextActionRuntimeRetrievesLocalTextMemoryForCurrentDocument )
{
    AI_LOCAL_TEXT_MEMORY_INDEX index;

    AI_LOCAL_TEXT_MEMORY_RECORD matching;
    matching.m_Id = wxS( "artifact:next-action-via-rule" );
    matching.m_ProjectId = wxS( "project-a" );
    matching.m_DocumentId = wxS( "board-1" );
    matching.m_AgentKind = wxS( "next_action" );
    matching.m_Type = wxS( "validation_report" );
    matching.m_Text =
            wxS( "NEXT_ACTION_LOCAL_TEXT_NEEDLE GND via placement should keep "
                 "clearance around the visible via row." );
    matching.m_Source = wxS( "validate_hidden_attempt" );
    matching.m_ProvenanceJson = wxS( "{\"kind\":\"artifact_summary\"}" );
    matching.m_AcceptanceState = wxS( "trace" );
    matching.m_TrustLevel = 60;
    matching.m_Sequence = 40;
    index.AddRecord( matching );

    AI_LOCAL_TEXT_MEMORY_RECORD wrongDocument = matching;
    wrongDocument.m_Id = wxS( "artifact:wrong-document" );
    wrongDocument.m_DocumentId = wxS( "board-2" );
    wrongDocument.m_Text = wxS( "NEXT_ACTION_LOCAL_WRONG_DOCUMENT_NEEDLE via" );
    wrongDocument.m_Sequence = 41;
    index.AddRecord( wrongDocument );

    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetLocalTextMemoryIndex( &index );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    context.m_ProjectId = wxS( "project-a" );
    context.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context,
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( nextActionProvider->m_Requests.size(), 2 );

    for( const AI_PROVIDER_REQUEST& request : nextActionProvider->m_Requests )
    {
        bool sawLocalText = false;

        for( const AI_PROVIDER_INPUT_BLOCK& block : request.m_RetrievedMemoryBlocks )
        {
            if( block.m_Id == wxS( "local.artifact:next-action-via-rule" ) )
            {
                sawLocalText = true;
                BOOST_CHECK( block.m_Text.Contains(
                        wxS( "NEXT_ACTION_LOCAL_TEXT_NEEDLE" ) ) );
                BOOST_CHECK( block.m_MetadataJson.Contains(
                        wxS( "artifact_summary" ) ) );
            }

            BOOST_CHECK( !block.m_Text.Contains(
                    wxS( "NEXT_ACTION_LOCAL_WRONG_DOCUMENT_NEEDLE" ) ) );
        }

        BOOST_CHECK( sawLocalText );
    }
}


BOOST_AUTO_TEST_CASE( AcceptedNextActionWritesDurableMemoryButRejectAndExpireDoNot )
{
    wxString path = uniquePanelPromptTracePath( wxS( "next_action_accept_memory" ) );

    AI_MEMORY_STORE memoryStore( path );

    auto runSuggestion = [&]( const wxString& aProjectId,
                              const wxString& aDocumentId )
            -> std::pair<std::unique_ptr<AI_AGENT_PANEL_MODEL>,
                         std::optional<AI_SUGGESTION_RECORD>>
    {
        auto model = std::make_unique<AI_AGENT_PANEL_MODEL>(
                std::make_unique<AI_STUB_PROVIDER>() );
        model->SetMemoryStore( &memoryStore );

        auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
                { wxS( "{\"decision_kind\":\"attempt\","
                       "\"opportunity_type\":\"placement\"}" ),
                  wxS( "{\"decision_kind\":\"publish\","
                       "\"reason_code\":\"acceptable\","
                       "\"review_basis\":{\"render_valid\":true,"
                       "\"validation_passed\":true,"
                       "\"budget_within_limits\":true,"
                       "\"self_review_passed\":true}}" ) } );
        model->SetNextActionProvider(
                std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );

        static PASSING_SESSION_PREVIEW_SERVICE    previewService;
        static PASSING_SESSION_VALIDATION_SERVICE validationService;
        model->ConfigureNextActionServices( &previewService, &validationService );
        model->SetBackgroundAgentEnabled( true );

        AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
        context.m_ProjectId = aProjectId;
        context.m_DocumentId = aDocumentId;

        AI_ACTIVITY_RECORD activity = makeSuggestionActivity();
        std::optional<AI_SUGGESTION_RECORD> suggestion =
                model->UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                             wxS( "activity" ) );

        return { std::move( model ), suggestion };
    };

    auto [acceptModel, acceptedSuggestion] =
            runSuggestion( wxS( "project-a" ), wxS( "board-accepted" ) );
    BOOST_REQUIRE( acceptedSuggestion.has_value() );

    AI_CONTEXT_SNAPSHOT acceptedContext = makeViaNextActionContext();
    acceptedContext.m_ProjectId = wxS( "project-a" );
    acceptedContext.m_DocumentId = wxS( "board-accepted" );

    AI_NEXT_ACTION_CONTEXT_VERSION acceptVersion =
            AiNextActionContextVersionFromSnapshot( acceptedContext,
                                                    makeSuggestionActivity().m_Sequence );

    FAKE_EDIT_ADAPTER acceptedAdapter;
    AI_EDIT_SESSION   acceptedEdit( acceptedAdapter );
    BOOST_REQUIRE( acceptModel->AcceptSuggestion( acceptedSuggestion->m_Id,
                                                  acceptedEdit, acceptVersion ) );

    wxString error;
    AI_MEMORY_QUERY acceptedQuery;
    acceptedQuery.m_ProjectId = wxS( "project-a" );
    acceptedQuery.m_DocumentId = wxS( "board-accepted" );
    acceptedQuery.m_AcceptanceState = wxS( "accepted" );

    std::vector<AI_MEMORY_RECORD> acceptedRecords =
            memoryStore.Query( acceptedQuery, error );
    BOOST_REQUIRE_EQUAL( acceptedRecords.size(), 1 );
    BOOST_CHECK_EQUAL( acceptedRecords.front().m_AgentKind,
                       wxString( wxS( "nextaction" ) ) );
    BOOST_CHECK_EQUAL( acceptedRecords.front().m_Type,
                       wxString( wxS( "accepted_next_action" ) ) );
    BOOST_CHECK( acceptedRecords.front().m_Text.Contains(
            acceptedSuggestion->m_Title ) );
    BOOST_CHECK( acceptedRecords.front().m_ProvenanceJson.Contains(
            wxS( "accept_token" ) ) );

    auto [rejectModel, rejectedSuggestion] =
            runSuggestion( wxS( "project-a" ), wxS( "board-rejected" ) );
    BOOST_REQUIRE( rejectedSuggestion.has_value() );
    BOOST_REQUIRE( rejectModel->RejectSuggestion( rejectedSuggestion->m_Id ) );

    AI_MEMORY_QUERY rejectedQuery;
    rejectedQuery.m_ProjectId = wxS( "project-a" );
    rejectedQuery.m_DocumentId = wxS( "board-rejected" );
    rejectedQuery.m_AcceptanceState = wxS( "accepted" );
    BOOST_CHECK( memoryStore.Query( rejectedQuery, error ).empty() );

    auto [expireModel, expiredSuggestion] =
            runSuggestion( wxS( "project-a" ), wxS( "board-expired" ) );
    BOOST_REQUIRE( expiredSuggestion.has_value() );
    BOOST_REQUIRE( expireModel->ExpireSuggestion( expiredSuggestion->m_Id ) );

    AI_MEMORY_QUERY expiredQuery;
    expiredQuery.m_ProjectId = wxS( "project-a" );
    expiredQuery.m_DocumentId = wxS( "board-expired" );
    expiredQuery.m_AcceptanceState = wxS( "accepted" );
    BOOST_CHECK( memoryStore.Query( expiredQuery, error ).empty() );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RecordActivityReturnsSequencedRecord )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );

    AI_ACTIVITY_RECORD recorded = model.RecordActivity( activity );

    BOOST_CHECK_EQUAL( recorded.m_Sequence, 1 );
    BOOST_CHECK_EQUAL( recorded.m_ActionName,
                       wxString( wxS( "common.Interactive.selected" ) ) );

    std::vector<AI_ACTIVITY_RECORD> records = model.ActivityRecords();
    BOOST_REQUIRE_EQUAL( records.size(), 1 );
    BOOST_CHECK_EQUAL( records.front().m_Sequence, recorded.m_Sequence );
    BOOST_CHECK_EQUAL( records.front().m_ActionName, recorded.m_ActionName );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentIsDisabledByDefault )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( !model.BackgroundAgentEnabled() );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentDisabledSuppressesAutomaticSuggestions )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentEnabledWithoutRuntimeDoesNotPublishSuggestion )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    model.SetNextActionProvider( nullptr );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentCannotMutateWhileChatOwnsDocumentWriteLease )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity();
    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );

    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "chat" ),
                                                         ownershipContext ) );
    BOOST_REQUIRE( model.ActiveDocumentWriteOwnerNamespace().has_value() );
    BOOST_CHECK_EQUAL( *model.ActiveDocumentWriteOwnerNamespace(),
                       wxString( wxS( "chat" ) ) );

    std::optional<AI_SUGGESTION_RECORD> blocked =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_CHECK( !blocked.has_value() );
    BOOST_CHECK_EQUAL( nextActionProvider->m_CallCount, 0 );
    BOOST_CHECK( model.Suggestions().empty() );
    BOOST_CHECK( !model.TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                                          ownershipContext ) );

    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "chat" ),
                                                      ownershipContext ) );
    BOOST_CHECK( !model.ActiveDocumentWriteOwnerNamespace().has_value() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( nextActionProvider->m_CallCount, 2 );
}


BOOST_AUTO_TEST_CASE( UnknownDocumentWriteLeaseBlocksKnownDocumentMutation )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_NEXT_ACTION_CONTEXT_VERSION unknownContext;
    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "chat" ),
                                                         unknownContext ) );

    std::optional<AI_SUGGESTION_RECORD> blocked =
            model.UpdateSuggestionsIfBackgroundEnabled( makeViaNextActionContext(),
                                                        makeSuggestionActivity(),
                                                        wxS( "activity" ) );

    BOOST_CHECK( !blocked.has_value() );
    BOOST_CHECK_EQUAL( nextActionProvider->m_CallCount, 0 );
}


BOOST_AUTO_TEST_CASE( DocumentWriteOwnershipUsesProjectDocumentIdentityForIndependentDocuments )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_CONTEXT_SNAPSHOT documentA;
    documentA.m_ProjectId = wxS( "project-a" );
    documentA.m_DocumentId = wxS( "board-a" );

    AI_CONTEXT_SNAPSHOT documentB;
    documentB.m_ProjectId = wxS( "project-a" );
    documentB.m_DocumentId = wxS( "board-b" );

    AI_NEXT_ACTION_CONTEXT_VERSION ownershipA =
            AiNextActionContextVersionFromSnapshot( documentA );
    AI_NEXT_ACTION_CONTEXT_VERSION ownershipB =
            AiNextActionContextVersionFromSnapshot( documentB );

    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "chat" ),
                                                         ownershipA ) );
    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                                         ownershipB ) );
    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "nextaction" ),
                                                      ownershipB ) );
    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "chat" ),
                                                      ownershipA ) );
}


BOOST_AUTO_TEST_CASE( DocumentWriteOwnershipConflictsOnSameProjectDocumentDespiteRevisionDrift )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_CONTEXT_SNAPSHOT beforeEdit;
    beforeEdit.m_ProjectId = wxS( "project-a" );
    beforeEdit.m_DocumentId = wxS( "board-a" );
    beforeEdit.m_Version.m_DocumentRevision = 1;

    AI_CONTEXT_SNAPSHOT afterEdit = beforeEdit;
    afterEdit.m_Version.m_DocumentRevision = 2;

    AI_NEXT_ACTION_CONTEXT_VERSION beforeOwnership =
            AiNextActionContextVersionFromSnapshot( beforeEdit );
    AI_NEXT_ACTION_CONTEXT_VERSION afterOwnership =
            AiNextActionContextVersionFromSnapshot( afterEdit );

    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "chat" ),
                                                         beforeOwnership ) );
    BOOST_CHECK( !model.TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                                          afterOwnership ) );
    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "chat" ),
                                                      beforeOwnership ) );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentRuntimeSuggestionsAreTokenGatedAndAcceptable )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                    "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD activity = makeSuggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( nextActionProvider->m_CallCount, 2 );
    BOOST_CHECK( !suggestion->m_EditObjects.empty() );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "accept_token" ) ) );
    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION edit( editAdapter );
    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );
    BOOST_CHECK( model.AcceptSuggestion( suggestion->m_Id, edit,
                                         currentContext ) );
    BOOST_CHECK( !editAdapter.m_Applied.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored = model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentAcceptIsBlockedWhileChatOwnsDocumentWriteLease )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );
    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "chat" ),
                                                         currentContext ) );

    FAKE_EDIT_ADAPTER blockedAdapter;
    AI_EDIT_SESSION   blockedEdit( blockedAdapter );
    BOOST_CHECK( !model.AcceptSuggestion( suggestion->m_Id, blockedEdit,
                                          currentContext ) );
    BOOST_CHECK( blockedAdapter.m_Applied.empty() );

    std::optional<AI_SUGGESTION_RECORD> stillPending =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stillPending.has_value() );
    BOOST_CHECK( stillPending->m_Status == AI_SUGGESTION_STATUS::Pending );

    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "chat" ),
                                                      currentContext ) );

    FAKE_EDIT_ADAPTER acceptedAdapter;
    AI_EDIT_SESSION   acceptedEdit( acceptedAdapter );
    BOOST_CHECK( model.AcceptSuggestion( suggestion->m_Id, acceptedEdit,
                                         currentContext ) );
    BOOST_CHECK( !acceptedAdapter.m_Applied.empty() );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentRuntimeUsesConfiguredPreviewServiceGate )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    BLOCKING_SESSION_PREVIEW_SERVICE previewService;
    model.ConfigureNextActionServices( &previewService, nullptr );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                    "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeViaNextActionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( model.Suggestions().empty() );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 1 );
    BOOST_CHECK_NE( previewService.m_LastSessionId, 0 );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentRuntimeUsesConfiguredPublishTimeContextSampler )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity();
    AI_NEXT_ACTION_CONTEXT_VERSION drifted =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );
    drifted.m_ActivitySequence += 1;
    model.ConfigureNextActionCurrentContextSampler(
            [drifted]()
            {
                return drifted;
            } );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( model.Suggestions().empty() );
    BOOST_CHECK_EQUAL( nextActionProvider->m_CallCount, 2 );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentRuntimeAcceptUsesFullDependencyContext )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                    "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    AI_NEXT_ACTION_CONTEXT_VERSION drifted =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );
    drifted.m_ActivitySequence += 1;

    BOOST_CHECK( !model.AcceptSuggestion( suggestion->m_Id, edit, drifted ) );
    BOOST_CHECK( editAdapter.m_Applied.empty() );
}


BOOST_AUTO_TEST_CASE( ModelCanMarkActionPreviewSuggestionAccepted )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makeActionPreviewSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( model.MarkSuggestionAccepted( suggestion->m_Id ) );

    std::optional<AI_SUGGESTION_RECORD> updated =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( WorkspaceContextStatePreservesIndependentContextState )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( model.ActiveWorkspaceContext() == AI_AGENT_WORKSPACE_CONTEXT_KIND::General );

    AI_AGENT_WORKSPACE_CONTEXT_STATE routingState;
    routingState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;
    routingState.m_Title = wxS( "Routing" );
    routingState.m_StateJson = wxS( "{\"draft\":\"finish GND trace\"}" );
    routingState.m_LastActivitySequence = 7;

    model.SaveWorkspaceContextState( routingState );

    AI_AGENT_WORKSPACE_CONTEXT_STATE viaState;
    viaState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement;
    viaState.m_Title = wxS( "Via placement" );
    viaState.m_StateJson = wxS( "{\"last_spacing\":2500000}" );
    viaState.m_LastActivitySequence = 11;

    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement );
    model.SaveWorkspaceContextState( viaState );
    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );

    AI_AGENT_WORKSPACE_CONTEXT_STATE activeState = model.ActiveWorkspaceContextState();

    BOOST_CHECK( activeState.m_ContextKind == AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );
    BOOST_CHECK_EQUAL( activeState.m_Title, wxString( wxS( "Routing" ) ) );
    BOOST_CHECK_EQUAL( activeState.m_StateJson,
                       wxString( wxS( "{\"draft\":\"finish GND trace\"}" ) ) );
    BOOST_CHECK_EQUAL( activeState.m_LastActivitySequence, 7 );

    std::optional<AI_AGENT_WORKSPACE_CONTEXT_STATE> savedViaState =
            model.WorkspaceContextState( AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement );

    BOOST_REQUIRE( savedViaState );
    BOOST_CHECK_EQUAL( savedViaState->m_StateJson,
                       wxString( wxS( "{\"last_spacing\":2500000}" ) ) );
}


BOOST_AUTO_TEST_CASE( StopMarksLastRequestCancelled )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    model.SendUserText( wxS( "cancel after response" ), AI_EDITOR_KIND::Schematic );

    BOOST_CHECK_EQUAL( model.LastRequestId(), 1 );
    BOOST_CHECK( !model.LastRequestCancelled() );
    BOOST_CHECK( model.CancelLastRequest() );
    BOOST_CHECK( model.LastRequestCancelled() );
    BOOST_CHECK( !model.CancelRequest( 999 ) );
}


BOOST_AUTO_TEST_CASE( SendPassesContextSnapshotToProvider )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Schematic;
    snapshot.m_Version.m_DocumentRevision = 3;
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), SCH_SYMBOL_T, wxS( "U3" ) ) );
    snapshot.m_Actions.push_back( { wxS( "common.Control.zoomFitScreen" ),
                                    wxS( "Zoom Fit" ),
                                    wxS( "Zoom to fit" ),
                                    AI_EDITOR_KIND::Schematic,
                                    AI_ACTION_SAFETY::ReadOnly,
                                    true } );
    snapshot.m_Visual.m_Source = wxS( "test.image" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,abc" );
    snapshot.m_Visual.m_WidthPx = 4;
    snapshot.m_Visual.m_HeightPx = 2;
    snapshot.m_Visual.m_ByteSize = 12;

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    activity.m_Message = wxS( "selection changed" );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    model.RecordActivity( activity );

    AI_PROVIDER_RESPONSE response =
            model.SendUserText( wxS( "what am I selecting?" ), AI_EDITOR_KIND::Schematic, snapshot );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_UserText,
                       wxString( wxS( "what am I selecting?" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Schematic );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Version.m_DocumentRevision, 3 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Visual.m_Source,
                       wxString( wxS( "test.image" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity.size(), 1 );
    BOOST_CHECK_EQUAL(
            provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity.at( 0 ).m_ActionName,
            wxString( wxS( "common.Interactive.selected" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_SelectedObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Actions.size(), 1 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "common.Control.zoomFitScreen" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "visual: test.image image/png pixels=yes" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "recent activity: 1" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "common.Interactive.selected" ) ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextUsesInstalledToolCallHandler )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>() );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;

    model.SetToolCallHandler( &handler );

    AI_PROVIDER_RESPONSE response = model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( response.m_ToolCalls.front().m_Allowed );
    BOOST_CHECK( response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ResultJson,
                       wxString( wxS( "{\"status\":\"panel-executed\"}" ) ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextContinuesMultiRoundToolCalls )
{
    auto* provider = new TWO_ROUND_TOOL_CALL_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_PANEL_TOOL_CALL_HANDLER handler;

    model.SetToolCallHandler( &handler );

    AI_PROVIDER_RESPONSE response =
            model.SendUserText( wxS( "preview a via" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 2 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Two tool results received." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 2 );
    BOOST_CHECK( response.m_ToolCalls.at( 0 ).m_Executed );
    BOOST_CHECK( response.m_ToolCalls.at( 1 ).m_Executed );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 2 );
}


BOOST_AUTO_TEST_CASE( SendUserTextDoesNotRunToolCallsWhileNextActionOwnsDocumentWriteLease )
{
    auto* provider = new TOOL_CALL_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_PANEL_TOOL_CALL_HANDLER handler;

    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT context = makeViaNextActionContext();
    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext =
            AiNextActionContextVersionFromSnapshot( context, 1 );
    BOOST_CHECK( model.TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                                         ownershipContext ) );

    AI_PROVIDER_RESPONSE blocked =
            model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb,
                                context );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 0 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 0 );
    BOOST_CHECK( blocked.m_ToolCalls.empty() );
    BOOST_CHECK( blocked.m_Body.Contains( wxS( "document write ownership" ) ) );

    BOOST_CHECK( model.ReleaseDocumentWriteOwnership( wxS( "nextaction" ),
                                                      ownershipContext ) );

    AI_PROVIDER_RESPONSE allowed =
            model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb,
                                context );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_REQUIRE_EQUAL( allowed.m_ToolCalls.size(), 1 );
    BOOST_CHECK( allowed.m_ToolCalls.front().m_Executed );
}


BOOST_AUTO_TEST_CASE( ObservabilityEntriesExposeRuntimeTraceAndActivity )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>() );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            model.ObservabilityEntries( 16 );

    BOOST_REQUIRE_GE( entries.size(), 4 );

    bool sawInput = false;
    bool sawToolCall = false;
    bool sawToolResult = false;
    bool sawOutput = false;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : entries )
    {
        sawInput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput;
        sawToolCall |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
        sawToolResult |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ToolResult;
        sawOutput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    }

    BOOST_CHECK( sawInput );
    BOOST_CHECK( sawToolCall );
    BOOST_CHECK( sawToolResult );
    BOOST_CHECK( sawOutput );
}


BOOST_AUTO_TEST_CASE( ObservabilityEntriesExposeProviderRecoveryNotice )
{
    wxString path = uniquePanelArtifactManifestPath( wxS( "recovery_notice" ) );

    AI_ARTIFACT_STORE artifactStore( path );
    AI_AGENT_PANEL_MODEL model{
        std::make_unique<ERROR_AFTER_PANEL_TOOL_PROVIDER>()
    };
    RECOVERABLE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetArtifactStore( &artifactStore );
    model.SetToolCallHandler( &handler );

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ProjectId = wxS( "project-recovery-notice" );
    snapshot.m_DocumentId = wxS( "board-recovery-notice" );
    snapshot.m_Version.m_DocumentRevision = 88;

    model.SendUserText( wxS( "trigger recoverable provider failure" ),
                        AI_EDITOR_KIND::Pcb, snapshot );

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            model.ObservabilityEntries( 32 );

    bool sawRecoveryNotice = false;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : entries )
    {
        if( entry.m_Kind != AI_AGENT_OBSERVABILITY_KIND::System )
            continue;

        if( entry.m_Title != wxS( "Provider recovery required" ) )
            continue;

        sawRecoveryNotice = true;
        BOOST_CHECK( entry.m_Summary.Contains( wxS( "checkpoint/journal" ) ) );
        BOOST_CHECK( entry.m_Summary.Contains( wxS( "blind replay disabled" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "provider_recovery" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "do_not_blindly_reexecute_tools" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "kisurf.ai.provider_recovery_resume" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "\"resume_action\":\"resume_from_checkpoint_or_journal\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "\"resume_packet\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "kisurf.ai.provider_recovery_plan" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "\"resume_plan\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains(
                wxS( "verify_live_document_version_matches_recovery_basis" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "replay_candidates" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "panel-session-9" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "checkpoint_id" ) ) );
    }

    BOOST_CHECK( sawRecoveryNotice );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    wxRemoveFile( path );

    for( const AI_ARTIFACT_RECORD& artifact : artifacts )
        wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ObservabilityEntriesExposeNextActionReplayTrace )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );

    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeViaNextActionContext(), makeSuggestionActivity(), wxS( "idle" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            model.ObservabilityEntries( 32 );

    bool sawReplayTrace = false;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : entries )
    {
        if( entry.m_Kind != AI_AGENT_OBSERVABILITY_KIND::NextActionReplay )
            continue;

        sawReplayTrace = true;
        BOOST_CHECK_EQUAL( entry.m_Title, wxString( wxS( "Next Action replay" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "\"semantic_event\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "\"observation_packet\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "\"hidden_attempt_journal\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "\"llm_review_decision\"" ) ) );
        BOOST_CHECK( entry.m_DetailsJson.Contains( wxS( "\"published_suggestion_id\"" ) ) );
    }

    BOOST_CHECK( sawReplayTrace );
}


BOOST_AUTO_TEST_CASE( SendUserTextIncludesPriorRuntimeActivity )
{
    auto* provider = new RUNTIME_ACTIVITY_CAPTURE_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );
    model.SendUserText( wxS( "what happened?" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );

    const std::vector<AI_ACTIVITY_RECORD>& activity =
            provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity;

    BOOST_REQUIRE_GE( activity.size(), 2 );

    bool sawToolRequest = false;
    bool sawToolResult = false;

    for( const AI_ACTIVITY_RECORD& record : activity )
    {
        if( record.m_ToolCallId != wxS( "call_panel" ) )
            continue;

        if( record.m_Kind == AI_ACTIVITY_KIND::ModelToolRequest )
            sawToolRequest = true;

        if( record.m_Kind == AI_ACTIVITY_KIND::ToolResult
            && record.m_ResultJson.Contains( wxS( "panel-executed" ) ) )
        {
            sawToolResult = true;
        }
    }

    BOOST_CHECK( sawToolRequest );
    BOOST_CHECK( sawToolResult );
}


BOOST_AUTO_TEST_CASE( ActivityRecordsShareSequenceAcrossUserAndRuntimeRecords )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>() );

    AI_ACTIVITY_RECORD userActivity;
    userActivity.m_ActionName = wxS( "common.Interactive.selected" );
    userActivity.m_Kind = AI_ACTIVITY_KIND::UserAction;

    AI_ACTIVITY_RECORD recorded = model.RecordActivity( userActivity );
    BOOST_CHECK_EQUAL( recorded.m_Sequence, 1 );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    std::vector<AI_ACTIVITY_RECORD> records = model.ActivityRecords();

    BOOST_REQUIRE_EQUAL( records.size(), 3 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Sequence, 1 );
    BOOST_CHECK( records.at( 0 ).m_Kind == AI_ACTIVITY_KIND::UserAction );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Sequence, 2 );
    BOOST_CHECK( records.at( 1 ).m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK_EQUAL( records.at( 2 ).m_Sequence, 3 );
    BOOST_CHECK( records.at( 2 ).m_Kind == AI_ACTIVITY_KIND::ToolResult );
}


BOOST_AUTO_TEST_CASE( AddSuggestionStoresToolGeneratedSuggestion )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_ContextVersion.m_DocumentRevision = 3;
    suggestion.m_Title = wxS( "Preview moving selection" );
    suggestion.m_ArgumentsJson = wxS( "{\"operation\":\"move_selected\",\"dx\":10,\"dy\":20}" );
    suggestion.m_PreviewObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;

    std::optional<AI_SUGGESTION_RECORD> stored =
            model.AddSuggestion( std::move( suggestion ) );

    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK_EQUAL( stored->m_Id, 1 );
    BOOST_CHECK_EQUAL( model.LatestActiveSuggestionId().value_or( 0 ), 1 );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
    BOOST_CHECK_EQUAL( model.Suggestions().front().m_Title,
                       wxString( wxS( "Preview moving selection" ) ) );
}


BOOST_AUTO_TEST_CASE( DuplicateActiveSuggestionsSupersedeEarlierRecord )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    AI_SUGGESTION_RECORD record = makeModelSuggestion();
    record.m_Fingerprint = wxS( "same" );

    std::optional<AI_SUGGESTION_RECORD> first = model.AddSuggestion( record );
    BOOST_REQUIRE( first.has_value() );

    std::optional<AI_SUGGESTION_RECORD> second = model.AddSuggestion( record );
    BOOST_REQUIRE( second.has_value() );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 2 );
    BOOST_CHECK( model.FindSuggestion( first->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Superseded );
    BOOST_CHECK( model.FindSuggestion( second->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Pending );
}


BOOST_AUTO_TEST_CASE( SuggestionLifecycleUsesRuntimeSuggestionStore )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makeModelSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, 1 );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Shown.size(), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    BOOST_CHECK( !model.AcceptSuggestion( suggestion->m_Id, edit,
                                          nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK( editAdapter.m_Applied.empty() );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( OperationOnlySuggestionExposesPreviewOnlyCapability )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makePanelFillOperationSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, suggestion->m_Id );
    BOOST_CHECK( previewAdapter.m_Shown.empty() );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Operations.size(), 1 );
    BOOST_CHECK( previewAdapter.m_Operations.front().IsPanelFillColumnPreview() );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    BOOST_CHECK( !model.AcceptSuggestion( suggestion->m_Id, edit,
                                          nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK( editAdapter.m_Applied.empty() );
}


BOOST_AUTO_TEST_CASE( AnchorFocusSuggestionExposesPreviewOnlyCapability )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makeAnchorFocusOperationSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, suggestion->m_Id );
    BOOST_CHECK( previewAdapter.m_Shown.empty() );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Operations.size(), 1 );
    BOOST_CHECK( previewAdapter.m_Operations.front().IsAnchorFocusPreview() );
    BOOST_CHECK_EQUAL( previewAdapter.m_Operations.front().m_AnchorId,
                       wxString( wxS( "tool.routing.orthogonal.horizontal" ) ) );
}


BOOST_AUTO_TEST_CASE( LatestActiveSuggestionReturnsNewestPendingOrPreviewingRecord )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );

    std::optional<AI_SUGGESTION_RECORD> first =
            model.AddSuggestion( makeModelSuggestion( wxS( "First" ) ) );
    BOOST_REQUIRE( first.has_value() );

    std::optional<AI_SUGGESTION_RECORD> second =
            model.AddSuggestion( makeModelSuggestion( wxS( "Second" ) ) );
    BOOST_REQUIRE( second.has_value() );

    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), second->m_Id );

    BOOST_CHECK( model.RejectSuggestion( second->m_Id ) );
    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), first->m_Id );

    BOOST_CHECK( model.RejectSuggestion( first->m_Id ) );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
}


BOOST_AUTO_TEST_CASE( ExpireSuggestionsMarksOnlyStaleActiveRecords )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_SUGGESTION_RECORD record = makeModelSuggestion();
    record.m_ContextVersion = makeSuggestionContext( 1 ).m_Version;
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( record );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current;
    current.m_DocumentRevision = 2;
    BOOST_CHECK_EQUAL( model.ExpireSuggestions( current ), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Expired );
}


BOOST_AUTO_TEST_CASE( ExpireSuggestionsUsesFullDependencyContextForRuntimePreview )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    auto* nextActionProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\","
                   "\"review_basis\":{\"render_valid\":true,"
                   "\"validation_passed\":true,"
                   "\"budget_within_limits\":true,"
                   "\"self_review_passed\":true}}" ) } );
    model.SetNextActionProvider( std::unique_ptr<AI_PROVIDER>( nextActionProvider ) );
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    PASSING_SESSION_VALIDATION_SERVICE validationService;
    model.ConfigureNextActionServices( &previewService, &validationService );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = makeViewportBoundViaNextActionContext();
    AI_ACTIVITY_RECORD  activity = makeSuggestionActivity( 88 );
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );

    AI_CONTEXT_SNAPSHOT driftedContext = makeViewportDriftedViaNextActionContext();
    AI_NEXT_ACTION_CONTEXT_VERSION drifted =
            AiNextActionContextVersionFromSnapshot( driftedContext,
                                                    activity.m_Sequence );

    BOOST_CHECK_EQUAL( model.ExpireSuggestions( drifted ), 1 );

    std::optional<AI_SUGGESTION_RECORD> stored =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_SUITE_END()
