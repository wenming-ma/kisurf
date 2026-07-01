#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_next_action_session_store.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <json_common.h>
#include <qa_utils/wx_utils/unit_test_utils.h>

#include <wx/filefn.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/utils.h>

#include <chrono>
#include <deque>
#include <memory>
#include <set>
#include <thread>

namespace
{
wxString publishReview();


wxString uniqueNextActionPromptTracePath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "kst" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format(
            wxS( "kisurf_next_action_prompt_trace_%s_%lu.jsonl" ),
            aSuffix,
            static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


wxString uniqueNextActionArtifactManifestPath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksa" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format(
            wxS( "kisurf_next_action_artifacts_%s_%lu.jsonl" ),
            aSuffix,
            static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


wxString uniqueNextActionSessionDirectory( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksn" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format(
            wxS( "kisurf_next_action_sessions_%s_%lu" ),
            aSuffix,
            static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxDirExists( path.GetFullPath() ) )
        wxFileName::Rmdir( path.GetFullPath(), wxPATH_RMDIR_RECURSIVE );

    return path.GetFullPath();
}


AI_VISUAL_SNAPSHOT makeValidationIssueSourceVisualSnapshot()
{
    wxImage image( 96, 64 );
    image.SetRGB( wxRect( 0, 0, 96, 64 ), 245, 245, 245 );
    image.SetRGB( wxRect( 30, 20, 16, 12 ), 220, 48, 48 );

    AI_VISUAL_SNAPSHOT snapshot = MakeAiVisualSnapshotFromImage(
            image, wxS( "pcbnew.canvas.annotated_roi" ) );
    snapshot.m_FrameId = wxS( "frame-validation-source" );
    snapshot.m_FrameKind = wxS( "annotated_roi" );
    snapshot.m_SidecarJson =
            wxS( "{\"frame_id\":\"frame-validation-source\","
                 "\"frame_kind\":\"annotated_roi\","
                 "\"pixel_world_transform\":{"
                 "\"world_origin\":{\"x\":1000,\"y\":2000},"
                 "\"world_x_per_pixel_x\":10,"
                 "\"world_x_per_pixel_y\":0,"
                 "\"world_y_per_pixel_x\":0,"
                 "\"world_y_per_pixel_y\":10},"
                 "\"anchors\":[{\"anchor_id\":\"near-pad-A\","
                 "\"object_id\":\"pad-A\","
                 "\"handle\":\"handle-pad-A\","
                 "\"pixel_bounds\":{\"left\":28,\"top\":18,"
                 "\"right\":48,\"bottom\":36},"
                 "\"world_bounds\":{\"left\":1280,\"top\":2180,"
                 "\"right\":1480,\"bottom\":2360}}]}" );
    return snapshot;
}


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


class RUNTIME_PREVIEW_RECORDING_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override
    {
        m_BeginIds.push_back( aPreviewId );
    }

    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override
    {
        m_ObjectPreviewIds.push_back( aPreviewId );
        m_Objects.push_back( aObject );
    }

    void ShowOperation( uint64_t aPreviewId,
                        const AI_SUGGESTION_OPERATION& aOperation ) override
    {
        wxUnusedVar( aOperation );
        m_OperationPreviewIds.push_back( aPreviewId );
    }

    void ShowOverlay( uint64_t aPreviewId,
                      const AI_PREVIEW_ITEM_OVERLAY& aOverlay ) override
    {
        m_OverlayPreviewIds.push_back( aPreviewId );
        m_Overlays.push_back( aOverlay );
    }

    void ClearPreview( uint64_t aPreviewId ) override
    {
        m_ClearIds.push_back( aPreviewId );
    }

    std::vector<uint64_t>                m_BeginIds;
    std::vector<uint64_t>                m_ObjectPreviewIds;
    std::vector<uint64_t>                m_OperationPreviewIds;
    std::vector<uint64_t>                m_OverlayPreviewIds;
    std::vector<uint64_t>                m_ClearIds;
    std::vector<AI_OBJECT_REF>           m_Objects;
    std::vector<AI_PREVIEW_ITEM_OVERLAY> m_Overlays;
};


class TOOL_CALLING_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit TOOL_CALLING_NEXT_ACTION_PROVIDER(
            wxString aValidationArgumentsJson = wxS( "{\"level\":\"drc_lite\"}" ) ) :
            m_ValidationArgumentsJson( std::move( aValidationArgumentsJson ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "tool calling next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need observation facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_observation" );
                call.m_ToolName = wxS( "observation_read" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"reason_code\":\"tool_result_supported\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need validation facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = m_ValidationArgumentsJson;
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = wxS( "{\"decision_kind\":\"publish\","
                                  "\"reason_code\":\"tool_review_passed\","
                                  "\"review_basis\":{\"render_valid\":true,"
                                  "\"validation_passed\":true,"
                                  "\"budget_within_limits\":true,"
                                  "\"self_review_passed\":true}}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    wxString                         m_ValidationArgumentsJson;
};


class TOOL_ROUND_BUDGET_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "tool round budget next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            const size_t nextCallIndex = aRequest.m_ToolResults.size() + 1;

            response.m_Body = wxS( "Need another observation tool call." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxString::Format(
                    wxS( "call_observation_%llu" ),
                    static_cast<unsigned long long>( nextCallIndex ) );
            call.m_ToolName = wxS( "observation_read" );
            call.m_ArgumentsJson = wxS( "{}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class PUBLISH_WITH_OVER_BUDGET_REVIEW_TOOL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "publish with over-budget review tool" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            response.m_Body = publishReview();

            const size_t nextCallIndex = aRequest.m_ToolResults.size() + 1;

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxString::Format(
                    wxS( "call_review_observation_%llu" ),
                    static_cast<unsigned long long>( nextCallIndex ) );
            call.m_ToolName = wxS( "observation_read" );
            call.m_ArgumentsJson = wxS( "{}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class RENDER_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit RENDER_TOOL_NEXT_ACTION_PROVIDER( wxString aRenderArgumentsJson ) :
            m_RenderArgumentsJson( std::move( aRenderArgumentsJson ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "render tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need requested render facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_requested" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = m_RenderArgumentsJson;
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    wxString                         m_RenderArgumentsJson;
};


class LARGE_UNKNOWN_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "large unknown tool next action" );

        if( aRequest.m_RequestKind != AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = publishReview();
            return response;
        }

        if( aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Need oversized unknown tool failure." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxS( "call_large_unknown" );

            wxString toolName = wxS( "unknown_tool_" );

            for( int i = 0; i < 6000; ++i )
                toolName << wxS( "x" );

            call.m_ToolName = toolName;
            call.m_ArgumentsJson = wxS( "{}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\","
                              "\"reason_code\":\"large_tool_result_archived\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


wxString largeBoundedScriptPlanArguments()
{
    nlohmann::json operations = nlohmann::json::array();

    for( int i = 0; i < 7; ++i )
    {
        std::string alias = "script_output_via_" + std::to_string( i ) + "_";
        alias.append( 700, 's' );

        operations.push_back(
                { { "kind", "pcb.create_via" },
                  { "arguments",
                    { { "position", { { "x", 1000 + i * 100 }, { "y", 2000 } } },
                      { "net", "GND" },
                      { "diameter", 600 },
                      { "drill", 300 },
                      { "layer_pair", { { "top", "F.Cu" }, { "bottom", "B.Cu" } } },
                      { "alias", alias } } } } );
    }

    nlohmann::json args = {
        { "plan", { { "operations", std::move( operations ) } } },
        { "max_steps", 7 },
        { "plan_id", "large-script-output-plan" }
    };

    return wxString::FromUTF8( args.dump().c_str() );
}


wxString replacementPathScriptPlanArguments( const std::string& aHandleJson,
                                            bool aRequiresValidationBeforePublish )
{
    nlohmann::json handle = nlohmann::json::parse( aHandleJson, nullptr, false );

    if( handle.is_discarded() || !handle.is_object() )
        handle = nlohmann::json::object( { { "handle", "track-old-1" } } );

    nlohmann::json polylineMetadata = {
        { "source_tool", "script.replacement_path_plan" }
    };

    if( aRequiresValidationBeforePublish )
    {
        polylineMetadata["validation_hint"] =
                "run_validate_hidden_attempt_before_publish";
        polylineMetadata["source_tool"] =
                "script.constraint_aware_reroute_plan";
    }

    nlohmann::json operations = nlohmann::json::array(
            { { { "kind", "pcb.delete_items" },
                { "arguments",
                  { { "handles", nlohmann::json::array( { handle } ) },
                    { "metadata",
                      { { "source_tool", "script.replacement_path_plan" } } } } } },
              { { "kind", "pcb.create_track_polyline" },
                { "arguments",
                  { { "points",
                      nlohmann::json::array(
                              { { { "x", 1000000 }, { "y", 1000000 } },
                                { { "x", 1400000 }, { "y", 1250000 } },
                                { { "x", 1800000 }, { "y", 1000000 } } } ) },
                    { "layer", "F.Cu" },
                    { "net", "GND" },
                    { "width", 150000 },
                    { "alias", "replace_path_polyline" },
                    { "metadata", std::move( polylineMetadata ) } } } } } );

    nlohmann::json args = {
        { "plan", { { "operations", std::move( operations ) } } },
        { "max_steps", 4 },
        { "plan_id", aRequiresValidationBeforePublish
                             ? "constraint-aware-reroute-script-plan"
                             : "replacement-path-script-plan" }
    };

    return wxString::FromUTF8( args.dump().c_str() );
}


class LARGE_SCRIPT_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "large script tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"large_script_output_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview
            && aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Need oversized script output." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxS( "call_large_script" );
            call.m_ToolName = wxS( "script_run_bounded_plan" );
            call.m_ArgumentsJson = largeBoundedScriptPlanArguments();
            response.m_ToolCalls.push_back( call );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\","
                              "\"reason_code\":\"large_script_output_archived\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER( wxString aToolName,
                                                       wxString aArgumentsJson ) :
            m_ToolName( std::move( aToolName ) ),
            m_ArgumentsJson( std::move( aArgumentsJson ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "invalid review tool argument next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need runtime tool facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_invalid_review_tool" );
                call.m_ToolName = m_ToolName;
                call.m_ArgumentsJson = m_ArgumentsJson;
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = wxS( "{\"decision_kind\":\"abandon\","
                                  "\"reason_code\":\"invalid_tool_result_seen\"}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    wxString                         m_ToolName;
    wxString                         m_ArgumentsJson;
};


class PLAN_CANDIDATE_RESULT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "plan candidate result next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxString::Format(
                            wxS( "{\"decision_kind\":\"attempt\","
                                 "\"opportunity_type\":\"routing\","
                                 "\"selected_candidate_index\":0,"
                                 "\"reason_code\":\"plan_candidate_tool_result\","
                                 "\"provider_tool_results\":[{"
                                 "\"request_id\":%llu,"
                                 "\"tool_call_id\":\"call_plan_candidates\","
                                 "\"tool_name\":\"routing_generate_polyline_plan_candidates\","
                                 "\"arguments_json\":\"{}\","
                                 "\"allowed\":true,"
                                 "\"executed\":true,"
                                 "\"error_code\":\"\","
                                 "\"message\":\"Candidates generated.\","
                                 "\"result\":{"
                                 "\"tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"status\":\"candidates_generated\","
                                 "\"candidate_count\":1,"
                                 "\"candidates\":[{"
                                 "\"index\":0,"
                                 "\"title\":\"Polyline plan route\","
                                 "\"body\":\"Candidate bounded plan for a polyline route.\","
                                 "\"source_tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"context_kind\":\"routing\","
                                 "\"preview_object_count\":1,"
                                 "\"edit_object_count\":1,"
                                 "\"operation\":\"polyline_route_plan\","
                                 "\"arguments\":{"
                                 "\"operation\":\"polyline_route_plan\","
                                 "\"source_tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"candidate_strategy\":\"tool_generated_plan\","
                                 "\"plan\":{\"operations\":[{"
                                 "\"kind\":\"pcb.create_track_polyline\","
                                 "\"arguments\":{"
                                 "\"points\":[{\"x\":10,\"y\":10},{\"x\":60,\"y\":40},{\"x\":110,\"y\":40}],"
                                 "\"layer\":\"F.Cu\","
                                 "\"net\":\"GND\","
                                 "\"width\":150000,"
                                 "\"alias\":\"plan_polyline\"}}]}},"
                                 "\"plan\":{\"operations\":[{"
                                 "\"kind\":\"pcb.create_track_polyline\","
                                 "\"arguments\":{"
                                 "\"points\":[{\"x\":10,\"y\":10},{\"x\":60,\"y\":40},{\"x\":110,\"y\":40}],"
                                 "\"layer\":\"F.Cu\","
                                 "\"net\":\"GND\","
                                 "\"width\":150000,"
                                 "\"alias\":\"plan_polyline\"}}]},"
                                 "\"landing_facts\":{"
                                 "\"kind\":\"routing_landing\","
                                 "\"source\":\"polyline_plan.end\","
                                 "\"point\":{\"x\":110,\"y\":40},"
                                 "\"net\":\"GND\","
                                 "\"layer\":\"F.Cu\","
                                 "\"width\":150000},"
                                 "\"publish_allowed\":false}]}}]}" ),
                            static_cast<unsigned long long>( aRequest.m_RequestId ) );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class FAILING_PLAN_CANDIDATE_RESULT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "failing plan candidate result next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxString::Format(
                            wxS( "{\"decision_kind\":\"attempt\","
                                 "\"opportunity_type\":\"routing\","
                                 "\"selected_candidate_index\":0,"
                                 "\"reason_code\":\"failing_plan_candidate_tool_result\","
                                 "\"provider_tool_results\":[{"
                                 "\"request_id\":%llu,"
                                 "\"tool_call_id\":\"call_plan_candidates\","
                                 "\"tool_name\":\"routing_generate_polyline_plan_candidates\","
                                 "\"arguments_json\":\"{}\","
                                 "\"allowed\":true,"
                                 "\"executed\":true,"
                                 "\"error_code\":\"\","
                                 "\"message\":\"Candidates generated.\","
                                 "\"result\":{"
                                 "\"tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"status\":\"candidates_generated\","
                                 "\"candidate_count\":1,"
                                 "\"candidates\":[{"
                                 "\"index\":0,"
                                 "\"title\":\"Malformed polyline plan route\","
                                 "\"body\":\"Candidate with an executable plan that fails during hidden mutation.\","
                                 "\"source_tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"context_kind\":\"routing\","
                                 "\"preview_object_count\":1,"
                                 "\"edit_object_count\":1,"
                                 "\"operation\":\"polyline_route_plan\","
                                 "\"arguments\":{"
                                 "\"operation\":\"polyline_route_plan\","
                                 "\"source_tool\":\"routing.generate_polyline_plan_candidates\","
                                 "\"candidate_strategy\":\"tool_generated_plan\","
                                 "\"plan\":{\"operations\":[{"
                                 "\"kind\":\"pcb.create_track_polyline\","
                                 "\"arguments\":{"
                                 "\"layer\":\"F.Cu\","
                                 "\"net\":\"GND\","
                                 "\"width\":150000,"
                                 "\"alias\":\"bad_plan_polyline\"}}]}},"
                                 "\"plan\":{\"operations\":[{"
                                 "\"kind\":\"pcb.create_track_polyline\","
                                 "\"arguments\":{"
                                 "\"layer\":\"F.Cu\","
                                 "\"net\":\"GND\","
                                 "\"width\":150000,"
                                 "\"alias\":\"bad_plan_polyline\"}}]},"
                                 "\"landing_facts\":{"
                                 "\"kind\":\"routing_landing\","
                                 "\"source\":\"polyline_plan.invalid\","
                                 "\"net\":\"GND\","
                                 "\"layer\":\"F.Cu\","
                                 "\"width\":150000},"
                                 "\"publish_allowed\":false}]}}]}" ),
                            static_cast<unsigned long long>( aRequest.m_RequestId ) );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class APPLY_CANDIDATE_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "apply candidate tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"apply_candidate_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need explicit hidden mutation facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_apply_candidate" );
                call.m_ToolName = wxS( "shadow_apply_candidate" );
                call.m_ArgumentsJson = wxS( "{\"candidate_index\":0}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class BOUNDED_SCRIPT_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "bounded script tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"bounded_script_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class REVIEW_TOOL_ERROR_AFTER_EXECUTED_NEXT_ACTION_PROVIDER : public AI_PROVIDER
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
            response.m_Title = wxS( "decision before review failure" );
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"review_failure_probe\"}" );
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
                call.m_ToolCallId = wxS( "call_script_before_failure" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_before_failure\"}}]},"
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


class ATOMIC_OPERATION_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "atomic operation tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"atomic_operation_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need atomic operation execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_atomic_operation" );
                call.m_ToolName = wxS( "atomic_run_operation" );
                call.m_ArgumentsJson =
                        wxS( "{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"atomic_via_1\"}}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class FAILING_BOUNDED_SCRIPT_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "failing bounded script tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"failing_bounded_script_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need failed bounded script facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_failing_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"rollback_probe_via\"}},"
                             "{\"kind\":\"pcb.unsupported_operation\","
                             "\"arguments\":{}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class MUTATION_BUDGET_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "mutation budget next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"mutation_budget_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.size() < 8 )
            {
                const unsigned long long index =
                        static_cast<unsigned long long>(
                                aRequest.m_ToolResults.size() + 1 );
                const unsigned long long x = 1600000 + index * 100000;

                response.m_Body = wxS( "Need another bounded script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId =
                        wxString::Format( wxS( "call_script_plan_%llu" ),
                                          index );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxString::Format(
                                wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                                     "\"arguments\":{\"position\":{\"x\":%llu,\"y\":2400000},"
                                     "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                                     "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                                     "\"alias\":\"budget_via_%llu\"}}]},\"max_steps\":1}" ),
                                x, index );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class FACT_TOOL_BUDGET_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    FACT_TOOL_BUDGET_NEXT_ACTION_PROVIDER( wxString aToolName,
                                           wxString aToolCallPrefix,
                                           wxString aArgumentsJson ) :
            m_ToolName( std::move( aToolName ) ),
            m_ToolCallPrefix( std::move( aToolCallPrefix ) ),
            m_ArgumentsJson( std::move( aArgumentsJson ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "fact tool budget next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"fact_tool_budget_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.size() < 4 )
            {
                const unsigned long long index =
                        static_cast<unsigned long long>(
                                aRequest.m_ToolResults.size() + 1 );

                response.m_Body = wxS( "Need another hidden fact tool call." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId =
                        m_ToolCallPrefix
                        + wxString::Format( wxS( "_%llu" ), index );
                call.m_ToolName = m_ToolName;
                call.m_ArgumentsJson = m_ArgumentsJson;
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    wxString                         m_ToolName;
    wxString                         m_ToolCallPrefix;
    wxString                         m_ArgumentsJson;
};


class SURFACE_PATCH_SCRIPT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface patch script next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"column\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\","
                         "\"table_id\":\"clearance.rules\","
                         "\"column\":\"class\"},"
                         "\"reason_code\":\"surface_patch_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need SurfacePatch lowering facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_patch" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"expected_surface_revision\":17,"
                             "\"expected_schema_version\":\"net-class-v1\","
                             "\"expected_selection_fingerprint\":\"cell:row.power:class\","
                             "\"expected_overlap_set\":[\"row.power\",\"row.gpio\"],"
                             "\"target_scope\":{\"kind\":\"column\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"column\":\"class\"},"
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":["
                             "{\"op\":\"set_cell\",\"row_id\":\"row.power\","
                             "\"column_id\":\"class\",\"value\":\"Power\"},"
                             "{\"op\":\"set_cell\",\"row_id\":\"row.gpio\","
                             "\"column_id\":\"class\",\"value\":\"GPIO\"}]},"
                             "\"alias\":\"surface_patch_fill_class\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SCRIPT_RETRY_REVIEW_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script retry review next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"reason_code\":\"script_retry_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_UserText.Contains( wxS( "\"previous_attempts\"" ) ) )
            {
                response.m_Body = publishReview();
                return response;
            }

            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need script attempt facts before retry." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = wxS( "{\"decision_kind\":\"rollback_retry\","
                                  "\"reason_code\":\"inspect_script_result_on_retry\"}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SURFACE_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface patch then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"column\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\","
                         "\"table_id\":\"clearance.rules\","
                         "\"column\":\"class\"},"
                         "\"reason_code\":\"surface_patch_render_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need SurfacePatch lowering facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_patch" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"expected_surface_revision\":17,"
                             "\"expected_schema_version\":\"net-class-v1\","
                             "\"expected_selection_fingerprint\":\"cell:row.power:class\","
                             "\"expected_overlap_set\":[\"row.power\",\"row.gpio\"],"
                             "\"target_scope\":{\"kind\":\"column\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"column\":\"class\"},"
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":["
                             "{\"op\":\"set_cell\",\"row_id\":\"row.power\","
                             "\"column_id\":\"class\",\"value\":\"Power\"},"
                             "{\"op\":\"set_cell\",\"row_id\":\"row.gpio\","
                             "\"column_id\":\"class\",\"value\":\"GPIO\"}]},"
                             "\"alias\":\"surface_patch_fill_class\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need structured surface render facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_surface_patch" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need validation facts after structured surface render." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_surface_patch" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SURFACE_PATCH_FILL_OPS_THEN_RENDER_NEXT_ACTION_PROVIDER :
        public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface patch fill ops then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"surface\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\"},"
                         "\"reason_code\":\"surface_patch_fill_ops_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need batch SurfacePatch lowering facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_patch_fill_ops" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"target_scope\":{\"kind\":\"surface\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"surface_id\":\"board_setup.clearance\"},"
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":["
                             "{\"op\":\"fill_row\",\"row_id\":\"row.power\","
                             "\"values\":{\"class\":\"Power\",\"width\":\"0.30mm\"}},"
                             "{\"op\":\"fill_column\",\"column_id\":\"priority\","
                             "\"values\":{\"row.power\":1,\"row.gpio\":2}},"
                             "{\"op\":\"fill_range\",\"cells\":["
                             "{\"row_id\":\"row.gpio\",\"column_id\":\"class\","
                             "\"value\":\"GPIO\"}]},"
                             "{\"op\":\"set_property\","
                             "\"property_id\":\"default_clearance\","
                             "\"value\":\"0.20mm\"}]},"
                             "\"alias\":\"surface_patch_fill_ops\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered batch SurfacePatch facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_surface_patch_fill_ops" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need validation facts after batch render." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_surface_patch_fill_ops" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SURFACE_PATCH_WRONG_COLUMN_THEN_RENDER_NEXT_ACTION_PROVIDER :
        public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface patch wrong column next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"column\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\","
                         "\"table_id\":\"clearance.rules\","
                         "\"column\":\"class\"},"
                         "\"reason_code\":\"surface_patch_wrong_column_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need mismatched SurfacePatch facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_wrong_surface_patch" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"target_scope\":{\"kind\":\"column\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"column\":\"clearance\"},"
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":[{\"op\":\"set_cell\","
                             "\"row_id\":\"row.power\","
                             "\"column_id\":\"clearance\","
                             "\"value\":\"0.20mm\"}]},"
                             "\"alias\":\"surface_patch_wrong_column\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered mismatched SurfacePatch facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_wrong_surface_patch" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SURFACE_PATCH_REVISE_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface patch revise then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"column\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\","
                         "\"table_id\":\"clearance.rules\","
                         "\"column\":\"class\"},"
                         "\"reason_code\":\"surface_patch_revise_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need initial SurfacePatch facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_patch_initial" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":[{\"op\":\"set_cell\","
                             "\"row_id\":\"row.power\","
                             "\"column_id\":\"class\","
                             "\"value\":\"Power\"}]},"
                             "\"alias\":\"surface_patch_initial\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need revised SurfacePatch facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_patch_revised" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"surface.apply_patch\","
                             "\"arguments\":{"
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":[{\"op\":\"set_cell\","
                             "\"row_id\":\"row.power\","
                             "\"column_id\":\"class\","
                             "\"value\":\"HighPower\"}]},"
                             "\"alias\":\"surface_patch_revised\"}}]},"
                             "\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need value-aware surface diff render facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_surface_patch_revision" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SURFACE_REPAIR_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit SURFACE_REPAIR_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER(
            wxString aWritePolicy = wxString() ) :
            m_WritePolicy( std::move( aWritePolicy ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "surface repair patch then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body =
                    wxS( "{\"decision_kind\":\"attempt\","
                         "\"opportunity_type\":\"structured_surface\","
                         "\"selected_candidate_index\":0,"
                         "\"target_scope\":{\"kind\":\"column\","
                         "\"panel_id\":\"board_setup.clearance\","
                         "\"surface_id\":\"board_setup.clearance\","
                         "\"table_id\":\"clearance.rules\","
                         "\"column\":\"class\"},"
                         "\"reason_code\":\"surface_repair_patch_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need SurfacePatch repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_surface_repair" );
                call.m_ToolName = wxS( "surface_repair_patch" );
                wxString writePolicyJson;

                if( !m_WritePolicy.IsEmpty() )
                {
                    writePolicyJson = wxS( "\"write_policy\":\"" )
                                      + m_WritePolicy + wxS( "\"," );
                }

                call.m_ArgumentsJson =
                        wxS( "{\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"expected_surface_revision\":17,"
                             "\"expected_schema_version\":\"net-class-v1\","
                             "\"expected_selection_fingerprint\":\"cell:row.power:class\","
                             "\"expected_overlap_set\":[\"row.power\",\"row.gpio\"]," )
                        + writePolicyJson
                        + wxS(
                             "\"target_scope\":{\"kind\":\"column\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"surface_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"column\":\"class\"},"
                             "\"patch\":{\"kind\":\"SurfacePatch\","
                             "\"operations\":["
                             "{\"op\":\"set_cell\",\"row_id\":\"row.power\","
                             "\"column_id\":\"class\",\"value\":\"Power\"},"
                             "{\"op\":\"set_cell\",\"row_id\":\"row.gpio\","
                             "\"column_id\":\"class\",\"value\":\"GPIO\"}]},"
                             "\"alias\":\"surface_repair_fill_class\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered surface repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_surface_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    wxString                         m_WritePolicy;
};


class PLACEMENT_REPAIR_VIA_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "placement repair via then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"placement_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need placement repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_repair" );
                call.m_ToolName = wxS( "placement_repair_via" );
                call.m_ArgumentsJson =
                        wxS( "{\"position\":{\"x\":1800000,\"y\":2600000},"
                             "\"net\":\"GND\","
                             "\"diameter\":600000,"
                             "\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"placement_repair_via_1\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered placement repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_placement_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class PLACEMENT_REPAIR_MOVE_ITEMS_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "placement repair move items then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"placement_move_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need placement via before move repair." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_repair_subject" );
                call.m_ToolName = wxS( "placement_repair_via" );
                call.m_ArgumentsJson =
                        wxS( "{\"position\":{\"x\":1800000,\"y\":2600000},"
                             "\"net\":\"GND\","
                             "\"diameter\":600000,"
                             "\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"placement_repair_move_subject\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need move repair facts." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_move_repair" );
                call.m_ToolName = wxS( "placement_repair_move_items" );
                call.m_ArgumentsJson = wxString::Format(
                        wxS( "{\"handles\":[%s],"
                             "\"delta\":{\"x\":250000,\"y\":-100000},"
                             "\"alias\":\"placement_move_delta\"}" ),
                        wxString::FromUTF8( handle.dump().c_str() ) );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need rendered move repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_placement_move_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER :
        public AI_PROVIDER
{
public:
    explicit PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER(
            std::vector<std::pair<int, int>> aDeltas =
                    { { 250000, -100000 } } ) :
            m_Deltas( std::move( aDeltas ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title =
                wxS( "placement repair move items validate publish next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            m_CurrentEpisodeIndex =
                    m_Deltas.empty()
                            ? 0
                            : std::min( m_EpisodeCount, m_Deltas.size() - 1 );
            ++m_EpisodeCount;

            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"placement_move_accept_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need placement via before move repair." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_repair_subject" );
                call.m_ToolName = wxS( "placement_repair_via" );
                call.m_ArgumentsJson =
                        wxS( "{\"position\":{\"x\":1800000,\"y\":2600000},"
                             "\"net\":\"GND\","
                             "\"diameter\":600000,"
                             "\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"placement_repair_move_subject\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need move repair facts." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_move_repair" );
                call.m_ToolName = wxS( "placement_repair_move_items" );
                const std::pair<int, int> delta =
                        m_Deltas.empty()
                                ? std::make_pair( 250000, -100000 )
                                : m_Deltas.at( std::min( m_CurrentEpisodeIndex,
                                                         m_Deltas.size() - 1 ) );
                call.m_ArgumentsJson = wxString::Format(
                        wxS( "{\"handles\":[%s],"
                             "\"delta\":{\"x\":%d,\"y\":%d},"
                             "\"alias\":\"placement_move_delta\"}" ),
                        wxString::FromUTF8( handle.dump().c_str() ),
                        delta.first, delta.second );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need rendered move repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_placement_move_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 3 )
            {
                response.m_Body = wxS( "Need accept-grade validation facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_placement_move_repair" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{\"level\":\"drc_lite\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    std::vector<std::pair<int, int>> m_Deltas;
    size_t                           m_EpisodeCount = 0;
    size_t                           m_CurrentEpisodeIndex = 0;
};


class PLACEMENT_REPAIR_ORIENTATION_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "placement repair orientation then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"placement_orientation_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need placement item before orientation repair." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_orientation_subject" );
                call.m_ToolName = wxS( "placement_repair_via" );
                call.m_ArgumentsJson =
                        wxS( "{\"position\":{\"x\":1800000,\"y\":2600000},"
                             "\"net\":\"GND\","
                             "\"diameter\":600000,"
                             "\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"placement_repair_orientation_subject\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need footprint orientation repair facts." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_placement_orientation_repair" );
                call.m_ToolName = wxS( "placement_repair_footprint_orientation" );
                call.m_ArgumentsJson = wxString::Format(
                        wxS( "{\"handles\":[%s],"
                             "\"target_orientation_degrees\":90,"
                             "\"target_side\":\"B.Cu\","
                             "\"alias\":\"placement_orientation_90\"}" ),
                        wxString::FromUTF8( handle.dump().c_str() ) );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need rendered orientation repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_placement_orientation_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class ROUTING_REPAIR_SEGMENT_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "routing repair segment then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"routing_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need routing repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_routing_repair" );
                call.m_ToolName = wxS( "routing_repair_segment" );
                call.m_ArgumentsJson =
                        wxS( "{\"start\":{\"x\":1000000,\"y\":1000000},"
                             "\"end\":{\"x\":2200000,\"y\":1000000},"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":250000,"
                             "\"alias\":\"routing_repair_segment_1\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered routing repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_routing_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class ROUTING_REPAIR_POLYLINE_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "routing repair polyline then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"routing_polyline_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need routing polyline repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_routing_polyline_repair" );
                call.m_ToolName = wxS( "routing_repair_polyline" );
                call.m_ArgumentsJson =
                        wxS( "{\"points\":[{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1500000,\"y\":1000000},"
                             "{\"x\":1500000,\"y\":1600000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"routing_repair_polyline_1\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered routing polyline facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_routing_polyline_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class ROUTING_REPAIR_BUS_SEGMENTS_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "routing repair bus segments then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"routing_bus_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need routing bus repair facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_routing_bus_repair" );
                call.m_ToolName = wxS( "routing_repair_bus_segments" );
                call.m_ArgumentsJson =
                        wxS( "{\"segments\":["
                             "{\"start\":{\"x\":1000000,\"y\":1000000},"
                             "\"end\":{\"x\":2200000,\"y\":1000000},"
                             "\"net\":\"D0\"},"
                             "{\"start\":{\"x\":1000000,\"y\":1250000},"
                             "\"end\":{\"x\":2200000,\"y\":1250000},"
                             "\"net\":\"D1\"}],"
                             "\"layer\":\"F.Cu\","
                             "\"width\":150000,"
                             "\"alias\":\"routing_bus_repair\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need rendered routing bus facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_routing_bus_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class ROUTING_CANDIDATE_PLAN_THEN_SCRIPT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit ROUTING_CANDIDATE_PLAN_THEN_SCRIPT_NEXT_ACTION_PROVIDER(
            bool aUseConstraintAwareCandidate = false,
            bool aValidateBeforePublish = false ) :
            m_UseConstraintAwareCandidate( aUseConstraintAwareCandidate ),
            m_ValidateBeforePublish( aValidateBeforePublish )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "routing candidate plan then script next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"routing_candidate_plan_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Create a route item before asking for a replacement plan." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_create_route_subject" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"pcb.create_track_polyline\","
                             "\"arguments\":{\"points\":["
                             "{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1800000,\"y\":1000000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"route_subject\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Run the replacement path bounded script plan." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_execute_candidate_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson = replacementPathScriptPlanArguments(
                        handle.dump(), m_UseConstraintAwareCandidate );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Render the replacement path result." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_candidate_plan" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 3 && m_ValidateBeforePublish )
            {
                response.m_Body = wxS( "Validate the hinted constraint-aware replacement path." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_candidate_plan" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson =
                        wxS( "{\"level\":\"drc_lite\",\"scope\":\"affected_area\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    bool                             m_UseConstraintAwareCandidate = false;
    bool                             m_ValidateBeforePublish = false;
};


class CONSTRAINT_REROUTE_VALIDATE_BEFORE_EXECUTE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title =
                wxS( "constraint reroute validate before execute next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"stale_validation_order_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Create a route item before constraint reroute." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_create_route_subject" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"pcb.create_track_polyline\","
                             "\"arguments\":{\"points\":["
                             "{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1800000,\"y\":1000000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"route_subject\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Validate too early, before executing the candidate plan." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_before_candidate_plan" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson =
                        wxS( "{\"level\":\"drc_lite\",\"scope\":\"affected_area\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body =
                        wxS( "Execute the candidate plan after the validation." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_execute_candidate_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson = replacementPathScriptPlanArguments(
                        handle.dump(), true );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 3 )
            {
                response.m_Body = wxS( "Render the post-validation mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_stale_validation" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class CONSTRAINT_CANDIDATE_OBSERVE_ONLY_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "constraint candidate observe only next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"candidate_exploration_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Create a route subject for candidate exploration." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_create_route_subject" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"pcb.create_track_polyline\","
                             "\"arguments\":{\"points\":["
                             "{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1800000,\"y\":1000000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"route_subject\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Observe a non-mutating constraint hint." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_observe_constraint_hint" );
                call.m_ToolName = wxS( "observation_resolve_visual_reference" );
                call.m_ArgumentsJson =
                        wxS( "{\"reference\":{\"kind\":\"routing_anchor\","
                             "\"anchor_id\":\"candidate-only-anchor\","
                             "\"validation_hint\":\"run_validate_hidden_attempt_before_publish\"},"
                             "\"sidecar\":{\"anchors\":[]}}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Render after candidate exploration only." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_candidate_exploration" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class CONSTRAINT_REROUTE_EXECUTE_THEN_ROLLBACK_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "constraint reroute execute then rollback next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"hinted_mutation_rollback_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Create a route subject before rollback probe." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_create_route_subject" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"pcb.create_track_polyline\","
                             "\"arguments\":{\"points\":["
                             "{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1800000,\"y\":1000000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"route_subject\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Execute the hinted candidate plan." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_execute_candidate_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson = replacementPathScriptPlanArguments(
                        handle.dump(), true );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Rollback the hinted candidate plan." );

                nlohmann::json scriptResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.at( 1 ).m_ResultJson.ToStdString(),
                        nullptr, false );
                const uint64_t checkpointId =
                        scriptResult.value( "checkpoint_id", 0ULL );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_rollback_candidate_plan" );
                call.m_ToolName = wxS( "rollback_attempt" );
                call.m_ArgumentsJson = wxString::Format(
                        wxS( "{\"checkpoint_id\":%llu,"
                             "\"tool_call_id\":\"call_execute_candidate_plan\"}" ),
                        static_cast<unsigned long long>( checkpointId ) );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 3 )
            {
                response.m_Body = wxS( "Render after rolling back the hinted plan." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_candidate_rollback" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class CONSTRAINT_REROUTE_RENDER_BEFORE_EXECUTE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title =
                wxS( "constraint reroute render before execute next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"routing\","
                                  "\"selected_candidate_index\":0,"
                                  "\"declared_net\":\"GND\","
                                  "\"declared_layer\":\"F.Cu\","
                                  "\"reason_code\":\"stale_render_order_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Create a route subject before render-order probe." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_create_route_subject" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{"
                             "\"kind\":\"pcb.create_track_polyline\","
                             "\"arguments\":{\"points\":["
                             "{\"x\":1000000,\"y\":1000000},"
                             "{\"x\":1800000,\"y\":1000000}],"
                             "\"layer\":\"F.Cu\","
                             "\"net\":\"GND\","
                             "\"width\":150000,"
                             "\"alias\":\"route_subject\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Render too early, before executing the candidate plan." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_before_candidate_plan" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body =
                        wxS( "Execute the candidate plan after the render." );

                nlohmann::json subjectResult = nlohmann::json::parse(
                        aRequest.m_ToolResults.front().m_ResultJson.ToStdString(),
                        nullptr, false );
                nlohmann::json handle =
                        subjectResult["session_journal"]["operations"].back()
                                     ["created_handles"].front();

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_execute_candidate_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson = replacementPathScriptPlanArguments(
                        handle.dump(), true );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 3 )
            {
                response.m_Body = wxS( "Validate the post-render mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_stale_render" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson =
                        wxS( "{\"level\":\"drc_lite\",\"scope\":\"affected_area\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SCRIPT_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"script_then_render_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need render facts after the script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_script" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SCRIPT_RENDER_PUBLISH_GATE_VALIDATE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script render publish gate validate next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"gate_feedback_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            bool hasGateFeedback = false;
            bool hasValidation = false;

            for( const AI_TOOL_CALL_RECORD& result : aRequest.m_ToolResults )
            {
                if( result.m_ToolName == wxS( "preview_gate_feedback" )
                    && result.m_ResultJson.Contains(
                               wxS( "validation_freshness_failed" ) ) )
                {
                    hasGateFeedback = true;
                }

                if( result.m_ToolName == wxS( "validate_hidden_attempt" ) )
                    hasValidation = true;
            }

            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need render facts after the script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_script" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( hasGateFeedback && !hasValidation )
            {
                response.m_Body =
                        wxS( "Gate feedback requires validation after the script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_gate_feedback" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class VALIDATION_GATE_REPAIR_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "validation gate repair next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"reason_code\":\"validation_gate_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            bool hasGateFeedback = false;
            bool hasValidation = false;

            for( const AI_TOOL_CALL_RECORD& result : aRequest.m_ToolResults )
            {
                if( result.m_ToolName == wxS( "preview_gate_feedback" )
                    && result.m_ResultJson.Contains(
                               wxS( "validation_gate_failed" ) ) )
                {
                    hasGateFeedback = true;
                }

                if( result.m_ToolName == wxS( "validate_hidden_attempt" ) )
                    hasValidation = true;
            }

            if( hasGateFeedback && !hasValidation )
            {
                response.m_Body =
                        wxS( "Gate feedback requires validation repair evidence." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_validation_gate" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( hasValidation )
            {
                response.m_Body = publishReview();
                return response;
            }

            response.m_Body =
                    wxS( "{\"decision_kind\":\"publish\","
                         "\"reason_code\":\"validation_gate_is_repairable\","
                         "\"repairable_gate_failures\":[\"validation_gate_failed\"],"
                         "\"review_basis\":{\"render_valid\":true,"
                         "\"validation_passed\":true,"
                         "\"budget_within_limits\":true,"
                         "\"self_review_passed\":true}}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class RENDER_GATE_REPAIR_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "render gate repair next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"reason_code\":\"render_gate_repair_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            bool hasGateFeedback = false;
            bool hasRender = false;

            for( const AI_TOOL_CALL_RECORD& result : aRequest.m_ToolResults )
            {
                if( result.m_ToolName == wxS( "preview_gate_feedback" )
                    && result.m_ResultJson.Contains(
                               wxS( "render_gate_failed" ) ) )
                {
                    hasGateFeedback = true;
                }

                if( result.m_ToolName == wxS( "render_hidden_attempt" ) )
                    hasRender = true;
            }

            if( hasGateFeedback && !hasRender )
            {
                response.m_Body =
                        wxS( "Gate feedback requires render repair evidence." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_render_gate" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( hasRender )
            {
                response.m_Body = publishReview();
                return response;
            }

            response.m_Body =
                    wxS( "{\"decision_kind\":\"publish\","
                         "\"reason_code\":\"render_gate_is_repairable\","
                         "\"repairable_gate_failures\":[\"render_gate_failed\"],"
                         "\"review_basis\":{\"render_valid\":true,"
                         "\"validation_passed\":true,"
                         "\"budget_within_limits\":true,"
                         "\"self_review_passed\":true}}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


bool previewGateFeedbackHasReason( const wxString& aFeedbackJson,
                                   const std::string& aReason )
{
    nlohmann::json feedback = nlohmann::json::parse(
            aFeedbackJson.ToStdString(), nullptr, false );

    if( feedback.is_discarded() || !feedback.is_object()
        || !feedback.contains( "preview_gate_result" ) )
    {
        return false;
    }

    const nlohmann::json& gate = feedback["preview_gate_result"];

    if( !gate.is_object() || !gate.contains( "reasons" )
        || !gate["reasons"].is_array() )
    {
        return false;
    }

    for( const nlohmann::json& reason : gate["reasons"] )
    {
        if( reason.is_string() && reason.get<std::string>() == aReason )
            return true;
    }

    return false;
}


class SCRIPT_PUBLISH_GATE_RENDER_VALIDATE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script publish gate render validate next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"sequential_gate_feedback_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            wxString latestFeedbackJson;
            bool hasRender = false;
            bool hasValidation = false;

            for( const AI_TOOL_CALL_RECORD& result : aRequest.m_ToolResults )
            {
                if( result.m_ToolName == wxS( "preview_gate_feedback" ) )
                    latestFeedbackJson = result.m_ResultJson;

                if( result.m_ToolName == wxS( "render_hidden_attempt" ) )
                    hasRender = true;

                if( result.m_ToolName == wxS( "validate_hidden_attempt" ) )
                    hasValidation = true;
            }

            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            const bool latestFeedbackNeedsRender =
                    previewGateFeedbackHasReason(
                            latestFeedbackJson, "render_freshness_failed" );
            const bool latestFeedbackNeedsValidation =
                    previewGateFeedbackHasReason(
                            latestFeedbackJson, "validation_freshness_failed" );

            if( latestFeedbackNeedsRender && !hasRender )
            {
                response.m_Body = wxS( "Gate feedback requires render after mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_gate_feedback" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( latestFeedbackNeedsValidation && !latestFeedbackNeedsRender
                && hasRender && !hasValidation )
            {
                response.m_Body =
                        wxS( "Gate feedback requires validation after render." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_gate_feedback" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class REPAIR_THEN_RENDER_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "repair then render next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"repair_then_render_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need repair execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_repair_plan" );
                call.m_ToolName = wxS( "repair_apply_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1700000,\"y\":2500000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"repair_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need render facts after the repair mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_repair" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SCRIPT_THEN_VALIDATE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script then validate next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"script_then_validate_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Need validation facts after the script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_script" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need render facts after the script mutation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_render_after_script" );
                call.m_ToolName = wxS( "render_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class SCRIPT_ROLLBACK_VALIDATE_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "script rollback validate next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0,"
                                  "\"reason_code\":\"script_rollback_validate_probe\"}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"script_via_1\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Rollback the script batch before validation." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_rollback_script" );
                call.m_ToolName = wxS( "rollback_attempt" );
                call.m_ArgumentsJson =
                        wxS( "{\"checkpoint_id\":1,"
                             "\"tool_call_id\":\"call_script_plan\"}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 2 )
            {
                response.m_Body = wxS( "Need validation facts after rollback." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_validate_after_rollback" );
                call.m_ToolName = wxS( "validate_hidden_attempt" );
                call.m_ArgumentsJson = wxS( "{}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = publishReview();
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class INVALID_ROLLBACK_AFTER_SCRIPT_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "invalid rollback after script next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                                  "\"opportunity_type\":\"placement\","
                                  "\"selected_candidate_index\":0}" );
            return response;
        }

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need bounded script plan execution facts." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_script_plan" );
                call.m_ToolName = wxS( "script_run_bounded_plan" );
                call.m_ArgumentsJson =
                        wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                             "\"arguments\":{\"position\":{\"x\":1600000,\"y\":2400000},"
                             "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                             "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                             "\"alias\":\"invalid_rollback_via\"}}]},\"max_steps\":4}" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( aRequest.m_ToolResults.size() == 1 )
            {
                response.m_Body = wxS( "Try rollback with invalid JSON." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_invalid_rollback" );
                call.m_ToolName = wxS( "rollback_attempt" );
                call.m_ArgumentsJson = wxS( "{" );
                response.m_ToolCalls.push_back( call );
                return response;
            }

            response.m_Body = wxS( "{\"decision_kind\":\"abandon\","
                                  "\"reason_code\":\"invalid_rollback_result_seen\"}" );
            return response;
        }

        response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
};


class RECORDING_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_AppliedObjects.push_back( aObject );
        return true;
    }

    std::vector<AI_OBJECT_REF> m_AppliedObjects;
};


class ACCEPT_BLOCKING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"issue_count\":0,"
                     "\"accept_validation_sufficient\":false,"
                     "\"accept_validation_reason\":\"accept-grade DRC required\"}}" );
        return result;
    }

    int m_RunCount = 0;
};


class INEXACT_ACCEPT_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"level\":\"full_drc\","
                     "\"preview_state_exact\":false,"
                     "\"accept_validation_sufficient\":true,"
                     "\"accept_validation_reason\":\"native result was not exact\"}}" );
        return result;
    }

    int m_RunCount = 0;
};


class SLOW_PREVIEW_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION&, const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;
        std::this_thread::sleep_for( std::chrono::milliseconds( m_SleepMs ) );

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = true;
        result.m_ResultJson =
                wxS( "{\"validation\":{\"status\":\"validated\","
                     "\"issue_count\":0}}" );
        return result;
    }

    int      m_RunCount = 0;
    uint64_t m_SleepMs = 300;
};


class PASSING_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aRenderArgs ) override
    {
        ++m_RenderCount;
        m_RenderArgs.push_back( aRenderArgs.ToStdString() );

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = true;
        result.m_PreviewId = 900 + m_RenderCount;
        result.m_RenderedItemCount = aSession.ShadowBoard().LiveItemCount();
        result.m_ResultJson =
                wxS( "{\"status\":\"preview_rendered\","
                     "\"render_valid\":true,"
                     "\"native_preview\":true}" );
        return result;
    }

    void ClearPreview( uint64_t ) override {}

    int                      m_RenderCount = 0;
    std::vector<std::string> m_RenderArgs;
};


class FIRST_FAILING_THEN_PASSING_SESSION_PREVIEW_SERVICE :
        public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aRenderArgs ) override
    {
        wxUnusedVar( aSession );
        wxUnusedVar( aRenderArgs );
        ++m_RenderCount;

        AI_SESSION_PREVIEW_RESULT result;
        result.m_Ok = m_RenderCount > 1;
        result.m_PreviewId = 1200 + m_RenderCount;
        result.m_RenderedItemCount = result.m_Ok ? 1 : 0;
        result.m_ResultJson =
                result.m_Ok
                        ? wxS( "{\"status\":\"preview_rendered\","
                               "\"render_valid\":true,"
                               "\"native_preview\":true}" )
                        : wxS( "{\"status\":\"render_failed\","
                               "\"render_valid\":false,"
                               "\"native_preview\":true,"
                               "\"message\":\"temporary render failure\"}" );
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


class BLOCKING_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"issue_count\":1,"
                     "\"issues\":[{\"kind\":\"geometry_overlap\","
                     "\"severity\":\"warning\","
                     "\"blocking\":true,"
                     "\"message\":\"candidate overlaps existing footprint\"}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class FIRST_BLOCKING_THEN_PASSING_SESSION_VALIDATION_SERVICE :
        public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION&, const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = true;

        if( m_RunCount == 1 )
        {
            result.m_ResultJson =
                    wxS( "{\"validation\":{\"status\":\"validated\","
                         "\"issue_count\":1,"
                         "\"issues\":[{\"kind\":\"geometry_overlap\","
                         "\"severity\":\"warning\","
                         "\"blocking\":true,"
                         "\"message\":\"candidate overlaps existing footprint\"}]}}" );
        }
        else
        {
            result.m_ResultJson =
                    wxS( "{\"validation\":{\"status\":\"validated\","
                         "\"issue_count\":0}}" );
        }

        return result;
    }

    int m_RunCount = 0;
};


class RULE_LOAD_BLOCKING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":0,"
                     "\"rule_load\":{\"status\":\"warning\","
                     "\"blocks_publish\":true,"
                     "\"message\":\"project rule file was not fully loaded\"},"
                     "\"connectivity\":{\"status\":\"current\"},"
                     "\"refill\":{\"status\":\"not_required\"}}}" );
        return result;
    }

    int m_RunCount = 0;
};


class STALE_DERIVED_STATE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":0,"
                     "\"rule_load\":{\"status\":\"loaded\"},"
                     "\"connectivity\":{\"status\":\"stale\","
                     "\"message\":\"connectivity graph needs rebuild\"},"
                     "\"refill\":{\"status\":\"required\","
                     "\"message\":\"zone refill is required before preview\"}}}" );
        return result;
    }

    int m_RunCount = 0;
};


class GEOMETRY_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":1,"
                     "\"issues\":[{\"kind\":\"clearance\","
                     "\"severity\":\"warning\","
                     "\"message\":\"candidate is close to an existing track\","
                     "\"geometry\":{\"bbox\":{\"x\":10,\"y\":20,\"w\":30,\"h\":40},"
                     "\"layer\":\"F.Cu\","
                     "\"net\":\"GND\"}}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class PIXEL_BOUNDS_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":1,"
                     "\"issues\":[{\"kind\":\"clearance\","
                     "\"key\":\"clearance\","
                     "\"title\":\"Clearance violation\","
                     "\"severity\":\"warning\","
                     "\"message\":\"candidate is too close to pad A\","
                     "\"pixel_bounds\":{\"left\":30,\"top\":20,"
                     "\"right\":46,\"bottom\":32},"
                     "\"world_bounds\":{\"left\":1300,\"top\":2200,"
                     "\"right\":1460,\"bottom\":2320},"
                     "\"layer_name\":\"F.Cu\"}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class WORLD_BOUNDS_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":1,"
                     "\"issues\":[{\"kind\":\"clearance\","
                     "\"key\":\"clearance\","
                     "\"title\":\"Clearance violation\","
                     "\"severity\":\"warning\","
                     "\"message\":\"candidate is too close to pad A\","
                     "\"world_bounds\":{\"left\":1300,\"top\":2200,"
                     "\"right\":1460,\"bottom\":2320},"
                     "\"layer_name\":\"F.Cu\"}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class ITEM_BBOX_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":1,"
                     "\"issues\":[{\"kind\":\"clearance\","
                     "\"severity\":\"warning\","
                     "\"message\":\"candidate overlaps another net\","
                     "\"main_item_uuid\":\"main-uuid\","
                     "\"main_item_bbox\":{\"x\":10,\"y\":20,"
                     "\"width\":30,\"height\":40},"
                     "\"aux_item_uuid\":\"aux-uuid\","
                     "\"aux_item_bbox\":{\"x\":50,\"y\":60,"
                     "\"width\":70,\"height\":80},"
                     "\"layer_name\":\"F.Cu\"}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class POSITION_ISSUE_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                wxS( "{\"validation\":{\"status\":\"native_checked\","
                     "\"backend\":\"native_drc\","
                     "\"grade\":\"preview\","
                     "\"issue_count\":1,"
                     "\"issues\":[{\"source\":\"pcbnew.drc_engine\","
                     "\"code\":17,"
                     "\"key\":\"clearance\","
                     "\"title\":\"Clearance violation\","
                     "\"message\":\"track is too close to pad\","
                     "\"severity\":\"warning\","
                     "\"position\":{\"x\":123,\"y\":456},"
                     "\"layer\":0,"
                     "\"layer_name\":\"F.Cu\"}]}}" );
        return result;
    }

    int m_RunCount = 0;
};


class SUMMARY_FACTS_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
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
                     "\"backend\":\"native_drc\","
                     "\"scope\":\"affected_area\","
                     "\"grade\":\"preview\","
                     "\"exactness\":\"preview_state\","
                     "\"issue_count\":0,"
                     "\"rule_load\":{\"status\":\"loaded\"},"
                     "\"connectivity\":{\"status\":\"current\"},"
                     "\"refill\":{\"status\":\"not_required\"}}}" );
        return result;
    }

    int m_RunCount = 0;
};


class TRACKING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession, const wxString& aValidationArgs,
            const wxString& ) override
    {
        ++m_RunCount;
        m_LiveItemCounts.push_back( aSession.ShadowBoard().LiveItemCount() );
        m_BoardIds.push_back( aSession.BoardId().ToStdString() );
        m_CheckpointCounts.push_back( aSession.Checkpoints().size() );
        m_ValidationArgs.push_back( aValidationArgs.ToStdString() );

        AI_SESSION_VALIDATION_RESULT result;
        result.m_Ok = true;
        result.m_ResultJson =
                wxString::Format(
                        wxS( "{\"validation\":{\"status\":\"validated\","
                             "\"issue_count\":0,"
                             "\"shadow_live_item_count\":%zu}}" ),
                        aSession.ShadowBoard().LiveItemCount() );
        return result;
    }

    int                 m_RunCount = 0;
    std::vector<size_t> m_LiveItemCounts;
    std::vector<std::string> m_BoardIds;
    std::vector<size_t> m_CheckpointCounts;
    std::vector<std::string> m_ValidationArgs;
};


struct PUBLISH_READY_NEXT_ACTION_SERVICES
{
    PASSING_SESSION_VALIDATION_SERVICE m_Validation;
    PASSING_SESSION_PREVIEW_SERVICE    m_Preview;
};


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


AI_OBJECT_REF trackRef( int aStartX, int aStartY, int aEndX, int aEndY,
                        const wxString& aNetName = wxS( "GND" ) )
{
    wxString details;
    details << wxS( "{\"kind\":\"track\",\"start\":{\"x\":" ) << aStartX
            << wxS( ",\"y\":" ) << aStartY << wxS( "},\"end\":{\"x\":" )
            << aEndX << wxS( ",\"y\":" ) << aEndY
            << wxS( "},\"layer\":\"F.Cu\",\"width\":150000,\"net_name\":\"" )
            << aNetName << wxS( "\"}" );

    return AI_OBJECT_REF( KIID(), PCB_TRACE_T,
                          wxString::Format( wxS( "track:%d,%d->%d,%d" ),
                                            aStartX, aStartY, aEndX, aEndY ),
                          details );
}


AI_OBJECT_REF footprintRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_FOOTPRINT_T, wxS( "footprint:U1" ),
            wxS( "{\"kind\":\"footprint\",\"reference\":\"U1\","
                 "\"value\":\"LDO-3V3\","
                 "\"bbox\":{\"x\":150,\"y\":40,\"width\":100,\"height\":80},"
                 "\"courtyard_bbox\":{\"x\":140,\"y\":30,\"width\":120,\"height\":100},"
                 "\"layer\":\"F.Cu\"}" ) );
}


AI_OBJECT_REF padRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_PAD_T, wxS( "pad:U4.1" ),
            wxS( "{\"kind\":\"pad\",\"footprint_reference\":\"U4\","
                 "\"footprint_value\":\"SensorAFE\",\"number\":\"1\","
                 "\"position\":{\"x\":240,\"y\":210},"
                 "\"bbox\":{\"x\":220,\"y\":190,\"width\":40,\"height\":40},"
                 "\"layer\":\"F.Cu\",\"net_name\":\"GND\"}" ) );
}


AI_OBJECT_REF keepoutRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_ZONE_T, wxS( "keepout:J1" ),
            wxS( "{\"kind\":\"zone\",\"zone_kind\":\"keepout\",\"name\":\"J1\","
                 "\"bbox\":{\"x\":210,\"y\":20,\"width\":90,\"height\":70},"
                 "\"layers\":{\"names\":[\"F.Cu\",\"B.Cu\"]},"
                 "\"has_keepout\":true,"
                 "\"keepout\":{\"tracks\":true,\"vias\":true,"
                 "\"pads\":true,\"footprints\":true,\"zone_fills\":false}}" ) );
}


