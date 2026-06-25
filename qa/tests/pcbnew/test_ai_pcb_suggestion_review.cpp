#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_session_apply_adapter.h>
#include <kisurf_ai_pcb_suggestion_review.h>

#include <board.h>
#include <commit.h>
#include <json_common.h>
#include <netinfo.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>

#include <optional>
#include <deque>
#include <utility>
#include <vector>
#include <wx/string.h>

namespace
{

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

    int                  m_CallCount = 0;
    std::deque<wxString> m_Bodies;
};


class PCB_ADD_SPY_COMMIT : public COMMIT
{
public:
    struct ADDED_ITEM
    {
        BOARD_ITEM*  m_Item = nullptr;
        BASE_SCREEN* m_Screen = nullptr;
        CHANGE_TYPE  m_ChangeType = CHT_ADD;
    };

    explicit PCB_ADD_SPY_COMMIT( BOARD& aBoard ) :
            m_Board( aBoard )
    {
    }

    COMMIT& Stage( EDA_ITEM* aItem, CHANGE_TYPE aChangeType,
                   BASE_SCREEN* aScreen = nullptr,
                   RECURSE_MODE aRecurse = RECURSE_MODE::NO_RECURSE ) override
    {
        wxUnusedVar( aRecurse );

        BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( aItem );
        int         changeType = aChangeType & CHT_TYPE;

        if( boardItem && changeType == CHT_ADD )
            m_StagedAdded.push_back( { boardItem, aScreen, aChangeType } );

        return *this;
    }

    void Push( const wxString& aMessage = wxEmptyString, int aFlags = 0 ) override
    {
        wxUnusedVar( aFlags );
        ++m_PushCount;
        m_LastMessage = aMessage;

        for( const ADDED_ITEM& item : m_StagedAdded )
        {
            m_Board.Add( item.m_Item );
            m_Added.push_back( item );
        }

        m_StagedAdded.clear();
    }

    void Revert() override
    {
        ++m_RevertCount;

        for( const ADDED_ITEM& item : m_StagedAdded )
            delete item.m_Item;

        m_StagedAdded.clear();
    }

    std::vector<ADDED_ITEM> m_Added;
    int                    m_PushCount = 0;
    int                    m_RevertCount = 0;
    wxString               m_LastMessage;

private:
    EDA_ITEM* undoLevelItem( EDA_ITEM* aItem ) const override { return aItem; }
    EDA_ITEM* makeImage( EDA_ITEM* aItem ) const override { return aItem->Clone(); }

    BOARD&                  m_Board;
    std::vector<ADDED_ITEM> m_StagedAdded;
};


class JOURNAL_REPLAY_SPY_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override
    {
        wxUnusedVar( aError );

        ++m_BeginCount;
        m_BaseHash = aSession.BaseHash();
        return true;
    }

    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override
    {
        wxUnusedVar( aError );

        m_AppliedKinds.push_back( aOperation.m_Kind );
        m_AppliedArguments.push_back( aOperation.m_ArgumentsJson );
        return true;
    }

    bool CommitTransaction( wxString& aError ) override
    {
        wxUnusedVar( aError );

        ++m_CommitCount;
        return true;
    }

    int                                    m_BeginCount = 0;
    int                                    m_CommitCount = 0;
    wxString                               m_BaseHash;
    std::vector<AI_SESSION_OPERATION_KIND> m_AppliedKinds;
    std::vector<wxString>                  m_AppliedArguments;
};


