#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_suggestion_review.h>

#include <board.h>
#include <commit.h>
#include <netinfo.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>

#include <optional>
#include <vector>
#include <wx/string.h>

namespace
{

class QUEUED_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override
    {
        wxUnusedVar( aTrigger );

        if( !m_NextSuggestion )
            return std::nullopt;

        AI_SUGGESTION_RECORD suggestion = *m_NextSuggestion;
        m_NextSuggestion.reset();
        return suggestion;
    }

    std::optional<AI_SUGGESTION_RECORD> m_NextSuggestion;
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


AI_CONTEXT_SNAPSHOT suggestionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 1;
    return snapshot;
}


AI_ACTIVITY_RECORD suggestionActivity()
{
    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 1;
    activity.m_ActionName = wxS( "kisurf.ai.nextAction" );
    return activity;
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
    auto*              suggestionProvider = new QUEUED_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = routeSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            suggestionContext(), suggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );

    std::vector<PCB_TRACK*> tracks = boardTracksOfType( fixture.m_Board, PCB_TRACE_T );
    BOOST_REQUIRE_EQUAL( tracks.size(), 1 );
    BOOST_CHECK_EQUAL( tracks.front()->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( RejectSuggestionLeavesBoardUnchanged )
{
    PCB_REVIEW_FIXTURE fixture;
    auto*              suggestionProvider = new QUEUED_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = routeSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            suggestionContext(), suggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( model.RejectSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( boardTracksOfType( fixture.m_Board, PCB_TRACE_T ).empty() );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
}


BOOST_AUTO_TEST_CASE( AcceptSuggestionDispatchesCopperZonePreviewToOperationEditAdapter )
{
    PCB_REVIEW_FIXTURE fixture;
    auto*              suggestionProvider = new QUEUED_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = zoneSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            suggestionContext(), suggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Zones().size(), 1 );
    BOOST_CHECK_EQUAL( fixture.m_Board.Zones().front()->GetNetCode(),
                       fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( AcceptSuggestionDispatchesShapePreviewToOperationEditAdapter )
{
    PCB_REVIEW_FIXTURE fixture;
    auto*              suggestionProvider = new QUEUED_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = shapeSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            suggestionContext(), suggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );

    BOOST_CHECK( AcceptAiPcbSuggestion( model, suggestion->m_Id, resolver, commit ) );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Drawings().size(), 1 );

    BOARD_ITEM* drawing = fixture.m_Board.Drawings().front();
    BOOST_REQUIRE_EQUAL( drawing->Type(), PCB_SHAPE_T );
    BOOST_CHECK( static_cast<PCB_SHAPE*>( drawing )->GetShape() == SHAPE_T::RECTANGLE );
}


BOOST_AUTO_TEST_SUITE_END()