AI_CONTEXT_ANCHOR contextAnchor( const wxString& aId,
                                 AI_CONTEXT_ANCHOR_KIND aKind,
                                 const wxString& aLabel,
                                 int aX,
                                 int aY )
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aLabel;
    anchor.m_Position = VECTOR2I( aX, aY );
    anchor.m_HasPosition = true;
    anchor.m_Confidence = 0.75;
    return anchor;
}


AI_SUGGESTION_TRIGGER makeViaTrigger()
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
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 100, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 200, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 300, 50 ) );
    trigger.m_Activity.m_Sequence = 44;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.placeVia" );
    trigger.m_Reason = wxS( "cursor paused" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeActiveViaPlacementTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_VisibleObjects.clear();
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"placing_via\",\"net\":\"\","
                 "\"diameter\":700000,\"drill\":330000,"
                 "\"cursor\":{\"x\":710,\"y\":820}}" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeMouseMoveTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_Activity.m_Sequence = 45;
    trigger.m_Activity.m_ActionName = wxS( "mouse.move" );
    return trigger;
}


wxString routingModeContext()
{
    return wxS( "{\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000,"
                "\"start\":{\"x\":100,\"y\":200},"
                "\"cursor\":{\"x\":260,\"y\":200}}" );
}


AI_SUGGESTION_TRIGGER makeRoutingTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 21;
    trigger.m_ContextVersion.m_ViewRevision = 8;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson = routingModeContext();
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    trigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 240, 220 );
    trigger.m_Activity.m_Sequence = 52;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.InteractiveRouter.route" );
    trigger.m_Reason = wxS( "routing active" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeDrawingZoneTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 24;
    trigger.m_ContextVersion.m_ViewRevision = 9;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::DrawingZone;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"drawing_zone\",\"layer\":\"Dwgs.User\","
                 "\"cursor\":{\"x\":1000,\"y\":2000}}" );
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    trigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition =
            VECTOR2I( 1000, 2000 );
    trigger.m_Activity.m_Sequence = 53;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.InteractiveDrawing.zone" );
    trigger.m_Reason = wxS( "drawing zone active" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeCopperZoneDrawingTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeDrawingZoneTrigger();
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"drawing_zone\",\"layer\":\"F.Cu\","
                 "\"net\":\"GND\","
                 "\"cursor\":{\"x\":1000,\"y\":2000}}" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeAutofillTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 31;
    trigger.m_ContextVersion.m_ViewRevision = 2;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "properties" );
    panel.m_Title = wxS( "Properties" );
    panel.m_FocusedControlId = wxS( "net_class" );
    panel.m_FocusedControlLabel = wxS( "Net class" );
    panel.m_Summary = wxS( "Net properties table" );
    panel.m_StateJson = wxS( "{\"schema_version\":\"net-class-v1\","
                             "\"surface_revision\":42,"
                             "\"selection_fingerprint\":\"cell:1:class\","
                             "\"overlap_set\":[\"row.default\",\"row.power\"],"
                             "\"columns\":[\"net\",\"class\"],"
                             "\"row_count\":4,"
                             "\"target_scope\":{\"kind\":\"cell\","
                             "\"row\":1,\"column\":\"class\"},"
                             "\"neighbor_values\":["
                             "{\"row\":0,\"column\":\"class\","
                             "\"value\":\"Default\"}],"
                             "\"value_provenance\":{\"class\":\"project_default\"},"
                             "\"validation_state\":{\"status\":\"needs_value\"}}" );
    trigger.m_ContextSnapshot.m_PanelStates.push_back( panel );

    trigger.m_Activity.m_Sequence = 62;
    trigger.m_Activity.m_ActionName = wxS( "panel.properties.focusCell" );
    trigger.m_Reason = wxS( "panel focus" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeAgentPanelSelfSurfaceTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 32;
    trigger.m_ContextVersion.m_ViewRevision = 3;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "agent.panel" );
    panel.m_Title = wxS( "Agent" );
    panel.m_FocusedControlId = wxS( "agent.input" );
    panel.m_FocusedControlLabel = wxS( "Agent input" );
    panel.m_Summary = wxS( "KiSurf agent chat input" );
    panel.m_StateJson = wxS( "{\"schema_version\":\"agent-panel-v1\","
                             "\"focused_control\":\"agent.input\"}" );
    trigger.m_ContextSnapshot.m_PanelStates.push_back( panel );

    trigger.m_Activity.m_Sequence = 63;
    trigger.m_Activity.m_ActionName = wxS( "agent.panel.focusInput" );
    trigger.m_Reason = wxS( "agent panel self focus" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeIdleMouseMoveTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeAutofillTrigger();
    trigger.m_Activity.m_Sequence = 46;
    trigger.m_Activity.m_ActionName = wxS( "mouse.move" );
    trigger.m_Reason = wxS( "raw cursor move" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makePanelFillTriggerWithTargetScope()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 33;
    trigger.m_ContextVersion.m_ViewRevision = 4;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Unknown;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.rules.row.default.class" );
    panel.m_FocusedControlLabel = wxS( "Net class" );
    panel.m_StateJson = wxS( "{\"schema_version\":\"net-class-v1\","
                             "\"target_scope\":{\"kind\":\"column\","
                             "\"panel_id\":\"board_setup.clearance\","
                             "\"table_id\":\"clearance.rules\","
                             "\"column\":\"class\"},"
                             "\"focused_cell\":{\"table_id\":\"clearance.rules\","
                             "\"row_id\":\"row.default\","
                             "\"column_id\":\"class\"},"
                             "\"tables\":[{\"id\":\"clearance.rules\","
                             "\"title\":\"Clearance rules\","
                             "\"columns\":[{\"id\":\"class\","
                             "\"label\":\"Class\"}],"
                             "\"rows\":["
                             "{\"id\":\"row.default\",\"label\":\"Default\","
                             "\"cells\":{\"class\":\"Signal\"}},"
                             "{\"id\":\"row.power\",\"label\":\"Power\","
                             "\"cells\":{\"class\":\"\"}},"
                             "{\"id\":\"row.gpio\",\"label\":\"GPIO\","
                             "\"cells\":{\"class\":\"\"}}]}]}" );
    trigger.m_ContextSnapshot.m_PanelStates.push_back( panel );

    trigger.m_Activity.m_Sequence = 64;
    trigger.m_Activity.m_ActionName = wxS( "panel.properties.focusColumn" );
    trigger.m_Reason = wxS( "panel fill focus" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeChangedViaTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextVersion.m_ViewRevision = 6;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_Activity.m_Sequence = 45;
    trigger.m_Reason = wxS( "context advanced" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeViewportBoundViaTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    trigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 1200, 2200 );
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"viewport\":{\"center\":{\"x\":1000,\"y\":2000},"
                 "\"zoom\":3.5,\"width\":640,\"height\":480},"
                 "\"cursor_region\":{\"x\":1000,\"y\":2000,"
                 "\"width\":500,\"height\":500}}" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeViewportDriftedViaTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViewportBoundViaTrigger();
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"viewport\":{\"center\":{\"x\":9000,\"y\":2000},"
                 "\"zoom\":3.5,\"width\":640,\"height\":480},"
                 "\"cursor_region\":{\"x\":1000,\"y\":2000,"
                 "\"width\":500,\"height\":500}}" );
    return trigger;
}


AI_NEXT_ACTION_CONTEXT_VERSION makeDependencyContext(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    AI_NEXT_ACTION_CONTEXT_VERSION context;
    context.m_ContextVersion = aTrigger.m_ContextVersion;
    context.m_ToolModeVersion =
            static_cast<uint64_t>( aTrigger.m_ContextSnapshot.m_ToolState.m_Kind );
    context.m_UiFocusVersion =
            static_cast<uint64_t>( aTrigger.m_ContextSnapshot.m_PanelStates.size() );
    context.m_ActivitySequence = aTrigger.m_Activity.m_Sequence;
    return context;
}


wxString publishReview()
{
    return wxS( "{\"decision_kind\":\"publish\","
                "\"reason_code\":\"acceptable\","
                "\"review_basis\":{\"render_valid\":true,"
                "\"validation_passed\":true,"
                "\"budget_within_limits\":true,"
                "\"self_review_passed\":true}}" );
}


wxString repositoryGoldenDatasetPath( const wxString& aFileName )
{
    wxFileName datasetPath( KI_TEST::GetTestDataRootDir() );
    datasetPath.AppendDir( wxS( "ai" ) );
    datasetPath.AppendDir( wxS( "next_action" ) );
    datasetPath.AppendDir( wxS( "golden" ) );
    datasetPath.SetFullName( aFileName );
    return datasetPath.GetFullPath();
}


nlohmann::json providerRequestJson( const AI_PROVIDER_REQUEST& aRequest )
{
    return nlohmann::json::parse( aRequest.m_UserText.ToStdString() );
}


std::vector<AI_TOOL_CALL_RECORD> toolResultsWithoutPreviewGateFeedback(
        const AI_PROVIDER_REQUEST& aRequest )
{
    std::vector<AI_TOOL_CALL_RECORD> results;

    for( const AI_TOOL_CALL_RECORD& result : aRequest.m_ToolResults )
    {
        if( result.m_ToolName == wxS( "preview_gate_feedback" ) )
            continue;

        results.push_back( result );
    }

    return results;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiNextActionRuntime )


BOOST_AUTO_TEST_CASE( SchedulerSuppressesRawMouseMoveOutsideActiveToolState )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( !scheduler.BuildSemanticEvent(
                           makeIdleMouseMoveTrigger() )
                          .has_value() );
    BOOST_CHECK( scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );
}


BOOST_AUTO_TEST_CASE( SchedulerIgnoresAgentPanelSelfSurface )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( !scheduler.BuildSemanticEvent(
                           makeAgentPanelSelfSurfaceTrigger() )
                          .has_value() );
    BOOST_CHECK( scheduler.BuildSemanticEvent( makeAutofillTrigger() ).has_value() );
}


BOOST_AUTO_TEST_CASE( SchedulerAllowsMouseMoveDuringActiveRoutingOrPlacement )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( scheduler.BuildSemanticEvent( makeMouseMoveTrigger() ).has_value() );

    AI_NEXT_ACTION_SCHEDULER routingScheduler;
    AI_SUGGESTION_TRIGGER routing = makeRoutingTrigger();
    routing.m_Activity.m_ActionName = wxS( "cursor.move" );
    routing.m_Reason = wxS( "active routing cursor move" );
    BOOST_CHECK( routingScheduler.BuildSemanticEvent( routing ).has_value() );
}


BOOST_AUTO_TEST_CASE( SchedulerDebouncesSameSemanticSlot )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );
    BOOST_CHECK( !scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );

    AI_SUGGESTION_TRIGGER changed = makeViaTrigger();
    changed.m_ContextVersion.m_ViewRevision = 99;
    changed.m_ContextSnapshot.m_Version = changed.m_ContextVersion;
    changed.m_ContextSnapshot.m_ToolState.m_ContextVersion = changed.m_ContextVersion;

    BOOST_CHECK( scheduler.BuildSemanticEvent( changed ).has_value() );
}


BOOST_AUTO_TEST_CASE( ToolCatalogDeclaresLayeredMutationToolsAndNoDirectPublish )
{
    AI_NEXT_ACTION_TOOL_REGISTRY tools;
    const wxString catalog = tools.ToolCatalogJson();
    const nlohmann::json catalogJson =
            nlohmann::json::parse( catalog.ToStdString() );

    auto catalogTool =
            [&]( const std::string& aName ) -> const nlohmann::json*
            {
                for( const nlohmann::json& tool : catalogJson )
                {
                    if( tool.is_object()
                        && tool.value( "name", std::string() ) == aName )
                    {
                        return &tool;
                    }
                }

                return nullptr;
            };

    BOOST_CHECK( !catalog.Contains( wxS( ".generate_" ) ) );
    BOOST_CHECK( !catalog.Contains( wxS( "_candidates" ) ) );
    BOOST_REQUIRE( catalogTool( "render.hidden_attempt" ) );
    BOOST_CHECK_EQUAL(
            catalogTool( "render.hidden_attempt" )->value( "namespace", std::string() ),
            "runtime" );
    BOOST_REQUIRE( catalogTool( "atomic.run_operation" ) );
    BOOST_CHECK_EQUAL(
            catalogTool( "atomic.run_operation" )->value( "namespace", std::string() ),
            "atomic" );
    BOOST_CHECK_EQUAL(
            catalogTool( "atomic.run_operation" )->value( "layer", std::string() ),
            "atomic" );
    BOOST_CHECK_EQUAL(
            catalogTool( "atomic.run_operation" )->value( "role", std::string() ),
            "hidden_mutation" );
    BOOST_CHECK( !catalogTool( "atomic.run_operation" )->value( "can_publish", true ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"integrated\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"side_effect\":\"read_only\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"atomic\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"side_effect\":\"shadow_mutation\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"script.run_bounded_plan\"" ) ) );
    BOOST_REQUIRE( catalogTool( "script.run_bounded_plan" ) );
    BOOST_CHECK_EQUAL(
            catalogTool( "script.run_bounded_plan" )->value( "namespace", std::string() ),
            "script" );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"script\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"bounded_batch_composition\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"repair.apply_bounded_plan\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"bounded_repair\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"placement.repair_via\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "empty string for NoNet" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"placement.repair_move_items\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"placement.repair_footprint_orientation\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"placement_repair\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"routing.repair_segment\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"routing.repair_polyline\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"routing.repair_bus_segments\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"routing_repair\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"surface.repair_patch\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"surface_repair\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"raw_board_access\":false" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"publication_policy\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"runtime_gate_owned\":true" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"required_review_decision\":\"publish\"" ) ) );
    BOOST_CHECK( !catalog.Contains( wxS( "\"name\":\"publish.preview\"" ) ) );

    bool sawPublicationPolicy = false;
    for( const nlohmann::json& entry : catalogJson )
    {
        if( entry.is_object() && entry.contains( "publication_policy" ) )
        {
            sawPublicationPolicy = true;
            BOOST_CHECK( !entry.contains( "name" ) );
            BOOST_CHECK( !entry.contains( "namespace" ) );
            BOOST_CHECK( !entry.contains( "layer" ) );
        }
    }
    BOOST_CHECK( sawPublicationPolicy );

    BOOST_CHECK( !catalog.Contains( wxS( "\"can_publish\":true" ) ) );
    BOOST_CHECK( !catalog.Contains( wxS( "\"name\":\"candidate.generate\"" ) ) );
    BOOST_CHECK( !catalog.Contains(
            wxS( "AI_VIA_PATTERN_NEXT_ACTION_PROVIDER" ) ) );
    BOOST_CHECK( !catalog.Contains(
            wxS( "AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER" ) ) );
    BOOST_CHECK( !catalog.Contains(
            wxS( "AI_PANEL_TABLE_NEXT_ACTION_PROVIDER" ) ) );
}


