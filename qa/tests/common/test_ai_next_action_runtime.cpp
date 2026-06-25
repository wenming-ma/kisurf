#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <json_common.h>

#include <chrono>
#include <deque>
#include <memory>
#include <set>
#include <thread>

namespace
{
wxString publishReview();


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
        wxUnusedVar( aObject );
        m_ObjectPreviewIds.push_back( aPreviewId );
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
    std::vector<AI_PREVIEW_ITEM_OVERLAY> m_Overlays;
};


class TOOL_CALLING_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
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
                call.m_ArgumentsJson = wxS( "{\"level\":\"drc_lite\"}" );
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
};


class CANDIDATE_TOOL_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    CANDIDATE_TOOL_NEXT_ACTION_PROVIDER( wxString aToolName,
                                         wxString aOpportunityType,
                                         int aSelectedCandidateIndex = -1,
                                         wxString aToolArgumentsJson = wxS( "{}" ) ) :
            m_ToolName( std::move( aToolName ) ),
            m_OpportunityType( std::move( aOpportunityType ) ),
            m_SelectedCandidateIndex( aSelectedCandidateIndex ),
            m_ToolArgumentsJson( std::move( aToolArgumentsJson ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "candidate tool next action" );

        if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
        {
            if( aRequest.m_ToolResults.empty() )
            {
                response.m_Body = wxS( "Need generated candidates." );

                AI_TOOL_CALL_RECORD call;
                call.m_RequestId = aRequest.m_RequestId;
                call.m_ToolCallId = wxS( "call_candidates" );
                call.m_ToolName = m_ToolName;
                call.m_ArgumentsJson = m_ToolArgumentsJson;
                response.m_ToolCalls.push_back( call );
                return response;
            }

            if( m_SelectedCandidateIndex >= 0 )
            {
                response.m_Body = wxString::Format(
                        wxS( "{\"decision_kind\":\"attempt\","
                             "\"opportunity_type\":\"%s\","
                             "\"selected_candidate_index\":%d,"
                             "\"reason_code\":\"candidate_tool_result_supported\"}" ),
                        m_OpportunityType, m_SelectedCandidateIndex );
            }
            else
            {
                response.m_Body = wxString::Format(
                        wxS( "{\"decision_kind\":\"attempt\","
                             "\"opportunity_type\":\"%s\","
                             "\"reason_code\":\"candidate_tool_result_supported\"}" ),
                        m_OpportunityType );
            }
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

    wxString                         m_ToolName;
    wxString                         m_OpportunityType;
    int                              m_SelectedCandidateIndex = -1;
    wxString                         m_ToolArgumentsJson;
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
                call.m_ArgumentsJson =
                        wxS( "{\"surface_id\":\"board_setup.clearance\","
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
            const wxString& ) override
    {
        ++m_RenderCount;

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


class TRACKING_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession, const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;
        m_LiveItemCounts.push_back( aSession.ShadowBoard().LiveItemCount() );
        m_BoardIds.push_back( aSession.BoardId().ToStdString() );
        m_CheckpointCounts.push_back( aSession.Checkpoints().size() );

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
                 "\"bbox\":{\"x\":150,\"y\":40,\"width\":100,\"height\":80},"
                 "\"courtyard_bbox\":{\"x\":140,\"y\":30,\"width\":120,\"height\":100},"
                 "\"layer\":\"F.Cu\"}" ) );
}