class ACCEPT_GATE_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    explicit ACCEPT_GATE_VALIDATION_SERVICE( bool aAcceptSufficient,
                                             bool aPreviewStateExact = true ) :
            m_AcceptSufficient( aAcceptSufficient ),
            m_PreviewStateExact( aPreviewStateExact )
    {
    }

    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson,
            const wxString& aCurrentResultJson ) override
    {
        wxUnusedVar( aCurrentResultJson );

        ++m_RunCount;
        m_ArgumentsJson = aArgumentsJson;
        m_MutationCount = 0;

        for( const AI_SESSION_OPERATION_RECORD& operation :
                aSession.Journal().Operations() )
        {
            if( operation.IsMutation()
                && operation.m_Kind != AI_SESSION_OPERATION_KIND::RunValidation )
            {
                ++m_MutationCount;
            }
        }

        AI_SESSION_VALIDATION_RESULT result;
        nlohmann::json validation =
                { { "validation",
                    { { "status", "native_checked" },
                      { "level", "full_drc" },
                      { "preview_state_exact", m_PreviewStateExact },
                      { "accept_validation_sufficient", m_AcceptSufficient },
                      { "accept_validation_reason",
                        m_AcceptSufficient
                                ? "accept gate passed"
                                : "accept gate failed" } } } };
        result.m_ResultJson = wxString::FromUTF8( validation.dump().c_str() );
        return result;
    }

    bool     m_AcceptSufficient = true;
    bool     m_PreviewStateExact = true;
    int      m_RunCount = 0;
    size_t   m_MutationCount = 0;
    wxString m_ArgumentsJson;
};


class PUBLISH_READY_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION&,
            const wxString& ) override
    {
        ++m_RenderCount;

        AI_SESSION_PREVIEW_RESULT result;
        result.m_PreviewId = 700 + m_RenderCount;
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


class PUBLISH_READY_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION&,
            const wxString&,
            const wxString& ) override
    {
        ++m_RunCount;

        AI_SESSION_VALIDATION_RESULT result;
        result.m_ResultJson =
                wxS( "{\"validation\":{\"status\":\"native_checked\","
                     "\"level\":\"drc_lite\","
                     "\"issue_count\":0}}" );
        return result;
    }

    int m_RunCount = 0;
};


struct PUBLISH_READY_NEXT_ACTION_SERVICES
{
    void Configure( AI_AGENT_PANEL_MODEL& aModel )
    {
        aModel.ConfigureNextActionServices( &m_Preview, &m_Validation );
    }

    PUBLISH_READY_SESSION_PREVIEW_SERVICE    m_Preview;
    PUBLISH_READY_SESSION_VALIDATION_SERVICE m_Validation;
};


struct PCB_REVIEW_FIXTURE
{
    PCB_REVIEW_FIXTURE()
    {
        m_Gnd = new NETINFO_ITEM( &m_Board, wxS( "GND" ), 1 );
        m_Board.Add( m_Gnd );
    }

    BOARD         m_Board;
    NETINFO_ITEM* m_Gnd = nullptr;
};


AI_OBJECT_REF routePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_TRACE_T, wxS( "preview:route" ),
            wxS( "{\"operation\":\"route_segment_preview\",\"net\":\"GND\","
                 "\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"end\":{\"x\":300,\"y\":200}}" ) );
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


AI_OBJECT_REF visibleViaRef( int aX, int aY, const wxString& aNetName = wxS( "GND" ) )
{
    return AI_OBJECT_REF( KIID(), PCB_VIA_T,
                          wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
                          viaDetails( aX, aY, aNetName ) );
}


AI_OBJECT_REF zonePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_ZONE_T, wxS( "preview:copper_zone" ),
            wxS( "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\","
                 "\"layer\":\"F.Cu\",\"points\":["
                 "{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":500},{\"x\":0,\"y\":500}]}" ) );
}