BOOST_AUTO_TEST_CASE( CallableToolCatalogUsesProviderFunctionToolSchema )
{
    AI_NEXT_ACTION_TOOL_REGISTRY tools;
    nlohmann::json callable =
            nlohmann::json::parse( tools.CallableToolCatalogJson().ToStdString() );

    BOOST_REQUIRE( callable.is_array() );
    BOOST_CHECK( !callable.empty() );

    bool sawScriptTool = false;
    bool sawVisualReferenceTool = false;
    bool sawVisualReferenceRequiredReference = false;
    bool sawVisualReferenceOptionalSidecarJson = false;
    bool sawAtomicRunTool = false;
    bool sawAtomicRunCreateViaKind = false;
    bool sawAtomicRunOperationContracts = false;
    bool sawRepairTool = false;
    bool sawPlacementRepairTool = false;
    bool sawPlacementMoveRepairTool = false;
    bool sawPlacementOrientationRepairTool = false;
    bool sawRoutingRepairTool = false;
    bool sawRoutingPolylineRepairTool = false;
    bool sawRoutingBusRepairTool = false;
    bool sawSurfaceRepairTool = false;
    bool sawScriptSurfacePatchKind = false;
    bool sawScriptShapePolygonContract = false;
    bool sawScriptGeometryPatchContract = false;
    bool sawScriptZoneOutlineContract = false;
    bool sawScriptTypedPropsContract = false;
    bool sawScriptAffectedAreaContract = false;
    bool sawScriptSurfacePatchFillOpsContract = false;
    bool sawRepairSurfacePatchKind = false;
    bool sawRepairAffectedAreaContract = false;
    bool sawRepairSurfacePatchFillOpsContract = false;
    bool sawAtomicRunAffectedAreaContract = false;
    bool sawAtomicRunSurfacePatchFillOpsContract = false;
    bool sawScriptMaintenanceScopeContract = false;
    bool sawRepairMaintenanceScopeContract = false;
    bool sawAtomicRunMaintenanceScopeContract = false;
    bool sawPlacementRepairRequiredPosition = false;
    bool sawPlacementMoveRepairRequiredHandles = false;
    bool sawPlacementOrientationRepairRequiredFacts = false;
    bool sawRoutingRepairRequiredSegment = false;
    bool sawRoutingPolylineRepairRequiredPoints = false;
    bool sawRoutingBusRepairRequiredSegments = false;
    bool sawSurfaceRepairRequiredPatch = false;
    bool sawSurfaceRepairExpectedMetadata = false;
    bool sawSurfaceRepairWritePolicy = false;
    bool sawSurfaceRepairPatchFillOpsContract = false;
    bool sawPlacementRepairPointSchema = false;
    bool sawPlacementMoveDeltaPointSchema = false;
    bool sawRoutingRepairSegmentPointSchema = false;
    bool sawRoutingPolylinePointSchema = false;
    bool sawRoutingBusSegmentPointSchema = false;
    bool sawAtomicRunRelativePointExpressionContract = false;
    bool sawPlacementMoveHandleSchema = false;
    bool sawPlacementOrientationRepairHandleSchema = false;
    bool sawRoutingNamespaceDescription = false;
    bool sawScriptNamespaceDescription = false;
    bool sawSurfaceNamespaceDescription = false;
    bool sawRenderHiddenAttemptTypedArgs = false;
    bool sawValidateHiddenAttemptTypedArgs = false;

    auto pointSchemaRequiresXY =
            []( const nlohmann::json& aSchema )
            {
                auto schemaDirectlyRequiresXY =
                        []( const nlohmann::json& aCandidate )
                        {
                            if( !aCandidate.is_object()
                                || !aCandidate.contains( "properties" )
                                || !aCandidate["properties"].is_object()
                                || !aCandidate.contains( "required" )
                                || !aCandidate["required"].is_array() )
                            {
                                return false;
                            }

                            const nlohmann::json& properties =
                                    aCandidate["properties"];
                            bool hasX = properties.contains( "x" );
                            bool hasY = properties.contains( "y" );
                            bool requiresX = false;
                            bool requiresY = false;

                            for( const nlohmann::json& value :
                                 aCandidate["required"] )
                            {
                                if( !value.is_string() )
                                    continue;

                                if( value.get<std::string>() == "x" )
                                    requiresX = true;

                                if( value.get<std::string>() == "y" )
                                    requiresY = true;
                            }

                            return hasX && hasY && requiresX && requiresY
                                   && aCandidate.value( "additionalProperties",
                                                        true ) == false;
                        };

                if( schemaDirectlyRequiresXY( aSchema ) )
                    return true;

                if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
                    || !aSchema["anyOf"].is_array() )
                {
                    return false;
                }

                for( const nlohmann::json& variant : aSchema["anyOf"] )
                    if( schemaDirectlyRequiresXY( variant ) )
                        return true;

                return false;
            };

    auto pointSchemaSupportsRelativeExpressions =
            []( const nlohmann::json& aSchema )
            {
                if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
                    || !aSchema["anyOf"].is_array() )
                {
                    return false;
                }

                bool sawRelativeTo = false;
                bool sawBetween = false;

                for( const nlohmann::json& variant : aSchema["anyOf"] )
                {
                    if( !variant.is_object() || !variant.contains( "properties" )
                        || !variant["properties"].is_object() )
                    {
                        continue;
                    }

                    const nlohmann::json& properties = variant["properties"];
                    sawRelativeTo =
                            sawRelativeTo
                            || ( properties.contains( "relative_to" )
                                 && properties.contains( "offset" ) );
                    sawBetween =
                            sawBetween
                            || ( properties.contains( "between" )
                                 && properties.contains( "percent" ) );
                }

                return sawRelativeTo && sawBetween;
            };

    auto boxSchemaSupportsCanonicalForms =
            [&]( const nlohmann::json& aSchema )
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
                    if( !variant.is_object()
                        || !variant.contains( "properties" )
                        || !variant["properties"].is_object() )
                    {
                        continue;
                    }

                    const nlohmann::json& properties = variant["properties"];

                    sawOriginSizeBox =
                            sawOriginSizeBox
                            || ( properties.contains( "x" )
                                 && properties.contains( "y" )
                                 && properties.contains( "width" )
                                 && properties.contains( "height" ) );

                    sawMinMaxBox =
                            sawMinMaxBox
                            || ( properties.contains( "min" )
                                 && properties.contains( "max" )
                                 && pointSchemaRequiresXY( properties["min"] )
                                 && pointSchemaRequiresXY( properties["max"] ) );
                }

                return sawOriginSizeBox && sawMinMaxBox;
            };

    auto stringEnumContainsAll =
            []( const nlohmann::json& aSchema,
                std::initializer_list<const char*> aValues )
            {
                if( !aSchema.is_object()
                    || aSchema.value( "type", std::string() ) != "string"
                    || !aSchema.contains( "enum" )
                    || !aSchema["enum"].is_array() )
                {
                    return false;
                }

                for( const char* value : aValues )
                {
                    if( std::find( aSchema["enum"].begin(),
                                   aSchema["enum"].end(), value )
                        == aSchema["enum"].end() )
                    {
                        return false;
                    }
                }

                return true;
            };

    auto surfacePatchSchemaDeclaresFillOps =
            [&]( const nlohmann::json& aPatchSchema )
            {
                if( !aPatchSchema.is_object()
                    || aPatchSchema.value( "type", std::string() ) != "object"
                    || !aPatchSchema.contains( "properties" )
                    || !aPatchSchema["properties"].contains( "operations" ) )
                {
                    return false;
                }

                const nlohmann::json& operations =
                        aPatchSchema["properties"]["operations"];

                if( !operations.is_object()
                    || operations.value( "type", std::string() ) != "array"
                    || !operations.contains( "items" )
                    || !operations["items"].contains( "properties" )
                    || !operations["items"]["properties"].contains( "op" ) )
                {
                    return false;
                }

                const nlohmann::json& opSchema =
                        operations["items"]["properties"]["op"];

                if( !stringEnumContainsAll(
                            opSchema,
                            { "set_cell", "fill_row", "fill_column",
                              "fill_range", "set_field", "set_property" } ) )
                {
                    return false;
                }

                const nlohmann::json& opProperties =
                        operations["items"]["properties"];

                return opProperties.contains( "row_id" )
                       && opProperties.contains( "column_id" )
                       && opProperties.contains( "field_id" )
                       && opProperties.contains( "property_id" )
                       && opProperties.contains( "value" )
                       && opProperties.contains( "values" )
                       && opProperties.contains( "cells" );
            };

    auto contractsDeclareMaintenanceScopes =
            [&]( const nlohmann::json& aContracts )
            {
                return aContracts.contains( "pcb.rebuild_connectivity" )
                       && aContracts["pcb.rebuild_connectivity"].contains(
                                  "properties" )
                       && aContracts["pcb.rebuild_connectivity"]["properties"]
                                  .contains( "scope" )
                       && stringEnumContainsAll(
                                  aContracts["pcb.rebuild_connectivity"]
                                            ["properties"]["scope"],
                                  { "session", "affected_area", "selection",
                                    "region" } )
                       && aContracts.contains( "pcb.run_validation" )
                       && aContracts["pcb.run_validation"].contains(
                                  "properties" )
                       && aContracts["pcb.run_validation"]["properties"]
                                  .contains( "scope" )
                       && stringEnumContainsAll(
                                  aContracts["pcb.run_validation"]["properties"]
                                            ["scope"],
                                  { "session", "affected_area", "selection",
                                    "region" } )
                       && stringEnumContainsAll(
                                  aContracts["pcb.run_validation"]["properties"]
                                            ["level"],
                                  { "geometry", "drc_lite", "full_drc" } );
            };

    auto handleSchemaRequiresHandleId =
            []( const nlohmann::json& aSchema )
            {
                if( !aSchema.is_object()
                    || !aSchema.contains( "properties" )
                    || !aSchema["properties"].is_object()
                    || !aSchema.contains( "required" )
                    || !aSchema["required"].is_array() )
                {
                    return false;
                }

                const nlohmann::json& properties = aSchema["properties"];
                bool hasHandleId = properties.contains( "handle_id" );
                bool hasGeneration = properties.contains( "generation" );
                bool hasAlias = properties.contains( "alias" );
                bool requiresHandleId = false;

                for( const nlohmann::json& value : aSchema["required"] )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "handle_id" )
                    {
                        requiresHandleId = true;
                    }
                }

                return hasHandleId && hasGeneration && hasAlias
                       && requiresHandleId
                       && aSchema.value( "additionalProperties", true )
                                  == false;
            };

    for( const nlohmann::json& tool : callable )
    {
        BOOST_REQUIRE( tool.is_object() );
        BOOST_CHECK_EQUAL( tool.value( "type", std::string() ), "function" );
        BOOST_REQUIRE( tool.contains( "function" ) );
        BOOST_REQUIRE( tool["function"].is_object() );

        const nlohmann::json& function = tool["function"];
        BOOST_REQUIRE( function.contains( "name" ) );
        BOOST_REQUIRE( function["name"].is_string() );
        BOOST_CHECK( function["name"].get<std::string>().find( '.' )
                     == std::string::npos );
        BOOST_CHECK( function.contains( "description" ) );
        BOOST_CHECK( function.contains( "parameters" ) );
        BOOST_CHECK( function["parameters"].is_object() );
        BOOST_CHECK( function["parameters"].contains( "type" ) );
        BOOST_CHECK_EQUAL( function["parameters"]["type"].get<std::string>(),
                           "object" );

        const std::string functionName = function["name"].get<std::string>();
        const std::string description =
                function.value( "description", std::string() );

        if( functionName == "routing_repair_segment"
            && description.find( "namespace=routing" ) != std::string::npos )
        {
            sawRoutingNamespaceDescription = true;
        }

        if( functionName == "script_run_bounded_plan"
            && description.find( "namespace=script" ) != std::string::npos )
        {
            sawScriptNamespaceDescription = true;
        }

        if( functionName == "surface_repair_patch"
            && description.find( "namespace=surface" ) != std::string::npos )
        {
            sawSurfaceNamespaceDescription = true;
        }

        if( functionName == "atomic_run_operation" )
        {
            sawAtomicRunTool = true;
            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool requiresKind = false;
            bool requiresArguments = false;

            for( const nlohmann::json& item : required )
            {
                if( !item.is_string() )
                    continue;

                if( item.get<std::string>() == "kind" )
                    requiresKind = true;

                if( item.get<std::string>() == "arguments" )
                    requiresArguments = true;
            }

            BOOST_CHECK( requiresKind );
            BOOST_CHECK( requiresArguments );

            const nlohmann::json& kindEnum =
                    function["parameters"]["properties"]["kind"]["enum"];
            BOOST_REQUIRE( kindEnum.is_array() );

            for( const nlohmann::json& kind : kindEnum )
            {
                if( kind.is_string()
                    && kind.get<std::string>() == "pcb.create_via" )
                {
                    sawAtomicRunCreateViaKind = true;
                }
            }

            if( function["parameters"].contains( "$defs" )
                && function["parameters"]["$defs"].contains( "operation_contracts" ) )
            {
                const nlohmann::json& contracts =
                        function["parameters"]["$defs"]["operation_contracts"];
                sawAtomicRunOperationContracts =
                        contracts.contains( "pcb.create_via" )
                        && contracts["pcb.create_via"]["required"].dump().find(
                                   "position" ) != std::string::npos
                        && contracts.contains( "pcb.create_track_segment" )
                        && contracts["pcb.create_track_segment"]["properties"]
                                   .contains( "width" )
                        && contracts.contains( "pcb.move_items" )
                        && contracts["pcb.move_items"]["properties"].contains(
                                   "target_positions" )
                        && contracts.contains( "surface.apply_patch" )
                        && contracts["surface.apply_patch"]["required"].dump().find(
                                   "surface_id" ) != std::string::npos
                        && contracts["surface.apply_patch"]["required"].dump().find(
                                   "patch" ) != std::string::npos
                        && contracts["surface.apply_patch"]["properties"]
                                   ["expected_surface_revision"].is_object()
                        && contracts["surface.apply_patch"]["properties"]
                                   ["expected_schema_version"].is_object();

                if( contracts.contains( "surface.apply_patch" )
                    && contracts["surface.apply_patch"].contains( "properties" )
                    && contracts["surface.apply_patch"]["properties"].contains(
                               "patch" ) )
                {
                    sawAtomicRunSurfacePatchFillOpsContract =
                            surfacePatchSchemaDeclaresFillOps(
                                    contracts["surface.apply_patch"]["properties"]
                                             ["patch"] );
                }

                sawAtomicRunAffectedAreaContract =
                        contracts.contains( "pcb.refill_zones" )
                        && contracts["pcb.refill_zones"].contains( "properties" )
                        && contracts["pcb.refill_zones"]["properties"].contains(
                                   "affected_area" )
                        && boxSchemaSupportsCanonicalForms(
                                   contracts["pcb.refill_zones"]["properties"]
                                            ["affected_area"] );

                sawAtomicRunMaintenanceScopeContract =
                        contractsDeclareMaintenanceScopes( contracts );

                if( contracts.contains( "pcb.create_via" )
                    && contracts["pcb.create_via"].contains( "properties" )
                    && contracts["pcb.create_via"]["properties"].contains(
                               "position" )
                    && contracts.contains( "pcb.create_track_segment" )
                    && contracts["pcb.create_track_segment"].contains(
                               "properties" ) )
                {
                    const nlohmann::json& viaPosition =
                            contracts["pcb.create_via"]["properties"]["position"];
                    const nlohmann::json& trackSegmentProperties =
                            contracts["pcb.create_track_segment"]["properties"];

                    sawAtomicRunRelativePointExpressionContract =
                            pointSchemaRequiresXY( viaPosition )
                            && pointSchemaSupportsRelativeExpressions( viaPosition )
                            && trackSegmentProperties.contains( "parallel_to" )
                            && trackSegmentProperties.contains( "offset" );
                }
            }
        }

        if( functionName == "render_hidden_attempt" )
        {
            const nlohmann::json& properties =
                    function["parameters"]["properties"];

            sawRenderHiddenAttemptTypedArgs =
                    properties.contains( "region" )
                    && boxSchemaSupportsCanonicalForms( properties["region"] )
                    && properties.contains( "layer_mask" )
                    && properties["layer_mask"].value( "type", std::string() )
                               == "array"
                    && properties["layer_mask"].contains( "items" )
                    && properties["layer_mask"]["items"].value(
                               "type", std::string() )
                               == "string"
                    && properties.contains( "view_mode" )
                    && properties["view_mode"].value( "type", std::string() )
                               == "string";
        }

        if( functionName == "validate_hidden_attempt" )
        {
            const nlohmann::json& properties =
                    function["parameters"]["properties"];

            sawValidateHiddenAttemptTypedArgs =
                    properties.contains( "scope" )
                    && stringEnumContainsAll(
                            properties["scope"],
                            { "session", "affected_area", "selection",
                              "region" } )
                    && properties.contains( "level" )
                    && stringEnumContainsAll(
                            properties["level"],
                            { "geometry", "drc_lite", "full_drc" } )
                    && properties.contains( "region" )
                    && boxSchemaSupportsCanonicalForms( properties["region"] )
                    && properties.contains( "handles" )
                    && properties["handles"].value( "type", std::string() )
                               == "array"
                    && properties["handles"].contains( "items" )
                    && properties["handles"]["items"].contains( "anyOf" )
                    && properties.contains( "gate" )
                    && stringEnumContainsAll( properties["gate"],
                                              { "preview", "accept" } );
        }

        if( functionName == "script_run_bounded_plan"
            || functionName == "repair_apply_bounded_plan" )
        {
            const nlohmann::json& kindEnum =
                    function["parameters"]["properties"]["plan"]["properties"]
                            ["operations"]["items"]["properties"]["kind"]["enum"];

            BOOST_REQUIRE( kindEnum.is_array() );

            for( const nlohmann::json& kind : kindEnum )
            {
                if( kind.is_string()
                    && kind.get<std::string>() == "surface.apply_patch" )
                {
                    if( functionName == "script_run_bounded_plan" )
                        sawScriptSurfacePatchKind = true;

                    if( functionName == "repair_apply_bounded_plan" )
                        sawRepairSurfacePatchKind = true;
                }
            }

            if( function["parameters"].contains( "$defs" )
                && function["parameters"]["$defs"].contains( "operation_contracts" ) )
            {
                const nlohmann::json& contracts =
                        function["parameters"]["$defs"]["operation_contracts"];

                const bool hasAffectedAreaContract =
                        contracts.contains( "pcb.refill_zones" )
                        && contracts["pcb.refill_zones"].contains( "properties" )
                        && contracts["pcb.refill_zones"]["properties"].contains(
                                   "affected_area" )
                        && boxSchemaSupportsCanonicalForms(
                                   contracts["pcb.refill_zones"]["properties"]
                                            ["affected_area"] );

                if( functionName == "script_run_bounded_plan" )
                {
                    sawScriptAffectedAreaContract = hasAffectedAreaContract;
                    sawScriptMaintenanceScopeContract =
                            contractsDeclareMaintenanceScopes( contracts );

                    if( contracts.contains( "surface.apply_patch" )
                        && contracts["surface.apply_patch"].contains(
                                   "properties" )
                        && contracts["surface.apply_patch"]["properties"]
                                   .contains( "patch" ) )
                    {
                        sawScriptSurfacePatchFillOpsContract =
                                surfacePatchSchemaDeclaresFillOps(
                                        contracts["surface.apply_patch"]
                                                 ["properties"]["patch"] );
                    }
                }

                if( functionName == "repair_apply_bounded_plan" )
                {
                    sawRepairAffectedAreaContract = hasAffectedAreaContract;
                    sawRepairMaintenanceScopeContract =
                            contractsDeclareMaintenanceScopes( contracts );

                    if( contracts.contains( "surface.apply_patch" )
                        && contracts["surface.apply_patch"].contains(
                                   "properties" )
                        && contracts["surface.apply_patch"]["properties"]
                                   .contains( "patch" ) )
                    {
                        sawRepairSurfacePatchFillOpsContract =
                                surfacePatchSchemaDeclaresFillOps(
                                        contracts["surface.apply_patch"]
                                                 ["properties"]["patch"] );
                    }
                }
            }

            if( functionName == "script_run_bounded_plan"
                && function["parameters"].contains( "$defs" )
                && function["parameters"]["$defs"].contains( "operation_contracts" ) )
            {
                const nlohmann::json& contracts =
                        function["parameters"]["$defs"]["operation_contracts"];

                if( contracts.contains( "pcb.create_shape" ) )
                {
                    const nlohmann::json& shapeContract =
                            contracts["pcb.create_shape"];

                    const bool hasPolygonKind =
                            shapeContract["properties"].contains( "shape_type" )
                            && shapeContract["properties"]["shape_type"].contains( "enum" )
                            && std::find(
                                       shapeContract["properties"]["shape_type"]
                                                    ["enum"].begin(),
                                       shapeContract["properties"]["shape_type"]
                                                    ["enum"].end(),
                                       "polygon" )
                                       != shapeContract["properties"]["shape_type"]
                                                      ["enum"].end();
                    const bool hasPolygonPoints =
                            shapeContract["properties"].contains( "geometry" )
                            && shapeContract["properties"]["geometry"].contains(
                                       "properties" )
                            && shapeContract["properties"]["geometry"]["properties"]
                                       .contains( "points" )
                            && shapeContract["properties"]["geometry"]["properties"]
                                            ["points"].value( "minItems", 0 )
                                       == 3
                            && pointSchemaRequiresXY(
                                       shapeContract["properties"]["geometry"]
                                                    ["properties"]["points"]["items"] );

                    sawScriptShapePolygonContract =
                            hasPolygonKind && hasPolygonPoints;
                }

                if( contracts.contains( "pcb.create_zone" ) )
                {
                    const nlohmann::json& zoneContract =
                            contracts["pcb.create_zone"];

                    const bool hasOutlinePoints =
                            zoneContract.contains( "properties" )
                            && zoneContract["properties"].contains( "outline" )
                            && zoneContract["properties"]["outline"].contains(
                                       "properties" )
                            && zoneContract["properties"]["outline"]["properties"]
                                       .contains( "points" )
                            && zoneContract["properties"]["outline"]["properties"]
                                           ["points"].value( "minItems", 0 )
                                       == 3
                            && pointSchemaRequiresXY(
                                       zoneContract["properties"]["outline"]
                                                   ["properties"]["points"]["items"] );

                    sawScriptZoneOutlineContract = hasOutlinePoints;
                }

                if( contracts.contains( "pcb.update_item_geometry" ) )
                {
                    const nlohmann::json& updateGeometryContract =
                            contracts["pcb.update_item_geometry"];

                    if( updateGeometryContract.contains( "properties" )
                        && updateGeometryContract["properties"].contains(
                                   "geometry_patch" )
                        && updateGeometryContract["properties"]["geometry_patch"]
                                   .contains( "properties" ) )
                    {
                        const nlohmann::json& patchProperties =
                                updateGeometryContract["properties"]
                                                      ["geometry_patch"]
                                                      ["properties"];

                        sawScriptGeometryPatchContract =
                                patchProperties.contains( "start" )
                                && patchProperties.contains( "end" )
                                && patchProperties.contains( "center" )
                                && patchProperties.contains( "mid" )
                                && patchProperties.contains( "radius" )
                                && patchProperties.contains( "points" )
                                && patchProperties["points"].value(
                                           "minItems", 0 )
                                           == 3
                                && pointSchemaRequiresXY(
                                           patchProperties["points"]["items"] );
                    }
                }

                if( contracts.contains( "pcb.set_item_properties" ) )
                {
                    const nlohmann::json& setPropertiesContract =
                            contracts["pcb.set_item_properties"];

                    if( setPropertiesContract.contains( "properties" )
                        && setPropertiesContract["properties"].contains(
                                   "typed_props" )
                        && setPropertiesContract["properties"]["typed_props"]
                                   .contains( "properties" ) )
                    {
                        const nlohmann::json& typedProps =
                                setPropertiesContract["properties"]["typed_props"]
                                                     ["properties"];

                        sawScriptTypedPropsContract =
                                typedProps.contains( "diameter" )
                                && typedProps.contains( "drill" )
                                && typedProps.contains( "width" )
                                && typedProps.contains( "fill" )
                                && typedProps.contains( "clearance" )
                                && typedProps.contains( "priority" )
                                && typedProps.contains( "fill_mode" )
                                && typedProps.contains( "reference" )
                                && typedProps.contains( "value" )
                                && typedProps.contains( "side" )
                                && typedProps.contains( "orientation_degrees" )
                                && typedProps["fill"].value( "type",
                                                             std::string() )
                                           == "boolean"
                                && typedProps["reference"].value(
                                           "type", std::string() )
                                           == "string"
                                && typedProps["side"].value( "type",
                                                             std::string() )
                                           == "string"
                                && typedProps["fill_mode"].contains( "enum" )
                                && std::find(
                                           typedProps["fill_mode"]["enum"].begin(),
                                           typedProps["fill_mode"]["enum"].end(),
                                           "hatch_pattern" )
                                           != typedProps["fill_mode"]["enum"].end();
                    }
                }
            }
        }

        if( functionName == "script_run_bounded_plan" )
            sawScriptTool = true;

        if( functionName == "observation_resolve_visual_reference" )
        {
            sawVisualReferenceTool = true;
            sawVisualReferenceOptionalSidecarJson =
                    function["parameters"].contains( "properties" )
                    && function["parameters"]["properties"].contains(
                               "sidecar_json" )
                    && function["parameters"]["properties"]["sidecar_json"].value(
                               "type", std::string() ) == "string";

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "reference" )
                    {
                        sawVisualReferenceRequiredReference = true;
                    }
                }
            }
        }

        if( functionName == "repair_apply_bounded_plan" )
            sawRepairTool = true;

        if( functionName == "placement_repair_via" )
        {
            sawPlacementRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasPosition = false;
            bool hasNet = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "position" )
                    {
                        hasPosition = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "net" )
                    {
                        hasNet = true;
                    }
                }
            }

            sawPlacementRepairRequiredPosition = hasPosition && hasNet;
            sawPlacementRepairPointSchema = pointSchemaRequiresXY(
                    function["parameters"]["properties"]["position"] );
        }

        if( functionName == "placement_repair_move_items" )
        {
            sawPlacementMoveRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasHandles = false;
            bool hasDelta = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "handles" )
                    {
                        hasHandles = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "delta" )
                    {
                        hasDelta = true;
                    }
                }
            }

            sawPlacementMoveRepairRequiredHandles = hasHandles && hasDelta;
            sawPlacementMoveHandleSchema =
                    function["parameters"]["properties"]["handles"]
                            .contains( "items" )
                    && handleSchemaRequiresHandleId(
                            function["parameters"]["properties"]["handles"]
                                    ["items"] );
            sawPlacementMoveDeltaPointSchema = pointSchemaRequiresXY(
                    function["parameters"]["properties"]["delta"] );
        }

        if( functionName == "placement_repair_footprint_orientation" )
        {
            sawPlacementOrientationRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasHandles = false;
            bool hasTargetOrientation = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "handles" )
                    {
                        hasHandles = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>()
                                   == "target_orientation_degrees" )
                    {
                        hasTargetOrientation = true;
                    }
                }
            }

            sawPlacementOrientationRepairRequiredFacts =
                    hasHandles && hasTargetOrientation;
            sawPlacementOrientationRepairHandleSchema =
                    function["parameters"]["properties"]["handles"]
                            .contains( "items" )
                    && handleSchemaRequiresHandleId(
                            function["parameters"]["properties"]["handles"]
                                    ["items"] );
        }

        if( functionName == "routing_repair_segment" )
        {
            sawRoutingRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasStart = false;
            bool hasEnd = false;
            bool hasLayer = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "start" )
                    {
                        hasStart = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "end" )
                    {
                        hasEnd = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "layer" )
                    {
                        hasLayer = true;
                    }
                }
            }

            sawRoutingRepairRequiredSegment = hasStart && hasEnd && hasLayer;
            const nlohmann::json& properties =
                    function["parameters"]["properties"];
            sawRoutingRepairSegmentPointSchema =
                    pointSchemaRequiresXY( properties["start"] )
                    && pointSchemaRequiresXY( properties["end"] );
        }

        if( functionName == "routing_repair_polyline" )
        {
            sawRoutingPolylineRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasPoints = false;
            bool hasLayer = false;
            bool hasNet = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "points" )
                    {
                        hasPoints = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "layer" )
                    {
                        hasLayer = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "net" )
                    {
                        hasNet = true;
                    }
                }
            }

            sawRoutingPolylineRepairRequiredPoints = hasPoints && hasLayer && hasNet;
            sawRoutingPolylinePointSchema = pointSchemaRequiresXY(
                    function["parameters"]["properties"]["points"]["items"] );
        }

        if( functionName == "routing_repair_bus_segments" )
        {
            sawRoutingBusRepairTool = true;

            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasSegments = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "segments" )
                    {
                        hasSegments = true;
                    }
                }
            }

            sawRoutingBusRepairRequiredSegments = hasSegments;
            const nlohmann::json& segmentSchema =
                    function["parameters"]["properties"]["segments"]["items"];
            sawRoutingBusSegmentPointSchema =
                    segmentSchema.contains( "properties" )
                    && pointSchemaRequiresXY(
                            segmentSchema["properties"]["start"] )
                    && pointSchemaRequiresXY(
                            segmentSchema["properties"]["end"] );
        }

        if( functionName == "surface_repair_patch" )
        {
            sawSurfaceRepairTool = true;

            const nlohmann::json& properties =
                    function["parameters"].contains( "properties" )
                            ? function["parameters"]["properties"]
                            : nlohmann::json::object();
            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();

            bool hasSurfaceId = false;
            bool hasPatch = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "surface_id" )
                    {
                        hasSurfaceId = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "patch" )
                    {
                        hasPatch = true;
                    }
                }
            }

            sawSurfaceRepairRequiredPatch = hasSurfaceId && hasPatch;
            sawSurfaceRepairExpectedMetadata =
                    properties.contains( "expected_surface_revision" )
                    && properties.contains( "expected_schema_version" )
                    && properties.contains( "expected_selection_fingerprint" )
                    && properties.contains( "expected_overlap_set" );

            if( properties.contains( "write_policy" )
                && properties["write_policy"].is_object()
                && properties["write_policy"].contains( "enum" )
                && properties["write_policy"]["enum"].is_array() )
            {
                for( const nlohmann::json& value :
                     properties["write_policy"]["enum"] )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "fill_empty_only" )
                    {
                        sawSurfaceRepairWritePolicy = true;
                    }
                }
            }

            sawSurfaceRepairPatchFillOpsContract =
                    properties.contains( "patch" )
                    && surfacePatchSchemaDeclaresFillOps(
                               properties["patch"] );

        }
    }

    BOOST_CHECK( sawScriptTool );
    BOOST_CHECK( sawVisualReferenceTool );
    BOOST_CHECK( sawVisualReferenceRequiredReference );
    BOOST_CHECK( sawVisualReferenceOptionalSidecarJson );
    BOOST_CHECK( sawAtomicRunTool );
    BOOST_CHECK( sawAtomicRunCreateViaKind );
    BOOST_CHECK( sawAtomicRunOperationContracts );
    BOOST_CHECK( sawRepairTool );
    BOOST_CHECK( sawPlacementRepairTool );
    BOOST_CHECK( sawPlacementMoveRepairTool );
    BOOST_CHECK( sawPlacementOrientationRepairTool );
    BOOST_CHECK( sawRoutingRepairTool );
    BOOST_CHECK( sawRoutingPolylineRepairTool );
    BOOST_CHECK( sawRoutingBusRepairTool );
    BOOST_CHECK( sawSurfaceRepairTool );
    BOOST_CHECK( sawScriptSurfacePatchKind );
    BOOST_CHECK( sawScriptShapePolygonContract );
    BOOST_CHECK( sawScriptGeometryPatchContract );
    BOOST_CHECK( sawScriptZoneOutlineContract );
    BOOST_CHECK( sawScriptTypedPropsContract );
    BOOST_CHECK( sawScriptAffectedAreaContract );
    BOOST_CHECK( sawScriptSurfacePatchFillOpsContract );
    BOOST_CHECK( sawRepairSurfacePatchKind );
    BOOST_CHECK( sawRepairAffectedAreaContract );
    BOOST_CHECK( sawRepairSurfacePatchFillOpsContract );
    BOOST_CHECK( sawAtomicRunAffectedAreaContract );
    BOOST_CHECK( sawAtomicRunSurfacePatchFillOpsContract );
    BOOST_CHECK( sawScriptMaintenanceScopeContract );
    BOOST_CHECK( sawRepairMaintenanceScopeContract );
    BOOST_CHECK( sawAtomicRunMaintenanceScopeContract );
    BOOST_CHECK( sawAtomicRunRelativePointExpressionContract );
    BOOST_CHECK( sawPlacementRepairRequiredPosition );
    BOOST_CHECK( sawPlacementMoveRepairRequiredHandles );
    BOOST_CHECK( sawPlacementOrientationRepairRequiredFacts );
    BOOST_CHECK( sawRoutingRepairRequiredSegment );
    BOOST_CHECK( sawRoutingPolylineRepairRequiredPoints );
    BOOST_CHECK( sawRoutingBusRepairRequiredSegments );
    BOOST_CHECK( sawPlacementRepairPointSchema );
    BOOST_CHECK( sawPlacementMoveDeltaPointSchema );
    BOOST_CHECK( sawRoutingRepairSegmentPointSchema );
    BOOST_CHECK( sawRoutingPolylinePointSchema );
    BOOST_CHECK( sawRoutingBusSegmentPointSchema );
    BOOST_CHECK( sawPlacementMoveHandleSchema );
    BOOST_CHECK( sawPlacementOrientationRepairHandleSchema );
    BOOST_CHECK( sawRoutingNamespaceDescription );
    BOOST_CHECK( sawScriptNamespaceDescription );
    BOOST_CHECK( sawSurfaceNamespaceDescription );
    BOOST_CHECK( sawRenderHiddenAttemptTypedArgs );
    BOOST_CHECK( sawValidateHiddenAttemptTypedArgs );
    BOOST_CHECK( sawSurfaceRepairRequiredPatch );
    BOOST_CHECK( sawSurfaceRepairExpectedMetadata );
    BOOST_CHECK( sawSurfaceRepairWritePolicy );
    BOOST_CHECK( sawSurfaceRepairPatchFillOpsContract );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "publish_preview" ) ) );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "generate_" ) ) );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "_candidates" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsLegacyCandidateGenerationToolsAtCallBoundary )
{
    AI_NEXT_ACTION_TOOL_REGISTRY registry;
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 530;
    AI_OBSERVATION_PACKET observation;

    const std::vector<wxString> legacyToolNames = {
        wxS( "placement_generate_via_pattern_candidates" ),
        wxS( "placement_generate_footprint_transform_candidates" ),
        wxS( "placement_generate_footprint_orientation_candidates" ),
        wxS( "routing_generate_segment_candidates" ),
        wxS( "routing_generate_parallel_segment_candidates" ),
        wxS( "routing_generate_bus_segment_candidates" ),
        wxS( "routing_generate_replace_path_candidates" ),
        wxS( "routing_generate_constraint_aware_reroute_candidates" ),
        wxS( "surface_generate_fill_candidates" )
    };

    for( const wxString& toolName : legacyToolNames )
    {
        AI_TOOL_CALL_RECORD call;
        call.m_ToolCallId = wxS( "call_legacy_candidate" );
        call.m_ToolName = toolName;
        call.m_ArgumentsJson = wxS( "{}" );

        AI_TOOL_INVOCATION_RESULT result =
                registry.HandleToolCall( request, call, observation );

        BOOST_CHECK_MESSAGE( !result.m_Allowed,
                             "legacy tool should be rejected: "
                                     << toolName.ToStdString() );
        BOOST_CHECK_MESSAGE( !result.m_Executed,
                             "legacy tool should not execute: "
                                     << toolName.ToStdString() );
        BOOST_CHECK_EQUAL( result.m_ErrorCode,
                           wxString( wxS( "unknown_tool" ) ) );
    }
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationIncludesWorkStateAttemptPolicy )
{
    auto* layoutProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"policy_probe\"}" ) } );
    AI_NEXT_ACTION_RUNTIME layoutRuntime{ std::unique_ptr<AI_PROVIDER>( layoutProvider ) };

    BOOST_CHECK( !layoutRuntime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( layoutProvider->m_Requests.size(), 1 );
    BOOST_CHECK( layoutProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"attempt_policy\"" ) ) );
    BOOST_CHECK( layoutProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"work_state\":\"layout\"" ) ) );
    BOOST_CHECK( layoutProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"max_attempts\":3" ) ) );

    auto* routingProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"policy_probe\"}" ) } );
    AI_NEXT_ACTION_RUNTIME routingRuntime{ std::unique_ptr<AI_PROVIDER>( routingProvider ) };

    BOOST_CHECK( !routingRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( routingProvider->m_Requests.size(), 1 );
    BOOST_CHECK( routingProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"work_state\":\"routing\"" ) ) );
    BOOST_CHECK( routingProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"max_attempts\":5" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeAttemptsWorkStateCandidateAfterUnstructuredDecisionText )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "I should try the next placement candidate." ),
              publishReview() } );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Status == AI_SUGGESTION_STATUS::Pending );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_LlmDecisionJson.Contains(
            wxS( "unstructured_decision_fallback" ) ) );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationAttemptPolicyUsesRejectHistory )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"reject_policy_probe\"}" ) } );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( first.has_value() );
    BOOST_REQUIRE( runtime.Reject( first->m_Id ) );

    BOOST_CHECK( !runtime.Update( makeChangedViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );

    const AI_PROVIDER_REQUEST& secondDecisionRequest =
            provider->m_Requests.back();
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"attempt_policy\"" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"base_max_attempts\":3" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"max_attempts\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"reject_history_count\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"recent_reject_history\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectHistoryCapsRollbackRetryAttemptLoop )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"revise_after_reject\"}" ),
              publishReview() } );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( first.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_REQUIRE( runtime.Reject( first->m_Id ) );

    BOOST_CHECK( !runtime.Update( makeChangedViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( runtime.Attempts().size(), 2 );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 4 );
    BOOST_CHECK_EQUAL( provider->m_Bodies.size(), 1 );
    BOOST_REQUIRE_GE( runtime.Steps().size(), 2 );
    BOOST_CHECK( runtime.Steps().back().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK_EQUAL( runtime.Steps().back().m_AttemptIds.size(), 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationAttemptPolicyUsesContextChurnHistory )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"churn_policy_probe\"}" ) } );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE( runtime.Update( makeChangedViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Suggestions().size(), 2 );
    BOOST_CHECK( runtime.Suggestions().front().m_Status
                 == AI_SUGGESTION_STATUS::Superseded );

    AI_SUGGESTION_TRIGGER thirdTrigger = makeChangedViaTrigger();
    thirdTrigger.m_ContextVersion.m_ViewRevision = 7;
    thirdTrigger.m_ContextSnapshot.m_Version = thirdTrigger.m_ContextVersion;
    thirdTrigger.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            thirdTrigger.m_ContextVersion;
    thirdTrigger.m_Activity.m_Sequence = 46;
    thirdTrigger.m_Reason = wxS( "context advanced again" );

    BOOST_CHECK( !runtime.Update( thirdTrigger ).has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& thirdDecisionRequest =
            provider->m_Requests.back();
    BOOST_CHECK( thirdDecisionRequest.m_UserText.Contains(
            wxS( "\"attempt_policy\"" ) ) );
    BOOST_CHECK( thirdDecisionRequest.m_UserText.Contains(
            wxS( "\"base_max_attempts\":3" ) ) );
    BOOST_CHECK( thirdDecisionRequest.m_UserText.Contains(
            wxS( "\"max_attempts\":1" ) ) );
    BOOST_CHECK( thirdDecisionRequest.m_UserText.Contains(
            wxS( "\"context_churn_count\":1" ) ) );
    BOOST_CHECK( thirdDecisionRequest.m_UserText.Contains(
            wxS( "\"context_churn_history\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationAttemptPolicyUsesLatencyHistory )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"latency_policy_probe\"}" ) } );
    SLOW_PREVIEW_SESSION_VALIDATION_SERVICE validation;
    validation.m_SleepMs = 300;
    PASSING_SESSION_PREVIEW_SERVICE preview;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validation,
                                    &preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_GE( runtime.Attempts().front().m_BudgetCounters.m_WallTimeMs,
                    250 );

    BOOST_CHECK( !runtime.Update( makeChangedViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );

    const AI_PROVIDER_REQUEST& secondDecisionRequest =
            provider->m_Requests.back();
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"attempt_policy\"" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"base_max_attempts\":3" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"max_attempts\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"latency_over_budget_count\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"latency_over_budget_history\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationAttemptPolicyUsesCandidateQualityHistory )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"quality_policy_probe\"}" ) } );
    BLOCKING_ISSUE_SESSION_VALIDATION_SERVICE validation;
    PASSING_SESSION_PREVIEW_SERVICE           preview;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validation,
                                    &preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_gate_failed" ) ) );

    BOOST_CHECK( !runtime.Update( makeChangedViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );

    const AI_PROVIDER_REQUEST& secondDecisionRequest =
            provider->m_Requests.back();
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"attempt_policy\"" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"base_max_attempts\":3" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"max_attempts\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"candidate_quality_failure_count\":1" ) ) );
    BOOST_CHECK( secondDecisionRequest.m_UserText.Contains(
            wxS( "\"candidate_quality_history\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationIncludesWorkStatePackets )
{
    auto* placementProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"packet_probe\"}" ) } );
    AI_NEXT_ACTION_RUNTIME placementRuntime{
            std::unique_ptr<AI_PROVIDER>( placementProvider ) };
    AI_SUGGESTION_TRIGGER placementTrigger = makeViaTrigger();
    placementTrigger.m_ContextSnapshot.m_Summary =
            wxS( "{\"kind\":\"pcb_board_summary\","
                 "\"board_edges_bbox\":{\"x\":0,\"y\":0,"
                 "\"width\":1000000,\"height\":500000},"
                 "\"layer_context\":{\"source\":\"board\","
                 "\"copper_layer_count\":2,\"enabled_layer_count\":4,"
                 "\"visible_layer_count\":3,"
                 "\"active_layer\":{\"name\":\"F.Cu\",\"copper\":true}},"
                 "\"constraint_facts\":{\"source\":\"board\","
                 "\"minimums\":{\"min_clearance\":125000,"
                 "\"min_track_width\":100000,\"min_via_size\":450000},"
                 "\"rule_area_count\":2,\"keepout_count\":1,"
                 "\"effective_constraints\":{"
                 "\"drc_engine_present\":true,\"rules_valid\":true,"
                 "\"geometry_dependent_rules_present\":true,"
                 "\"geometry_specific_rule_coverage\":[{"
                 "\"rule\":\"clearance\",\"geometry\":\"pad_to_track\","
                 "\"covered\":true,\"source\":\"DRC_ENGINE::EvalRules\"}],"
                 "\"worst_constraints\":[{\"type\":\"clearance\","
                 "\"value\":{\"min\":175000,\"has_min\":true}}],"
                 "\"pair_effective_constraints\":[{\"type\":\"clearance\","
                 "\"layer\":\"F.Cu\","
                 "\"value\":{\"min\":220000,\"has_min\":true},"
                 "\"evaluation_source\":\"DRC_ENGINE::EvalRules\"}]}},"
                 "\"placement_facts\":{\"source\":\"board\","
                 "\"footprint_count\":2,"
                 "\"footprints_with_courtyard_count\":2,"
                 "\"courtyard_pair_count\":1,"
                 "\"courtyard_overlap_count\":0,"
                 "\"minimum_courtyard_bbox_spacing\":150000,"
                 "\"minimum_non_overlapping_courtyard_bbox_spacing\":150000,"
                 "\"courtyard_pairs\":[{"
                 "\"kind\":\"footprint_courtyard_pair\","
                 "\"side\":\"front\","
                 "\"layer\":\"F.Courtyard\","
                 "\"bbox_spacing\":150000,"
                 "\"bbox_overlaps\":false,"
                 "\"source_footprint\":{\"label\":\"U1\","
                 "\"courtyard_bbox\":{\"x\":0,\"y\":0,"
                 "\"width\":100000,\"height\":100000}},"
                 "\"target_footprint\":{\"label\":\"U2\","
                 "\"courtyard_bbox\":{\"x\":250000,\"y\":0,"
                 "\"width\":100000,\"height\":100000}}}],"
                 "\"courtyard_pair_sample_truncated\":false}}" );
    placementTrigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    placementTrigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition =
            VECTOR2I( 180, 80 );
    placementTrigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"cursor_region\":{\"x\":140,\"y\":80,"
                 "\"width\":100,\"height\":100},"
                 "\"net\":\"GND\",\"diameter\":600000,"
                 "\"drill\":300000,"
                 "\"layer_pair\":{\"start\":\"F.Cu\",\"end\":\"B.Cu\"},"
                 "\"viewport\":{\"center\":{\"x\":180,\"y\":80},"
                 "\"zoom\":3.0,\"width\":600,\"height\":400}}" );
    placementTrigger.m_ContextSnapshot.m_Anchors.push_back(
            contextAnchor( wxS( "place_candidate_1" ),
                           AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                           wxS( "Place candidate 1" ), 220, 90 ) );
    placementTrigger.m_ContextSnapshot.m_VisibleObjects.push_back( footprintRef() );
    placementTrigger.m_ContextSnapshot.m_VisibleObjects.push_back( padRef() );
    placementTrigger.m_ContextSnapshot.m_VisibleObjects.push_back( keepoutRef() );

    BOOST_CHECK( !placementRuntime.Update( placementTrigger ).has_value() );
    BOOST_REQUIRE_EQUAL( placementProvider->m_Requests.size(), 1 );
    BOOST_CHECK( placementProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"work_state_packet\"" ) ) );
    BOOST_CHECK( placementProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"packet_kind\":\"placement\"" ) ) );
    BOOST_CHECK( placementProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"placeable_kind\":\"via\"" ) ) );
    BOOST_CHECK( placementProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"visible_object_count\":6" ) ) );

    nlohmann::json placementRequest =
            providerRequestJson( placementProvider->m_Requests.front() );
    const nlohmann::json& placementPacket =
            placementRequest["observation"]["structured_facts"]["work_state_packet"];
    BOOST_REQUIRE( placementPacket.contains( "planning_target" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["planning_target"]["kind"].get<std::string>(),
            "place_current_item" );
    BOOST_CHECK_EQUAL(
            placementPacket["planning_target"]["placeable_kind"].get<std::string>(),
            "via" );
    BOOST_CHECK( placementPacket["planning_target"]["click_required_to_materialize"]
                         .get<bool>() );
    BOOST_REQUIRE( placementPacket.contains( "board_context_summary" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["board_context_summary"]["source"].get<std::string>(),
            "context_summary.pcb_board_summary" );
    BOOST_CHECK_EQUAL(
            placementPacket["board_context_summary"]["board_edges_bbox"]["width"]
                    .get<int>(),
            1000000 );
    BOOST_CHECK_EQUAL(
            placementPacket["board_context_summary"]["layer_context"]
                    ["active_layer"]["name"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            placementPacket["board_context_summary"]["layer_context"]
                    ["visible_layer_count"].get<int>(),
            3 );
    BOOST_REQUIRE( placementPacket.contains( "constraint_summary" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["constraint_summary"]["source"].get<std::string>(),
            "context_summary.constraint_facts" );
    BOOST_CHECK_EQUAL(
            placementPacket["constraint_summary"]["minimums"]["min_clearance"].get<int>(),
            125000 );
    BOOST_CHECK_EQUAL(
            placementPacket["constraint_summary"]["rule_area_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            placementPacket["constraint_summary"]["keepout_count"].get<int>(),
            1 );
    BOOST_CHECK(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["drc_engine_present"].get<bool>() );
    BOOST_CHECK(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["geometry_dependent_rules_present"].get<bool>() );
    BOOST_REQUIRE_EQUAL(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["geometry_specific_rule_coverage"].size(),
            1 );
    BOOST_CHECK_EQUAL(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["geometry_specific_rule_coverage"].at( 0 )["geometry"]
                    .get<std::string>(),
            "pad_to_track" );
    BOOST_REQUIRE_EQUAL(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["worst_constraints"].size(),
            1 );
    BOOST_REQUIRE_EQUAL(
            placementPacket["constraint_summary"]["effective_constraints"]
                    ["pair_effective_constraints"].size(),
            1 );
    BOOST_REQUIRE( placementPacket.contains( "placement_anchors" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["placement_anchors"].size(), 1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_anchors"].at( 0 )["id"].get<std::string>(),
            "place_candidate_1" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_anchors"].at( 0 )["position"]["x"].get<int>(),
            220 );
    BOOST_REQUIRE(
            placementPacket["placement_anchors"].at( 0 ).contains( "provenance" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_anchors"].at( 0 )["provenance"]["source"]
                    .get<std::string>(),
            "context_anchor" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_anchors"].at( 0 )["provenance"]["source_id"]
                    .get<std::string>(),
            "place_candidate_1" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_anchors"].at( 0 )["provenance"]["editor_kind"]
                    .get<std::string>(),
            "pcb" );
    BOOST_CHECK(
            placementPacket["placement_anchors"].at( 0 )["provenance"]["has_position"]
                    .get<bool>() );
    BOOST_CHECK(
            !placementPacket["placement_anchors"].at( 0 )["provenance"]
                     ["details_present"].get<bool>() );
    BOOST_REQUIRE( placementPacket.contains( "placement_candidate_facts" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["placement_candidate_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["anchor_id"]
                    .get<std::string>(),
            "place_candidate_1" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["reference_position"]
                    ["x"].get<int>(),
            180 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["candidate_position"]
                    ["x"].get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["dx"].get<int>(),
            40 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["dy"].get<int>(),
            10 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["manhattan_distance"]
                    .get<int>(),
            50 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )["placeable_kind"]
                    .get<std::string>(),
            "via" );
    BOOST_REQUIRE(
            placementPacket["placement_candidate_facts"].at( 0 )
                    .contains( "interaction_semantics" ) );
    const nlohmann::json& placementInteraction =
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["interaction_semantics"];
    BOOST_CHECK_EQUAL( placementInteraction["mode"].get<std::string>(),
                       "active_interactive_placement" );
    BOOST_CHECK_EQUAL( placementInteraction["planning_target"].get<std::string>(),
                       "place_current_item" );
    BOOST_CHECK_EQUAL( placementInteraction["placement_anchor_source"].get<std::string>(),
                       "placement_anchor" );
    BOOST_CHECK( placementInteraction["cursor_attached_item"].get<bool>() );
    BOOST_CHECK( placementInteraction["manual_click_to_materialize"].get<bool>() );
    BOOST_CHECK( placementInteraction["manual_click_supersedes_attempt"].get<bool>() );
    BOOST_CHECK( placementInteraction["preview_must_be_rebased_after_click"].get<bool>() );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["name"].get<std::string>(),
            "placement.repair_via" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["position"]["x"]
                    .get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["net"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["diameter"].get<int>(),
            600000 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["drill"].get<int>(),
            300000 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["layer_pair"]["start"]
                    .get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_render_region"]["source"].get<std::string>(),
            "placement_candidate_region" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_render_region"]["mode"].get<std::string>(),
            "placement_candidate_review" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_render_region"]["bbox"]["x"].get<int>(),
            170 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_render_region"]["bbox"]["y"].get<int>(),
            40 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_facts"].at( 0 )
                    ["suggested_render_region"]["bbox"]["width"].get<int>(),
            100 );
    BOOST_REQUIRE( placementPacket.contains( "placement_candidate_summary" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["source"].get<std::string>(),
            "placement_candidate_facts" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["candidate_count"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["nearest_manhattan_distance"]
                    .get<int>(),
            50 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["top_anchor_id"]
                    .get<std::string>(),
            "place_candidate_1" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["top_placeable_kind"]
                    .get<std::string>(),
            "via" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["top_candidate_position"]["x"]
                    .get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["top_suggested_tool_call"]
                    ["name"].get<std::string>(),
            "placement.repair_via" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_candidate_summary"]["top_candidate_obstacle_count"]
                    .get<int>(),
            3 );
    BOOST_REQUIRE(
            placementPacket["placement_candidate_facts"].at( 0 )
                    .contains( "candidate_obstacle_facts" ) );

    std::set<std::string> placementCandidateObstacleLabels;

    for( const nlohmann::json& fact : placementPacket["placement_candidate_facts"]
                                              .at( 0 )["candidate_obstacle_facts"] )
    {
        placementCandidateObstacleLabels.insert( fact["label"].get<std::string>() );
    }

    BOOST_CHECK( placementCandidateObstacleLabels.find( "via:200,50" )
                 != placementCandidateObstacleLabels.end() );
    BOOST_CHECK( placementCandidateObstacleLabels.find( "footprint:U1" )
                 != placementCandidateObstacleLabels.end() );
    BOOST_CHECK( placementCandidateObstacleLabels.find( "keepout:J1" )
                 != placementCandidateObstacleLabels.end() );
    BOOST_CHECK( placementCandidateObstacleLabels.find( "via:100,50" )
                 == placementCandidateObstacleLabels.end() );
    BOOST_CHECK( placementCandidateObstacleLabels.find( "via:300,50" )
                 == placementCandidateObstacleLabels.end() );
    BOOST_REQUIRE( placementPacket.contains( "visible_object_summaries" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["visible_object_summaries"].size(), 6 );
    BOOST_CHECK_EQUAL(
            placementPacket["visible_object_summaries"].at( 0 )["label"].get<std::string>(),
            "via:100,50" );
    BOOST_CHECK_EQUAL(
            placementPacket["visible_object_summaries"].at( 0 )["details"]["position"]["x"]
                    .get<int>(),
            100 );
    BOOST_REQUIRE( placementPacket.contains( "placement_obstacle_facts" ) );
    BOOST_REQUIRE_GE( placementPacket["placement_obstacle_facts"].size(), 4 );

    bool sawViaObstacle = false;
    bool sawFootprintObstacle = false;
    bool sawPadObstacle = false;

    for( const nlohmann::json& fact : placementPacket["placement_obstacle_facts"] )
    {
        if( fact.value( "kind", "" ) == "via_obstacle"
            && fact["position"]["x"].get<int>() == 100
            && fact["diameter"].get<int>() == 600000 )
        {
            sawViaObstacle = true;
        }

        if( fact.value( "kind", "" ) == "footprint_obstacle"
            && fact.value( "reference", "" ) == "U1"
            && fact.value( "value", "" ) == "LDO-3V3"
            && fact["bbox"]["x"].get<int>() == 150 )
        {
            sawFootprintObstacle = true;
        }

        if( fact.value( "kind", "" ) == "pad_obstacle"
            && fact.value( "footprint_reference", "" ) == "U4"
            && fact.value( "footprint_value", "" ) == "SensorAFE"
            && fact.value( "pad_number", "" ) == "1"
            && fact["position"]["x"].get<int>() == 240 )
        {
            sawPadObstacle = true;
        }
    }

    BOOST_CHECK( sawViaObstacle );
    BOOST_CHECK( sawPadObstacle );
    BOOST_CHECK( sawFootprintObstacle );
    BOOST_REQUIRE( placementPacket.contains( "placement_keepout_facts" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["placement_keepout_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_keepout_facts"].at( 0 )["kind"].get<std::string>(),
            "keepout" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_keepout_facts"].at( 0 )["bbox"]["width"].get<int>(),
            90 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_keepout_facts"].at( 0 )["zone_kind"].get<std::string>(),
            "keepout" );
    BOOST_CHECK(
            placementPacket["placement_keepout_facts"].at( 0 )["keepout"]["tracks"]
                    .get<bool>() );
    BOOST_REQUIRE( placementPacket.contains( "placement_footprint_geometry_facts" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["placement_footprint_geometry_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_footprint_geometry_facts"].at( 0 )["reference"]
                    .get<std::string>(),
            "U1" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_footprint_geometry_facts"].at( 0 )["value"]
                    .get<std::string>(),
            "LDO-3V3" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_footprint_geometry_facts"].at( 0 )
                    ["courtyard_bbox"]["width"].get<int>(),
            120 );
    BOOST_REQUIRE( placementPacket.contains( "placement_context_facts" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]["footprint_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]
                    ["footprints_with_courtyard_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]["courtyard_pair_count"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]["courtyard_overlap_count"].get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]
                    ["minimum_courtyard_bbox_spacing"].get<int>(),
            150000 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_context_facts"]
                    ["minimum_non_overlapping_courtyard_bbox_spacing"].get<int>(),
            150000 );
    BOOST_REQUIRE( placementPacket.contains( "placement_courtyard_pair_facts" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["placement_courtyard_pair_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_courtyard_pair_facts"].at( 0 )["kind"]
                    .get<std::string>(),
            "footprint_courtyard_pair" );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_courtyard_pair_facts"].at( 0 )
                    ["bbox_spacing"].get<int>(),
            150000 );
    BOOST_CHECK(
            !placementPacket["placement_courtyard_pair_facts"].at( 0 )
                     ["bbox_overlaps"].get<bool>() );
    BOOST_CHECK_EQUAL(
            placementPacket["placement_courtyard_pair_facts"].at( 0 )
                    ["source_footprint"]["label"].get<std::string>(),
            "U1" );
    BOOST_REQUIRE( placementPacket.contains( "locality_region" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["locality_region"]["source"].get<std::string>(),
            "cursor_region" );
    BOOST_CHECK_EQUAL(
            placementPacket["locality_region"]["bbox"]["x"].get<int>(),
            90 );
    BOOST_REQUIRE( placementPacket.contains( "local_obstacle_facts" ) );

    std::set<std::string> localObstacleLabels;

    for( const nlohmann::json& fact : placementPacket["local_obstacle_facts"] )
        localObstacleLabels.insert( fact["label"].get<std::string>() );

    BOOST_CHECK( localObstacleLabels.find( "via:100,50" )
                 != localObstacleLabels.end() );
    BOOST_CHECK( localObstacleLabels.find( "footprint:U1" )
                 != localObstacleLabels.end() );
    BOOST_CHECK( localObstacleLabels.find( "via:300,50" )
                 == localObstacleLabels.end() );
    BOOST_REQUIRE( placementPacket.contains( "local_obstacle_summary" ) );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["source"].get<std::string>(),
            "local_obstacle_facts" );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["locality_source"].get<std::string>(),
            "cursor_region" );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["obstacle_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["nearest_obstacle_distance"]
                    .get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["distance_metric"]
                    .get<std::string>(),
            "manhattan_bbox_gap" );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["kind_counts"]
                    ["via_obstacle"].dump(),
            "1" );
    BOOST_CHECK_EQUAL(
            placementPacket["local_obstacle_summary"]["kind_counts"]
                    ["footprint_obstacle"].dump(),
            "1" );

    auto* routingProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"packet_probe\"}" ) } );
    AI_NEXT_ACTION_RUNTIME routingRuntime{
            std::unique_ptr<AI_PROVIDER>( routingProvider ) };
    AI_SUGGESTION_TRIGGER routingTrigger = makeRoutingTrigger();
    routingTrigger.m_ContextSnapshot.m_Anchors.push_back(
            contextAnchor( wxS( "route_start" ),
                           AI_CONTEXT_ANCHOR_KIND::RouteStart,
                           wxS( "Route start" ), 100, 200 ) );
    routingTrigger.m_ContextSnapshot.m_Anchors.push_back(
            contextAnchor( wxS( "route_candidate_1" ),
                           AI_CONTEXT_ANCHOR_KIND::RouteCandidate,
                           wxS( "Route candidate 1" ), 320, 200 ) );
    routingTrigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":30,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"cursor\":{\"x\":260,\"y\":200},"
                 "\"cursor_region\":{\"x\":250,\"y\":210,"
                 "\"width\":180,\"height\":120}}" );
    routingTrigger.m_ContextSnapshot.m_Summary =
            wxS( "{\"kind\":\"pcb_board_summary\","
                 "\"constraint_facts\":{\"source\":\"board\","
                 "\"minimums\":{\"min_clearance\":90000,"
                 "\"min_track_width\":75000},"
                 "\"rule_area_count\":0,\"keepout_count\":0,"
                 "\"effective_constraints\":{"
                 "\"drc_engine_present\":true,\"rules_valid\":true,"
                 "\"worst_constraints\":[{\"type\":\"track_width\","
                 "\"value\":{\"min\":75000,\"has_min\":true}}]}},"
                 "\"connectivity_summary\":{\"source\":\"board_connectivity\","
                 "\"present\":true,\"net_count\":2,\"node_count\":7,"
                 "\"pad_count\":3,\"ratsnest_unconnected_count\":3,"
                 "\"visible_ratsnest_unconnected_count\":1,"
                 "\"local_ratsnest_line_count\":1,"
                 "\"net_component_summaries\":[{\"net_code\":1,"
                 "\"net_name\":\"GND\",\"component_count\":2,"
                 "\"ratsnest_component_edge_count\":1}],"
                 "\"net_component_summary_sample_truncated\":false,"
                 "\"component_graph_nodes\":[{\"id\":\"U1.1\","
                 "\"net_code\":1,\"net_name\":\"GND\"},"
                 "{\"id\":\"U2.1\",\"net_code\":1,\"net_name\":\"GND\"},"
                 "{\"id\":\"U3.1\",\"net_code\":1,\"net_name\":\"GND\"}],"
                 "\"component_graph_edges\":[{\"from\":\"U1.1\","
                 "\"to\":\"U2.1\",\"net_code\":1,\"net_name\":\"GND\","
                 "\"kind\":\"ratsnest\",\"visible\":true,"
                 "\"estimated_manhattan_length\":420,"
                 "\"source\":{\"position\":{\"x\":100,\"y\":200},"
                 "\"item_uuid\":\"u1\",\"item_type\":1},"
                 "\"target\":{\"position\":{\"x\":320,\"y\":200},"
                 "\"item_uuid\":\"u2\",\"item_type\":1}},"
                 "{\"from\":\"U1.1\",\"to\":\"U3.1\","
                 "\"net_code\":1,\"net_name\":\"GND\","
                 "\"kind\":\"ratsnest\",\"visible\":false,"
                 "\"estimated_manhattan_length\":100,"
                 "\"source\":{\"position\":{\"x\":100,\"y\":200},"
                 "\"item_uuid\":\"u1\",\"item_type\":1},"
                 "\"target\":{\"position\":{\"x\":180,\"y\":220},"
                 "\"item_uuid\":\"u3\",\"item_type\":1}}],"
                 "\"unconnected_edges\":[{\"net_code\":1,"
                 "\"net_name\":\"GND\",\"visible\":true}],"
                 "\"unconnected_edge_sample_truncated\":false},"
                 "\"net_facts\":[{\"code\":1,\"name\":\"GND\","
                 "\"routed_track_length\":780,\"routed_track_segment_count\":3,"
                 "\"routed_via_count\":2,"
                 "\"routed_layer_lengths\":[{\"layer\":\"F.Cu\","
                 "\"routed_track_length\":780,"
                 "\"routed_track_segment_count\":3},"
                 "{\"layer\":\"B.Cu\",\"routed_track_length\":120,"
                 "\"routed_track_segment_count\":1}],"
                 "\"netclass\":{\"name\":\"Default\",\"clearance\":90000,"
                 "\"track_width\":150000,\"via_diameter\":450000,"
                 "\"via_drill\":250000},"
                 "\"topology\":{\"node_count\":7,\"component_count\":2,"
                 "\"visible_unconnected_edge_count\":1}}]}" );
    routingTrigger.m_ContextSnapshot.m_VisibleObjects.push_back(
            viaRef( 400, 220, wxS( "GND" ) ) );
    routingTrigger.m_ContextSnapshot.m_VisibleObjects.push_back( padRef() );
    routingTrigger.m_ContextSnapshot.m_VisibleObjects.push_back(
            trackRef( 180, 210, 300, 210, wxS( "GND" ) ) );
    routingTrigger.m_ContextSnapshot.m_VisibleObjects.push_back(
            trackRef( 1000, 1000, 1200, 1000, wxS( "GND" ) ) );

    BOOST_CHECK( !routingRuntime.Update( routingTrigger ).has_value() );
    BOOST_REQUIRE_EQUAL( routingProvider->m_Requests.size(), 1 );
    BOOST_CHECK( routingProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"packet_kind\":\"routing\"" ) ) );
    BOOST_CHECK( routingProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"routing_active\":true" ) ) );

    nlohmann::json routingRequest =
            providerRequestJson( routingProvider->m_Requests.front() );
    const nlohmann::json& routingPacket =
            routingRequest["observation"]["structured_facts"]["work_state_packet"];
    BOOST_REQUIRE( routingPacket.contains( "planning_target" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["planning_target"]["kind"].get<std::string>(),
            "next_landing_from_current_route_head" );
    BOOST_CHECK_EQUAL(
            routingPacket["planning_target"]["net"].get<std::string>(),
            "GND" );
    BOOST_CHECK( routingPacket["planning_target"]["cursor_is_sorting_hint"]
                         .get<bool>() );
    BOOST_REQUIRE( routingPacket.contains( "constraint_summary" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["constraint_summary"]["source"].get<std::string>(),
            "context_summary.constraint_facts" );
    BOOST_CHECK_EQUAL(
            routingPacket["constraint_summary"]["minimums"]["min_track_width"].get<int>(),
            75000 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["constraint_summary"]["effective_constraints"]
                    ["worst_constraints"].size(),
            1 );
    BOOST_REQUIRE( routingPacket.contains( "connectivity_summary" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["connectivity_summary"]["source"].get<std::string>(),
            "context_summary.connectivity_summary" );
    BOOST_CHECK( routingPacket["connectivity_summary"]["present"].get<bool>() );
    BOOST_CHECK_EQUAL(
            routingPacket["connectivity_summary"]["ratsnest_unconnected_count"]
                    .get<int>(),
            3 );
    BOOST_CHECK_EQUAL(
            routingPacket["connectivity_summary"]["visible_ratsnest_unconnected_count"]
                    .get<int>(),
            1 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["connectivity_summary"]["net_component_summaries"].size(),
            1 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["connectivity_summary"]["component_graph_nodes"].size(),
            3 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["connectivity_summary"]["component_graph_edges"].size(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["connectivity_summary"]["component_graph_edges"]
                    .at( 0 )["from"].get<std::string>(),
            "U1.1" );
    BOOST_REQUIRE_EQUAL(
            routingPacket["connectivity_summary"]["unconnected_edges"].size(),
            1 );
    BOOST_REQUIRE( routingPacket.contains( "active_net_summary" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["source"].get<std::string>(),
            "context_summary.net_facts" );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["name"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["netclass"]["track_width"].get<int>(),
            150000 );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["topology"]
                    ["visible_unconnected_edge_count"].get<int>(),
            1 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["active_net_summary"]["component_graph_nodes"].size(),
            3 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["active_net_summary"]["component_graph_edges"].size(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["component_graph_edges"]
                    .at( 0 )["kind"].get<std::string>(),
            "ratsnest" );
    BOOST_CHECK_EQUAL(
            routingPacket["active_net_summary"]["component_graph_edges"]
                    .at( 0 )["from"].get<std::string>(),
            "U1.1" );
    BOOST_REQUIRE( routingPacket.contains( "routing_progress_facts" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["source"].get<std::string>(),
            "active_net_summary.component_graph_edges" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["active_net"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["remaining_component_edge_count"]
                    .get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]
                    ["visible_remaining_component_edge_count"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]
                    ["remaining_estimated_manhattan_length"].get<int>(),
            520 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]
                    ["shortest_remaining_estimated_manhattan_length"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["routed_track_length"].get<int>(),
            780 );
    BOOST_REQUIRE( routingPacket["routing_progress_facts"].contains(
            "routed_track_segment_count" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["routed_track_segment_count"].get<int>(),
            3 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["routed_via_count"].get<int>(),
            2 );
    BOOST_REQUIRE( routingPacket["routing_progress_facts"].contains(
            "routed_layer_lengths" ) );
    BOOST_REQUIRE_EQUAL(
            routingPacket["routing_progress_facts"]["routed_layer_lengths"].size(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["routed_layer_lengths"].at( 0 )
                    ["layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["routed_layer_lengths"].at( 0 )
                    ["routed_track_length"].get<int>(),
            780 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["active_layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]
                    ["active_layer_routed_track_length"].get<int>(),
            780 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]
                    ["active_layer_routed_track_segment_count"].get<int>(),
            3 );
    BOOST_REQUIRE( routingPacket.contains(
            "routing_layer_reachability_facts" ) );
    BOOST_REQUIRE_EQUAL(
            routingPacket["routing_layer_reachability_facts"].size(), 1 );
    const nlohmann::json& activeLayerReachability =
            routingPacket["routing_layer_reachability_facts"].at( 0 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["source"].get<std::string>(),
            "active_net_summary.component_graph_edges+mode_context.layer" );
    BOOST_CHECK_EQUAL( activeLayerReachability["layer"].get<std::string>(),
                       "F.Cu" );
    BOOST_CHECK( activeLayerReachability["active_layer"].get<bool>() );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["candidate_edge_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["visible_candidate_edge_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["estimated_manhattan_length"].get<int>(), 520 );
    BOOST_CHECK(
            !activeLayerReachability["requires_layer_switch"].get<bool>() );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["via_transition_count_estimate"].get<int>(), 0 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["routed_track_length"].get<int>(), 780 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["routed_track_segment_count"].get<int>(), 3 );
    BOOST_CHECK_EQUAL(
            activeLayerReachability["cost_model"].get<std::string>(),
            "heuristic_manhattan_active_layer" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_progress_facts"]["estimated_total_work_length"]
                    .get<int>(),
            1300 );
    BOOST_REQUIRE( routingPacket.contains( "routing_reachability_facts" ) );
    BOOST_REQUIRE_EQUAL( routingPacket["routing_reachability_facts"].size(), 2 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["source"]
                    .get<std::string>(),
            "active_net_summary.component_graph_edges" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["active_net"]
                    .get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["from"]
                    .get<std::string>(),
            "U1.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["to"]
                    .get<std::string>(),
            "U2.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["edge_kind"]
                    .get<std::string>(),
            "ratsnest" );
    BOOST_CHECK(
            routingPacket["routing_reachability_facts"].at( 0 )["visible"]
                    .get<bool>() );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["estimated_manhattan_length"].get<int>(),
            420 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["from_endpoint"]["position"]["x"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["to_endpoint"]["position"]["x"].get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["route_head"]["x"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["route_head_to_from_endpoint_manhattan"].get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["route_head_to_to_endpoint_manhattan"].get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["nearest_endpoint_role"].get<std::string>(),
            "from" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_landing_endpoint_role"].get<std::string>(),
            "to" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_landing_endpoint"]["position"]["x"].get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["name"].get<std::string>(),
            "routing.repair_segment" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["current_position"]["x"]
                    .get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["target_position"]["x"]
                    .get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["net"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["width"].get<int>(),
            30 );
    BOOST_REQUIRE(
            routingPacket["routing_reachability_facts"].at( 0 )
                    .contains( "layer_reachability" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["layer_reachability"]["source"].get<std::string>(),
            "mode_context.layer" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["layer_reachability"]["active_layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["layer_reachability"]["candidate_layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK(
            !routingPacket["routing_reachability_facts"].at( 0 )
                     ["layer_reachability"]["requires_layer_switch"].get<bool>() );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_render_region"]["source"].get<std::string>(),
            "routing_reachability_swept_bbox" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_render_region"]["mode"].get<std::string>(),
            "routing_reachability_review" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_render_region"]["bbox"]["x"].get<int>(),
            85 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["suggested_render_region"]["bbox"]["width"].get<int>(),
            250 );
    BOOST_REQUIRE(
            routingPacket["routing_reachability_facts"].at( 0 )
                    .contains( "reachability_obstacle_facts" ) );
    BOOST_REQUIRE(
            routingPacket["routing_reachability_facts"].at( 0 )
                    .contains( "router_cost_hint" ) );
    const nlohmann::json& routerCostHint =
            routingPacket["routing_reachability_facts"].at( 0 )
                    ["router_cost_hint"];
    BOOST_CHECK_EQUAL( routerCostHint["source"].get<std::string>(),
                       "routing_reachability_fact" );
    BOOST_CHECK_EQUAL( routerCostHint["cost_model"].get<std::string>(),
                       "heuristic_manhattan_obstacle_layer" );
    BOOST_CHECK_EQUAL(
            routerCostHint["route_head_to_nearest_endpoint_manhattan"].get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routerCostHint["remaining_endpoint_span_manhattan"].get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            routerCostHint["candidate_obstacle_count"].get<int>(), 2 );
    BOOST_CHECK(
            !routerCostHint["requires_layer_switch"].get<bool>() );
    BOOST_CHECK_EQUAL(
            routerCostHint["via_transition_count_estimate"].get<int>(), 0 );
    BOOST_CHECK( routerCostHint["review_required"].get<bool>() );

    std::set<std::string> reachabilityObstacleLabels;

    for( const nlohmann::json& fact :
         routingPacket["routing_reachability_facts"].at( 0 )
                 ["reachability_obstacle_facts"] )
    {
        reachabilityObstacleLabels.insert( fact["label"].get<std::string>() );
    }

    BOOST_CHECK( reachabilityObstacleLabels.find( "track:180,210->300,210" )
                 != reachabilityObstacleLabels.end() );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["from_node"]
                    ["id"].get<std::string>(),
            "U1.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["to_node"]
                    ["id"].get<std::string>(),
            "U2.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["priority_rank"]
                    .get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 0 )["priority_reason"]
                    .get<std::string>(),
            "visible_remaining_connection" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 1 )["to"]
                    .get<std::string>(),
            "U3.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 1 )["priority_rank"]
                    .get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_facts"].at( 1 )["priority_reason"]
                    .get<std::string>(),
            "shorter_estimated_connection" );
    BOOST_REQUIRE( routingPacket.contains( "routing_reachability_summary" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["source"].get<std::string>(),
            "routing_reachability_facts" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["active_net"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["candidate_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["visible_candidate_count"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]
                    ["shortest_estimated_manhattan_length"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]
                    ["visible_shortest_estimated_manhattan_length"].get<int>(),
            420 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["top_priority_rank"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["top_priority_reason"]
                    .get<std::string>(),
            "visible_remaining_connection" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["top_from"].get<std::string>(),
            "U1.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["top_to"].get<std::string>(),
            "U2.1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]
                    ["top_suggested_landing_endpoint"]["position"]["x"].get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_reachability_summary"]["top_router_cost_hint"]
                    ["candidate_obstacle_count"].get<int>(),
            2 );
    BOOST_REQUIRE( routingPacket.contains( "active_route_segment" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["active_route_segment"]["start"]["x"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["active_route_segment"]["end"]["x"].get<int>(),
            260 );
    BOOST_CHECK_EQUAL(
            routingPacket["active_route_segment"]["end_source"].get<std::string>(),
            "mode_context.cursor" );
    BOOST_REQUIRE( routingPacket.contains( "route_anchors" ) );
    BOOST_REQUIRE_EQUAL( routingPacket["route_anchors"].size(), 2 );
    BOOST_CHECK_EQUAL(
            routingPacket["route_anchors"].at( 1 )["id"].get<std::string>(),
            "route_candidate_1" );
    BOOST_CHECK_EQUAL(
            routingPacket["route_anchors"].at( 1 )["position"]["x"].get<int>(),
            320 );
    BOOST_REQUIRE(
            routingPacket["route_anchors"].at( 1 ).contains( "provenance" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["route_anchors"].at( 1 )["provenance"]["source"]
                    .get<std::string>(),
            "context_anchor" );
    BOOST_CHECK_EQUAL(
            routingPacket["route_anchors"].at( 1 )["provenance"]["source_id"]
                    .get<std::string>(),
            "route_candidate_1" );
    BOOST_CHECK_EQUAL(
            routingPacket["route_anchors"].at( 1 )["provenance"]["editor_kind"]
                    .get<std::string>(),
            "pcb" );
    BOOST_CHECK(
            routingPacket["route_anchors"].at( 1 )["provenance"]["has_position"]
                    .get<bool>() );
    BOOST_CHECK(
            !routingPacket["route_anchors"].at( 1 )["provenance"]["details_present"]
                     .get<bool>() );
    BOOST_REQUIRE( routingPacket.contains( "routing_corridor_facts" ) );
    BOOST_REQUIRE_EQUAL( routingPacket["routing_corridor_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["anchor_id"].get<std::string>(),
            "route_candidate_1" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["start"]["x"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["end"]["x"].get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["dx"].get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["dy"].get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["corridor_bbox"]["x"].get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["corridor_bbox"]["y"].get<int>(),
            200 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["corridor_bbox"]["width"]
                    .get<int>(),
            220 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["corridor_bbox"]["height"]
                    .get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["corridor_bbox"]["center"]
                    ["x"].get<int>(),
            210 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["width"].get<int>(),
            30 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["name"].get<std::string>(),
            "routing.repair_segment" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["current_position"]["x"]
                    .get<int>(),
            100 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["target_position"]["x"]
                    .get<int>(),
            320 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["net"].get<std::string>(),
            "GND" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["layer"].get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["suggested_tool_call"]["arguments"]["width"].get<int>(),
            30 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["swept_bbox"]["x"].get<int>(),
            85 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["swept_bbox"]["y"].get<int>(),
            185 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["swept_bbox"]["width"]
                    .get<int>(),
            250 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["swept_bbox"]["height"]
                    .get<int>(),
            30 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["suggested_render_region"]
                    ["source"].get<std::string>(),
            "routing_corridor_swept_bbox" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["suggested_render_region"]
                    ["mode"].get<std::string>(),
            "routing_corridor_review" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["suggested_render_region"]
                    ["bbox"]["x"].get<int>(),
            85 );
    BOOST_REQUIRE(
            routingPacket["routing_corridor_facts"].at( 0 )
                    .contains( "corridor_obstacle_facts" ) );

    std::set<std::string> corridorObstacleLabels;

    for( const nlohmann::json& fact : routingPacket["routing_corridor_facts"]
                                              .at( 0 )["corridor_obstacle_facts"] )
    {
        corridorObstacleLabels.insert( fact["label"].get<std::string>() );
    }

    BOOST_CHECK( corridorObstacleLabels.find( "track:180,210->300,210" )
                 != corridorObstacleLabels.end() );
    BOOST_CHECK( corridorObstacleLabels.find( "pad:U4.1" )
                 != corridorObstacleLabels.end() );
    BOOST_CHECK( corridorObstacleLabels.find( "via:400,220" )
                 == corridorObstacleLabels.end() );
    BOOST_CHECK( corridorObstacleLabels.find( "track:1000,1000->1200,1000" )
                 == corridorObstacleLabels.end() );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["segment_style"]
                    .get<std::string>(),
            "horizontal" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["manhattan_length"]
                    .get<int>(),
            220 );
    BOOST_CHECK(
            routingPacket["routing_corridor_facts"].at( 0 )["cursor_is_sorting_hint"]
                    .get<bool>() );
    BOOST_REQUIRE(
            routingPacket["routing_corridor_facts"].at( 0 )
                    .contains( "interaction_semantics" ) );
    const nlohmann::json& corridorInteraction =
            routingPacket["routing_corridor_facts"].at( 0 )
                    ["interaction_semantics"];
    BOOST_CHECK_EQUAL( corridorInteraction["mode"].get<std::string>(),
                       "active_interactive_routing" );
    BOOST_CHECK_EQUAL( corridorInteraction["planning_target"].get<std::string>(),
                       "next_landing_from_current_route_head" );
    BOOST_CHECK_EQUAL( corridorInteraction["route_head_source"].get<std::string>(),
                       "mode_context.start" );
    BOOST_CHECK_EQUAL( corridorInteraction["landing_anchor_source"].get<std::string>(),
                       "route_anchor" );
    BOOST_CHECK( corridorInteraction["cursor_is_sorting_hint"].get<bool>() );
    BOOST_CHECK( corridorInteraction["manual_click_to_materialize"].get<bool>() );
    BOOST_CHECK( corridorInteraction["manual_click_supersedes_attempt"].get<bool>() );
    BOOST_CHECK( corridorInteraction["preview_must_be_rebased_after_click"].get<bool>() );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["cursor"]["x"].get<int>(),
            260 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["cursor_dx"].get<int>(),
            60 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["cursor_dy"].get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["cursor_manhattan_distance"]
                    .get<int>(),
            60 );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["net"].get<std::string>(),
            "GND" );
    BOOST_REQUIRE( routingPacket.contains( "visible_object_summaries" ) );
    BOOST_REQUIRE_EQUAL( routingPacket["visible_object_summaries"].size(), 4 );
    BOOST_CHECK_EQUAL(
            routingPacket["visible_object_summaries"].at( 0 )["details"]["net_name"]
                    .get<std::string>(),
            "GND" );
    BOOST_REQUIRE( routingPacket.contains( "local_obstacle_facts" ) );

    std::set<std::string> routingLocalObstacleLabels;
    bool sawRoutingPadIdentity = false;

    for( const nlohmann::json& fact : routingPacket["local_obstacle_facts"] )
    {
        routingLocalObstacleLabels.insert( fact["label"].get<std::string>() );

        if( fact.value( "kind", "" ) == "pad_obstacle"
            && fact.value( "footprint_reference", "" ) == "U4"
            && fact.value( "footprint_value", "" ) == "SensorAFE"
            && fact.value( "pad_number", "" ) == "1" )
        {
            sawRoutingPadIdentity = true;
        }
    }

    BOOST_CHECK( routingLocalObstacleLabels.find( "track:180,210->300,210" )
                 != routingLocalObstacleLabels.end() );
    BOOST_CHECK( routingLocalObstacleLabels.find( "pad:U4.1" )
                 != routingLocalObstacleLabels.end() );
    BOOST_CHECK( sawRoutingPadIdentity );
    BOOST_CHECK( routingLocalObstacleLabels.find( "track:1000,1000->1200,1000" )
                 == routingLocalObstacleLabels.end() );
    BOOST_REQUIRE( routingPacket.contains( "local_obstacle_summary" ) );
    BOOST_CHECK_EQUAL(
            routingPacket["local_obstacle_summary"]["locality_source"].get<std::string>(),
            "cursor_region" );
    BOOST_CHECK_EQUAL(
            routingPacket["local_obstacle_summary"]["obstacle_count"].get<int>(),
            2 );
    BOOST_CHECK_EQUAL(
            routingPacket["local_obstacle_summary"]["nearest_obstacle_distance"]
                    .get<int>(),
            0 );
    BOOST_CHECK_EQUAL(
            routingPacket["local_obstacle_summary"]["kind_counts"]
                    ["routing_track_obstacle"].dump(),
            "1" );
    BOOST_CHECK_EQUAL(
            routingPacket["local_obstacle_summary"]["kind_counts"]
                    ["pad_obstacle"].dump(),
            "1" );

    auto* autofillProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"packet_probe\"}" ) } );
    AI_NEXT_ACTION_RUNTIME autofillRuntime{
            std::unique_ptr<AI_PROVIDER>( autofillProvider ) };

    BOOST_CHECK( !autofillRuntime.Update( makeAutofillTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( autofillProvider->m_Requests.size(), 1 );
    BOOST_CHECK( autofillProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"packet_kind\":\"structured_surface\"" ) ) );
    BOOST_CHECK( autofillProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"surface_count\":1" ) ) );
    BOOST_CHECK( autofillProvider->m_Requests.front().m_UserText.Contains(
            wxS( "\"focused_control_id\":\"net_class\"" ) ) );

    nlohmann::json autofillRequest =
            providerRequestJson( autofillProvider->m_Requests.front() );
    const nlohmann::json& packet =
            autofillRequest["observation"]["structured_facts"]["work_state_packet"];

    BOOST_REQUIRE( packet.contains( "schema_version" ) );
    BOOST_REQUIRE( packet.contains( "target_scope" ) );
    BOOST_REQUIRE( packet.contains( "neighbor_values" ) );
    BOOST_REQUIRE( packet.contains( "value_provenance" ) );
    BOOST_REQUIRE( packet.contains( "validation_state" ) );
    BOOST_REQUIRE( packet.contains( "normalized_schema" ) );
    BOOST_REQUIRE( packet.contains( "field_origin_facts" ) );
    BOOST_REQUIRE( packet.contains( "surface_guard_facts" ) );
    BOOST_REQUIRE( packet.contains( "structured_surface_work_summary" ) );
    BOOST_REQUIRE( packet.contains( "interaction_semantics" ) );
    const nlohmann::json& surfaceInteraction =
            packet["interaction_semantics"];
    BOOST_CHECK_EQUAL( surfaceInteraction["mode"].get<std::string>(),
                       "focused_structured_surface" );
    BOOST_CHECK_EQUAL( surfaceInteraction["planning_target"].get<std::string>(),
                       "auto_fill_or_refill_visible_surface" );
    BOOST_CHECK_EQUAL( surfaceInteraction["preview_artifact"].get<std::string>(),
                       "structured_surface_patch_overlay" );
    BOOST_CHECK_EQUAL( surfaceInteraction["accept_unit"].get<std::string>(),
                       "guarded_surface_patch" );
    BOOST_CHECK( surfaceInteraction["model_decides_patch_scope"].get<bool>() );
    BOOST_CHECK( surfaceInteraction["guarded_accept_required"].get<bool>() );
    BOOST_CHECK( surfaceInteraction["surface_revision_required"].get<bool>() );
    BOOST_CHECK( surfaceInteraction["selection_fingerprint_required"].get<bool>() );
    BOOST_CHECK_EQUAL( packet["schema_version"].get<std::string>(),
                       "net-class-v1" );
    BOOST_CHECK_EQUAL( packet["target_scope"]["kind"].get<std::string>(),
                       "cell" );
    BOOST_CHECK_EQUAL( packet["target_scope"]["column"].get<std::string>(),
                       "class" );
    BOOST_CHECK_EQUAL( packet["neighbor_values"].at( 0 )["value"].get<std::string>(),
                       "Default" );
    BOOST_CHECK_EQUAL( packet["value_provenance"]["class"].get<std::string>(),
                       "project_default" );
    BOOST_CHECK_EQUAL( packet["validation_state"]["status"].get<std::string>(),
                       "needs_value" );
    BOOST_CHECK_EQUAL( packet["normalized_schema"]["surface_id"].get<std::string>(),
                       "properties" );
    BOOST_CHECK_EQUAL( packet["normalized_schema"]["schema_version"].get<std::string>(),
                       "net-class-v1" );
    BOOST_CHECK_EQUAL( packet["normalized_schema"]["row_count"].get<int>(), 4 );
    BOOST_REQUIRE_EQUAL( packet["normalized_schema"]["fields"].size(), 2 );
    BOOST_CHECK_EQUAL(
            packet["normalized_schema"]["fields"].at( 0 )["id"].get<std::string>(),
            "net" );
    BOOST_CHECK_EQUAL(
            packet["normalized_schema"]["fields"].at( 0 )["kind"].get<std::string>(),
            "column" );
    BOOST_CHECK_EQUAL(
            packet["normalized_schema"]["fields"].at( 1 )["id"].get<std::string>(),
            "class" );
    BOOST_REQUIRE_EQUAL( packet["field_origin_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            packet["field_origin_facts"].at( 0 )["field_id"].get<std::string>(),
            "class" );
    BOOST_CHECK_EQUAL(
            packet["field_origin_facts"].at( 0 )["origin"].get<std::string>(),
            "project_default" );
    BOOST_CHECK_EQUAL(
            packet["field_origin_facts"].at( 0 )["source"].get<std::string>(),
            "value_provenance" );
    const nlohmann::json& guardFacts = packet["surface_guard_facts"];
    BOOST_CHECK_EQUAL( guardFacts["surface_id"].get<std::string>(),
                       "properties" );
    BOOST_CHECK( guardFacts["has_complete_accept_guard"].get<bool>() );
    BOOST_CHECK_EQUAL(
            guardFacts["surface_revision"]["value"].get<int>(), 42 );
    BOOST_CHECK_EQUAL(
            guardFacts["surface_revision"]["source"].get<std::string>(),
            "panel_state.state.surface_revision" );
    BOOST_CHECK_EQUAL(
            guardFacts["surface_revision"]["expected_argument"].get<std::string>(),
            "expected_surface_revision" );
    BOOST_CHECK_EQUAL(
            guardFacts["schema_version"]["value"].get<std::string>(),
            "net-class-v1" );
    BOOST_CHECK_EQUAL(
            guardFacts["schema_version"]["source"].get<std::string>(),
            "panel_state.state.schema_version" );
    BOOST_CHECK_EQUAL(
            guardFacts["selection_fingerprint"]["value"].get<std::string>(),
            "cell:1:class" );
    BOOST_CHECK_EQUAL(
            guardFacts["selection_fingerprint"]["expected_argument"].get<std::string>(),
            "expected_selection_fingerprint" );
    BOOST_REQUIRE_EQUAL( guardFacts["overlap_set"]["value"].size(), 2 );
    BOOST_CHECK_EQUAL(
            guardFacts["overlap_set"]["expected_argument"].get<std::string>(),
            "expected_overlap_set" );
    const nlohmann::json& surfaceSummary =
            packet["structured_surface_work_summary"];
    BOOST_CHECK_EQUAL( surfaceSummary["source"].get<std::string>(),
                       "structured_surface_work_state" );
    BOOST_CHECK_EQUAL( surfaceSummary["surface_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( surfaceSummary["focused_surface_id"].get<std::string>(),
                       "properties" );
    BOOST_CHECK_EQUAL( surfaceSummary["focused_control_id"].get<std::string>(),
                       "net_class" );
    BOOST_CHECK_EQUAL( surfaceSummary["schema_version"].get<std::string>(),
                       "net-class-v1" );
    BOOST_CHECK( surfaceSummary["has_target_scope"].get<bool>() );
    BOOST_CHECK( surfaceSummary["has_complete_accept_guard"].get<bool>() );
    BOOST_CHECK_EQUAL( surfaceSummary["normalized_field_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( surfaceSummary["field_origin_fact_count"].get<int>(), 1 );
    BOOST_CHECK( surfaceSummary["validation_state_present"].get<bool>() );
    BOOST_CHECK_EQUAL( surfaceSummary["validation_status"].get<std::string>(),
                       "needs_value" );
}


BOOST_AUTO_TEST_CASE( RuntimeDecisionObservationIncludesViewportAndCursorRegionFingerprints )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"fingerprint_probe\"}" ) } );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViewportBoundViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 1 );
    BOOST_CHECK( provider->m_Requests.front().m_UserText.Contains(
            wxS( "\"viewport_fingerprint\":\"fnv1a64:" ) ) );
    BOOST_CHECK( provider->m_Requests.front().m_UserText.Contains(
            wxS( "\"cursor_region_fingerprint\":\"fnv1a64:" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesOnlyAfterDecisionAndReviewTurns )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 2 );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK_GE( provider->m_Requests.at( 0 ).m_MaxToolRounds, 3 );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_DisableDefaultTools );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( ".generate_" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "_candidates" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "atomic.run_operation" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "context_access" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "minimal_prompt_tools_on_demand" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "kisurf_get_workspace_view" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "\"context_snapshot\"" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "\"visual\":{\"source\"" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "via:100,50" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "dependency_fingerprint" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "publication_policy" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "\"publish_is_callable_tool\":false" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "\"name\":\"publish.preview\"" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_ToolCatalogJson.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 1 ).m_ToolCatalogJson.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "validate.hidden_attempt" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "\"hidden_mutation_requires_fresh_render\":true" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "\"hidden_mutation_requires_fresh_validation\":true" ) ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "next via" ) ) );
    BOOST_CHECK( !suggestion->m_PreviewOnly );
    BOOST_CHECK( !suggestion->m_EditObjects.empty() );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "attempt_id" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "preview_lease" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "preview_id" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "touched_object_set_fingerprint" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "fnv1a64:" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "tool_results" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "session_journal" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"preview_gate_result\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":true" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "pcb.create_via" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "placement.generate_via_pattern_candidates" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_HiddenSessionId, 0 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_HiddenStepId, 0 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_BaseCheckpointId, 0 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "pcb.create_via" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 400 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 50 );
}


BOOST_AUTO_TEST_CASE( RuntimeWritesPromptTraceForDecisionAndReviewTurns )
{
    wxString path = uniqueNextActionPromptTracePath( wxS( "decision_review" ) );

    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    AI_PROMPT_TRACE_STORE traceStore( path );
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    runtime.SetPromptTraceStore( &traceStore );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK( entries.at( 0 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    BOOST_CHECK( entries.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_ProviderStatus,
                       wxString( wxS( "provider_response" ) ) );
    BOOST_CHECK( entries.at( 0 ).m_PromptTraceJson.Contains( wxS( "user.request" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_PromptTraceJson.Contains( wxS( "user.request" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeMarksNextActionProviderFailureAfterExecutedReviewTool )
{
    wxString path = uniqueNextActionPromptTracePath( wxS( "post_side_effect" ) );

    auto* provider = new REVIEW_TOOL_ERROR_AFTER_EXECUTED_NEXT_ACTION_PROVIDER();
    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetPromptTraceStore( &traceStore );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 3 );
    BOOST_CHECK( entries.back().m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK_EQUAL( entries.back().m_ProviderStatus,
                       wxString( wxS( "provider_error" ) ) );
    BOOST_CHECK( entries.back().m_ProviderTraceJson.Contains(
            wxS( "post_side_effect_ambiguity" ) ) );
    BOOST_CHECK( entries.back().m_ProviderTraceJson.Contains(
            wxS( "call_script_before_failure" ) ) );
    BOOST_CHECK( entries.back().m_ProviderTraceJson.Contains(
            wxS( "do_not_blindly_reexecute_tools" ) ) );

    nlohmann::json trace = nlohmann::json::parse(
            entries.back().m_ProviderTraceJson.ToStdString() );
    BOOST_REQUIRE( trace.contains( "runtime_guard" ) );
    const nlohmann::json& guard = trace["runtime_guard"];
    BOOST_REQUIRE( guard.contains( "recovery_basis" ) );
    const nlohmann::json& recovery = guard["recovery_basis"];
    BOOST_CHECK( recovery["requires_checkpoint_resume"].get<bool>() );
    BOOST_CHECK_EQUAL( recovery["executed_tool_result_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( recovery["board_state_version"]["document_revision"].get<int>(), 12 );
    BOOST_CHECK_EQUAL( recovery["board_state_version"]["view_revision"].get<int>(), 5 );
    BOOST_REQUIRE_EQUAL( recovery["tool_results"].size(), 1 );
    BOOST_CHECK_EQUAL( recovery["tool_results"].at( 0 )["tool_call_id"].get<std::string>(),
                       "call_script_before_failure" );
    BOOST_CHECK( recovery["tool_results"].at( 0 ).contains( "checkpoint_id" ) );
    BOOST_CHECK_GT( recovery["tool_results"].at( 0 )["journal_operation_count"].get<int>(),
                    0 );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesProviderRecoveryArtifactAfterExecutedReviewToolFailure )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "provider_recovery" ) );

    auto* provider = new REVIEW_TOOL_ERROR_AFTER_EXECUTED_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxS( "post_side_effect_ambiguity" ) ) );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "provider_recovery" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "recovery_basis" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "call_script_before_failure" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "session_journal" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesLargeProviderToolResults )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "tool_result" ) );
    wxString tracePath = uniqueNextActionPromptTracePath( wxS( "tool_result" ) );

    auto* provider = new LARGE_UNKNOWN_TOOL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_PROMPT_TRACE_STORE traceStore( tracePath );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );
    runtime.SetPromptTraceStore( &traceStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json rawToolResult = nlohmann::json::parse(
            provider->m_Requests.at( 1 ).m_ToolResults.front().m_ResultJson.ToStdString() );
    BOOST_CHECK( !rawToolResult.contains( "artifact_ref" ) );
    BOOST_CHECK_EQUAL( rawToolResult["status"].get<std::string>(),
                       "unknown_tool" );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind, wxString( wxS( "tool_result" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "next_action" ) ) );

    std::vector<AI_PROMPT_TRACE_ENTRY> traces = traceStore.LoadAll( error );
    bool sawCompiledArtifactRef = false;

    for( const AI_PROMPT_TRACE_ENTRY& trace : traces )
    {
        if( trace.m_PromptTraceJson.Contains( artifacts.front().m_Uri )
            && trace.m_PromptTraceJson.Contains( wxS( "\"artifact_ref\"" ) )
            && trace.m_PromptTraceJson.Contains( wxS( "\"tool_result\"" ) ) )
        {
            sawCompiledArtifactRef = true;
        }
    }

    BOOST_CHECK( sawCompiledArtifactRef );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "\"status\"" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "unknown_tool_" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( tracePath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesLargeScriptToolResultsAsScriptOutput )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "script_output" ) );
    wxString tracePath = uniqueNextActionPromptTracePath( wxS( "script_output" ) );

    auto* provider = new LARGE_SCRIPT_TOOL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_PROMPT_TRACE_STORE traceStore( tracePath );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );
    runtime.SetPromptTraceStore( &traceStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.back().m_ToolResults.size(), 1 );

    nlohmann::json rawToolResult = nlohmann::json::parse(
            provider->m_Requests.back().m_ToolResults.front().m_ResultJson.ToStdString() );
    BOOST_CHECK( !rawToolResult.contains( "artifact_ref" ) );
    BOOST_CHECK_EQUAL( rawToolResult["status"].get<std::string>(),
                       "script_plan_executed" );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "script_output" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind, wxString( wxS( "script_output" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_Source,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxS( "large-script-output-plan" ) ) );

    std::vector<AI_PROMPT_TRACE_ENTRY> traces = traceStore.LoadAll( error );
    bool sawCompiledArtifactRef = false;

    for( const AI_PROMPT_TRACE_ENTRY& trace : traces )
    {
        if( trace.m_PromptTraceJson.Contains( artifacts.front().m_Uri )
            && trace.m_PromptTraceJson.Contains( wxS( "\"artifact_ref\"" ) )
            && trace.m_PromptTraceJson.Contains( wxS( "\"script_output\"" ) ) )
        {
            sawCompiledArtifactRef = true;
        }
    }

    BOOST_CHECK( sawCompiledArtifactRef );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "script_plan_executed" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "script_output_via_0" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( tracePath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesAbandonedHiddenAttemptAuditArtifact )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "failed_attempt" ) );

    auto* provider = new APPLY_CANDIDATE_TOOL_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Steps().back().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "failed_hidden_attempt" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains( wxS( "abandoned" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxS( "render_freshness_failed" ) ) );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "failed_hidden_attempt" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "attempt_journal" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "review_decision" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "call_apply_candidate" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesOversizedVisualObservationBeforeNextActionDecision )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "visual" ) );

    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"visual_artifact_recorded\"}" ) } );
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );
    trigger.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas.annotated_roi" );
    trigger.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    trigger.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,abcdefghijklmnopqrstuvwxyz0123456789" );
    trigger.m_ContextSnapshot.m_Visual.m_FrameId = wxS( "frame-next-1" );
    trigger.m_ContextSnapshot.m_Visual.m_FrameKind = wxS( "annotated_roi" );
    trigger.m_ContextSnapshot.m_Visual.m_WidthPx = 640;
    trigger.m_ContextSnapshot.m_Visual.m_HeightPx = 480;
    trigger.m_ContextSnapshot.m_Visual.m_ByteSize = 4096;
    trigger.m_ContextSnapshot.m_Visual.m_SidecarJson =
            wxS( "{\"frame_id\":\"frame-next-1\","
                 "\"frame_kind\":\"annotated_roi\","
                 "\"attempt_id\":\"attempt-next\","
                 "\"preview_id\":\"preview-next\","
                 "\"anchors\":[{\"anchor_id\":\"route-A\"}]}" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 1 );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind,
                       wxString( wxS( "visual_observation" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId,
                       wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId,
                       wxString( wxS( "board-1" ) ) );
    BOOST_CHECK( provider->m_Requests.front().m_ContextSnapshot.m_Visual.m_SidecarJson
                         .Contains( artifacts.front().m_Uri ) );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains(
            wxS( "data:image/png;base64,abcdefghijklmnopqrstuvwxyz0123456789" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "frame-next-1" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "route-A" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesValidationToolResultAsValidationReportArtifact )
{
    wxString path = uniqueNextActionArtifactManifestPath( wxS( "validation_report" ) );

    auto* provider = new TOOL_CALLING_NEXT_ACTION_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    runtime.SetServices( &services.m_Validation, &services.m_Preview );
    runtime.SetArtifactStore( &artifactStore );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "validation_report" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind,
                       wxString( wxS( "next_action" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_Source,
                       wxString( wxS( "validate_hidden_attempt" ) ) );

    bool sawValidationArtifactRef = false;

    for( const AI_PROVIDER_REQUEST& request : provider->m_Requests )
    {
        for( const AI_TOOL_CALL_RECORD& result : request.m_ToolResults )
        {
            if( result.m_ToolName != wxS( "validate_hidden_attempt" ) )
                continue;

            nlohmann::json parsed = nlohmann::json::parse(
                    result.m_ResultJson.ToStdString(), nullptr, false );

            if( parsed.is_discarded() || !parsed.is_object()
                || !parsed.contains( "artifact_ref" ) )
            {
                continue;
            }

            sawValidationArtifactRef = true;
            BOOST_CHECK_EQUAL( parsed["artifact_ref"]["kind"].get<std::string>(),
                               "validation_report" );
            BOOST_CHECK_EQUAL( parsed["artifact_ref"]["uri"].get<std::string>(),
                               artifacts.front().m_Uri.ToStdString() );
        }
    }

    BOOST_CHECK( sawValidationArtifactRef );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "validation" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "issue_count" ) ) );

    wxRemoveFile( path );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeReplayTraceRecordsFullNextActionLoop )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    const nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    BOOST_REQUIRE( trace["schema"].is_object() );
    BOOST_CHECK_EQUAL( trace["schema"]["name"].get<std::string>(),
                       "kisurf.next_action.replay_trace" );
    BOOST_CHECK_EQUAL( trace["schema"]["version"].get<unsigned>(),
                       AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION );
    BOOST_CHECK( AiValidateNextActionReplayTraceJson(
                         traces.front().m_ReplayJson )
                         .m_Valid );
    BOOST_CHECK_EQUAL( trace["runtime_step_id"].get<uint64_t>(), 1 );
    BOOST_CHECK_EQUAL( trace["terminal_state"].get<std::string>(), "published" );
    BOOST_REQUIRE( trace["semantic_event"].is_object() );
    BOOST_CHECK_EQUAL( trace["semantic_event"]["kind"].get<std::string>(), "layout" );
    BOOST_CHECK( trace["semantic_event"]["slot_id"].get<std::string>().find(
                         "layout" ) != std::string::npos );
    BOOST_REQUIRE( trace["observation_packet"].is_object() );
    BOOST_CHECK_EQUAL( trace["observation_packet"]["observation_packet_id"].get<uint64_t>(),
                       1 );
    BOOST_CHECK( trace["observation_packet"]["structured_facts"].contains(
            "work_state_packet" ) );
    BOOST_REQUIRE( trace["llm_decision"].is_object() );
    BOOST_CHECK_EQUAL( trace["llm_decision"]["decision_kind"].get<std::string>(),
                       "attempt" );
    BOOST_REQUIRE( trace["attempts"].is_array() );
    BOOST_REQUIRE_EQUAL( trace["attempts"].size(), 1 );
    BOOST_CHECK( trace["attempts"].at( 0 )["hidden_attempt_journal"]
                         ["operations"]
                                 .is_array() );
    BOOST_CHECK( trace["attempts"].at( 0 )["render_outputs"].is_object() );
    BOOST_CHECK( trace["attempts"].at( 0 )["validation_facts"].is_object() );
    BOOST_CHECK( trace["attempts"].at( 0 )["budget_counters"].is_object() );
    BOOST_REQUIRE( trace["llm_review_decision"].is_object() );
    BOOST_CHECK_EQUAL( trace["llm_review_decision"]["decision_kind"].get<std::string>(),
                       "publish" );
    BOOST_REQUIRE( trace["publish_decision"].is_object() );
    BOOST_CHECK( trace["publish_decision"]["preview_gate_result"]["allowed"].get<bool>() );
    BOOST_CHECK_EQUAL( trace["published_suggestion_id"].get<uint64_t>(),
                       suggestion->m_Id );
}