AI_OBJECT_REF padRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_PAD_T, wxS( "pad:U4.1" ),
            wxS( "{\"kind\":\"pad\",\"footprint\":\"U4\",\"pad_name\":\"1\","
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


nlohmann::json providerRequestJson( const AI_PROVIDER_REQUEST& aRequest )
{
    return nlohmann::json::parse( aRequest.m_UserText.ToStdString() );
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


BOOST_AUTO_TEST_CASE( ToolCatalogDeclaresLayeredCandidateToolsAndNoDirectPublish )
{
    AI_NEXT_ACTION_TOOL_REGISTRY tools;
    const wxString catalog = tools.ToolCatalogJson();

    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"placement.generate_via_pattern_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"placement.generate_footprint_transform_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_footprint_transform_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"placement.generate_footprint_orientation_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_footprint_orientation_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"routing.generate_segment_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"routing.generate_parallel_segment_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_parallel_routing_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"routing.generate_bus_segment_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_bus_routing_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"routing.generate_replace_path_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_replace_path_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"routing.generate_constraint_aware_reroute_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"candidate_source\":\"internal_constraint_aware_reroute_library\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"name\":\"surface.generate_fill_candidates\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"integrated\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"side_effect\":\"read_only\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"atomic\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"side_effect\":\"shadow_mutation\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"script.run_bounded_plan\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"script\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"bounded_batch_composition\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"repair.apply_bounded_plan\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"role\":\"bounded_repair\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"name\":\"placement.repair_via\"" ) ) );
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
    BOOST_CHECK( catalog.Contains( wxS( "\"layer\":\"runtime_gate\"" ) ) );
    BOOST_CHECK( catalog.Contains( wxS( "\"side_effect\":\"publish_gated\"" ) ) );
    BOOST_CHECK( catalog.Contains(
            wxS( "\"requires_review_decision\":\"publish\"" ) ) );
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
    bool sawRepairTool = false;
    bool sawPlacementFootprintTransformTool = false;
    bool sawPlacementFootprintTransformRequiredPoints = false;
    bool sawPlacementFootprintOrientationTool = false;
    bool sawPlacementFootprintOrientationRequiredFacts = false;
    bool sawPlacementRepairTool = false;
    bool sawPlacementMoveRepairTool = false;
    bool sawPlacementOrientationRepairTool = false;
    bool sawRoutingRepairTool = false;
    bool sawRoutingPolylineRepairTool = false;
    bool sawRoutingBusRepairTool = false;
    bool sawRoutingParallelCandidateTool = false;
    bool sawRoutingParallelRequiredReference = false;
    bool sawRoutingBusCandidateTool = false;
    bool sawRoutingBusRequiredReference = false;
    bool sawRoutingReplacePathCandidateTool = false;
    bool sawRoutingReplacePathRequiredPlan = false;
    bool sawRoutingConstraintRerouteCandidateTool = false;
    bool sawRoutingConstraintRerouteRequiredFacts = false;
    bool sawSurfaceRepairTool = false;
    bool sawScriptSurfacePatchKind = false;
    bool sawRepairSurfacePatchKind = false;
    bool sawPlacementRepairRequiredPosition = false;
    bool sawPlacementMoveRepairRequiredHandles = false;
    bool sawPlacementOrientationRepairRequiredFacts = false;
    bool sawRoutingRepairRequiredSegment = false;
    bool sawRoutingPolylineRepairRequiredPoints = false;
    bool sawRoutingBusRepairRequiredSegments = false;
    bool sawSurfaceRepairRequiredPatch = false;
    bool sawSurfaceRepairExpectedMetadata = false;

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
        }

        if( functionName == "script_run_bounded_plan" )
            sawScriptTool = true;

        if( functionName == "repair_apply_bounded_plan" )
            sawRepairTool = true;

        if( functionName == "placement_generate_footprint_transform_candidates" )
        {
            sawPlacementFootprintTransformTool = true;
            const nlohmann::json& required =
                    function["parameters"].contains( "required" )
                            ? function["parameters"]["required"]
                            : nlohmann::json::array();
            bool hasCurrentPosition = false;
            bool hasTargetPosition = false;

            if( required.is_array() )
            {
                for( const nlohmann::json& value : required )
                {
                    if( value.is_string()
                        && value.get<std::string>() == "current_position" )
                    {
                        hasCurrentPosition = true;
                    }

                    if( value.is_string()
                        && value.get<std::string>() == "target_position" )
                    {
                        hasTargetPosition = true;
                    }
                }
            }

            sawPlacementFootprintTransformRequiredPoints =
                    hasCurrentPosition && hasTargetPosition;
        }

        if( functionName == "placement_generate_footprint_orientation_candidates" )
        {
            sawPlacementFootprintOrientationTool = true;
            const nlohmann::json& parameters = function["parameters"];
            BOOST_REQUIRE( parameters.contains( "required" ) );
            bool hasHandles = false;
            bool hasCurrentOrientation = false;
            bool hasTargetOrientation = false;

            for( const nlohmann::json& value : parameters["required"] )
            {
                if( !value.is_string() )
                    continue;

                if( value.get<std::string>() == "handles" )
                    hasHandles = true;

                if( value.get<std::string>() == "current_orientation_degrees" )
                    hasCurrentOrientation = true;

                if( value.get<std::string>() == "target_orientation_degrees" )
                    hasTargetOrientation = true;
            }

            sawPlacementFootprintOrientationRequiredFacts =
                    hasHandles && hasCurrentOrientation && hasTargetOrientation;
        }

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
        }

        if( functionName == "routing_generate_parallel_segment_candidates" )
        {
            sawRoutingParallelCandidateTool = true;
            const nlohmann::json& parameters = function["parameters"];
            BOOST_REQUIRE( parameters.contains( "required" ) );
            bool hasReferenceStart = false;
            bool hasReferenceEnd = false;
            bool hasOffset = false;

            for( const nlohmann::json& value : parameters["required"] )
            {
                if( !value.is_string() )
                    continue;

                if( value.get<std::string>() == "reference_start" )
                    hasReferenceStart = true;

                if( value.get<std::string>() == "reference_end" )
                    hasReferenceEnd = true;

                if( value.get<std::string>() == "offset" )
                    hasOffset = true;
            }

            sawRoutingParallelRequiredReference =
                    hasReferenceStart && hasReferenceEnd && hasOffset;
        }

        if( functionName == "routing_generate_bus_segment_candidates" )
        {
            sawRoutingBusCandidateTool = true;
            const nlohmann::json& parameters = function["parameters"];
            BOOST_REQUIRE( parameters.contains( "required" ) );
            bool hasReferenceStart = false;
            bool hasReferenceEnd = false;
            bool hasLaneOffsets = false;

            for( const nlohmann::json& value : parameters["required"] )
            {
                if( !value.is_string() )
                    continue;

                if( value.get<std::string>() == "reference_start" )
                    hasReferenceStart = true;

                if( value.get<std::string>() == "reference_end" )
                    hasReferenceEnd = true;

                if( value.get<std::string>() == "lane_offsets" )
                    hasLaneOffsets = true;
            }

            sawRoutingBusRequiredReference =
                    hasReferenceStart && hasReferenceEnd && hasLaneOffsets;
        }

        if( functionName == "routing_generate_replace_path_candidates" )
        {
            sawRoutingReplacePathCandidateTool = true;
            const nlohmann::json& parameters = function["parameters"];
            BOOST_REQUIRE( parameters.contains( "required" ) );
            bool hasReplaceHandles = false;
            bool hasReplacementPoints = false;
            bool hasNet = false;
            bool hasLayer = false;
            bool hasWidth = false;

            for( const nlohmann::json& value : parameters["required"] )
            {
                if( !value.is_string() )
                    continue;

                if( value.get<std::string>() == "replace_handles" )
                    hasReplaceHandles = true;

                if( value.get<std::string>() == "replacement_points" )
                    hasReplacementPoints = true;

                if( value.get<std::string>() == "net" )
                    hasNet = true;

                if( value.get<std::string>() == "layer" )
                    hasLayer = true;

                if( value.get<std::string>() == "width" )
                    hasWidth = true;
            }

            sawRoutingReplacePathRequiredPlan =
                    hasReplaceHandles && hasReplacementPoints && hasNet && hasLayer
                    && hasWidth;
        }

        if( functionName == "routing_generate_constraint_aware_reroute_candidates" )
        {
            sawRoutingConstraintRerouteCandidateTool = true;
            const nlohmann::json& parameters = function["parameters"];
            BOOST_REQUIRE( parameters.contains( "required" ) );
            bool hasReplaceHandles = false;
            bool hasReplacementPoints = false;
            bool hasConstraints = false;
            bool hasNet = false;
            bool hasLayer = false;
            bool hasWidth = false;

            for( const nlohmann::json& value : parameters["required"] )
            {
                if( !value.is_string() )
                    continue;

                if( value.get<std::string>() == "replace_handles" )
                    hasReplaceHandles = true;

                if( value.get<std::string>() == "replacement_points" )
                    hasReplacementPoints = true;

                if( value.get<std::string>() == "constraints" )
                    hasConstraints = true;

                if( value.get<std::string>() == "net" )
                    hasNet = true;

                if( value.get<std::string>() == "layer" )
                    hasLayer = true;

                if( value.get<std::string>() == "width" )
                    hasWidth = true;
            }

            sawRoutingConstraintRerouteRequiredFacts =
                    hasReplaceHandles && hasReplacementPoints && hasConstraints
                    && hasNet && hasLayer && hasWidth;
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
        }
    }

    BOOST_CHECK( sawScriptTool );
    BOOST_CHECK( sawRepairTool );
    BOOST_CHECK( sawPlacementFootprintTransformTool );
    BOOST_CHECK( sawPlacementFootprintTransformRequiredPoints );
    BOOST_CHECK( sawPlacementFootprintOrientationTool );
    BOOST_CHECK( sawPlacementFootprintOrientationRequiredFacts );
    BOOST_CHECK( sawPlacementRepairTool );
    BOOST_CHECK( sawPlacementMoveRepairTool );
    BOOST_CHECK( sawPlacementOrientationRepairTool );
    BOOST_CHECK( sawRoutingRepairTool );
    BOOST_CHECK( sawRoutingPolylineRepairTool );
    BOOST_CHECK( sawRoutingBusRepairTool );
    BOOST_CHECK( sawRoutingParallelCandidateTool );
    BOOST_CHECK( sawRoutingParallelRequiredReference );
    BOOST_CHECK( sawRoutingBusCandidateTool );
    BOOST_CHECK( sawRoutingBusRequiredReference );
    BOOST_CHECK( sawRoutingReplacePathCandidateTool );
    BOOST_CHECK( sawRoutingReplacePathRequiredPlan );
    BOOST_CHECK( sawRoutingConstraintRerouteCandidateTool );
    BOOST_CHECK( sawRoutingConstraintRerouteRequiredFacts );
    BOOST_CHECK( sawSurfaceRepairTool );
    BOOST_CHECK( sawScriptSurfacePatchKind );
    BOOST_CHECK( sawRepairSurfacePatchKind );
    BOOST_CHECK( sawPlacementRepairRequiredPosition );
    BOOST_CHECK( sawPlacementMoveRepairRequiredHandles );
    BOOST_CHECK( sawPlacementOrientationRepairRequiredFacts );
    BOOST_CHECK( sawRoutingRepairRequiredSegment );
    BOOST_CHECK( sawRoutingPolylineRepairRequiredPoints );
    BOOST_CHECK( sawRoutingBusRepairRequiredSegments );
    BOOST_CHECK( sawSurfaceRepairRequiredPatch );
    BOOST_CHECK( sawSurfaceRepairExpectedMetadata );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "publish_preview" ) ) );
    BOOST_CHECK( !tools.CallableToolCatalogJson().Contains(
            wxS( "publish.preview" ) ) );
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
                 "\"evaluation_source\":\"DRC_ENGINE::EvalRules\"}]}}}" );
    placementTrigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    placementTrigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition =
            VECTOR2I( 180, 80 );
    placementTrigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"cursor_region\":{\"x\":140,\"y\":80,"
                 "\"width\":100,\"height\":100},"
                 "\"viewport\":{\"center\":{\"x\":180,\"y\":80},"
                 "\"zoom\":3.0,\"width\":600,\"height\":400}}" );
    placementTrigger.m_ContextSnapshot.m_Anchors.push_back(
            contextAnchor( wxS( "place_candidate_1" ),
                           AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                           wxS( "Place candidate 1" ), 220, 90 ) );
    placementTrigger.m_ContextSnapshot.m_VisibleObjects.push_back( footprintRef() );
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
            wxS( "\"visible_object_count\":5" ) ) );

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
    BOOST_REQUIRE( placementPacket.contains( "visible_object_summaries" ) );
    BOOST_REQUIRE_EQUAL( placementPacket["visible_object_summaries"].size(), 5 );
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
            && fact["bbox"]["x"].get<int>() == 150 )
        {
            sawFootprintObstacle = true;
        }
    }

    BOOST_CHECK( sawViaObstacle );
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
            placementPacket["placement_footprint_geometry_facts"].at( 0 )
                    ["courtyard_bbox"]["width"].get<int>(),
            120 );
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
            wxS( "{\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000,"
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
                 "{\"id\":\"U2.1\",\"net_code\":1,\"net_name\":\"GND\"}],"
                 "\"component_graph_edges\":[{\"from\":\"U1.1\","
                 "\"to\":\"U2.1\",\"net_code\":1,\"net_name\":\"GND\","
                 "\"kind\":\"ratsnest\"}],"
                 "\"unconnected_edges\":[{\"net_code\":1,"
                 "\"net_name\":\"GND\",\"visible\":true}],"
                 "\"unconnected_edge_sample_truncated\":false},"
                 "\"net_facts\":[{\"code\":1,\"name\":\"GND\","
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
            2 );
    BOOST_REQUIRE_EQUAL(
            routingPacket["connectivity_summary"]["component_graph_edges"].size(),
            1 );
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
            routingPacket["routing_corridor_facts"].at( 0 )["segment_style"]
                    .get<std::string>(),
            "horizontal" );
    BOOST_CHECK_EQUAL(
            routingPacket["routing_corridor_facts"].at( 0 )["manhattan_length"]
                    .get<int>(),
            220 );
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

    for( const nlohmann::json& fact : routingPacket["local_obstacle_facts"] )
        routingLocalObstacleLabels.insert( fact["label"].get<std::string>() );

    BOOST_CHECK( routingLocalObstacleLabels.find( "track:180,210->300,210" )
                 != routingLocalObstacleLabels.end() );
    BOOST_CHECK( routingLocalObstacleLabels.find( "pad:U4.1" )
                 != routingLocalObstacleLabels.end() );
    BOOST_CHECK( routingLocalObstacleLabels.find( "track:1000,1000->1200,1000" )
                 == routingLocalObstacleLabels.end() );

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
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_DisableDefaultTools );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "placement.generate_via_pattern_candidates" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "context_snapshot" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "visible_objects" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "via:100,50" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "dependency_fingerprint" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 0 ).m_ToolCatalogJson.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( !provider->m_Requests.at( 1 ).m_ToolCatalogJson.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "validate.hidden_attempt" ) ) );
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
            wxS( "\"visible_objects\"" ) ) );

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