AI_OBJECT_REF shapePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_SHAPE_T, wxS( "preview:shape" ),
            wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"rectangle\","
                 "\"layer\":\"F.SilkS\",\"width\":120000,"
                 "\"start\":{\"x\":10,\"y\":20},"
                 "\"end\":{\"x\":110,\"y\":220}}" ) );
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString jsonText( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


PCB_TRACK* addTrackSegment( BOARD& aBoard, NETINFO_ITEM* aNet,
                            const VECTOR2I& aStart, const VECTOR2I& aEnd )
{
    PCB_TRACK* track = new PCB_TRACK( &aBoard );
    track->SetStart( aStart );
    track->SetEnd( aEnd );
    track->SetLayer( F_Cu );
    track->SetWidth( 100000 );
    track->SetNet( aNet );
    aBoard.Add( track );
    return track;
}


AI_SUGGESTION_RECORD routeSuggestion()
{
    AI_OBJECT_REF routeRef = routePreviewRef();

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview route segment" );
    suggestion.m_Body = wxS( "Preview before applying." );
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = routeRef.m_DetailsJson;
    suggestion.m_PreviewObjects.push_back( routeRef );
    suggestion.m_EditObjects.push_back( routeRef );
    return suggestion;
}


AI_SUGGESTION_RECORD shapeSuggestion()
{
    AI_OBJECT_REF shapeRef = shapePreviewRef();

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview graphic shape" );
    suggestion.m_Body = wxS( "Preview before applying." );
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = shapeRef.m_DetailsJson;
    suggestion.m_PreviewObjects.push_back( shapeRef );
    suggestion.m_EditObjects.push_back( shapeRef );
    return suggestion;
}


AI_SUGGESTION_RECORD zoneSuggestion()
{
    AI_OBJECT_REF zoneRef = zonePreviewRef();

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview copper zone" );
    suggestion.m_Body = wxS( "Preview before applying." );
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = zoneRef.m_DetailsJson;
    suggestion.m_PreviewObjects.push_back( zoneRef );
    suggestion.m_EditObjects.push_back( zoneRef );
    return suggestion;
}


AI_NEXT_ACTION_CONTEXT_VERSION runtimeAcceptContext()
{
    AI_NEXT_ACTION_CONTEXT_VERSION context;
    context.m_BoardBaseHash = wxS( "board-hash-a" );
    context.m_ContextVersion.m_DocumentRevision = 12;
    context.m_ContextVersion.m_ViewRevision = 5;
    context.m_ActivitySequence = 1;
    return context;
}


wxString dependencyFingerprintFor( const AI_NEXT_ACTION_CONTEXT_VERSION& aContext )
{
    nlohmann::json fingerprint =
            { { "board_base_hash", toUtf8String( aContext.m_BoardBaseHash ) },
              { "document_revision",
                aContext.m_ContextVersion.m_DocumentRevision },
              { "selection_revision",
                aContext.m_ContextVersion.m_SelectionRevision },
              { "view_revision", aContext.m_ContextVersion.m_ViewRevision },
              { "tool_mode_version", aContext.m_ToolModeVersion },
              { "ui_focus_version", aContext.m_UiFocusVersion },
              { "activity_sequence", aContext.m_ActivitySequence },
              { "viewport_fingerprint",
                toUtf8String( aContext.m_ViewportFingerprint ) },
              { "cursor_region_fingerprint",
                toUtf8String( aContext.m_CursorRegionFingerprint ) } };

    return jsonText( fingerprint );
}


AI_SUGGESTION_RECORD nextActionTrackGeometrySuggestion(
        const PCB_TRACK& aTrack,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aContext )
{
    constexpr uint64_t sessionId = 77;
    constexpr uint64_t handleId = 3;
    constexpr uint64_t generation = 1;
    constexpr uint64_t leaseId = 1;
    constexpr uint64_t attemptId = 99;

    nlohmann::json handle =
            { { "session_id", sessionId },
              { "handle_id", handleId },
              { "generation", generation } };
    nlohmann::json updateArgs =
            { { "handle", handle },
              { "geometry_patch",
                { { "end", { { "x", 500 }, { "y", 600 } } },
                  { "width", 150000 } } } };
    nlohmann::json shadowItem =
            { { "handle", handle },
              { "type", "track_segment" },
              { "net", "GND" },
              { "layer", "F.Cu" },
              { "layers", nlohmann::json::array( { "F.Cu" } ) },
              { "geometry",
                { { "start", { { "x", 0 }, { "y", 0 } } },
                  { "end", { { "x", 100 }, { "y", 0 } } },
                  { "width", 100000 } } },
              { "properties", nlohmann::json::object() },
              { "metadata",
                { { "live_uuid", toUtf8String( aTrack.m_Uuid.AsString() ) },
                  { "live_kicad_type", std::to_string( aTrack.Type() ) } } },
              { "created_epoch", 0 },
              { "updated_epoch", 0 },
              { "deleted", false } };
    nlohmann::json operation =
            { { "operation_id", 1 },
              { "step_id", 1 },
              { "kind", toUtf8String( AiSessionOperationKindId(
                          AI_SESSION_OPERATION_KIND::UpdateItemGeometry ) ) },
              { "arguments", updateArgs },
              { "resolved_handles", nlohmann::json::array( { handle } ) },
              { "created_handles", nlohmann::json::array() },
              { "warnings", nlohmann::json::array() },
              { "result", nlohmann::json::object() },
              { "before_epoch", 0 },
              { "after_epoch", 1 },
              { "is_mutation", true } };
    nlohmann::json contextJson =
            { { "board_base_hash", toUtf8String( aContext.m_BoardBaseHash ) },
              { "document_revision",
                aContext.m_ContextVersion.m_DocumentRevision },
              { "selection_revision",
                aContext.m_ContextVersion.m_SelectionRevision },
              { "view_revision", aContext.m_ContextVersion.m_ViewRevision },
              { "tool_mode_version", aContext.m_ToolModeVersion },
              { "ui_focus_version", aContext.m_UiFocusVersion },
              { "activity_sequence", aContext.m_ActivitySequence },
              { "viewport_fingerprint",
                toUtf8String( aContext.m_ViewportFingerprint ) },
              { "cursor_region_fingerprint",
                toUtf8String( aContext.m_CursorRegionFingerprint ) } };
    nlohmann::json previewLease =
            { { "lease_id", leaseId },
              { "owner_namespace", "nextaction" },
              { "active", true } };
    nlohmann::json acceptToken =
            { { "lease_id", leaseId },
              { "owner_namespace", "nextaction" },
              { "attempt_id", attemptId },
              { "context_version", contextJson },
              { "dependency_fingerprint",
                toUtf8String( dependencyFingerprintFor( aContext ) ) },
              { "touched_object_set_fingerprint", "seeded-track" } };
    nlohmann::json provenance =
            { { "runtime", "next_action" },
              { "runtime_step_id", 1 },
              { "attempt_id", attemptId },
              { "dependency_fingerprint",
                toUtf8String( dependencyFingerprintFor( aContext ) ) },
              { "preview_lease", previewLease },
              { "accept_token", acceptToken },
              { "attempt",
                { { "attempt_id", attemptId },
                  { "session_journal",
                    { { "session_id", sessionId },
                      { "board_id", "pcb-test" },
                      { "base_hash", "board-hash-a" },
                      { "operations", nlohmann::json::array( { operation } ) },
                      { "shadow_items",
                        nlohmann::json::array( { shadowItem } ) } } },
                  { "tool_results",
                    nlohmann::json::array( {
                            { { "tool", "validate.hidden_attempt" },
                              { "validation",
                                { { "accept_validation_sufficient", true },
                                  { "preview_state_exact", true } } } } } ) } } } };

    AI_OBJECT_REF previewRef = routePreviewRef();

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Patch seeded track" );
    suggestion.m_Body = wxS( "Preview before applying." );
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = aContext.m_ContextVersion;
    suggestion.m_ArgumentsJson = previewRef.m_DetailsJson;
    suggestion.m_PreviewObjects.push_back( previewRef );
    suggestion.m_EditObjects.push_back( previewRef );
    suggestion.m_RuntimeProvenanceJson = jsonText( provenance );
    return suggestion;
}


AI_CONTEXT_SNAPSHOT suggestionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 1;
    return snapshot;
}