BOOST_AUTO_TEST_CASE( ReplayTraceValidationRejectsMissingSchema )
{
    AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT result =
            AiValidateNextActionReplayTraceJson(
                    wxS( "{\"runtime\":\"next_action\","
                         "\"runtime_step_id\":1,"
                         "\"terminal_state\":\"published\"}" ) );

    BOOST_CHECK( !result.m_Valid );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "missing_schema" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceEvaluationSummarizesPublishedAttempt )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson( traces.front().m_ReplayJson );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_SchemaVersion,
                       AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION );
    BOOST_CHECK_EQUAL( evaluation.m_RuntimeStepId, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_TerminalState,
                       wxString( wxS( "published" ) ) );
    BOOST_CHECK( evaluation.m_Published );
    BOOST_CHECK( !evaluation.m_Accepted );
    BOOST_CHECK_EQUAL( evaluation.m_AttemptCount, 1 );
    BOOST_CHECK_GE( evaluation.m_HiddenOperationCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_RenderResultCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_ValidationResultCount, 1 );
    BOOST_CHECK_GE( evaluation.m_BudgetToolRoundCount, 1 );
    BOOST_CHECK_GE( evaluation.m_BudgetMutationCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_BudgetRenderCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_BudgetValidationCount, 1 );
    BOOST_CHECK_GE( evaluation.m_BudgetCreatedObjectCount, 1 );
    BOOST_CHECK_GE( evaluation.m_BudgetTouchedObjectCount, 1 );
    BOOST_CHECK( evaluation.m_PreviewGateAllowed );
    BOOST_CHECK( evaluation.m_WorkStateInteractionSemanticsPresent );
    BOOST_CHECK( !evaluation.m_HasBlockingValidationIssue );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"hidden_operation_count\"" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"budget_mutation_count\"" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"budget_render_count\":1" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"preview_gate_allowed\":true" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"work_state_interaction_semantics_present\":true" ) ) );

    wxArrayString batchInput;
    batchInput.Add( traces.front().m_ReplayJson );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_REQUIRE( batch.m_Valid );
    BOOST_CHECK_GE( batch.m_BudgetToolRoundCount, 1 );
    BOOST_CHECK_GE( batch.m_BudgetMutationCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_BudgetRenderCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_BudgetValidationCount, 1 );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"budget_render_count\":1" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceEvaluationCountsValidationIssues )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    trace["attempts"].at( 0 )["validation_facts"]["issues"] =
            nlohmann::json::array(
                    { { { "kind", "clearance" },
                        { "severity", "warning" },
                        { "blocking", true } },
                      { { "kind", "courtyard_overlap" },
                        { "severity", "error" },
                        { "blocking", false } } } );

    wxString traceJson = wxString::FromUTF8( trace.dump().c_str() );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson( traceJson );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_ValidationIssueCount, 2 );
    BOOST_CHECK( evaluation.m_ValidationIssueKindCountsJson.Contains(
            wxS( "\"clearance\":1" ) ) );
    BOOST_CHECK( evaluation.m_ValidationIssueKindCountsJson.Contains(
            wxS( "\"courtyard_overlap\":1" ) ) );
    BOOST_CHECK( evaluation.m_ValidationIssueSeverityCountsJson.Contains(
            wxS( "\"warning\":1" ) ) );
    BOOST_CHECK( evaluation.m_ValidationIssueSeverityCountsJson.Contains(
            wxS( "\"error\":1" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"validation_issue_count\":2" ) ) );

    wxArrayString batchInput;
    batchInput.Add( traceJson );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_REQUIRE( batch.m_Valid );
    BOOST_CHECK_EQUAL( batch.m_ValidationIssueCount, 2 );
    BOOST_CHECK( batch.m_ValidationIssueKindCountsJson.Contains(
            wxS( "\"clearance\":1" ) ) );
    BOOST_CHECK( batch.m_ValidationIssueSeverityCountsJson.Contains(
            wxS( "\"error\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"validation_issue_count\":2" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceEvaluationCountsPreviewGateFeedback )
{
    auto* provider =
            new SCRIPT_RENDER_PUBLISH_GATE_VALIDATE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson( traces.front().m_ReplayJson );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_DecisionToolResultCount, 0 );
    BOOST_CHECK_EQUAL( evaluation.m_ReviewToolResultCount, 4 );
    BOOST_CHECK_EQUAL( evaluation.m_PreviewGateFeedbackCount, 1 );
    BOOST_CHECK( evaluation.m_PreviewGateFeedbackReasonCountsJson.Contains(
            wxS( "\"validation_freshness_failed\":1" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"review_tool_result_count\":4" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"preview_gate_feedback_count\":1" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"validation_freshness_failed\":1" ) ) );

    wxArrayString batchInput;
    batchInput.Add( traces.front().m_ReplayJson );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_REQUIRE( batch.m_Valid );
    BOOST_CHECK_EQUAL( batch.m_DecisionToolResultCount, 0 );
    BOOST_CHECK_EQUAL( batch.m_ReviewToolResultCount, 4 );
    BOOST_CHECK_EQUAL( batch.m_PreviewGateFeedbackCount, 1 );
    BOOST_CHECK( batch.m_PreviewGateFeedbackReasonCountsJson.Contains(
            wxS( "\"validation_freshness_failed\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"review_tool_result_count\":4" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"preview_gate_feedback_count\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"validation_freshness_failed\":1" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceEvaluationCountsRollbackAttempts )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"revise_candidate\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson( traces.front().m_ReplayJson );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_AttemptCount, 2 );
    BOOST_CHECK_EQUAL( evaluation.m_RollbackAttemptCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_RolledBackAttemptCount, 1 );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"rollback_attempt_count\":1" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.Contains(
            wxS( "\"rolled_back_attempt_count\":1" ) ) );

    wxArrayString batchInput;
    batchInput.Add( traces.front().m_ReplayJson );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_REQUIRE( batch.m_Valid );
    BOOST_CHECK_EQUAL( batch.m_RollbackAttemptCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_RolledBackAttemptCount, 1 );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"rollback_attempt_count\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"rolled_back_attempt_count\":1" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceEvaluationRejectsInvalidTrace )
{
    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson(
                    wxS( "{\"runtime\":\"next_action\","
                         "\"runtime_step_id\":1,"
                         "\"terminal_state\":\"published\"}" ) );

    BOOST_CHECK( !evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_ErrorCode, wxString( wxS( "missing_schema" ) ) );
    BOOST_CHECK( evaluation.m_QualityMetricJson.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( ReplayTraceMigrationKeepsCurrentSchemaTraceUnchanged )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    AI_NEXT_ACTION_REPLAY_TRACE_MIGRATION_RESULT migration =
            AiMigrateNextActionReplayTraceJson(
                    traces.front().m_ReplayJson,
                    AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION );

    BOOST_CHECK( migration.m_Valid );
    BOOST_CHECK( !migration.m_Migrated );
    BOOST_CHECK_EQUAL( migration.m_SourceSchemaVersion,
                       AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION );
    BOOST_CHECK_EQUAL( migration.m_TargetSchemaVersion,
                       AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION );
    BOOST_CHECK_EQUAL( migration.m_ReplayJson, traces.front().m_ReplayJson );
    BOOST_CHECK( AiValidateNextActionReplayTraceJson(
                         migration.m_ReplayJson )
                         .m_Valid );
}


BOOST_AUTO_TEST_CASE( ReplayTraceMigrationRejectsUnsupportedTargetVersion )
{
    AI_NEXT_ACTION_REPLAY_TRACE_MIGRATION_RESULT migration =
            AiMigrateNextActionReplayTraceJson(
                    wxS( "{\"schema\":{\"name\":\"kisurf.next_action.replay_trace\","
                         "\"version\":1},"
                         "\"runtime\":\"next_action\","
                         "\"runtime_step_id\":1,"
                         "\"terminal_state\":\"published\","
                         "\"semantic_event\":{},"
                         "\"observation_packet\":{},"
                         "\"llm_decision\":{},"
                         "\"tool_results\":{},"
                         "\"attempts\":[],"
                         "\"llm_review_decision\":{}}" ),
                    AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION + 1 );

    BOOST_CHECK( !migration.m_Valid );
    BOOST_CHECK_EQUAL( migration.m_ErrorCode,
                       wxString( wxS( "unsupported_target_schema_version" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceBatchEvaluationAggregatesValidAndInvalidTraces )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    wxArrayString batchInput;
    batchInput.Add( traces.front().m_ReplayJson );
    batchInput.Add( wxS( "{\"runtime\":\"next_action\","
                         "\"runtime_step_id\":2,"
                         "\"terminal_state\":\"published\"}" ) );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_CHECK( !batch.m_Valid );
    BOOST_CHECK_EQUAL( batch.m_TotalTraceCount, 2 );
    BOOST_CHECK_EQUAL( batch.m_ValidTraceCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_InvalidTraceCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_FirstErrorCode,
                       wxString( wxS( "missing_schema" ) ) );
    BOOST_CHECK_EQUAL( batch.m_PublishedCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_AttemptCount, 1 );
    BOOST_CHECK_GE( batch.m_HiddenOperationCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_RenderResultCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_ValidationResultCount, 1 );
    BOOST_CHECK_EQUAL( batch.m_WorkStateInteractionSemanticsPresentCount, 1 );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"invalid_trace_count\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"published_count\":1" ) ) );
    BOOST_CHECK( batch.m_SummaryJson.Contains(
            wxS( "\"work_state_interaction_semantics_present_count\":1" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceBatchJsonEvaluationUsesVersionedBatchSchema )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json batch =
            { { "schema",
                { { "name", "kisurf.next_action.replay_batch" },
                  { "version", AI_NEXT_ACTION_REPLAY_BATCH_SCHEMA_VERSION } } },
              { "id", "placement-replay-batch" },
              { "traces", nlohmann::json::array( { trace } ) } };

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceBatchJson(
                    wxString::FromUTF8( batch.dump().c_str() ) );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_TotalTraceCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_ValidTraceCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_PublishedCount, 1 );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"batch_id\":\"placement-replay-batch\"" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayTraceBatchJsonEvaluationRejectsNonObjectTraces )
{
    nlohmann::json batch =
            { { "schema",
                { { "name", "kisurf.next_action.replay_batch" },
                  { "version", AI_NEXT_ACTION_REPLAY_BATCH_SCHEMA_VERSION } } },
              { "id", "bad-replay-batch" },
              { "traces", nlohmann::json::array( { 17 } ) } };

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceBatchJson(
                    wxString::FromUTF8( batch.dump().c_str() ) );

    BOOST_CHECK( !evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_FirstErrorCode == wxS( "invalid_trace_entry" ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"batch_id\":\"bad-replay-batch\"" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksExpectedTraceOutcome )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json golden =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-published" },
              { "description", "placement via next action publishes a preview" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_hidden_operation_count", 1 } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( golden.dump().c_str() ) );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_RecordId,
                       wxString( wxS( "placement-via-published" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"record_id\":\"placement-via-published\"" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains( wxS( "\"passed\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksExpectedWorkState )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json golden =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-wrong-work-state" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "work_state", "routing" } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( golden.dump().c_str() ) );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( !evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_WorkState,
                       wxString( wxS( "layout" ) ) );
    BOOST_CHECK_EQUAL( evaluation.m_ErrorCode,
                       wxString( wxS( "work_state_mismatch" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"work_state\":\"layout\"" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksInnerLoopMetrics )
{
    auto* provider =
            new SCRIPT_RENDER_PUBLISH_GATE_VALIDATE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-gate-feedback-loop" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_review_tool_result_count", 4 },
                  { "min_preview_gate_feedback_count", 1 },
                  { "min_preview_gate_feedback_reason_counts",
                    { { "validation_freshness_failed", 1 } } } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT passEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( passing.dump().c_str() ) );

    BOOST_CHECK( passEvaluation.m_Valid );
    BOOST_CHECK( passEvaluation.m_Passed );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_review_tool_result_count\":4" ) ) );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_preview_gate_feedback_count\":1" ) ) );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_preview_gate_feedback_reason_counts\":{\"validation_freshness_failed\":1}" ) ) );

    nlohmann::json failing = passing;
    failing["id"] = "placement-gate-feedback-loop-too-high";
    failing["expected"]["min_preview_gate_feedback_count"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failEvaluation.m_Valid );
    BOOST_CHECK( !failEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failEvaluation.m_ErrorCode,
            wxString( wxS( "preview_gate_feedback_count_below_minimum" ) ) );

    failing = passing;
    failing["id"] = "placement-gate-feedback-loop-reason-too-high";
    failing["expected"]["min_preview_gate_feedback_reason_counts"]
           ["validation_freshness_failed"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failReasonEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failReasonEvaluation.m_Valid );
    BOOST_CHECK( !failReasonEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failReasonEvaluation.m_ErrorCode,
            wxString( wxS( "preview_gate_feedback_reason_count_below_minimum" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksAcceptGateMetrics )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );
    AI_CONTEXT_VERSION     drifted = suggestion->m_ContextVersion;
    drifted.m_DocumentRevision += 1;

    BOOST_REQUIRE( !runtime.Accept( suggestion->m_Id, edit, drifted ) );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-accept-context-drift" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "expired" },
                  { "published", true },
                  { "min_accept_gate_result_count", 1 },
                  { "min_accept_gate_reason_counts",
                    { { "context_drift", 1 } } } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT passEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( passing.dump().c_str() ) );

    BOOST_CHECK( passEvaluation.m_Valid );
    BOOST_CHECK( passEvaluation.m_Passed );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_accept_gate_result_count\":1" ) ) );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_accept_gate_reason_counts\":{\"context_drift\":1}" ) ) );

    nlohmann::json failing = passing;
    failing["id"] = "placement-via-accept-context-drift-too-high";
    failing["expected"]["min_accept_gate_result_count"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failEvaluation.m_Valid );
    BOOST_CHECK( !failEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failEvaluation.m_ErrorCode,
            wxString( wxS( "accept_gate_result_count_below_minimum" ) ) );

    failing = passing;
    failing["id"] = "placement-via-accept-context-drift-reason-too-high";
    failing["expected"]["min_accept_gate_reason_counts"]["context_drift"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failReasonEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failReasonEvaluation.m_Valid );
    BOOST_CHECK( !failReasonEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failReasonEvaluation.m_ErrorCode,
            wxString( wxS( "accept_gate_reason_count_below_minimum" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksBudgetMetrics )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-budget-metrics" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_budget_mutation_count", 1 },
                  { "min_budget_render_count", 1 },
                  { "min_budget_validation_count", 1 },
                  { "min_budget_created_object_count", 1 },
                  { "min_budget_touched_object_count", 1 } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT passEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( passing.dump().c_str() ) );

    BOOST_CHECK( passEvaluation.m_Valid );
    BOOST_CHECK( passEvaluation.m_Passed );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_budget_render_count\":1" ) ) );

    nlohmann::json failing = passing;
    failing["id"] = "placement-via-budget-render-too-high";
    failing["expected"]["min_budget_render_count"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failEvaluation.m_Valid );
    BOOST_CHECK( !failEvaluation.m_Passed );
    BOOST_CHECK_EQUAL( failEvaluation.m_ErrorCode,
                       wxString( wxS( "budget_render_count_below_minimum" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksRollbackMetrics )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"revise_candidate\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-rollback-retry" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_rollback_attempt_count", 1 },
                  { "min_rolled_back_attempt_count", 1 } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT passEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( passing.dump().c_str() ) );

    BOOST_CHECK( passEvaluation.m_Valid );
    BOOST_CHECK( passEvaluation.m_Passed );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_rollback_attempt_count\":1" ) ) );
    BOOST_CHECK( passEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_rolled_back_attempt_count\":1" ) ) );

    wxArrayString dataset;
    dataset.Add( wxString::FromUTF8( passing.dump().c_str() ) );

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT datasetEvaluation =
            AiEvaluateNextActionReplayGoldenDataset( dataset );

    BOOST_REQUIRE( datasetEvaluation.m_Valid );
    BOOST_REQUIRE( datasetEvaluation.m_Passed );
    BOOST_CHECK_EQUAL( datasetEvaluation.m_TraceRollbackAttemptCount, 1 );
    BOOST_CHECK_EQUAL( datasetEvaluation.m_TraceRolledBackAttemptCount, 1 );
    BOOST_CHECK( datasetEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_rollback_attempt_count\":1" ) ) );
    BOOST_CHECK( datasetEvaluation.m_SummaryJson.Contains(
            wxS( "\"trace_rolled_back_attempt_count\":1" ) ) );

    nlohmann::json failing = passing;
    failing["expected"]["min_rollback_attempt_count"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT rollbackFail =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( rollbackFail.m_Valid );
    BOOST_CHECK( !rollbackFail.m_Passed );
    BOOST_CHECK_EQUAL(
            rollbackFail.m_ErrorCode,
            wxString( wxS( "rollback_attempt_count_below_minimum" ) ) );

    failing = passing;
    failing["expected"]["min_rolled_back_attempt_count"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT rolledBackFail =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( rolledBackFail.m_Valid );
    BOOST_CHECK( !rolledBackFail.m_Passed );
    BOOST_CHECK_EQUAL(
            rolledBackFail.m_ErrorCode,
            wxString( wxS( "rolled_back_attempt_count_below_minimum" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationChecksValidationIssueMetrics )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    trace["attempts"].at( 0 )["validation_facts"]["issues"] =
            nlohmann::json::array(
                    { { { "kind", "clearance" },
                        { "severity", "warning" },
                        { "blocking", false } },
                      { { "kind", "courtyard_overlap" },
                        { "severity", "error" },
                        { "blocking", false } } } );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-validation-issues" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_validation_issue_count", 2 },
                  { "min_validation_issue_kind_counts",
                    { { "clearance", 1 }, { "courtyard_overlap", 1 } } },
                  { "min_validation_issue_severity_counts",
                    { { "warning", 1 }, { "error", 1 } } } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT passEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( passing.dump().c_str() ) );

    BOOST_CHECK( passEvaluation.m_Valid );
    BOOST_CHECK( passEvaluation.m_Passed );

    nlohmann::json failing = passing;
    failing["id"] = "placement-via-validation-issues-too-high";
    failing["expected"]["min_validation_issue_count"] = 3;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failEvaluation.m_Valid );
    BOOST_CHECK( !failEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failEvaluation.m_ErrorCode,
            wxString( wxS( "validation_issue_count_below_minimum" ) ) );

    failing = passing;
    failing["id"] = "placement-via-validation-issue-kind-too-high";
    failing["expected"]["min_validation_issue_kind_counts"]["clearance"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failKindEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failKindEvaluation.m_Valid );
    BOOST_CHECK( !failKindEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failKindEvaluation.m_ErrorCode,
            wxString( wxS( "validation_issue_kind_count_below_minimum" ) ) );

    failing = passing;
    failing["id"] = "placement-via-validation-issue-severity-too-high";
    failing["expected"]["min_validation_issue_severity_counts"]["error"] = 2;

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT failSeverityEvaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( failing.dump().c_str() ) );

    BOOST_CHECK( failSeverityEvaluation.m_Valid );
    BOOST_CHECK( !failSeverityEvaluation.m_Passed );
    BOOST_CHECK_EQUAL(
            failSeverityEvaluation.m_ErrorCode,
            wxString( wxS( "validation_issue_severity_count_below_minimum" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenRecordEvaluationReportsExpectationMismatch )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json golden =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-wrong-terminal" },
              { "replay_trace", trace },
              { "expected", { { "terminal_state", "accepted" } } } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenRecordJson(
                    wxString::FromUTF8( golden.dump().c_str() ) );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( !evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_ErrorCode,
                       wxString( wxS( "terminal_state_mismatch" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetEvaluationAggregatesTraceQualityMetrics )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    trace["attempts"].at( 0 )["validation_facts"]["issues"] =
            nlohmann::json::array(
                    { { { "kind", "clearance" },
                        { "severity", "warning" },
                        { "blocking", false } } } );
    trace["publish_decision"]["accept_gate_result"] =
            { { "allowed", false },
              { "reasons", nlohmann::json::array( { "context_drift" } ) } };

    nlohmann::json record =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-quality-metrics" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_budget_mutation_count", 1 },
                  { "min_budget_render_count", 1 },
                  { "min_budget_validation_count", 1 },
                  { "min_accept_gate_result_count", 1 },
                  { "min_accept_gate_reason_counts",
                    { { "context_drift", 1 } } } } } };

    wxArrayString dataset;
    dataset.Add( wxString::FromUTF8( record.dump().c_str() ) );

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDataset( dataset );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_REQUIRE( evaluation.m_Passed );

    nlohmann::json summary =
            nlohmann::json::parse( evaluation.m_SummaryJson.ToStdString() );

    BOOST_REQUIRE( summary.is_object() );
    BOOST_CHECK_EQUAL( summary["trace_budget_mutation_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_render_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_validation_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_accept_gate_result_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL(
            summary["trace_accept_gate_reason_counts"]["context_drift"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL( summary["trace_validation_issue_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_kind_counts"]["clearance"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_severity_counts"]["warning"].get<int>(),
            1 );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetEvaluationAggregatesRecordResults )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json passing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-passes" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_hidden_operation_count", 1 } } } };

    nlohmann::json failing =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-fails" },
              { "replay_trace", trace },
              { "expected", { { "terminal_state", "accepted" } } } };

    wxArrayString dataset;
    dataset.Add( wxString::FromUTF8( passing.dump().c_str() ) );
    dataset.Add( wxString::FromUTF8( failing.dump().c_str() ) );

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDataset( dataset );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( !evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_TotalRecordCount, 2 );
    BOOST_CHECK_EQUAL( evaluation.m_ValidRecordCount, 2 );
    BOOST_CHECK_EQUAL( evaluation.m_InvalidRecordCount, 0 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedRecordCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_FailedRecordCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_FirstFailedRecordId,
                       wxString( wxS( "placement-via-fails" ) ) );
    BOOST_CHECK_EQUAL( evaluation.m_FirstErrorCode,
                       wxString( wxS( "terminal_state_mismatch" ) ) );
    BOOST_CHECK( evaluation.m_ErrorCodeCountsJson.Contains(
            wxS( "\"terminal_state_mismatch\":1" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"failed_record_count\":1" ) ) );

    nlohmann::json summary =
            nlohmann::json::parse( evaluation.m_SummaryJson.ToStdString() );

    BOOST_CHECK_EQUAL(
            summary["error_code_counts"]["terminal_state_mismatch"].get<int>(), 1 );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetJsonEvaluationUsesVersionedDatasetSchema )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json record =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-dataset-json" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_hidden_operation_count", 1 } } } };

    nlohmann::json dataset =
            { { "schema",
                { { "name", "kisurf.next_action.golden_dataset" },
                  { "version",
                    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_SCHEMA_VERSION } } },
              { "id", "next-action-placement-smoke" },
              { "records", nlohmann::json::array( { record } ) } };

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDatasetJson(
                    wxString::FromUTF8( dataset.dump().c_str() ) );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_TotalRecordCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedRecordCount, 1 );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"dataset_id\":\"next-action-placement-smoke\"" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetFileEvaluationRunsVersionedDataset )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    nlohmann::json record =
            { { "schema",
                { { "name", "kisurf.next_action.golden_trace" },
                  { "version", AI_NEXT_ACTION_REPLAY_GOLDEN_SCHEMA_VERSION } } },
              { "id", "placement-via-dataset-file" },
              { "replay_trace", trace },
              { "expected",
                { { "terminal_state", "published" },
                  { "published", true },
                  { "min_hidden_operation_count", 1 } } } };

    nlohmann::json dataset =
            { { "schema",
                { { "name", "kisurf.next_action.golden_dataset" },
                  { "version",
                    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_SCHEMA_VERSION } } },
              { "id", "next-action-placement-file-smoke" },
              { "records", nlohmann::json::array( { record } ) } };

    wxString datasetPath = wxFileName::CreateTempFileName(
            wxS( "kisurf_next_action_golden_dataset" ) );

    {
        wxFFile datasetFile( datasetPath, wxS( "wb" ) );
        BOOST_REQUIRE( datasetFile.IsOpened() );
        BOOST_REQUIRE( datasetFile.Write(
                wxString::FromUTF8( dataset.dump().c_str() ) ) );
    }

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDatasetFile( datasetPath );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_TotalRecordCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedRecordCount, 1 );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"dataset_id\":\"next-action-placement-file-smoke\"" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains( wxS( "\"dataset_path\":" ) ) );

    wxRemoveFile( datasetPath );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetFileEvaluationRunsRepositoryFixture )
{
    wxString datasetPath = repositoryGoldenDatasetPath(
            wxS( "placement_via_inner_loop_smoke.json" ) );

    BOOST_REQUIRE_MESSAGE( wxFileName::FileExists( datasetPath ),
                           "Missing repository golden dataset fixture: "
                                   << datasetPath );

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDatasetFile( datasetPath );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_TotalRecordCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedRecordCount, 1 );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"dataset_id\":\"next-action-placement-via-inner-loop-smoke\"" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains( wxS( "\"dataset_path\":" ) ) );
}