BOOST_AUTO_TEST_CASE( RuntimeExecutesIntegratedCandidateToolCalls )
{
    auto* provider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "placement_generate_via_pattern_candidates" ),
            wxS( "placement" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 1 ).m_ToolResults.front();
    BOOST_CHECK_EQUAL( result.m_ToolCallId, wxString( wxS( "call_candidates" ) ) );
    BOOST_CHECK_EQUAL( result.m_ToolName,
                       wxString( wxS( "placement_generate_via_pattern_candidates" ) ) );
    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains(
            wxS( "\"tool\":\"placement.generate_via_pattern_candidates\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"status\":\"candidates_generated\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"candidate_count\":1" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"candidates\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"source_tool\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"arguments\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"operation\"" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );

    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "placement.generate_via_pattern_candidates" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "placement.generate_via_pattern_candidates" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeCandidateToolResultsExposeLandingFacts )
{
    auto* placementProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "placement_generate_via_pattern_candidates" ),
            wxS( "placement" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES placementServices;
    AI_NEXT_ACTION_RUNTIME placementRuntime{
            std::unique_ptr<AI_PROVIDER>( placementProvider ),
            &placementServices.m_Validation,
            &placementServices.m_Preview };

    BOOST_REQUIRE( placementRuntime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( placementProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( placementProvider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json placementResult = nlohmann::json::parse(
            placementProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_REQUIRE_EQUAL( placementResult["candidates"].size(), 1 );
    const nlohmann::json& placementLanding =
            placementResult["candidates"].at( 0 )["landing_facts"];
    BOOST_CHECK_EQUAL( placementLanding["kind"].get<std::string>(),
                       "placement_landing" );
    BOOST_CHECK_EQUAL( placementLanding["position"]["x"].get<int>(), 400 );
    BOOST_CHECK_EQUAL( placementLanding["net"].get<std::string>(), "GND" );
    BOOST_CHECK( !placementLanding["source"].get<std::string>().empty() );

    auto* footprintTransformProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "placement_generate_footprint_transform_candidates" ),
            wxS( "placement" ),
            -1,
            wxS( "{\"footprint_ref\":\"U1\","
                 "\"current_position\":{\"x\":100,\"y\":200},"
                 "\"target_position\":{\"x\":160,\"y\":230}}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES footprintTransformServices;
    AI_NEXT_ACTION_RUNTIME footprintTransformRuntime{
            std::unique_ptr<AI_PROVIDER>( footprintTransformProvider ),
            &footprintTransformServices.m_Validation,
            &footprintTransformServices.m_Preview };

    BOOST_REQUIRE( footprintTransformRuntime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( footprintTransformProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL(
            footprintTransformProvider->m_Requests.at( 1 ).m_ToolResults.size(),
            1 );

    nlohmann::json footprintTransformResult = nlohmann::json::parse(
            footprintTransformProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( footprintTransformResult["tool"].get<std::string>(),
                       "placement.generate_footprint_transform_candidates" );
    BOOST_CHECK_EQUAL( footprintTransformResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( footprintTransformResult["candidate_count"].get<int>(), 1 );
    BOOST_CHECK( !footprintTransformResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( footprintTransformResult["candidates"].size(), 1 );

    const nlohmann::json& footprintCandidate =
            footprintTransformResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( footprintCandidate["source_tool"].get<std::string>(),
                       "placement.generate_footprint_transform_candidates" );
    BOOST_CHECK_EQUAL( footprintCandidate["arguments"]["operation"].get<std::string>(),
                       "move_selected" );
    BOOST_CHECK_EQUAL( footprintCandidate["arguments"]["dx"].get<int>(), 60 );
    BOOST_CHECK_EQUAL( footprintCandidate["arguments"]["dy"].get<int>(), 30 );
    BOOST_CHECK_EQUAL(
            footprintCandidate["landing_facts"]["source"].get<std::string>(),
            "footprint_transform.target_position" );
    BOOST_CHECK_EQUAL(
            footprintCandidate["landing_facts"]["position"]["x"].get<int>(),
            160 );
    BOOST_CHECK_EQUAL(
            footprintCandidate["footprint_transform_facts"]["footprint_ref"]
                    .get<std::string>(),
            "U1" );
    BOOST_CHECK_EQUAL(
            footprintCandidate["footprint_transform_facts"]["delta"]["y"].get<int>(),
            30 );

    auto* footprintOrientationProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "placement_generate_footprint_orientation_candidates" ),
            wxS( "placement" ),
            -1,
            wxS( "{\"handles\":[{\"handle\":\"footprint-U1\"}],"
                 "\"footprint_ref\":\"U1\","
                 "\"current_orientation_degrees\":0,"
                 "\"target_orientation_degrees\":90,"
                 "\"target_side\":\"B.Cu\"}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES footprintOrientationServices;
    AI_NEXT_ACTION_RUNTIME footprintOrientationRuntime{
            std::unique_ptr<AI_PROVIDER>( footprintOrientationProvider ),
            &footprintOrientationServices.m_Validation,
            &footprintOrientationServices.m_Preview };

    BOOST_REQUIRE( footprintOrientationRuntime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_GE( footprintOrientationProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL(
            footprintOrientationProvider->m_Requests.at( 1 ).m_ToolResults.size(),
            1 );

    nlohmann::json footprintOrientationResult = nlohmann::json::parse(
            footprintOrientationProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( footprintOrientationResult["tool"].get<std::string>(),
                       "placement.generate_footprint_orientation_candidates" );
    BOOST_CHECK_EQUAL( footprintOrientationResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( footprintOrientationResult["candidate_count"].get<int>(), 1 );
    BOOST_CHECK( !footprintOrientationResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( footprintOrientationResult["candidates"].size(), 1 );

    const nlohmann::json& orientationCandidate =
            footprintOrientationResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( orientationCandidate["source_tool"].get<std::string>(),
                       "placement.generate_footprint_orientation_candidates" );
    BOOST_CHECK_EQUAL( orientationCandidate["arguments"]["operation"].get<std::string>(),
                       "orient_selected_footprint" );
    BOOST_CHECK_EQUAL(
            orientationCandidate["footprint_orientation_facts"]["footprint_ref"]
                    .get<std::string>(),
            "U1" );
    BOOST_CHECK_EQUAL(
            orientationCandidate["footprint_orientation_facts"]
                    ["orientation_delta_degrees"].get<int>(),
            90 );
    BOOST_CHECK_EQUAL(
            orientationCandidate["footprint_orientation_facts"]["target_side"]
                    .get<std::string>(),
            "B.Cu" );
    BOOST_CHECK_EQUAL(
            orientationCandidate["landing_facts"]["source"].get<std::string>(),
            "footprint_orientation.target_orientation" );
    BOOST_CHECK_EQUAL(
            orientationCandidate["plan"]["operations"].at( 0 )["kind"]
                    .get<std::string>(),
            "pcb.set_item_properties" );
    BOOST_CHECK_EQUAL(
            orientationCandidate["plan"]["operations"].at( 0 )["arguments"]
                    ["typed_props"]["orientation_degrees"].get<int>(),
            90 );
    BOOST_CHECK_EQUAL(
            orientationCandidate["plan"]["operations"].at( 0 )["arguments"]
                    ["typed_props"]["side"].get<std::string>(),
            "B.Cu" );

    auto* routingProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_segment_candidates" ),
            wxS( "routing" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES routingServices;
    AI_NEXT_ACTION_RUNTIME routingRuntime{
            std::unique_ptr<AI_PROVIDER>( routingProvider ),
            &routingServices.m_Validation,
            &routingServices.m_Preview };

    BOOST_REQUIRE( routingRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_GE( routingProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( routingProvider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json routingResult = nlohmann::json::parse(
            routingProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_REQUIRE_EQUAL( routingResult["candidates"].size(), 1 );
    const nlohmann::json& routingLanding =
            routingResult["candidates"].at( 0 )["landing_facts"];
    BOOST_CHECK_EQUAL( routingLanding["kind"].get<std::string>(),
                       "routing_landing" );
    BOOST_CHECK_EQUAL( routingLanding["point"]["x"].get<int>(), 260 );
    BOOST_CHECK_EQUAL( routingLanding["net"].get<std::string>(), "GND" );
    BOOST_CHECK_EQUAL( routingLanding["layer"].get<std::string>(), "F.Cu" );

    auto* replacePathProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_replace_path_candidates" ),
            wxS( "routing" ),
            -1,
            wxS( "{\"replace_handles\":[{\"handle\":\"track-old-1\"}],"
                 "\"replacement_points\":[{\"x\":10,\"y\":10},"
                 "{\"x\":60,\"y\":40},{\"x\":110,\"y\":40}],"
                 "\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES replacePathServices;
    AI_NEXT_ACTION_RUNTIME replacePathRuntime{
            std::unique_ptr<AI_PROVIDER>( replacePathProvider ),
            &replacePathServices.m_Validation,
            &replacePathServices.m_Preview };

    BOOST_REQUIRE( replacePathRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_GE( replacePathProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL(
            replacePathProvider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json replacePathResult = nlohmann::json::parse(
            replacePathProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( replacePathResult["tool"].get<std::string>(),
                       "routing.generate_replace_path_candidates" );
    BOOST_CHECK_EQUAL( replacePathResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( replacePathResult["candidate_count"].get<int>(), 1 );
    BOOST_CHECK( !replacePathResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( replacePathResult["candidates"].size(), 1 );

    const nlohmann::json& replacePathCandidate =
            replacePathResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( replacePathCandidate["source_tool"].get<std::string>(),
                       "routing.generate_replace_path_candidates" );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["replace_path_facts"]["point_count"].get<int>(),
            3 );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["landing_facts"]["source"].get<std::string>(),
            "replace_path.replacement_points.end" );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["landing_facts"]["point"]["x"].get<int>(),
            110 );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["plan"]["operations"].at( 0 )["kind"]
                    .get<std::string>(),
            "pcb.delete_items" );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["plan"]["operations"].at( 1 )["kind"]
                    .get<std::string>(),
            "pcb.create_track_polyline" );
    BOOST_CHECK_EQUAL(
            replacePathCandidate["plan"]["operations"].at( 1 )["points"].size(),
            3 );

    auto* constraintRerouteProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_constraint_aware_reroute_candidates" ),
            wxS( "routing" ),
            -1,
            wxS( "{\"replace_handles\":[{\"handle\":\"track-old-1\"}],"
                 "\"replacement_points\":[{\"x\":10,\"y\":10},"
                 "{\"x\":70,\"y\":50},{\"x\":120,\"y\":50}],"
                 "\"constraints\":{\"min_clearance\":200000,"
                 "\"avoid_keepouts\":true,\"source\":\"drc_lite\"},"
                 "\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES constraintRerouteServices;
    AI_NEXT_ACTION_RUNTIME constraintRerouteRuntime{
            std::unique_ptr<AI_PROVIDER>( constraintRerouteProvider ),
            &constraintRerouteServices.m_Validation,
            &constraintRerouteServices.m_Preview };

    BOOST_REQUIRE( constraintRerouteRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_GE( constraintRerouteProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL(
            constraintRerouteProvider->m_Requests.at( 1 ).m_ToolResults.size(),
            1 );

    nlohmann::json constraintRerouteResult = nlohmann::json::parse(
            constraintRerouteProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( constraintRerouteResult["tool"].get<std::string>(),
                       "routing.generate_constraint_aware_reroute_candidates" );
    BOOST_CHECK_EQUAL( constraintRerouteResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( constraintRerouteResult["candidate_count"].get<int>(), 1 );
    BOOST_CHECK( !constraintRerouteResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( constraintRerouteResult["candidates"].size(), 1 );

    const nlohmann::json& constraintCandidate =
            constraintRerouteResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( constraintCandidate["source_tool"].get<std::string>(),
                       "routing.generate_constraint_aware_reroute_candidates" );
    BOOST_CHECK_EQUAL(
            constraintCandidate["constraint_aware_reroute_facts"]
                    ["constraints"]["min_clearance"].get<int>(),
            200000 );
    BOOST_CHECK_EQUAL(
            constraintCandidate["constraint_aware_reroute_facts"]
                    ["validation_hint"].get<std::string>(),
            "run_validate_hidden_attempt_before_publish" );
    BOOST_CHECK_EQUAL(
            constraintCandidate["landing_facts"]["source"].get<std::string>(),
            "constraint_reroute.replacement_points.end" );
    BOOST_CHECK_EQUAL(
            constraintCandidate["plan"]["operations"].at( 0 )["kind"]
                    .get<std::string>(),
            "pcb.delete_items" );
    BOOST_CHECK_EQUAL(
            constraintCandidate["plan"]["operations"].at( 1 )["kind"]
                    .get<std::string>(),
            "pcb.create_track_polyline" );

    auto* parallelProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_parallel_segment_candidates" ),
            wxS( "routing" ),
            -1,
            wxS( "{\"reference_start\":{\"x\":10,\"y\":10},"
                 "\"reference_end\":{\"x\":110,\"y\":10},"
                 "\"offset\":{\"x\":0,\"y\":40},"
                 "\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":150000}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES parallelServices;
    AI_NEXT_ACTION_RUNTIME parallelRuntime{
            std::unique_ptr<AI_PROVIDER>( parallelProvider ),
            &parallelServices.m_Validation,
            &parallelServices.m_Preview };

    BOOST_REQUIRE( parallelRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_GE( parallelProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( parallelProvider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json parallelResult = nlohmann::json::parse(
            parallelProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( parallelResult["tool"].get<std::string>(),
                       "routing.generate_parallel_segment_candidates" );
    BOOST_CHECK_EQUAL( parallelResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( parallelResult["candidate_count"].get<int>(), 1 );
    BOOST_CHECK( !parallelResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( parallelResult["candidates"].size(), 1 );

    const nlohmann::json& parallelCandidate = parallelResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( parallelCandidate["source_tool"].get<std::string>(),
                       "routing.generate_parallel_segment_candidates" );
    BOOST_CHECK_EQUAL( parallelCandidate["arguments"]["operation"].get<std::string>(),
                       "route_segment_preview" );
    BOOST_CHECK_EQUAL( parallelCandidate["arguments"]["start"]["x"].get<int>(), 10 );
    BOOST_CHECK_EQUAL( parallelCandidate["arguments"]["start"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( parallelCandidate["arguments"]["end"]["x"].get<int>(), 110 );
    BOOST_CHECK_EQUAL( parallelCandidate["arguments"]["end"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( parallelCandidate["landing_facts"]["source"].get<std::string>(),
                       "parallel_reference.offset" );
    BOOST_CHECK_EQUAL( parallelCandidate["landing_facts"]["point"]["x"].get<int>(), 110 );
    BOOST_CHECK_EQUAL( parallelCandidate["landing_facts"]["point"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( parallelCandidate["parallel_facts"]["offset"]["y"].get<int>(), 40 );

    auto* busProvider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "routing_generate_bus_segment_candidates" ),
            wxS( "routing" ),
            -1,
            wxS( "{\"reference_start\":{\"x\":10,\"y\":10},"
                 "\"reference_end\":{\"x\":110,\"y\":10},"
                 "\"lane_offsets\":[{\"x\":0,\"y\":20},{\"x\":0,\"y\":40}],"
                 "\"nets\":[\"D0\",\"D1\"],\"layer\":\"F.Cu\",\"width\":120000}" ) );

    PUBLISH_READY_NEXT_ACTION_SERVICES busServices;
    AI_NEXT_ACTION_RUNTIME busRuntime{
            std::unique_ptr<AI_PROVIDER>( busProvider ),
            &busServices.m_Validation,
            &busServices.m_Preview };

    BOOST_REQUIRE( busRuntime.Update( makeRoutingTrigger() ).has_value() );
    BOOST_REQUIRE_GE( busProvider->m_Requests.size(), 2 );
    BOOST_REQUIRE_EQUAL( busProvider->m_Requests.at( 1 ).m_ToolResults.size(), 1 );

    nlohmann::json busResult = nlohmann::json::parse(
            busProvider->m_Requests.at( 1 ).m_ToolResults.front()
                    .m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( busResult["tool"].get<std::string>(),
                       "routing.generate_bus_segment_candidates" );
    BOOST_CHECK_EQUAL( busResult["status"].get<std::string>(),
                       "candidates_generated" );
    BOOST_CHECK_EQUAL( busResult["candidate_count"].get<int>(), 2 );
    BOOST_CHECK( !busResult["publish_allowed"].get<bool>() );
    BOOST_REQUIRE_EQUAL( busResult["candidates"].size(), 2 );

    const nlohmann::json& firstBusCandidate = busResult["candidates"].at( 0 );
    BOOST_CHECK_EQUAL( firstBusCandidate["source_tool"].get<std::string>(),
                       "routing.generate_bus_segment_candidates" );
    BOOST_CHECK_EQUAL( firstBusCandidate["arguments"]["operation"].get<std::string>(),
                       "route_segment_preview" );
    BOOST_CHECK_EQUAL( firstBusCandidate["arguments"]["net"].get<std::string>(),
                       "D0" );
    BOOST_CHECK_EQUAL( firstBusCandidate["arguments"]["start"]["y"].get<int>(), 30 );
    BOOST_CHECK_EQUAL( firstBusCandidate["arguments"]["end"]["y"].get<int>(), 30 );
    BOOST_CHECK_EQUAL( firstBusCandidate["landing_facts"]["source"].get<std::string>(),
                       "bus_reference.offset" );
    BOOST_CHECK_EQUAL( firstBusCandidate["bus_facts"]["lane_index"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( firstBusCandidate["bus_facts"]["lane_count"].get<int>(), 2 );

    const nlohmann::json& secondBusCandidate = busResult["candidates"].at( 1 );
    BOOST_CHECK_EQUAL( secondBusCandidate["arguments"]["net"].get<std::string>(),
                       "D1" );
    BOOST_CHECK_EQUAL( secondBusCandidate["arguments"]["start"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( secondBusCandidate["arguments"]["end"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( secondBusCandidate["landing_facts"]["point"]["y"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( secondBusCandidate["bus_facts"]["offset"]["y"].get<int>(), 40 );
}


BOOST_AUTO_TEST_CASE( RuntimeRejectsOutOfRangeSelectedCandidateIndex )
{
    auto* provider = new CANDIDATE_TOOL_NEXT_ACTION_PROVIDER(
            wxS( "placement_generate_via_pattern_candidates" ),
            wxS( "placement" ),
            7 );

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK( runtime.Attempts().empty() );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
    BOOST_CHECK( runtime.Steps().front().m_LlmDecisionJson.Contains(
            wxS( "\"selected_candidate_index\":7" ) ) );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 3 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
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

    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"provider_tool_results\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"call_apply_candidate\"" ) ) );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 3 );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 2 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.at( 2 ).m_ToolResults.size(), 1 );

    const AI_TOOL_CALL_RECORD& result =
            provider->m_Requests.at( 2 ).m_ToolResults.front();
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

    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"provider_tool_results\"" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains(
            wxS( "\"call_script_plan\"" ) ) );

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

    BOOST_REQUIRE( suggestion.has_value() );
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
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"publish_allowed\":false" ) ) );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "\"direct_publish\":false" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );

    const wxString& journal = runtime.Attempts().front().m_JournalJson;
    BOOST_CHECK( journal.Contains( wxS( "\"kind\":\"surface.apply_patch\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"surface_patch_fill_class\"" ) ) );
    BOOST_CHECK( journal.Contains( wxS( "\"patch_operation_count\":2" ) ) );
    BOOST_CHECK( journal.Contains(
            wxS( "\"merged_from_tool_call_id\":\"call_surface_patch\"" ) ) );
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
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

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

    BOOST_REQUIRE( suggestion.has_value() );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            wxS( "\"merged_from_tool\":\"surface.repair_patch\"" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"direct_publish\":false" ) ) );
    BOOST_CHECK( repairResult.m_ResultJson.Contains(
            wxS( "\"publish_allowed\":false" ) ) );

    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 1 );
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
            wxS( "\"merged_from_tool\":\"surface.repair_patch\"" ) ) );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            publishRequest.m_ToolResults.at( 1 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& moveResult =
            publishRequest.m_ToolResults.at( 1 );
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
            publishRequest.m_ToolResults.at( 2 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 5 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 3 );

    const AI_TOOL_CALL_RECORD& orientationResult =
            publishRequest.m_ToolResults.at( 1 );
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
            publishRequest.m_ToolResults.at( 2 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            publishRequest.m_ToolResults.at( 1 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            publishRequest.m_ToolResults.at( 1 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            publishRequest.m_ToolResults.at( 1 );
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

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& renderResult =
            publishRequest.m_ToolResults.at( 1 );
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


BOOST_AUTO_TEST_CASE( RuntimeRepairToolWritesActiveFrameAndFeedsRender )
{
    auto* provider = new REPAIR_THEN_RENDER_NEXT_ACTION_PROVIDER();

    PUBLISH_READY_NEXT_ACTION_SERVICES services;
    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ),
                                    &services.m_Validation,
                                    &services.m_Preview };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_REQUIRE_GE( provider->m_Requests.size(), 4 );

    const AI_PROVIDER_REQUEST& publishRequest = provider->m_Requests.back();
    BOOST_CHECK( publishRequest.m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );

    const AI_TOOL_CALL_RECORD& repairResult =
            publishRequest.m_ToolResults.at( 0 );
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
            publishRequest.m_ToolResults.at( 1 );
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
    BOOST_REQUIRE_EQUAL( publishRequest.m_ToolResults.size(), 2 );
    const AI_TOOL_CALL_RECORD& validationResult =
            publishRequest.m_ToolResults.at( 1 );

    BOOST_CHECK_EQUAL( validationResult.m_ToolCallId,
                       wxString( wxS( "call_validate_after_script" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"tool\":\"validate.hidden_attempt\"" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"shadow_live_item_count\":2" ) ) );
    BOOST_CHECK( validationResult.m_ResultJson.Contains(
            wxS( "\"attempt_session_journal\"" ) ) );

    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_MutationCount,
                       2 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_CreatedObjectCount,
                       2 );
    BOOST_CHECK_EQUAL( runtime.Attempts().front().m_BudgetCounters.m_TouchedObjectCount,
                       2 );
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