AI_CONTEXT_SNAPSHOT viaNextActionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 12;
    snapshot.m_Version.m_ViewRevision = 5;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    snapshot.m_ToolState.m_ContextVersion = snapshot.m_Version;
    snapshot.m_VisibleObjects.push_back( visibleViaRef( 100, 50 ) );
    snapshot.m_VisibleObjects.push_back( visibleViaRef( 200, 50 ) );
    snapshot.m_VisibleObjects.push_back( visibleViaRef( 300, 50 ) );
    return snapshot;
}


AI_ACTIVITY_RECORD suggestionActivity()
{
    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 1;
    activity.m_ActionName = wxS( "kisurf.ai.nextAction" );
    return activity;
}


AI_NEXT_ACTION_CONTEXT_VERSION nextActionContextForSuggestion(
        const AI_SUGGESTION_RECORD& aSuggestion )
{
    AI_NEXT_ACTION_CONTEXT_VERSION context;
    context.m_ContextVersion = aSuggestion.m_ContextVersion;
    return context;
}


std::vector<PCB_TRACK*> boardTracksOfType( BOARD& aBoard, KICAD_T aType )
{
    std::vector<PCB_TRACK*> items;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( track->Type() == aType )
            items.push_back( track );
    }

    return items;
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbSuggestionReview )


BOOST_AUTO_TEST_CASE( AcceptSuggestionDispatchesRoutePreviewToOperationEditAdapter )
{
    PCB_REVIEW_FIXTURE fixture;
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( routeSuggestion() );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit,
                                        nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );

    std::vector<PCB_TRACK*> tracks = boardTracksOfType( fixture.m_Board, PCB_TRACE_T );
    BOOST_REQUIRE_EQUAL( tracks.size(), 1 );
    BOOST_CHECK_EQUAL( tracks.front()->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeViaPreviewAddsBoardVia )
{
    PCB_REVIEW_FIXTURE fixture;
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
    PUBLISH_READY_NEXT_ACTION_SERVICES publishServices;
    publishServices.Configure( model );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = viaNextActionContext();
    AI_ACTIVITY_RECORD activity = suggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "accept_token" ) ) );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER replayAdapter( fixture.m_Board );
    ACCEPT_GATE_VALIDATION_SERVICE validationService( true );

    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );
    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, replayAdapter,
                                        validationService,
                                        currentContext ) );

    std::vector<PCB_TRACK*> vias = boardTracksOfType( fixture.m_Board, PCB_VIA_T );
    BOOST_REQUIRE_EQUAL( vias.size(), 1 );
    BOOST_CHECK_EQUAL( vias.front()->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeUsesPublishedAttemptJournalReplay )
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
    PUBLISH_READY_NEXT_ACTION_SERVICES publishServices;
    publishServices.Configure( model );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = viaNextActionContext();
    AI_ACTIVITY_RECORD activity = suggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    JOURNAL_REPLAY_SPY_ADAPTER replayAdapter;
    ACCEPT_GATE_VALIDATION_SERVICE validationService( true );
    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id,
                                        replayAdapter, validationService,
                                        currentContext ) );

    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK_EQUAL( validationService.m_MutationCount, 1 );
    BOOST_CHECK( validationService.m_ArgumentsJson.Contains(
            wxS( "\"level\":\"full_drc\"" ) ) );
    BOOST_CHECK( validationService.m_ArgumentsJson.Contains(
            wxS( "\"gate\":\"accept\"" ) ) );
    BOOST_CHECK_EQUAL( replayAdapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( replayAdapter.m_CommitCount, 1 );
    BOOST_REQUIRE_EQUAL( replayAdapter.m_AppliedKinds.size(), 2 );
    BOOST_CHECK( replayAdapter.m_AppliedKinds.front()
                 == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( replayAdapter.m_AppliedKinds.back()
                 == AI_SESSION_OPERATION_KIND::RunValidation );
    BOOST_CHECK( replayAdapter.m_AppliedArguments.front().Contains(
            wxS( "\"net\":\"GND\"" ) ) );
    BOOST_CHECK( replayAdapter.m_AppliedArguments.front().Contains(
            wxS( "\"position\"" ) ) );

    std::optional<AI_SUGGESTION_RECORD> accepted =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( accepted.has_value() );
    BOOST_CHECK( accepted->m_Status == AI_SUGGESTION_STATUS::Accepted );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeJournalReplayCanPatchSeededLiveTrack )
{
    PCB_REVIEW_FIXTURE fixture;
    PCB_TRACK* track = addTrackSegment( fixture.m_Board, fixture.m_Gnd,
                                        VECTOR2I( 0, 0 ), VECTOR2I( 100, 0 ) );
    AI_NEXT_ACTION_CONTEXT_VERSION currentContext = runtimeAcceptContext();
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( nextActionTrackGeometrySuggestion(
                    *track, currentContext ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER replayAdapter( fixture.m_Board );
    ACCEPT_GATE_VALIDATION_SERVICE validationService( true );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id,
                                        replayAdapter, validationService,
                                        currentContext ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK_EQUAL( track->GetEnd().x, 500 );
    BOOST_CHECK_EQUAL( track->GetEnd().y, 600 );
    BOOST_CHECK_EQUAL( track->GetWidth(), 150000 );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeBlocksReplayWhenAcceptGateValidationFails )
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
    PUBLISH_READY_NEXT_ACTION_SERVICES publishServices;
    publishServices.Configure( model );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = viaNextActionContext();
    AI_ACTIVITY_RECORD activity = suggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    JOURNAL_REPLAY_SPY_ADAPTER replayAdapter;
    ACCEPT_GATE_VALIDATION_SERVICE validationService( false );
    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );

    BOOST_CHECK( !AcceptAiPcbSuggestion( model, suggestion->m_Id,
                                         replayAdapter, validationService,
                                         currentContext ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK_EQUAL( replayAdapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( replayAdapter.m_CommitCount, 0 );

    std::optional<AI_SUGGESTION_RECORD> expired =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( expired.has_value() );
    BOOST_CHECK( expired->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeBlocksReplayWhenAcceptGateValidationIsInexact )
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
    PUBLISH_READY_NEXT_ACTION_SERVICES publishServices;
    publishServices.Configure( model );
    model.SetBackgroundAgentEnabled( true );

    AI_CONTEXT_SNAPSHOT context = viaNextActionContext();
    AI_ACTIVITY_RECORD activity = suggestionActivity();
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled( context, activity,
                                                        wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    JOURNAL_REPLAY_SPY_ADAPTER replayAdapter;
    ACCEPT_GATE_VALIDATION_SERVICE validationService( true, false );
    AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
            AiNextActionContextVersionFromSnapshot( context, activity.m_Sequence );

    BOOST_CHECK( !AcceptAiPcbSuggestion( model, suggestion->m_Id,
                                         replayAdapter, validationService,
                                         currentContext ) );
    BOOST_CHECK_EQUAL( validationService.m_RunCount, 1 );
    BOOST_CHECK_EQUAL( replayAdapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( replayAdapter.m_CommitCount, 0 );

    std::optional<AI_SUGGESTION_RECORD> expired =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( expired.has_value() );
    BOOST_CHECK( expired->m_Status == AI_SUGGESTION_STATUS::Expired );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_CASE( AcceptNextActionRuntimeWithoutJournalDoesNotFallbackToEditObjects )
{
    PCB_REVIEW_FIXTURE fixture;
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );
    AI_SUGGESTION_RECORD suggestionWithoutJournal = routeSuggestion();
    suggestionWithoutJournal.m_RuntimeProvenanceJson = wxS( "{\"runtime\":\"next_action\"}" );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( suggestionWithoutJournal );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( !AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver,
                                         commit,
                                         nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 0 );
    BOOST_CHECK( boardTracksOfType( fixture.m_Board, PCB_TRACE_T ).empty() );
}


BOOST_AUTO_TEST_CASE( RejectSuggestionLeavesBoardUnchanged )
{
    PCB_REVIEW_FIXTURE fixture;
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( routeSuggestion() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.RejectSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( boardTracksOfType( fixture.m_Board, PCB_TRACE_T ).empty() );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
}


BOOST_AUTO_TEST_CASE( AcceptSuggestionDispatchesCopperZonePreviewToOperationEditAdapter )
{
    PCB_REVIEW_FIXTURE fixture;
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( zoneSuggestion() );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit,
                                        nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Zones().size(), 1 );
    BOOST_CHECK_EQUAL( fixture.m_Board.Zones().front()->GetNetCode(),
                       fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( AcceptSuggestionDispatchesShapePreviewToOperationEditAdapter )
{
    PCB_REVIEW_FIXTURE fixture;
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( shapeSuggestion() );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit,
                                        nextActionContextForSuggestion( *suggestion ) ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Drawings().size(), 1 );

    BOARD_ITEM* drawing = fixture.m_Board.Drawings().front();
    BOOST_REQUIRE_EQUAL( drawing->Type(), PCB_SHAPE_T );
    BOOST_CHECK( static_cast<PCB_SHAPE*>( drawing )->GetShape() == SHAPE_T::RECTANGLE );
}


BOOST_AUTO_TEST_SUITE_END()