BOOST_AUTO_TEST_CASE( ReplayGoldenDatasetFilesEvaluationAggregatesRepositoryFixtures )
{
    wxArrayString datasetPaths;
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "placement_via_inner_loop_smoke.json" ) ) );
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "routing_segment_inner_loop_smoke.json" ) ) );
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "surface_fill_inner_loop_smoke.json" ) ) );
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "accept_context_drift_inner_loop_smoke.json" ) ) );
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "validation_issue_inner_loop_smoke.json" ) ) );
    datasetPaths.Add( repositoryGoldenDatasetPath(
            wxS( "placement_rollback_retry_inner_loop_smoke.json" ) ) );

    for( const wxString& datasetPath : datasetPaths )
    {
        BOOST_REQUIRE_MESSAGE( wxFileName::FileExists( datasetPath ),
                               "Missing repository golden dataset fixture: "
                                       << datasetPath );
    }

    AI_NEXT_ACTION_REPLAY_GOLDEN_DATASET_BATCH_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayGoldenDatasetFiles( datasetPaths );

    BOOST_CHECK( evaluation.m_Valid );
    BOOST_CHECK( evaluation.m_Passed );
    BOOST_CHECK_EQUAL( evaluation.m_TotalDatasetCount, 6 );
    BOOST_CHECK_EQUAL( evaluation.m_ValidDatasetCount, 6 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedDatasetCount, 6 );
    BOOST_CHECK_EQUAL( evaluation.m_TotalRecordCount, 6 );
    BOOST_CHECK_EQUAL( evaluation.m_PassedRecordCount, 6 );
    BOOST_CHECK_EQUAL( evaluation.m_DatasetPassRate, 1.0 );
    BOOST_CHECK_EQUAL( evaluation.m_RecordPassRate, 1.0 );
    BOOST_CHECK( evaluation.m_WorkStateCountsJson.Contains(
            wxS( "\"placement\":4" ) ) );
    BOOST_CHECK( evaluation.m_WorkStateCountsJson.Contains(
            wxS( "\"routing\":1" ) ) );
    BOOST_CHECK( evaluation.m_WorkStateCountsJson.Contains(
            wxS( "\"structured_surface\":1" ) ) );
    BOOST_CHECK_EQUAL( evaluation.m_ErrorCodeCountsJson, wxString( wxS( "{}" ) ) );

    nlohmann::json summary =
            nlohmann::json::parse( evaluation.m_SummaryJson.ToStdString() );

    BOOST_REQUIRE( summary.is_object() );
    BOOST_CHECK_EQUAL( summary["dataset_pass_rate"].get<double>(), 1.0 );
    BOOST_CHECK_EQUAL( summary["record_pass_rate"].get<double>(), 1.0 );
    BOOST_CHECK_EQUAL( summary["work_state_counts"]["placement"].get<int>(), 4 );
    BOOST_CHECK_EQUAL( summary["work_state_counts"]["routing"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["work_state_counts"]["structured_surface"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_preview_gate_feedback_count"].get<int>(), 6 );
    BOOST_CHECK_EQUAL(
            summary["trace_preview_gate_feedback_reason_counts"]
                   ["render_validation_fresh"]
                           .get<int>(),
            6 );
    BOOST_CHECK_EQUAL( summary["trace_rollback_attempt_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_rolled_back_attempt_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( evaluation.m_TraceRollbackAttemptCount, 1 );
    BOOST_CHECK_EQUAL( evaluation.m_TraceRolledBackAttemptCount, 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_tool_round_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["trace_budget_mutation_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_render_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_validation_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_created_object_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_budget_touched_object_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["trace_accept_gate_result_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL(
            summary["trace_accept_gate_reason_counts"]["context_drift"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL( summary["trace_validation_issue_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_kind_counts"]["clearance"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_kind_counts"]["courtyard_overlap"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_severity_counts"]["warning"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            summary["trace_validation_issue_severity_counts"]["error"].get<int>(),
            1 );
    BOOST_REQUIRE( summary["error_code_counts"].is_object() );
    BOOST_CHECK( summary["error_code_counts"].empty() );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"total_dataset_count\":6" ) ) );
    BOOST_CHECK( evaluation.m_SummaryJson.Contains(
            wxS( "\"total_record_count\":6" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeExecutesProviderToolCallsBeforeDecisionAndReview )
{
    auto* provider = new TOOL_CALLING_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 4 );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 4 );

    BOOST_CHECK( provider->m_Requests.at( 0 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.front().m_ToolCallId,
                       wxString( wxS( "call_observation" ) ) );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.front().m_ToolName,
                       wxString( wxS( "observation_read" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_ToolResults.front().m_Executed );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"observation\"" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"context_access\"" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"minimal_prompt_tools_on_demand\"" ) ) );

    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 3 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 3 ).m_ToolResults.size(), 1 );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 3 ).m_ToolResults.front().m_ToolCallId,
                       wxString( wxS( "call_validate" ) ) );
    BOOST_CHECK_EQUAL( provider->m_Requests.at( 3 ).m_ToolResults.front().m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 3 ).m_ToolResults.front().m_Executed );
    BOOST_CHECK( provider->m_Requests.at( 3 ).m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"validate.hidden_attempt\"" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 3 ).m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"service_connected\":true" ) ) );

    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"provider_tool_results\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"call_validate\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeVisualReferenceToolResolvesObservationAnchor )
{
    AI_NEXT_ACTION_TOOL_REGISTRY registry;
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 77;

    AI_OBSERVATION_PACKET observation;
    observation.m_ContextSnapshot.m_Visual.m_SidecarJson =
            wxS( "{\"anchors\":[{"
                 "\"anchor_id\":\"A1\","
                 "\"object_id\":\"via-1\","
                 "\"handle\":\"ai://session/7/handle/3\","
                 "\"layer\":\"F.Cu\","
                 "\"net_name\":\"GND\","
                 "\"world_xy\":{\"x\":300.0,\"y\":200.0},"
                 "\"world_bounds\":{\"left\":280.0,\"top\":180.0,"
                 "\"right\":320.0,\"bottom\":220.0},"
                 "\"pixel_bounds\":{\"left\":20.0,\"top\":10.0,"
                 "\"right\":40.0,\"bottom\":30.0}"
                 "}]}" );

    AI_TOOL_CALL_RECORD call;
    call.m_ToolCallId = wxS( "call_resolve_anchor" );
    call.m_ToolName = wxS( "observation_resolve_visual_reference" );
    call.m_ArgumentsJson = wxS( "{\"reference\":{\"anchor_id\":\"A1\"}}" );

    AI_TOOL_INVOCATION_RESULT result =
            registry.HandleToolCall( request, call, observation );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ErrorCode.IsEmpty() );

    const nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( payload["tool"].get<std::string>(),
                       "observation.resolve_visual_reference" );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "resolved" );
    BOOST_CHECK_EQUAL( payload["resolution"]["kind"].get<std::string>(),
                       "anchor" );
    BOOST_CHECK_EQUAL( payload["resolution"]["anchor_id"].get<std::string>(),
                       "A1" );
    BOOST_CHECK_EQUAL( payload["resolution"]["handle"].get<std::string>(),
                       "ai://session/7/handle/3" );
    BOOST_CHECK_EQUAL( payload["resolution"]["object_id"].get<std::string>(),
                       "via-1" );
    BOOST_CHECK_EQUAL( payload["resolution"]["layer"].get<std::string>(),
                       "F.Cu" );
    BOOST_CHECK_EQUAL( payload["resolution"]["net_name"].get<std::string>(),
                       "GND" );
    BOOST_CHECK_EQUAL( payload["resolution"]["world_xy"]["x"].get<double>(),
                       300.0 );
    BOOST_CHECK( !payload.value( "publish_allowed", true ) );
}


BOOST_AUTO_TEST_CASE( RuntimeVisualReferenceToolResolvesProvidedPreviewSidecarAnchor )
{
    AI_NEXT_ACTION_TOOL_REGISTRY registry;
    AI_PROVIDER_REQUEST request;
    AI_OBSERVATION_PACKET observation;

    AI_TOOL_CALL_RECORD call;
    call.m_ToolCallId = wxS( "call_resolve_preview_anchor" );
    call.m_ToolName = wxS( "observation_resolve_visual_reference" );
    call.m_ArgumentsJson =
            wxS( "{\"reference\":{\"anchor_id\":\"preview_item:via-1\"},"
                 "\"sidecar_json\":\"{\\\"anchors\\\":[{"
                 "\\\"anchor_id\\\":\\\"preview_item:via-1\\\","
                 "\\\"object_id\\\":\\\"via-1\\\","
                 "\\\"handle\\\":\\\"session:117/handle:1/gen:1/via-1\\\","
                 "\\\"layer\\\":\\\"F.Cu\\\","
                 "\\\"net_name\\\":\\\"GND\\\","
                 "\\\"world_xy\\\":{\\\"x\\\":400.0,\\\"y\\\":500.0},"
                 "\\\"world_bounds\\\":{\\\"left\\\":300.0,\\\"top\\\":400.0,"
                 "\\\"right\\\":500.0,\\\"bottom\\\":600.0},"
                 "\\\"pixel_bounds\\\":{\\\"left\\\":3.0,\\\"top\\\":4.0,"
                 "\\\"right\\\":5.0,\\\"bottom\\\":6.0}"
                 "}]}\"}" );

    AI_TOOL_INVOCATION_RESULT result =
            registry.HandleToolCall( request, call, observation );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ErrorCode.IsEmpty() );

    const nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "resolved" );
    BOOST_CHECK_EQUAL( payload["visual_anchor_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( payload["resolution"]["anchor_id"].get<std::string>(),
                       "preview_item:via-1" );
    BOOST_CHECK_EQUAL( payload["resolution"]["handle"].get<std::string>(),
                       "session:117/handle:1/gen:1/via-1" );
    BOOST_CHECK_EQUAL( payload["resolution"]["pixel_bounds"]["right"].get<double>(),
                       5.0 );
    BOOST_CHECK( !payload.value( "publish_allowed", true ) );
}


BOOST_AUTO_TEST_CASE( RuntimeVisualReferenceToolRejectsPixelOnlyReference )
{
    AI_NEXT_ACTION_TOOL_REGISTRY registry;
    AI_PROVIDER_REQUEST request;
    AI_OBSERVATION_PACKET observation;

    AI_TOOL_CALL_RECORD call;
    call.m_ToolCallId = wxS( "call_resolve_pixel" );
    call.m_ToolName = wxS( "observation_resolve_visual_reference" );
    call.m_ArgumentsJson =
            wxS( "{\"reference\":{\"pixel_position\":{\"x\":21,\"y\":37}}}" );

    AI_TOOL_INVOCATION_RESULT result =
            registry.HandleToolCall( request, call, observation );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "pixel_only_reference" ) ) );

    const nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "rejected" );
    BOOST_CHECK_EQUAL( payload["error_code"].get<std::string>(),
                       "pixel_only_reference" );
    BOOST_CHECK( payload["message"].get<std::string>().find( "anchor" )
                 != std::string::npos );
    BOOST_CHECK( !payload.value( "publish_allowed", true ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsMalformedRenderToolArgumentsBeforeService )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "render_hidden_attempt" ), wxS( "{\"unexpected\":true}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"malformed_arguments\"" ) ) );
    BOOST_CHECK_EQUAL( services.m_Preview.m_RenderCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_RenderCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsInvalidJsonRenderToolArgumentsBeforeService )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "render_hidden_attempt" ), wxS( "{" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "valid JSON" ) ) );
    BOOST_CHECK_EQUAL( services.m_Preview.m_RenderCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_RenderCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsMalformedValidationToolArgumentsBeforeService )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "validate_hidden_attempt" ), wxS( "{\"gate\":\"publish\"}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"malformed_arguments\"" ) ) );
    BOOST_CHECK_EQUAL( services.m_Validation.m_RunCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_ValidationCount,
                       1 );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsInvalidJsonValidationToolArgumentsBeforeService )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "validate_hidden_attempt" ), wxS( "{" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "valid JSON" ) ) );
    BOOST_CHECK_EQUAL( services.m_Validation.m_RunCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_ValidationCount,
                       1 );
}


BOOST_AUTO_TEST_CASE( RuntimeAttemptsDecisionToolGeneratedPlanCandidate )
{
    auto* provider = new PLAN_CANDIDATE_RESULT_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    nlohmann::json arguments = nlohmann::json::parse(
            suggestion->m_ArgumentsJson.ToStdString() );

    BOOST_CHECK_EQUAL( arguments["source_tool"].get<std::string>(),
                       "routing.generate_polyline_plan_candidates" );
    BOOST_REQUIRE( arguments.contains( "plan" ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    const AI_NEXT_ACTION_ATTEMPT_RECORD& attempt = runtime.Attempts().front();
    BOOST_CHECK( attempt.m_Candidate.m_ArgumentsJson.Contains(
            wxS( "polyline_route_plan" ) ) );
    BOOST_CHECK( attempt.m_JournalJson.Contains(
            wxS( "pcb.create_track_segment" ) ) );
    BOOST_CHECK( attempt.m_JournalJson.Contains(
            wxS( "plan_polyline:segment:0" ) ) );
    BOOST_CHECK( attempt.m_JournalJson.Contains(
            wxS( "plan_polyline:segment:1" ) ) );
    BOOST_CHECK( attempt.m_ProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"routing.generate_polyline_plan_candidates\"" ) ) );
    BOOST_CHECK( runtime.CanPreview( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksFailedDecisionToolGeneratedPlanCandidate )
{
    auto* provider = new FAILING_PLAN_CANDIDATE_RESULT_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "invalid_arguments" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "journal_gate_failed" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeRecordsFailureFactForToolRoundBudgetExhaustion )
{
    auto* provider = new TOOL_ROUND_BUDGET_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );

    const wxString& toolResults =
            runtime.Steps().front().m_LlmDecisionToolResultsJson;

    BOOST_CHECK( toolResults.Contains( wxS( "call_observation_13" ) ) );
    BOOST_CHECK( toolResults.Contains( wxS( "tool_round_budget_exceeded" ) ) );
    BOOST_CHECK( toolResults.Contains( wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( toolResults.Contains( wxS( "\"executed\":false" ) ) );
    BOOST_CHECK( toolResults.Contains(
            wxS( "Next Action tool round budget was exhausted before this tool call could run." ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenReviewToolResultFailed )
{
    auto* provider = new PUBLISH_WITH_OVER_BUDGET_REVIEW_TOOL_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Suggestions().empty() );

    const wxString& review = runtime.Steps().front().m_ReviewDecisionJson;

    BOOST_CHECK( review.Contains( wxS( "call_review_observation_10" ) ) );
    BOOST_CHECK( review.Contains( wxS( "tool_round_budget_exceeded" ) ) );
    BOOST_CHECK( review.Contains( wxS( "provider_tool_result_failed" ) ) );
    BOOST_CHECK( review.Contains( wxS( "\"allowed\":false" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsMutationToolBeforeExceedingPolicy )
{
    auto* provider = new MUTATION_BUDGET_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 8 );

    BOOST_CHECK_EQUAL( toolResults.at( 0 ).m_ToolCallId,
                       wxString( wxS( "call_script_plan_1" ) ) );
    BOOST_CHECK( toolResults.at( 0 ).m_Executed );
    BOOST_CHECK_EQUAL( toolResults.at( 1 ).m_ToolCallId,
                       wxString( wxS( "call_script_plan_2" ) ) );
    BOOST_CHECK( toolResults.at( 1 ).m_Executed );

    BOOST_CHECK( toolResults.at( 6 ).m_Executed );

    const AI_TOOL_CALL_RECORD& rejected = toolResults.at( 7 );
    BOOST_CHECK_EQUAL( rejected.m_ToolCallId,
                       wxString( wxS( "call_script_plan_8" ) ) );
    BOOST_CHECK( !rejected.m_Allowed );
    BOOST_CHECK( !rejected.m_Executed );
    BOOST_CHECK_EQUAL( rejected.m_ErrorCode,
                       wxString( wxS( "mutation_budget_exceeded" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"status\":\"mutation_budget_exceeded\"" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"attempt_mutation_count\":8" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL(
            runtime.Attempts().front().m_BudgetCounters.m_MutationCount, 8 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "budget_via_8" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "provider_tool_result_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsRenderToolBeforeExceedingPolicy )
{
    auto* provider = new FACT_TOOL_BUDGET_NEXT_ACTION_PROVIDER(
            wxS( "render_hidden_attempt" ), wxS( "call_render_budget" ),
            wxS( "{\"mode\":\"native_preview_candidate\"}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 4 );
    BOOST_CHECK( toolResults.at( 2 ).m_Executed );

    const AI_TOOL_CALL_RECORD& rejected = toolResults.at( 3 );
    BOOST_CHECK_EQUAL( rejected.m_ToolCallId,
                       wxString( wxS( "call_render_budget_4" ) ) );
    BOOST_CHECK( !rejected.m_Allowed );
    BOOST_CHECK( !rejected.m_Executed );
    BOOST_CHECK_EQUAL( rejected.m_ErrorCode,
                       wxString( wxS( "render_budget_exceeded" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"status\":\"render_budget_exceeded\"" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"attempt_render_count\":4" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL(
            runtime.Attempts().front().m_BudgetCounters.m_RenderCount, 4 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "provider_tool_result_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsValidationToolBeforeExceedingPolicy )
{
    auto* provider = new FACT_TOOL_BUDGET_NEXT_ACTION_PROVIDER(
            wxS( "validate_hidden_attempt" ), wxS( "call_validate_budget" ),
            wxS( "{\"level\":\"drc_lite\"}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 4 );
    BOOST_CHECK( toolResults.at( 2 ).m_Executed );

    const AI_TOOL_CALL_RECORD& rejected = toolResults.at( 3 );
    BOOST_CHECK_EQUAL( rejected.m_ToolCallId,
                       wxString( wxS( "call_validate_budget_4" ) ) );
    BOOST_CHECK( !rejected.m_Allowed );
    BOOST_CHECK( !rejected.m_Executed );
    BOOST_CHECK_EQUAL( rejected.m_ErrorCode,
                       wxString( wxS( "validation_budget_exceeded" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"status\":\"validation_budget_exceeded\"" ) ) );
    BOOST_CHECK( rejected.m_ResultJson.Contains(
            wxS( "\"attempt_validation_count\":4" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL(
            runtime.Attempts().front().m_BudgetCounters.m_ValidationCount, 4 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "provider_tool_result_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeExecutesShadowApplyCandidateToolAgainstHiddenAttempt )
{
    auto* provider = new APPLY_CANDIDATE_TOOL_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            toolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId,
                       wxString( wxS( "call_apply_candidate" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "shadow_apply_candidate" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"tool\":\"shadow.apply_candidate\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"candidate_applied\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"candidate_index\":0" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"checkpoint_id\":" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"session_journal\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"mutation_applied\":true" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"pcb.create_via\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "render_freshness_failed" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeExecutesBoundedScriptPlanToolAgainstHiddenAttempt )
{
    auto* provider = new BOUNDED_SCRIPT_TOOL_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            toolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId,
                       wxString( wxS( "call_script_plan" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"tool\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_executed\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"checkpoint_id\":" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"session_journal\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"lowered_operations\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"script_step_count\":1" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"raw_board_access\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"pcb.create_via\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"provider_tool_results\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"call_script_plan\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"lowered_operations\"" ) ) );

    const wxString& mergedJournal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( mergedJournal.Contains( wxS( "\"script_via_1\"" ) ) );
    BOOST_CHECK( mergedJournal.Contains(
            wxS( "\"merged_from_tool\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( mergedJournal.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_script_plan\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "render_freshness_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeExecutesAtomicOperationToolAgainstHiddenAttempt )
{
    auto* provider = new ATOMIC_OPERATION_TOOL_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result = toolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId,
                       wxString( wxS( "call_atomic_operation" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "atomic_run_operation" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"tool\":\"atomic.run_operation\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"atomic_operation_executed\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"checkpoint_id\":" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"session_journal\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"lowered_operations\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"script_step_count\":1" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"raw_board_access\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"pcb.create_via\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"atomic_via_1\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"provider_tool_results\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"call_atomic_operation\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"atomic.run_operation\"" ) ) );

    const wxString& mergedJournal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( mergedJournal.Contains( wxS( "\"atomic_via_1\"" ) ) );
    BOOST_CHECK( mergedJournal.Contains(
            wxS( "\"merged_from_tool\":\"atomic.run_operation\"" ) ) );
    BOOST_CHECK( mergedJournal.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_atomic_operation\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimeRollsBackFailedBoundedScriptPlanBeforeReviewContinues )
{
    auto* provider = new FAILING_BOUNDED_SCRIPT_TOOL_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result = toolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId,
                       wxString( wxS( "call_failing_script_plan" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_failed\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"error_code\":\"unsupported_script_operation\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"rolled_back\":true" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"rollback_checkpoint_id\":" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"partial_mutation_discarded\":true" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    const wxString& journal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( !journal.Contains( wxS( "rollback_probe_via" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"rolled_back_tool_call_ids\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "call_failing_script_plan" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"partial_mutation_discarded\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksScriptMutationPublishWithoutFreshRender )
{
    auto* provider = new BOUNDED_SCRIPT_TOOL_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 1 );
    BOOST_CHECK_EQUAL( toolResults.front().m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "render_freshness_failed" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeLowersSurfacePatchPlanIntoHiddenAttemptJournal )
{
    auto* provider = new SURFACE_PATCH_SCRIPT_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId,
                       wxString( wxS( "call_surface_patch" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_executed\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"kind\":\"surface.apply_patch\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"patch_operation_count\":2" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"surface_patch_fill_class\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"direct_publish\":false" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    const wxString& journal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( journal.Contains( wxS( "\"kind\":\"surface.apply_patch\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"surface_patch_fill_class\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"patch_operation_count\":2" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );
    BOOST_CHECK( journal.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_surface_patch\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "render_freshness_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRenderToolExposesSurfacePatchPreviewFacts )
{
    auto* provider = new SURFACE_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_surface_patch" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"render.hidden_attempt\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"surface_patch_previews\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"surface_patch_preview\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"surface_id\":\"board_setup.clearance\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"table_id\":\"clearance.rules\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"patch_operation_count\":2" ) ) );
    nlohmann::json renderPayload =
            nlohmann::json::parse( renderResult.m_ResultJson.ToStdString() );
    const nlohmann::json& preview =
            renderPayload["surface_patch_previews"].front();
    BOOST_REQUIRE( preview.contains( "expected_surface_revision" ) );
    BOOST_REQUIRE( preview.contains( "expected_schema_version" ) );
    BOOST_REQUIRE( preview.contains( "expected_selection_fingerprint" ) );
    BOOST_REQUIRE( preview.contains( "expected_overlap_set" ) );
    BOOST_CHECK_EQUAL( preview["expected_surface_revision"].get<int>(), 17 );
    BOOST_CHECK_EQUAL( preview["expected_schema_version"].get<std::string>(),
                       "net-class-v1" );
    BOOST_CHECK_EQUAL(
            preview["expected_selection_fingerprint"].get<std::string>(),
            "cell:row.power:class" );
    BOOST_REQUIRE_EQUAL( preview["expected_overlap_set"].size(), 2 );
    BOOST_REQUIRE( preview.contains( "surface_patch_diff_entries" ) );
    BOOST_REQUIRE_EQUAL( preview["surface_patch_diff_entries"].size(), 2 );
    BOOST_CHECK_EQUAL( preview["surface_patch_diff_entry_count"].get<size_t>(), 2 );

    const nlohmann::json& cellDiff =
            preview["surface_patch_diff_entries"].at( 0 );
    BOOST_CHECK_EQUAL( cellDiff["kind"].get<std::string>(), "set_cell" );
    BOOST_CHECK_EQUAL( cellDiff["surface_id"].get<std::string>(),
                       "board_setup.clearance" );
    BOOST_CHECK_EQUAL( cellDiff["table_id"].get<std::string>(),
                       "clearance.rules" );
    BOOST_CHECK_EQUAL( cellDiff["row_id"].get<std::string>(), "row.power" );
    BOOST_CHECK_EQUAL( cellDiff["column_id"].get<std::string>(), "class" );
    BOOST_CHECK_EQUAL( cellDiff["value"].get<std::string>(), "Power" );
    BOOST_CHECK_EQUAL(
            cellDiff["target_path"].get<std::string>(),
            "surfaces.board_setup.clearance.tables.clearance.rules.rows.row.power.cells.class" );
    BOOST_REQUIRE( cellDiff.contains( "visual_target" ) );
    const nlohmann::json& cellVisualTarget = cellDiff["visual_target"];
    BOOST_CHECK_EQUAL( cellVisualTarget["kind"].get<std::string>(),
                       "table_cell" );
    BOOST_CHECK_EQUAL( cellVisualTarget["surface_id"].get<std::string>(),
                       "board_setup.clearance" );
    BOOST_CHECK_EQUAL( cellVisualTarget["table_id"].get<std::string>(),
                       "clearance.rules" );
    BOOST_CHECK_EQUAL( cellVisualTarget["row_id"].get<std::string>(),
                       "row.power" );
    BOOST_CHECK_EQUAL( cellVisualTarget["column_id"].get<std::string>(),
                       "class" );

    const nlohmann::json& secondCellDiff =
            preview["surface_patch_diff_entries"].at( 1 );
    BOOST_CHECK_EQUAL( secondCellDiff["kind"].get<std::string>(), "set_cell" );
    BOOST_CHECK_EQUAL( secondCellDiff["surface_id"].get<std::string>(),
                       "board_setup.clearance" );
    BOOST_CHECK_EQUAL( secondCellDiff["table_id"].get<std::string>(),
                       "clearance.rules" );
    BOOST_CHECK_EQUAL( secondCellDiff["row_id"].get<std::string>(), "row.gpio" );
    BOOST_CHECK_EQUAL( secondCellDiff["column_id"].get<std::string>(), "class" );
    BOOST_CHECK_EQUAL( secondCellDiff["value"].get<std::string>(), "GPIO" );
    BOOST_CHECK_EQUAL(
            secondCellDiff["target_path"].get<std::string>(),
            "surfaces.board_setup.clearance.tables.clearance.rules.rows.row.gpio.cells.class" );
    BOOST_REQUIRE( preview.contains( "surface_patch_diff_summary" ) );
    const nlohmann::json& diffSummary =
            preview["surface_patch_diff_summary"];
    BOOST_CHECK_EQUAL( diffSummary["diff_entry_count"].get<size_t>(), 2 );
    BOOST_CHECK_EQUAL( diffSummary["table_cell_count"].get<size_t>(), 2 );
    BOOST_CHECK_EQUAL( diffSummary["field_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL(
            diffSummary["unknown_previous_value_count"].get<size_t>(), 2 );
    BOOST_CHECK_EQUAL( diffSummary["changed_value_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL( diffSummary["unchanged_value_count"].get<size_t>(), 0 );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"surface_patch_fill_class\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );

    const AI_TOOL_CALL_RECORD& validationResult =
            publishRequest.m_ToolResults.at( 2 );
    BOOST_CHECK_EQUAL( validationResult.m_ToolCallId,
                       wxString( wxS( "call_validate_surface_patch" ) ) );
    BOOST_CHECK_EQUAL( validationResult.m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRenderToolExpandsSurfacePatchFillOps )
{
    auto* provider = new SURFACE_PATCH_FILL_OPS_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_surface_patch_fill_ops" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );

    nlohmann::json renderPayload =
            nlohmann::json::parse( renderResult.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( renderPayload.contains( "surface_patch_previews" ) );
    const nlohmann::json& preview =
            renderPayload["surface_patch_previews"].front();
    BOOST_CHECK_EQUAL( preview["patch_operation_count"].get<size_t>(), 4 );
    BOOST_REQUIRE( preview.contains( "surface_patch_diff_entries" ) );
    BOOST_REQUIRE_EQUAL( preview["surface_patch_diff_entries"].size(), 6 );
    BOOST_CHECK_EQUAL( preview["surface_patch_diff_entry_count"].get<size_t>(), 6 );

    size_t fillRowCount = 0;
    size_t fillColumnCount = 0;
    size_t fillRangeCount = 0;
    size_t setPropertyCount = 0;
    bool   sawPowerClass = false;
    bool   sawPowerPriority = false;
    bool   sawGpioClass = false;
    bool   sawDefaultClearance = false;

    for( const nlohmann::json& diff : preview["surface_patch_diff_entries"] )
    {
        const std::string sourceOp =
                diff.value( "source_patch_op", std::string() );

        if( sourceOp == "fill_row" )
            ++fillRowCount;
        else if( sourceOp == "fill_column" )
            ++fillColumnCount;
        else if( sourceOp == "fill_range" )
            ++fillRangeCount;
        else if( sourceOp == "set_property" )
            ++setPropertyCount;

        if( diff.value( "row_id", std::string() ) == "row.power"
            && diff.value( "column_id", std::string() ) == "class"
            && diff["value"].get<std::string>() == "Power" )
        {
            sawPowerClass = true;
        }

        if( diff.value( "row_id", std::string() ) == "row.power"
            && diff.value( "column_id", std::string() ) == "priority"
            && diff["value"].get<int>() == 1 )
        {
            sawPowerPriority = true;
        }

        if( diff.value( "row_id", std::string() ) == "row.gpio"
            && diff.value( "column_id", std::string() ) == "class"
            && diff["value"].get<std::string>() == "GPIO" )
        {
            sawGpioClass = true;
        }

        if( diff.value( "kind", std::string() ) == "set_field"
            && diff.value( "field_id", std::string() ) == "default_clearance"
            && diff["value"].get<std::string>() == "0.20mm" )
        {
            sawDefaultClearance = true;
        }
    }

    BOOST_CHECK_EQUAL( fillRowCount, 2 );
    BOOST_CHECK_EQUAL( fillColumnCount, 2 );
    BOOST_CHECK_EQUAL( fillRangeCount, 1 );
    BOOST_CHECK_EQUAL( setPropertyCount, 1 );
    BOOST_CHECK( sawPowerClass );
    BOOST_CHECK( sawPowerPriority );
    BOOST_CHECK( sawGpioClass );
    BOOST_CHECK( sawDefaultClearance );

    BOOST_REQUIRE( preview.contains( "surface_patch_diff_summary" ) );
    const nlohmann::json& diffSummary =
            preview["surface_patch_diff_summary"];
    BOOST_CHECK_EQUAL( diffSummary["diff_entry_count"].get<size_t>(), 6 );
    BOOST_CHECK_EQUAL( diffSummary["table_cell_count"].get<size_t>(), 5 );
    BOOST_CHECK_EQUAL( diffSummary["field_count"].get<size_t>(), 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeBeginPreviewProjectsSurfacePatchDiffOverlays )
{
    auto* provider = new SURFACE_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_REQUIRE( suggestion.has_value() );

    RUNTIME_PREVIEW_RECORDING_ADAPTER adapter;
    AI_PREVIEW_MANAGER                previewManager( adapter );

    BOOST_REQUIRE( runtime.BeginPreview( suggestion->m_Id, previewManager ) );
    BOOST_REQUIRE_EQUAL( adapter.m_BeginIds.size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_Overlays.size(), 2 );
    BOOST_REQUIRE_EQUAL( previewManager.CurrentPreviewOverlays().size(), 2 );

    const AI_PREVIEW_ITEM_OVERLAY& overlay = adapter.m_Overlays.front();
    BOOST_CHECK_EQUAL( overlay.m_OverlayKind,
                       wxString( wxS( "structured_surface_patch" ) ) );
    BOOST_CHECK_EQUAL( overlay.m_Severity, wxString( wxS( "preview" ) ) );
    BOOST_CHECK( overlay.m_ItemLabel.Contains(
            wxS( "surfaces.board_setup.clearance.tables.clearance.rules"
                 ".rows.row.power.cells.class" ) ) );
    BOOST_CHECK( overlay.m_Message.Contains( wxS( "row.power" ) ) );
    BOOST_CHECK( overlay.m_Message.Contains( wxS( "class" ) ) );
    BOOST_CHECK( overlay.m_Message.Contains( wxS( "Power" ) ) );
    BOOST_CHECK( overlay.m_GeometryJson.Contains(
            wxS( "\"kind\":\"table_cell\"" ) ) );
    BOOST_CHECK( overlay.m_GeometryJson.Contains(
            wxS( "\"column_id\":\"class\"" ) ) );
    BOOST_CHECK( overlay.m_GeometryJson.Contains(
            wxS( "\"proposed_value\":\"Power\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRenderToolExposesValueAwareSurfacePatchDiff )
{
    auto* provider = new SURFACE_PATCH_REVISE_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 2 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_surface_patch_revision" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );

    nlohmann::json renderPayload =
            nlohmann::json::parse( renderResult.m_ResultJson.ToStdString() );
    BOOST_REQUIRE( renderPayload.contains( "surface_patch_previews" ) );
    BOOST_REQUIRE_EQUAL( renderPayload["surface_patch_previews"].size(), 2 );

    const nlohmann::json& revisedPreview =
            renderPayload["surface_patch_previews"].at( 1 );
    BOOST_CHECK_EQUAL( revisedPreview["alias"].get<std::string>(),
                       "surface_patch_revised" );
    BOOST_REQUIRE( revisedPreview.contains( "surface_patch_diff_entries" ) );
    BOOST_REQUIRE_EQUAL( revisedPreview["surface_patch_diff_entries"].size(), 1 );

    const nlohmann::json& diff =
            revisedPreview["surface_patch_diff_entries"].front();
    BOOST_CHECK_EQUAL( diff["target_path"].get<std::string>(),
                       "surfaces.board_setup.clearance.tables.clearance.rules.rows.row.power.cells.class" );
    BOOST_REQUIRE( diff.contains( "previous_value" ) );
    BOOST_REQUIRE( diff.contains( "proposed_value" ) );
    BOOST_REQUIRE( diff.contains( "value_changed" ) );
    BOOST_CHECK_EQUAL( diff["previous_value"].get<std::string>(), "Power" );
    BOOST_CHECK_EQUAL( diff["proposed_value"].get<std::string>(), "HighPower" );
    BOOST_CHECK( diff["value_changed"].get<bool>() );
    BOOST_REQUIRE( revisedPreview.contains( "surface_patch_diff_summary" ) );
    const nlohmann::json& summary =
            revisedPreview["surface_patch_diff_summary"];
    BOOST_CHECK_EQUAL( summary["diff_entry_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["table_cell_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["field_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL( summary["changed_value_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["unchanged_value_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL(
            summary["unknown_previous_value_count"].get<size_t>(), 0 );
}


BOOST_AUTO_TEST_CASE( RuntimeSurfaceRepairPatchToolLowersAndFeedsRender )
{
    auto* provider = new SURFACE_REPAIR_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_surface_repair" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "surface_repair_patch" ) ) );
    BOOST_CHECK( repairResult.m_Allowed );
    BOOST_CHECK( repairResult.m_Executed );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"surface.repair_patch\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"surface.apply_patch\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"surface_repair_fill_class\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"expected_surface_revision\":17" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"expected_schema_version\":\"net-class-v1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"expected_selection_fingerprint\":\"cell:row.power:class\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"expected_overlap_set\":[\"row.power\",\"row.gpio\"]" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"surface.repair_patch\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_surface_repair" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"surface_patch_previews\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"surface_patch_preview\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"surface_repair_fill_class\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"surface.repair_patch\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeSurfaceRepairPatchPreservesExplicitWritePolicy )
{
    auto* provider =
            new SURFACE_REPAIR_PATCH_THEN_RENDER_NEXT_ACTION_PROVIDER(
                    wxS( "allow_overwrite" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makePanelFillTriggerWithTargetScope() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult = toolResults.at( 0 );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"allow_overwrite\"" ) ) );
    BOOST_CHECK( !repairResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult = toolResults.at( 1 );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"allow_overwrite\"" ) ) );
    BOOST_CHECK( !renderResult.m_ResultJson.Contains(
            wxS( "\"write_policy\":\"fill_empty_only\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePlacementRepairViaToolLowersAndFeedsRender )
{
    auto* provider = new PLACEMENT_REPAIR_VIA_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_placement_repair" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "placement_repair_via" ) ) );
    BOOST_CHECK( repairResult.m_Allowed );
    BOOST_CHECK( repairResult.m_Executed );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"placement.repair_via\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_via\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"placement_repair_via_1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"placement.repair_via\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_placement_repair" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"placement_repair_via_1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"placement.repair_via\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePlacementRepairMoveItemsToolLowersAndFeedsRender )
{
    auto* provider = new PLACEMENT_REPAIR_MOVE_ITEMS_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& moveResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( moveResult.m_ToolCallId,
                       wxString( wxS( "call_placement_move_repair" ) ) );
    BOOST_CHECK_EQUAL( moveResult.m_ToolName,
                       wxString( wxS( "placement_repair_move_items" ) ) );
    BOOST_CHECK( moveResult.m_Allowed );
    BOOST_CHECK( moveResult.m_Executed );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"placement.repair_move_items\"" ) ) );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.move_items\"" ) ) );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"placement_move_delta\"" ) ) );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"placement.repair_move_items\"" ) ) );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( moveResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 2 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_placement_move_repair" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"placement_repair_move_subject\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.move_items\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"x\":2050000" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"y\":2500000" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeAcceptUsesFinalPlacementRepairAttemptJournal )
{
    auto* provider =
            new PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( trigger );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );
    BOOST_REQUIRE_GE( suggestion->m_EditObjects.size(), 2 );

    bool sawMoveEdit = false;

    for( const AI_OBJECT_REF& editObject : suggestion->m_EditObjects )
    {
        if( editObject.m_DetailsJson.Contains( wxS( "\"kind\":\"pcb.move_items\"" ) )
            && editObject.m_DetailsJson.Contains(
                    wxS( "\"merged_from_tool\":\"placement.repair_move_items\"" ) )
            && editObject.m_DetailsJson.Contains(
                    wxS( "\"alias\":\"placement_move_delta\"" ) ) )
        {
            sawMoveEdit = true;
        }
    }

    BOOST_CHECK( sawMoveEdit );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );

    BOOST_CHECK( runtime.Accept( suggestion->m_Id, edit,
                                 makeDependencyContext( trigger ) ) );

    bool appliedMoveEdit = false;

    for( const AI_OBJECT_REF& appliedObject : editAdapter.m_AppliedObjects )
    {
        if( appliedObject.m_DetailsJson.Contains(
                    wxS( "\"kind\":\"pcb.move_items\"" ) )
            && appliedObject.m_DetailsJson.Contains(
                    wxS( "\"merged_from_tool\":\"placement.repair_move_items\"" ) ) )
        {
            appliedMoveEdit = true;
        }
    }

    BOOST_CHECK( appliedMoveEdit );
}


BOOST_AUTO_TEST_CASE( RuntimePreviewUsesFinalPlacementRepairAttemptJournal )
{
    auto* provider =
            new PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );

    RUNTIME_PREVIEW_RECORDING_ADAPTER adapter;
    AI_PREVIEW_MANAGER                previewManager( adapter );

    BOOST_REQUIRE( runtime.BeginPreview( suggestion->m_Id, previewManager ) );

    bool previewedMoveEdit = false;

    for( const AI_OBJECT_REF& object : adapter.m_Objects )
    {
        if( object.m_DetailsJson.Contains( wxS( "\"kind\":\"pcb.move_items\"" ) )
            && object.m_DetailsJson.Contains(
                    wxS( "\"merged_from_tool\":\"placement.repair_move_items\"" ) ) )
        {
            previewedMoveEdit = true;
        }
    }

    BOOST_CHECK( previewedMoveEdit );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectedPlacementRepairFingerprintUsesFinalJournal )
{
    auto* provider =
            new PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER(
                    { { 250000, -100000 }, { 500000, -100000 } } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( first.has_value() );
    const wxString firstFingerprint = first->m_Fingerprint;
    BOOST_REQUIRE( runtime.Reject( first->m_Id ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 550 ) );

    std::optional<AI_SUGGESTION_RECORD> second =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( second.has_value() );
    BOOST_CHECK_NE( second->m_Fingerprint, firstFingerprint );
    BOOST_CHECK( second->m_RuntimeProvenanceJson.Contains(
            wxS( "\"x\":500000" ) ) );
    BOOST_CHECK( runtime.CanAccept( second->m_Id ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectedIdenticalPlacementRepairFingerprintStaysSilent )
{
    auto* provider =
            new PLACEMENT_REPAIR_MOVE_ITEMS_VALIDATE_PUBLISH_NEXT_ACTION_PROVIDER(
                    { { 250000, -100000 }, { 250000, -100000 } } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( first.has_value() );
    const wxString firstFingerprint = first->m_Fingerprint;
    BOOST_REQUIRE( runtime.Reject( first->m_Id ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 550 ) );

    std::optional<AI_SUGGESTION_RECORD> second =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !second.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Suggestions().front().m_Fingerprint,
                       firstFingerprint );
    BOOST_CHECK( runtime.Suggestions().front().m_Status
                 == AI_SUGGESTION_STATUS::Rejected );
}


BOOST_AUTO_TEST_CASE( RuntimePlacementRepairFootprintOrientationToolLowersAndFeedsRender )
{
    auto* provider =
            new PLACEMENT_REPAIR_ORIENTATION_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& orientationResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( orientationResult.m_ToolCallId,
                       wxString( wxS( "call_placement_orientation_repair" ) ) );
    BOOST_CHECK_EQUAL(
            orientationResult.m_ToolName,
            wxString( wxS( "placement_repair_footprint_orientation" ) ) );
    BOOST_CHECK( orientationResult.m_Allowed );
    BOOST_CHECK( orientationResult.m_Executed );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"placement.repair_footprint_orientation\"" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.set_item_properties\"" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"orientation_degrees\":90" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"side\":\"B.Cu\"" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"placement_orientation_90\"" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"placement.repair_footprint_orientation\"" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( orientationResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 2 );
    BOOST_CHECK_EQUAL(
            renderResult.m_ToolCallId,
            wxString( wxS( "call_render_placement_orientation_repair" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"placement_repair_orientation_subject\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.set_item_properties\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"orientation_degrees\":90" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRoutingRepairSegmentToolLowersAndFeedsRender )
{
    auto* provider = new ROUTING_REPAIR_SEGMENT_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_routing_repair" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "routing_repair_segment" ) ) );
    BOOST_CHECK( repairResult.m_Allowed );
    BOOST_CHECK( repairResult.m_Executed );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"routing.repair_segment\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_segment\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_segment_1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"routing.repair_segment\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_routing_repair" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_segment_1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"routing.repair_segment\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRoutingRepairPolylineToolLowersAndFeedsRender )
{
    auto* provider = new ROUTING_REPAIR_POLYLINE_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_routing_polyline_repair" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "routing_repair_polyline" ) ) );
    BOOST_CHECK( repairResult.m_Allowed );
    BOOST_CHECK( repairResult.m_Executed );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"routing.repair_polyline\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_polyline\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_polyline_1:segment:0\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_polyline_1:segment:1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"routing.repair_polyline\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_routing_polyline_repair" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_polyline_1:segment:0\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"routing_repair_polyline_1:segment:1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_segment\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRoutingRepairBusSegmentsToolLowersAndFeedsRender )
{
    auto* provider =
            new ROUTING_REPAIR_BUS_SEGMENTS_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_routing_bus_repair" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "routing_repair_bus_segments" ) ) );
    BOOST_CHECK( repairResult.m_Allowed );
    BOOST_CHECK( repairResult.m_Executed );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"routing.repair_bus_segments\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"operation_count\":2" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_segment\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains( wxS( "\"net\":\"D0\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains( wxS( "\"net\":\"D1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"routing_bus_repair:lane:0\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"routing_bus_repair:lane:1\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"routing.repair_bus_segments\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_routing_bus_repair" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"routing_bus_repair:lane:0\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"routing_bus_repair:lane:1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_segment\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeExecutesRoutingCandidateBoundedPlanThroughScriptTool )
{
    auto* provider =
            new ROUTING_CANDIDATE_PLAN_THEN_SCRIPT_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& scriptResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( scriptResult.m_ToolCallId,
                       wxString( wxS( "call_execute_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL( scriptResult.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( scriptResult.m_Allowed );
    BOOST_CHECK( scriptResult.m_Executed );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_executed\"" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.delete_items\"" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"kind\":\"pcb.create_track_polyline\"" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"replace_path_polyline:segment:0\"" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"replace_path_polyline:segment:1\"" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 2 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"call_execute_candidate_plan\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "\"call_execute_candidate_plan\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "\"replace_path_polyline:segment:1\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksConstraintReroutePublishWithoutHintedValidation )
{
    auto* provider =
            new ROUTING_CANDIDATE_PLAN_THEN_SCRIPT_NEXT_ACTION_PROVIDER( true );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& scriptResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( scriptResult.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_executed\"" ) ) );
    BOOST_CHECK( scriptResult.m_ResultJson.Contains(
            wxS( "run_validate_hidden_attempt_before_publish" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_hint_not_satisfied" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesConstraintRerouteAfterHintedValidation )
{
    auto* provider =
            new ROUTING_CANDIDATE_PLAN_THEN_SCRIPT_NEXT_ACTION_PROVIDER( true, true );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 6 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 4 );

    const AI_TOOL_CALL_RECORD& validationResult =
            publishRequest.m_ToolResults.at( 3 );
    BOOST_CHECK_EQUAL( validationResult.m_ToolCallId,
                       wxString( wxS( "call_validate_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL( validationResult.m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK( validationResult.m_Allowed );
    BOOST_CHECK( validationResult.m_Executed );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"validate.hidden_attempt\"" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"scope\":\"affected_area\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
    BOOST_CHECK( !runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_hint_not_satisfied" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"call_validate_candidate_plan\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksConstraintRerouteWhenHintedValidationPrecedesMutation )
{
    auto* provider =
            new CONSTRAINT_REROUTE_VALIDATE_BEFORE_EXECUTE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 6 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 4 );

    BOOST_CHECK_EQUAL(
            toolResults.at( 1 ).m_ToolCallId,
            wxString( wxS( "call_validate_before_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL(
            toolResults.at( 2 ).m_ToolCallId,
            wxString( wxS( "call_execute_candidate_plan" ) ) );
    BOOST_CHECK( toolResults.at( 2 ).m_ResultJson.Contains(
            wxS( "\"status\":\"script_plan_executed\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_hint_not_satisfied" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeAllowsUnusedConstraintCandidateWithoutHintedValidation )
{
    auto* provider = new CONSTRAINT_CANDIDATE_OBSERVE_ONLY_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& observationResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL(
            observationResult.m_ToolName,
            wxString( wxS( "observation_resolve_visual_reference" ) ) );
    BOOST_CHECK( observationResult.m_ResultJson.Contains(
            wxS( "run_validate_hidden_attempt_before_publish" ) ) );

    BOOST_CHECK( !runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_hint_not_satisfied" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"call_observe_constraint_hint\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeAllowsRolledBackConstraintMutationWithoutHintedValidation )
{
    auto* provider =
            new CONSTRAINT_REROUTE_EXECUTE_THEN_ROLLBACK_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 6 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 4 );

    BOOST_CHECK_EQUAL(
            toolResults.at( 1 ).m_ToolCallId,
            wxString( wxS( "call_execute_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL(
            toolResults.at( 2 ).m_ToolCallId,
            wxString( wxS( "call_rollback_candidate_plan" ) ) );
    BOOST_CHECK( toolResults.at( 2 ).m_ResultJson.Contains(
            wxS( "\"rolled_back_tool_call_id\":\"call_execute_candidate_plan\"" ) ) );

    BOOST_CHECK( !runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_hint_not_satisfied" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"call_rollback_candidate_plan\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksConstraintRerouteWhenHintedRenderPrecedesMutation )
{
    auto* provider =
            new CONSTRAINT_REROUTE_RENDER_BEFORE_EXECUTE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 6 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 4 );

    BOOST_CHECK_EQUAL(
            toolResults.at( 1 ).m_ToolCallId,
            wxString( wxS( "call_render_before_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL(
            toolResults.at( 2 ).m_ToolCallId,
            wxString( wxS( "call_execute_candidate_plan" ) ) );
    BOOST_CHECK_EQUAL(
            toolResults.at( 3 ).m_ToolCallId,
            wxString( wxS( "call_validate_after_stale_render" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "render_hint_not_satisfied" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeHiddenAttemptExecutesAtomicOperationIntoShadowJournal )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    const wxString journal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( journal.Contains( wxS( "\"kind\":\"pcb.create_via\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"created_handles\":[{\"" ) ) );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_ToolRoundCount, 3 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_MutationCount, 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_RenderCount, 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_ValidationCount, 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_CreatedObjectCount, 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectCount, 1 );
    BOOST_CHECK( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectSetJson.Contains(
            wxS( "ai://session/" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "budget_counters" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "touched_object_set" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRepairsMalformedReviewBeforeAbandoningActiveToolPreview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "The hidden attempt looks acceptable; publish the preview." ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );

    const AI_PROVIDER_REQUEST& repairRequest = provider->m_Requests.back();
    BOOST_CHECK( repairRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_GE( repairRequest.m_ToolResults.size(), 1 );
    BOOST_CHECK_EQUAL( repairRequest.m_ToolResults.back().m_ToolName,
                       wxString( wxS( "review_schema_feedback" ) ) );
    BOOST_CHECK( repairRequest.m_ToolResults.back().m_ResultJson.Contains(
            wxS( "invalid_review_schema" ) ) );
    BOOST_CHECK( runtime.CanPreview( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( RuntimeCarriesRecentStepsInsideActiveNextActionSession )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"first_step\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"second_step\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE( runtime.Update( makeChangedViaTrigger() ).has_value() );

    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );
    const AI_PROVIDER_REQUEST& firstDecision = provider->m_Requests.at( 0 );
    const AI_PROVIDER_REQUEST& secondDecision = provider->m_Requests.at( 2 );

    BOOST_CHECK_EQUAL( firstDecision.m_ConversationId,
                       secondDecision.m_ConversationId );

    auto recentStepsBlock = std::find_if(
            secondDecision.m_ProviderInputBlocks.begin(),
            secondDecision.m_ProviderInputBlocks.end(),
            []( const AI_PROVIDER_INPUT_BLOCK& aBlock )
            {
                return aBlock.m_Id == wxS( "next_action.recent_steps" );
            } );

    BOOST_REQUIRE( recentStepsBlock != secondDecision.m_ProviderInputBlocks.end() );
    BOOST_CHECK( recentStepsBlock->m_Text.Contains( wxS( "first_step" ) ) );
    BOOST_CHECK( recentStepsBlock->m_Text.Contains( wxS( "published" ) ) );
    BOOST_CHECK( !recentStepsBlock->m_Text.Contains( wxS( "second_step" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeWritesNextActionSessionJsonPerActiveActionState )
{
    wxString directory = uniqueNextActionSessionDirectory( wxS( "active_state" ) );

    AI_NEXT_ACTION_SESSION_STORE store( directory );
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"placement_first\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"placement_second\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"routing\","
                   "\"reason_code\":\"routing_first\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    runtime.SetSessionStore( &store );

    AI_SUGGESTION_TRIGGER firstPlacement = makeViaTrigger();
    firstPlacement.m_ContextSnapshot.m_ProjectId = wxS( "project-session" );
    firstPlacement.m_ContextSnapshot.m_DocumentId = wxS( "board-session" );
    AI_SUGGESTION_TRIGGER secondPlacement = makeChangedViaTrigger();
    secondPlacement.m_ContextSnapshot.m_ProjectId = wxS( "project-session" );
    secondPlacement.m_ContextSnapshot.m_DocumentId = wxS( "board-session" );

    BOOST_REQUIRE( runtime.Update( firstPlacement ).has_value() );
    BOOST_REQUIRE( runtime.Update( secondPlacement ).has_value() );
    BOOST_REQUIRE_GE( runtime.Steps().size(), 2 );

    const uint64_t placementConversationId =
            runtime.Steps().at( 0 ).m_ConversationId;
    BOOST_CHECK_EQUAL( runtime.Steps().at( 1 ).m_ConversationId,
                       placementConversationId );
    BOOST_CHECK( wxFileExists( store.SessionPath( placementConversationId ) ) );

    wxFFile placementFile( store.SessionPath( placementConversationId ),
                           wxS( "rb" ) );
    BOOST_REQUIRE( placementFile.IsOpened() );
    wxString placementContent;
    BOOST_REQUIRE( placementFile.ReadAll( &placementContent, wxConvUTF8 ) );
    placementFile.Close();

    BOOST_CHECK( placementContent.Contains(
            wxS( "kisurf.ai.next_action_session" ) ) );
    nlohmann::json placementJson =
            nlohmann::json::parse( placementContent.ToStdString() );
    BOOST_CHECK_EQUAL( placementJson["session_type"].get<std::string>(),
                       std::string( "placement" ) );
    BOOST_CHECK( placementContent.Contains( wxS( "placement_first" ) ) );
    BOOST_CHECK( placementContent.Contains( wxS( "placement_second" ) ) );
    BOOST_CHECK_EQUAL( placementJson["step_count"].get<int>(), 2 );

    AI_SUGGESTION_TRIGGER routing = makeRoutingTrigger();
    routing.m_ContextSnapshot.m_ProjectId = wxS( "project-session" );
    routing.m_ContextSnapshot.m_DocumentId = wxS( "board-session" );

    BOOST_REQUIRE( runtime.Update( routing ).has_value() );
    BOOST_REQUIRE_GE( runtime.Steps().size(), 3 );

    const uint64_t routingConversationId =
            runtime.Steps().at( 2 ).m_ConversationId;
    BOOST_CHECK_NE( routingConversationId, placementConversationId );
    BOOST_CHECK( wxFileExists( store.SessionPath( routingConversationId ) ) );

    wxFFile routingFile( store.SessionPath( routingConversationId ),
                         wxS( "rb" ) );
    BOOST_REQUIRE( routingFile.IsOpened() );
    wxString routingContent;
    BOOST_REQUIRE( routingFile.ReadAll( &routingContent, wxConvUTF8 ) );
    routingFile.Close();

    nlohmann::json routingJson =
            nlohmann::json::parse( routingContent.ToStdString() );
    BOOST_CHECK_EQUAL( routingJson["session_type"].get<std::string>(),
                       std::string( "routing" ) );
    BOOST_CHECK( routingContent.Contains( wxS( "routing_first" ) ) );
    BOOST_CHECK( !routingContent.Contains( wxS( "placement_first" ) ) );

    wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotPublishWhenNativeBudgetCountersExceedPolicy )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );
    SLOW_PREVIEW_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE         previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_GE( runtime.Attempts().front().m_BudgetCounters.m_WallTimeMs,
                    validationService.m_SleepMs );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"preview_gate_result\"" ) ) );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "budget_policy_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeCandidateGenerationRecordsWorkStateSelectedTool )
{
    auto* placementProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );
    PUBLISH_READY_NEXT_ACTION_SERVICES placementServices;
    AI_NEXT_ACTION_RUNTIME placementRuntime{
            std::unique_ptr<AI_PROVIDER>( placementProvider ),
            &placementServices.m_Validation,
            &placementServices.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> placement =
            placementRuntime.Update( makeViaTrigger() );
    BOOST_REQUIRE( placement.has_value() );
    BOOST_CHECK( placement->m_RuntimeProvenanceJson.Contains(
            wxS( "\"candidate_generation\"" ) ) );
    BOOST_CHECK( placement->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selection_mode\":\"work_state_selected\"" ) ) );
    BOOST_CHECK( placement->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"placement.generate_via_pattern_candidates\"" ) ) );
    BOOST_CHECK( !placement->m_RuntimeProvenanceJson.Contains(
            wxS( "compatibility_fanout" ) ) );

    auto* routingProvider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"routing\"}" ),
              publishReview() } );
    PUBLISH_READY_NEXT_ACTION_SERVICES routingServices;
    AI_NEXT_ACTION_RUNTIME routingRuntime{
            std::unique_ptr<AI_PROVIDER>( routingProvider ),
            &routingServices.m_Validation,
            &routingServices.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> routing =
            routingRuntime.Update( makeRoutingTrigger() );
    BOOST_REQUIRE( routing.has_value() );
    BOOST_CHECK( routing->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"routing.generate_segment_candidates\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeUsesWorkStateCandidateWhenDecisionStructurallyAbandons )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"model_waited_for_user\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_LlmDecisionJson.Contains(
            wxS( "work_state_candidate_fallback" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"routing.generate_segment_candidates\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesActiveViaPlacementWithoutNet )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeActiveViaPlacementTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"placement.generate_via_pattern_candidates\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"net\":\"\"" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK( operation->m_NetName.IsEmpty() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 710 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 820 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "pcb.create_via" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesDrawingZoneShapeCandidate )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeDrawingZoneTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"placement.generate_shape_candidates\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "pcb.create_shape" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "Dwgs.User" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "pcb.create_shape" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesDrawingZoneCopperZoneCandidate )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeCopperZoneDrawingTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"placement.generate_shape_candidates\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "pcb.create_zone" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateCopperZonePreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "pcb.create_zone" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRollbackRetryRecordsCheckpointRollback )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"try_next_candidate\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 2 );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rollback.attempt" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rolled_back" ) ) );
    BOOST_CHECK( runtime.Attempts().back().m_RollbackJson.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( RuntimeRollbackRetryContinuesBoundedInnerLoop )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"revise_candidate\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 2 );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rollback.attempt" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rolled_back" ) ) );
    BOOST_CHECK( runtime.Attempts().back().m_RollbackJson.IsEmpty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Steps().front().m_AttemptIds.size(), 2 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
}


BOOST_AUTO_TEST_CASE( RuntimeReviewTurnReceivesPriorAttemptFeedbackAfterRollbackRetry )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"revise_candidate\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 3 );

    const AI_PROVIDER_REQUEST& secondReviewRequest = provider->m_Requests[2];
    BOOST_CHECK( secondReviewRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( secondReviewRequest.m_UserText.Contains(
            wxS( "\"previous_attempts\"" ) ) );
    BOOST_CHECK( secondReviewRequest.m_UserText.Contains(
            wxS( "\"rollback\"" ) ) );
    BOOST_CHECK( secondReviewRequest.m_UserText.Contains(
            wxS( "\"rollback.attempt\"" ) ) );
    BOOST_CHECK( secondReviewRequest.m_UserText.Contains(
            wxS( "\"previous_review_decision\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeReviewRetrySeesMergedScriptAttemptJournal )
{
    auto* provider = new SCRIPT_RETRY_REVIEW_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 2 );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& retryReviewRequest = provider->m_Requests.back();
    BOOST_CHECK( retryReviewRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( retryReviewRequest.m_UserText.Contains(
            wxS( "\"previous_attempts\"" ) ) );
    BOOST_CHECK( retryReviewRequest.m_UserText.Contains(
            wxS( "\"session_journal\"" ) ) );
    BOOST_CHECK( retryReviewRequest.m_UserText.Contains(
            wxS( "\"script_via_1\"" ) ) );
    BOOST_CHECK( retryReviewRequest.m_UserText.Contains(
            wxS( "\"merged_from_tool\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( retryReviewRequest.m_UserText.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_script_plan\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRenderToolSeesScriptJournalWithinSameReviewLoop )
{
    auto* provider = new SCRIPT_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_after_script" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"render.hidden_attempt\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains( wxS( "\"script_via_1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_script_plan\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_ToolRoundCount,
                       5 );

    nlohmann::json mergedJournal = nlohmann::json::parse(
            runtime.Attempts().front().m_JournalJson.ToStdString() );
    BOOST_REQUIRE( mergedJournal.contains( "merged_tool_batches" ) );
    BOOST_REQUIRE( mergedJournal["merged_tool_batches"].is_array() );
    BOOST_CHECK_EQUAL( mergedJournal["merged_tool_batches"].size(), 1 );

    size_t mergedScriptOperationCount = 0;

    for( const nlohmann::json& operation : mergedJournal["operations"] )
    {
        if( operation.is_object()
            && operation.value( "merged_from_tool_call_id", std::string() )
                       == "call_script_plan" )
        {
            ++mergedScriptOperationCount;
        }
    }

    BOOST_CHECK_EQUAL( mergedScriptOperationCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksScriptMutationPublishWithoutFreshValidation )
{
    auto* provider = new SCRIPT_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( provider->m_Requests.back() );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );
    BOOST_CHECK_EQUAL( toolResults.at( 1 ).m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_freshness_failed" ) ) );
    BOOST_CHECK( runtime.Suggestions().empty() );
}


BOOST_AUTO_TEST_CASE( RuntimeFeedsPreviewGateFailureBackIntoReviewLoop )
{
    auto* provider =
            new SCRIPT_RENDER_PUBLISH_GATE_VALIDATE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 6 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 4 );

    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 0 ).m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 1 ).m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 2 ).m_ToolName,
                       wxString( wxS( "preview_gate_feedback" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.at( 2 ).m_ResultJson.Contains(
            wxS( "validation_freshness_failed" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 3 ).m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL(
            publishRequest.m_ToolResults.at( 3 ).m_ToolCallId,
            wxString( wxS( "call_validate_after_gate_feedback" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
    BOOST_CHECK_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeFeedsRepairableValidationGateFailureBackIntoReviewLoop )
{
    auto* provider = new VALIDATION_GATE_REPAIR_NEXT_ACTION_PROVIDER();

    FIRST_BLOCKING_THEN_PASSING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE                        previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 2 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 0 ).m_ToolName,
                       wxString( wxS( "preview_gate_feedback" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.at( 0 ).m_ResultJson.Contains(
            wxS( "validation_gate_failed" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 1 ).m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL(
            publishRequest.m_ToolResults.at( 1 ).m_ToolCallId,
            wxString( wxS( "call_validate_after_validation_gate" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
    BOOST_CHECK_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeFeedsRepairableRenderGateFailureBackIntoReviewLoop )
{
    auto* provider = new RENDER_GATE_REPAIR_NEXT_ACTION_PROVIDER();

    PASSING_SESSION_VALIDATION_SERVICE                 validationService;
    FIRST_FAILING_THEN_PASSING_SESSION_PREVIEW_SERVICE previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );
    BOOST_CHECK_EQUAL( previewService.m_RenderCount, 2 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 0 ).m_ToolName,
                       wxString( wxS( "preview_gate_feedback" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.at( 0 ).m_ResultJson.Contains(
            wxS( "render_gate_failed" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 1 ).m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL(
            publishRequest.m_ToolResults.at( 1 ).m_ToolCallId,
            wxString( wxS( "call_render_after_render_gate" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
    BOOST_CHECK_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeFeedsSequentialPreviewGateFailuresBackIntoReviewLoop )
{
    auto* provider =
            new SCRIPT_PUBLISH_GATE_RENDER_VALIDATE_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 7 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 5 );

    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 0 ).m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 1 ).m_ToolName,
                       wxString( wxS( "preview_gate_feedback" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.at( 1 ).m_ResultJson.Contains(
            wxS( "render_freshness_failed" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 2 ).m_ToolName,
                       wxString( wxS( "render_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL(
            publishRequest.m_ToolResults.at( 2 ).m_ToolCallId,
            wxString( wxS( "call_render_after_gate_feedback" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 3 ).m_ToolName,
                       wxString( wxS( "preview_gate_feedback" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.at( 3 ).m_ResultJson.Contains(
            wxS( "validation_freshness_failed" ) ) );
    BOOST_CHECK_EQUAL( publishRequest.m_ToolResults.at( 4 ).m_ToolName,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL(
            publishRequest.m_ToolResults.at( 4 ).m_ToolCallId,
            wxString( wxS( "call_validate_after_gate_feedback" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
    BOOST_CHECK_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":true" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRepairToolWritesActiveFrameAndFeedsRender )
{
    auto* provider = new REPAIR_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    const std::vector<AI_TOOL_CALL_RECORD> toolResults =
            toolResultsWithoutPreviewGateFeedback( publishRequest );
    BOOST_REQUIRE_EQUAL( toolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            toolResults.at( 0 );
    BOOST_CHECK_EQUAL( repairResult.m_ToolCallId,
                       wxString( wxS( "call_repair_plan" ) ) );
    BOOST_CHECK_EQUAL( repairResult.m_ToolName,
                       wxString( wxS( "repair_apply_bounded_plan" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"repair.apply_bounded_plan\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"active_attempt_frame\":true" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            toolResults.at( 1 );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"render.hidden_attempt\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains( wxS( "\"repair_via_1\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool\":\"repair.apply_bounded_plan\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_repair_plan\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    const wxString& journal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( journal.Contains( wxS( "\"repair_via_1\"" ) ) );
    BOOST_CHECK( journal.Contains(
            wxS( "\"merged_from_tool\":\"repair.apply_bounded_plan\"" ) ) );
    BOOST_CHECK( journal.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_repair_plan\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeValidateToolRefreshesAttemptSessionAfterScriptInSameReviewLoop )
{
    auto* provider = new SCRIPT_THEN_VALIDATE_NEXT_ACTION_PROVIDER();

    TRACKING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE     previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    BOOST_REQUIRE_EQUAL( validationService.m_LiveItemCounts.size(), 2 );
    BOOST_CHECK_EQUAL( validationService.m_LiveItemCounts.front(), 1 );
    BOOST_CHECK_EQUAL( validationService.m_LiveItemCounts.back(), 2 );
    BOOST_REQUIRE_EQUAL( validationService.m_BoardIds.size(), 2 );
    BOOST_CHECK_EQUAL( validationService.m_BoardIds.front(),
                       "next-action-hidden" );
    BOOST_CHECK_EQUAL( validationService.m_BoardIds.back(),
                       "next-action-hidden" );
    BOOST_REQUIRE_EQUAL( validationService.m_CheckpointCounts.size(), 2 );
    BOOST_CHECK_EQUAL( validationService.m_CheckpointCounts.front(), 1 );
    BOOST_CHECK_GE( validationService.m_CheckpointCounts.back(), 2 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );
    const AI_TOOL_CALL_RECORD& validationResult =
            publishRequest.m_ToolResults.at( 1 );
    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 2 );

    BOOST_CHECK_EQUAL( validationResult.m_ToolCallId,
                       wxString( wxS( "call_validate_after_script" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"validate.hidden_attempt\"" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"shadow_live_item_count\":2" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );
    BOOST_CHECK_EQUAL( renderResult.m_ToolCallId,
                       wxString( wxS( "call_render_after_script" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"render.hidden_attempt\"" ) ) );
    BOOST_CHECK( renderResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_MutationCount,
                       2 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_CreatedObjectCount,
                       2 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectCount,
                       2 );
}


BOOST_AUTO_TEST_CASE( RuntimeValidateToolPassesRequestedValidationArgs )
{
    auto* provider = new TOOL_CALLING_NEXT_ACTION_PROVIDER(
            wxS( "{\"level\":\"full_drc\",\"scope\":\"affected_area\"}" ) );

    TRACKING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE     previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( validationService.m_ValidationArgs.size(), 2 );

    nlohmann::json toolValidationArgs = nlohmann::json::parse(
            validationService.m_ValidationArgs.back() );

    BOOST_CHECK_EQUAL( toolValidationArgs["level"].get<std::string>(),
                       "full_drc" );
    BOOST_CHECK_EQUAL( toolValidationArgs["scope"].get<std::string>(),
                       "affected_area" );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 1 );
    BOOST_CHECK( publishRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"validation_args\"" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"full_drc\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRenderToolPassesRequestedRenderArgs )
{
    auto* provider = new RENDER_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "{\"mode\":\"visual_review\","
                 "\"region\":{\"x\":10,\"y\":20,\"width\":300,\"height\":400},"
                 "\"layer_mask\":[\"F.Cu\",\"B.Cu\"]}" ) );

    PASSING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE    previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( previewService.m_RenderArgs.size(), 2 );

    nlohmann::json toolRenderArgs =
            nlohmann::json::parse( previewService.m_RenderArgs.back() );

    BOOST_CHECK_EQUAL( toolRenderArgs["mode"].get<std::string>(),
                       "visual_review" );
    BOOST_CHECK_EQUAL( toolRenderArgs["scope"].get<std::string>(),
                       "session" );
    BOOST_CHECK_EQUAL( toolRenderArgs["region"]["width"].get<int>(), 300 );
    BOOST_REQUIRE_EQUAL( toolRenderArgs["layer_mask"].size(), 2 );
    BOOST_CHECK_EQUAL( toolRenderArgs["layer_mask"].at( 0 ).get<std::string>(),
                       "F.Cu" );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 1 );
    BOOST_CHECK( publishRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"render_args\"" ) ) );
    BOOST_CHECK( publishRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "\"visual_review\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRollbackToolRestoresAttemptJournalBeforeValidationInSameReviewLoop )
{
    auto* provider = new SCRIPT_ROLLBACK_VALIDATE_NEXT_ACTION_PROVIDER();

    TRACKING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE     previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    BOOST_REQUIRE_EQUAL( validationService.m_LiveItemCounts.size(), 2 );
    BOOST_CHECK_EQUAL( validationService.m_LiveItemCounts.front(), 1 );
    BOOST_CHECK_EQUAL( validationService.m_LiveItemCounts.back(), 1 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& rollbackResult =
            publishRequest.m_ToolResults.at( 1 );
    BOOST_CHECK_EQUAL( rollbackResult.m_ToolCallId,
                       wxString( wxS( "call_rollback_script" ) ) );
    BOOST_CHECK( rollbackResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"rollback.attempt\"" ) ) );
    BOOST_CHECK( rollbackResult.m_ResultJson.Contains(
            wxS( "\"status\":\"rolled_back\"" ) ) );
    BOOST_CHECK( rollbackResult.m_ResultJson.Contains(
            wxS( "\"rolled_back_tool_call_id\":\"call_script_plan\"" ) ) );

    const AI_TOOL_CALL_RECORD& validationResult =
            publishRequest.m_ToolResults.at( 2 );
    BOOST_CHECK_EQUAL( validationResult.m_ToolCallId,
                       wxString( wxS( "call_validate_after_rollback" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"shadow_live_item_count\":1" ) ) );
    BOOST_CHECK( !validationResult.m_ResultJson.Contains(
            wxS( "script_via_1" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_MutationCount,
                       1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_CreatedObjectCount,
                       1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectCount,
                       1 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "script_via_1" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsInvalidJsonRollbackToolWithoutRollingBack )
{
    auto* provider = new INVALID_ROLLBACK_AFTER_SCRIPT_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.back().m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& rollbackResult =
            provider->m_Requests.back().m_ToolResults.at( 1 );
    BOOST_CHECK_EQUAL( rollbackResult.m_ToolCallId,
                       wxString( wxS( "call_invalid_rollback" ) ) );
    BOOST_CHECK_EQUAL( rollbackResult.m_ToolName,
                       wxString( wxS( "rollback_attempt" ) ) );
    BOOST_CHECK( !rollbackResult.m_Allowed );
    BOOST_CHECK( !rollbackResult.m_Executed );
    BOOST_CHECK_EQUAL( rollbackResult.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( rollbackResult.m_ResultJson.Contains(
            wxS( "\"status\":\"malformed_arguments\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "invalid_rollback_via" ) ) );
    BOOST_CHECK_GE( runtime.Attempts().front().m_BudgetCounters.m_MutationCount,
                    1 );
    BOOST_CHECK_GE( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectCount,
                    1 );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsInvalidJsonScriptPlanBeforeHiddenMutation )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "script_run_bounded_plan" ), wxS( "{" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "valid JSON" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"malformed_arguments\"" ) ) );
    BOOST_CHECK( !result.m_ResultJson.Contains(
            wxS( "\"script_step_id\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "merged_tool_batches" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsScriptPlanDirectPublishBeforeHiddenMutation )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "script_run_bounded_plan" ),
            wxS( "{\"plan\":{\"operations\":[{\"kind\":\"pcb.create_via\","
                 "\"direct_publish\":true,"
                 "\"arguments\":{\"position\":{\"x\":1000,\"y\":2000},"
                 "\"net\":\"GND\",\"diameter\":600,\"drill\":300,"
                 "\"layer_pair\":{\"top\":\"F.Cu\",\"bottom\":\"B.Cu\"},"
                 "\"alias\":\"forbidden_publish_via\"}}]},\"max_steps\":1}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "script_run_bounded_plan" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "forbidden_runtime_capability" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"forbidden_runtime_capability\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"forbidden_field\":\"plan.operations[0].direct_publish\"" ) ) );
    BOOST_CHECK( !result.m_ResultJson.Contains(
            wxS( "\"script_step_id\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "forbidden_publish_via" ) ) );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "merged_tool_batches" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAtomicOperationRawBoardAccessBeforeHiddenMutation )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "atomic_run_operation" ),
            wxS( "{\"kind\":\"pcb.create_via\","
                 "\"arguments\":{\"position\":{\"x\":1000,\"y\":2000},"
                 "\"net\":\"GND\",\"diameter\":600,\"drill\":300,"
                 "\"layer_pair\":{\"top\":\"F.Cu\",\"bottom\":\"B.Cu\"},"
                 "\"alias\":\"forbidden_raw_board_via\","
                 "\"raw_board_access\":true}}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "atomic_run_operation" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "forbidden_runtime_capability" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"forbidden_runtime_capability\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"forbidden_field\":\"plan.operations[0].arguments.raw_board_access\"" ) ) );
    BOOST_CHECK( !result.m_ResultJson.Contains(
            wxS( "\"script_step_id\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "forbidden_raw_board_via" ) ) );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "merged_tool_batches" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsInvalidJsonShadowApplyCandidateBeforeMutation )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "shadow_apply_candidate" ), wxS( "{" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "shadow_apply_candidate" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "valid JSON" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"malformed_arguments\"" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( !runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "\"tool\":\"shadow.apply_candidate\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsLegacyCandidateToolBeforeArgumentParsing )
{
    auto* provider = new INVALID_REVIEW_TOOL_ARGUMENT_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_parallel_segment_candidates" ), wxS( "{" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeRoutingTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 3 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "routing_generate_parallel_segment_candidates" ) ) );
    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "unknown_tool" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "Unknown Next Action runtime tool" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"status\":\"unknown_tool\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotPublishWithoutReviewApproval )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"low_confidence\"}" ) } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenReviewOmitsGateBasis )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"missing_objective_basis\"}" ) } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"preview_gate_result\"" ) ) );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "review_basis_failed" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotPublishWithoutNativeRenderAndValidationFacts )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_RenderOutputsJson.Contains(
            wxS( "\"tool\":\"render.hidden_attempt\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"tool\":\"validate.hidden_attempt\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenValidationIssueMarksBlocking )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    BLOCKING_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE          previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_gate_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"kind\":\"geometry_overlap\"" ) ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenValidationRuleLoadBlocksPublish )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    RULE_LOAD_BLOCKING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE              previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_gate_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"rule_load\"" ) ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenDerivedValidationStateIsStale )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    STALE_DERIVED_STATE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE               previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "validation_gate_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"connectivity\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"stale\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"refill\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_ValidationFactsJson.Contains(
            wxS( "\"required\"" ) ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationFactsExposeIssueGeometryForReview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    GEOMETRY_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE          previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "issue_geometry_facts" ) );
    BOOST_REQUIRE( facts["issue_geometry_facts"].is_array() );
    BOOST_REQUIRE_EQUAL( facts["issue_geometry_facts"].size(), 1 );
    BOOST_CHECK_EQUAL(
            facts["issue_geometry_facts"].at( 0 )["kind"].get<std::string>(),
            "clearance" );
    BOOST_CHECK_EQUAL(
            facts["issue_geometry_facts"].at( 0 )["severity"].get<std::string>(),
            "warning" );
    BOOST_CHECK_EQUAL(
            facts["issue_geometry_facts"].at( 0 )["geometry"]["bbox"]["w"]
                    .get<int>(),
            30 );
    BOOST_CHECK_EQUAL(
            facts["issue_geometry_facts"].at( 0 )["geometry"]["layer"]
                    .get<std::string>(),
            "F.Cu" );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationIssueGeometryCreatesVisualCropArtifact )
{
    wxString path = uniqueNextActionArtifactManifestPath(
            wxS( "validation_issue_crop" ) );

    auto* provider = new TOOL_CALLING_NEXT_ACTION_PROVIDER();
    PIXEL_BOUNDS_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE              previewService;
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };
    runtime.SetArtifactStore( &artifactStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );
    trigger.m_ContextSnapshot.m_Visual = makeValidationIssueSourceVisualSnapshot();

    BOOST_REQUIRE( runtime.Update( trigger ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "issue_visual_artifacts" ) );
    BOOST_REQUIRE( facts["issue_visual_artifacts"].is_array() );
    BOOST_REQUIRE_EQUAL( facts["issue_visual_artifacts"].size(), 1 );

    const nlohmann::json& cropRef = facts["issue_visual_artifacts"].at( 0 );
    BOOST_CHECK_EQUAL( cropRef["kind"].get<std::string>(),
                       "visual_observation" );
    BOOST_CHECK_EQUAL( cropRef["frame_kind"].get<std::string>(),
                       "issue_crop" );
    BOOST_CHECK_EQUAL( cropRef["source_issue"]["kind"].get<std::string>(),
                       "clearance" );

    wxString cropUri = wxString::FromUTF8(
            cropRef["uri"].get<std::string>().c_str() );

    wxString error;
    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( cropUri, archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "issue_crop" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "Clearance violation" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "frame-validation-source" ) ) );

    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "visual_observation" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    for( const AI_ARTIFACT_RECORD& artifact : artifacts )
    {
        if( wxFileExists( artifact.m_BlobPath ) )
            wxRemoveFile( artifact.m_BlobPath );
    }

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationWorldBoundsIssueCreatesVisualCropArtifact )
{
    wxString path = uniqueNextActionArtifactManifestPath(
            wxS( "validation_world_issue_crop" ) );

    auto* provider = new TOOL_CALLING_NEXT_ACTION_PROVIDER();
    WORLD_BOUNDS_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE              previewService;
    AI_ARTIFACT_STORE artifactStore( path );
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };
    runtime.SetArtifactStore( &artifactStore );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    trigger.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );
    trigger.m_ContextSnapshot.m_Visual = makeValidationIssueSourceVisualSnapshot();

    BOOST_REQUIRE( runtime.Update( trigger ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "issue_visual_artifacts" ) );
    BOOST_REQUIRE( facts["issue_visual_artifacts"].is_array() );
    BOOST_REQUIRE_EQUAL( facts["issue_visual_artifacts"].size(), 1 );

    const nlohmann::json& cropRef = facts["issue_visual_artifacts"].at( 0 );
    BOOST_CHECK_EQUAL( cropRef["frame_kind"].get<std::string>(),
                       "issue_crop" );
    BOOST_CHECK_EQUAL( cropRef["pixel_bounds"]["left"].get<int>(), 30 );
    BOOST_CHECK_EQUAL( cropRef["pixel_bounds"]["top"].get<int>(), 20 );
    BOOST_CHECK_EQUAL( cropRef["pixel_bounds"]["right"].get<int>(), 46 );
    BOOST_CHECK_EQUAL( cropRef["pixel_bounds"]["bottom"].get<int>(), 32 );

    wxString cropUri = wxString::FromUTF8(
            cropRef["uri"].get<std::string>().c_str() );

    wxString error;
    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( cropUri, archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "issue_crop" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "Clearance violation" ) ) );

    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "visual_observation" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    for( const AI_ARTIFACT_RECORD& artifact : artifacts )
    {
        if( wxFileExists( artifact.m_BlobPath ) )
            wxRemoveFile( artifact.m_BlobPath );
    }

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationFactsExposeNativeDrcItemBboxesForReview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    ITEM_BBOX_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE           previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "issue_geometry_facts" ) );
    BOOST_REQUIRE( facts["issue_geometry_facts"].is_array() );
    BOOST_REQUIRE_EQUAL( facts["issue_geometry_facts"].size(), 1 );

    const nlohmann::json& issueFact = facts["issue_geometry_facts"].at( 0 );
    BOOST_CHECK_EQUAL( issueFact["kind"].get<std::string>(), "clearance" );
    BOOST_CHECK_EQUAL( issueFact["severity"].get<std::string>(), "warning" );
    BOOST_REQUIRE( issueFact.contains( "main_item_bbox" ) );
    BOOST_CHECK_EQUAL( issueFact["main_item_bbox"]["width"].get<int>(), 30 );
    BOOST_REQUIRE( issueFact.contains( "aux_item_bbox" ) );
    BOOST_CHECK_EQUAL( issueFact["aux_item_bbox"]["height"].get<int>(), 80 );
    BOOST_REQUIRE( issueFact.contains( "main_aux_bbox_relation" ) );
    const nlohmann::json& relation = issueFact["main_aux_bbox_relation"];
    BOOST_CHECK_CLOSE( relation["center_delta"]["x"].get<double>(), 60.0,
                       0.001 );
    BOOST_CHECK_CLOSE( relation["center_delta"]["y"].get<double>(), 60.0,
                       0.001 );
    BOOST_CHECK_EQUAL( relation["spacing"]["x"].get<int>(), 10 );
    BOOST_CHECK_EQUAL( relation["spacing"]["y"].get<int>(), 0 );
    BOOST_CHECK( !relation["overlap"]["intersects"].get<bool>() );
    BOOST_REQUIRE( issueFact.contains( "suggested_fix_facts" ) );
    BOOST_REQUIRE_EQUAL( issueFact["suggested_fix_facts"].size(), 1 );
    const nlohmann::json& fixFact = issueFact["suggested_fix_facts"].at( 0 );
    BOOST_CHECK_EQUAL( fixFact["kind"].get<std::string>(),
                       "bbox_clearance_review_hint" );
    BOOST_CHECK_EQUAL( fixFact["source"].get<std::string>(),
                       "main_aux_bbox_relation" );
    BOOST_CHECK_EQUAL( fixFact["preferred_axis"].get<std::string>(), "y" );
    BOOST_CHECK_EQUAL( fixFact["current_spacing"]["x"].get<int>(), 10 );
    BOOST_CHECK_EQUAL( fixFact["current_spacing"]["y"].get<int>(), 0 );
    BOOST_CHECK( !fixFact["overlap"]["intersects"].get<bool>() );
    BOOST_CHECK( fixFact["requires_rule_clearance_context"].get<bool>() );
    BOOST_CHECK_EQUAL( issueFact["layer_name"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationFactsExposeNativeDrcIssuePositionForReview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    POSITION_ISSUE_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE          previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "issue_geometry_facts" ) );
    BOOST_REQUIRE( facts["issue_geometry_facts"].is_array() );
    BOOST_REQUIRE_EQUAL( facts["issue_geometry_facts"].size(), 1 );

    const nlohmann::json& issueFact = facts["issue_geometry_facts"].at( 0 );
    BOOST_CHECK_EQUAL( issueFact["source"].get<std::string>(),
                       "pcbnew.drc_engine" );
    BOOST_CHECK_EQUAL( issueFact["key"].get<std::string>(), "clearance" );
    BOOST_CHECK_EQUAL( issueFact["title"].get<std::string>(),
                       "Clearance violation" );
    BOOST_REQUIRE( issueFact.contains( "position" ) );
    BOOST_CHECK_EQUAL( issueFact["position"]["x"].get<int>(), 123 );
    BOOST_CHECK_EQUAL( issueFact["position"]["y"].get<int>(), 456 );
    BOOST_CHECK_EQUAL( issueFact["layer_name"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeValidationFactsExposeReviewSummary )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    SUMMARY_FACTS_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE         previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };

    BOOST_REQUIRE( runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    nlohmann::json facts = nlohmann::json::parse(
            runtime.Attempts().front().m_ValidationFactsJson.ToStdString() );

    BOOST_REQUIRE( facts.contains( "validation_summary" ) );
    BOOST_CHECK_EQUAL(
            facts["validation_summary"]["backend"].get<std::string>(),
            "native_drc" );
    BOOST_CHECK_EQUAL(
            facts["validation_summary"]["grade"].get<std::string>(),
            "preview" );
    BOOST_CHECK_EQUAL(
            facts["validation_summary"]["exactness"].get<std::string>(),
            "preview_state" );
    BOOST_CHECK_EQUAL(
            facts["validation_summary"]["connectivity"]["status"]
                    .get<std::string>(),
            "current" );
    BOOST_CHECK_EQUAL(
            facts["validation_summary"]["refill"]["status"].get<std::string>(),
            "not_required" );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPublishWhenDecisionOpportunityMismatchesWorkState )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"routing\","
                   "\"reason_code\":\"wrong_context\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"placement.generate_via_pattern_candidates\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksPlacementPublishWhenDecisionNetMismatchesCandidate )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"target_net\":\"VCC\","
                   "\"reason_code\":\"wrong_net\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_Candidate.m_ArgumentsJson.Contains(
            wxS( "\"net\":\"GND\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksRoutingPublishWhenDecisionLayerMismatchesCandidate )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"routing\","
                   "\"target_net\":\"GND\","
                   "\"target_layer\":\"B.Cu\","
                   "\"reason_code\":\"wrong_layer\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_Candidate.m_ArgumentsJson.Contains(
            wxS( "\"layer\":\"F.Cu\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksSurfacePublishWhenDecisionTargetScopeMismatchesCandidate )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"structured_surface\","
                   "\"target_scope\":{\"kind\":\"column\","
                   "\"panel_id\":\"board_setup.clearance\","
                   "\"table_id\":\"clearance.rules\","
                   "\"column\":\"clearance\"},"
                   "\"reason_code\":\"wrong_surface_column\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makePanelFillTriggerWithTargetScope() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_ProvenanceJson.Contains(
            wxS( "\"selected_tool\":\"surface.generate_fill_candidates\"" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_Candidate.m_ArgumentsJson.Contains(
            wxS( "\"column_id\":\"class\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeBlocksSurfacePatchPublishWhenRenderedTargetScopeMismatchesDecision )
{
    auto* provider =
            new SURFACE_PATCH_WRONG_COLUMN_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makePanelFillTriggerWithTargetScope() ).has_value() );
    BOOST_REQUIRE_GE( provider->m_CallCount, 4 );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "surface_patch_target_scope_failed" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"surface_patch_previews\"" ) ) );
    BOOST_CHECK( runtime.Steps().front().m_ReviewDecisionJson.Contains(
            wxS( "\"column_id\":\"clearance\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeWaitDecisionDoesNotRunAttemptOrReview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"user_busy\"}" ) } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_AttemptIds.empty() );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimeExpiresPublishedPreviewWhenContextChanges )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current = suggestion->m_ContextVersion;
    current.m_DocumentRevision = 99;

    BOOST_CHECK_EQUAL( runtime.ExpireStale( current ), 1 );

    std::optional<AI_SUGGESTION_RECORD> stored = runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
}


BOOST_AUTO_TEST_CASE( RuntimeExpiresPublishedPreviewWhenDependencyFingerprintChanges )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViewportBoundViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_SUGGESTION_TRIGGER driftedTrigger = makeViewportDriftedViaTrigger();
    AI_NEXT_ACTION_CONTEXT_VERSION drifted =
            AiNextActionContextVersionFromSnapshot( driftedTrigger.m_ContextSnapshot,
                                                    trigger.m_Activity.m_Sequence );

    BOOST_CHECK_EQUAL( runtime.ExpireStale( drifted ), 1 );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( !runtime.CanAccept( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotPublishWhenPublishTimeContextDrifted )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    AI_NEXT_ACTION_CONTEXT_VERSION drifted = makeDependencyContext( trigger );
    drifted.m_ActivitySequence += 1;
    runtime.SetCurrentContextSampler(
            [drifted]()
            {
                return drifted;
            } );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( runtime.Suggestions().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesWhenPublishTimeContextStillMatches )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    AI_NEXT_ACTION_CONTEXT_VERSION current = makeDependencyContext( trigger );
    runtime.SetCurrentContextSampler(
            [current]()
            {
                return current;
            } );

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Published );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAcceptWhenCurrentContextDrifted )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );
    AI_CONTEXT_VERSION     drifted = suggestion->m_ContextVersion;
    drifted.m_DocumentRevision += 1;

    BOOST_CHECK( !runtime.Accept( suggestion->m_Id, edit, drifted ) );
    BOOST_CHECK( editAdapter.m_AppliedObjects.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_gate_result\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"context_drift\"" ) ) );

    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> traces =
            runtime.ReplayTraceRecords();

    BOOST_REQUIRE_EQUAL( traces.size(), 1 );

    nlohmann::json trace =
            nlohmann::json::parse( traces.front().m_ReplayJson.ToStdString() );

    BOOST_REQUIRE( trace["publish_decision"].contains( "accept_gate_result" ) );
    BOOST_CHECK(
            !trace["publish_decision"]["accept_gate_result"]["allowed"].get<bool>() );
    BOOST_REQUIRE(
            trace["publish_decision"]["accept_gate_result"]["reasons"].is_array() );
    BOOST_CHECK_EQUAL(
            trace["publish_decision"]["accept_gate_result"]["reasons"].at( 0 )
                    .get<std::string>(),
            "context_drift" );

    AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT evaluation =
            AiEvaluateNextActionReplayTraceJson( traces.front().m_ReplayJson );

    BOOST_REQUIRE( evaluation.m_Valid );
    BOOST_CHECK_EQUAL( evaluation.m_AcceptGateResultCount, 1 );
    BOOST_CHECK( evaluation.m_AcceptGateReasonCountsJson.Contains(
            wxS( "\"context_drift\":1" ) ) );

    wxArrayString batchInput;
    batchInput.Add( traces.front().m_ReplayJson );

    AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT batch =
            AiEvaluateNextActionReplayTraceBatch( batchInput );

    BOOST_REQUIRE( batch.m_Valid );
    BOOST_CHECK_EQUAL( batch.m_AcceptGateResultCount, 1 );
    BOOST_CHECK( batch.m_AcceptGateReasonCountsJson.Contains(
            wxS( "\"context_drift\":1" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAcceptWhenDependencyFingerprintDrifted )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );
    BOOST_REQUIRE( suggestion.has_value() );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );
    AI_NEXT_ACTION_CONTEXT_VERSION drifted = makeDependencyContext( trigger );
    drifted.m_ActivitySequence += 1;

    BOOST_CHECK( !runtime.Accept( suggestion->m_Id, edit, drifted ) );
    BOOST_CHECK( editAdapter.m_AppliedObjects.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_gate_result\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"context_drift\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAcceptWhenViewportFingerprintDrifted )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };
    AI_SUGGESTION_TRIGGER trigger = makeViewportBoundViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );
    BOOST_REQUIRE( suggestion.has_value() );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );
    AI_SUGGESTION_TRIGGER driftedTrigger = makeViewportDriftedViaTrigger();
    AI_NEXT_ACTION_CONTEXT_VERSION drifted =
            AiNextActionContextVersionFromSnapshot( driftedTrigger.m_ContextSnapshot,
                                                    trigger.m_Activity.m_Sequence );

    BOOST_CHECK( !runtime.Accept( suggestion->m_Id, edit, drifted ) );
    BOOST_CHECK( editAdapter.m_AppliedObjects.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_gate_result\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"context_drift\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAcceptWhenAttemptValidationIsNotAcceptGrade )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );
    ACCEPT_BLOCKING_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE           previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );
    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );

    BOOST_CHECK( !runtime.Accept( suggestion->m_Id, edit,
                                  makeDependencyContext( trigger ) ) );
    BOOST_CHECK( editAdapter.m_AppliedObjects.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_gate_result\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_validation_failed\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsAcceptWhenAttemptValidationIsNotExactPreviewState )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );
    INEXACT_ACCEPT_SESSION_VALIDATION_SERVICE validationService;
    PASSING_SESSION_PREVIEW_SERVICE           previewService;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &validationService,
                                    &previewService };
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();

    std::optional<AI_SUGGESTION_RECORD> suggestion = runtime.Update( trigger );
    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );

    RECORDING_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION        edit( editAdapter );

    BOOST_CHECK( !runtime.Accept( suggestion->m_Id, edit,
                                  makeDependencyContext( trigger ) ) );
    BOOST_CHECK( editAdapter.m_AppliedObjects.empty() );

    std::optional<AI_SUGGESTION_RECORD> stored =
            runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_gate_result\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"allowed\":false" ) ) );
    BOOST_CHECK( stored->m_RuntimeProvenanceJson.Contains(
            wxS( "\"accept_validation_failed\"" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeSupersedesPreviousRuntimePreviewLeaseOnNewPublish )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview(),
              wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( first.has_value() );
    BOOST_CHECK( runtime.CanAccept( first->m_Id ) );

    std::optional<AI_SUGGESTION_RECORD> second =
            runtime.Update( makeChangedViaTrigger() );
    BOOST_REQUIRE( second.has_value() );

    std::optional<AI_SUGGESTION_RECORD> storedFirst =
            runtime.FindSuggestion( first->m_Id );
    std::optional<AI_SUGGESTION_RECORD> storedSecond =
            runtime.FindSuggestion( second->m_Id );
    BOOST_REQUIRE( storedFirst.has_value() );
    BOOST_REQUIRE( storedSecond.has_value() );

    BOOST_CHECK( storedFirst->m_Status == AI_SUGGESTION_STATUS::Superseded );
    BOOST_CHECK( !runtime.CanAccept( first->m_Id ) );
    BOOST_CHECK( !runtime.CanPreview( first->m_Id ) );
    BOOST_CHECK( storedFirst->m_RuntimeProvenanceJson.Contains(
            wxS( "\"active\":false" ) ) );

    BOOST_CHECK( storedSecond->m_Status == AI_SUGGESTION_STATUS::Pending );
    BOOST_CHECK( runtime.CanAccept( second->m_Id ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotRepublishRejectedRuntimeFingerprint )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              publishReview() } );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> first =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( first.has_value() );

    AI_SUGGESTION_RECORD duplicatePublish = *first;
    BOOST_REQUIRE( runtime.Reject( first->m_Id ) );

    std::optional<AI_SUGGESTION_RECORD> duplicate =
            runtime.AddPublishedSuggestion( duplicatePublish );

    BOOST_CHECK( !duplicate.has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Suggestions().size(), 1 );
    BOOST_CHECK( runtime.Suggestions().front().m_Status
                 == AI_SUGGESTION_STATUS::Rejected );
    BOOST_CHECK( !runtime.LatestActiveSuggestionId().has_value() );
}


BOOST_AUTO_TEST_SUITE_END()
