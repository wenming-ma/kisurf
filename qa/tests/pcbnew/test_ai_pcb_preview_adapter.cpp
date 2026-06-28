#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf_ai_pcb_context_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_preview_adapter.h>
#include <kisurf_ai_pcb_session_preview_service.h>
#include <kisurf_ai_pcb_session_shadow_seeder.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <view/view.h>
#include <zone.h>

#include <cstdlib>
#include <json_common.h>
#include <utility>

namespace
{

struct PCB_PREVIEW_FIXTURE
{
    PCB_PREVIEW_FIXTURE()
    {
        m_Footprint = new FOOTPRINT( &m_Board );
        m_PadA = new PAD( m_Footprint );
        m_PadB = new PAD( m_Footprint );

        m_Footprint->SetReference( wxS( "U1" ) );
        m_PadA->SetNumber( wxS( "1" ) );
        m_PadB->SetNumber( wxS( "2" ) );
        m_PadA->SetPosition( VECTOR2I( 1000, 2000 ) );
        m_PadB->SetPosition( VECTOR2I( 4000, 5000 ) );

        m_Footprint->Add( m_PadA );
        m_Footprint->Add( m_PadB );
        m_Board.Add( m_Footprint );
    }

    BOARD      m_Board;
    FOOTPRINT* m_Footprint = nullptr;
    PAD*       m_PadA = nullptr;
    PAD*       m_PadB = nullptr;
};

std::vector<AI_OBJECT_REF> visibleRefs( BOARD& aBoard )
{
    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( aBoard );
    return adapter.BuildIndex().VisibleObjects();
}

const AI_OBJECT_REF* findRefByLabel( const std::vector<AI_OBJECT_REF>& aRefs,
                                     const wxString& aLabel )
{
    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( ref.m_Label == aLabel )
            return &ref;
    }

    return nullptr;
}

AI_OBJECT_REF routePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_TRACE_T, wxS( "preview:route" ),
            wxS( "{\"operation\":\"route_segment_preview\",\"net\":\"GND\","
                 "\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"end\":{\"x\":300,\"y\":200}}" ) );
}

AI_OBJECT_REF viaPreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_VIA_T, wxS( "preview:via" ),
            wxS( "{\"operation\":\"place_via_preview\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
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


AI_OBJECT_REF zonePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_ZONE_T, wxS( "preview:copper_zone" ),
            wxS( "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\","
                 "\"layer\":\"F.Cu\",\"points\":["
                 "{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":500},{\"x\":0,\"y\":500}]}" ) );
}


AI_OBJECT_REF zonePreviewRefWithHole()
{
    return AI_OBJECT_REF(
            KIID(), PCB_ZONE_T, wxS( "preview:copper_zone_with_hole" ),
            wxS( "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\","
                 "\"layer\":\"F.Cu\",\"points\":["
                 "{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}],"
                 "\"holes\":[[{\"x\":250,\"y\":250},{\"x\":750,\"y\":250},"
                 "{\"x\":750,\"y\":750},{\"x\":250,\"y\":750}]]}" ) );
}


AI_SUGGESTION_OPERATION anchorFocusOperation( const wxString& aLayer = wxS( "F.Cu" ) )
{
    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;
    operation.m_AnchorId = wxS( "tool.routing.anchor.1" );
    operation.m_Position = VECTOR2I( 5000, 7000 );
    operation.m_FocusLayer = aLayer;
    operation.m_FocusNet = wxS( "/GPIO" );
    operation.m_DimUnfocusedLayers = true;
    return operation;
}

} // namespace

BOOST_AUTO_TEST_SUITE( AiPcbPreviewAdapter )

BOOST_AUTO_TEST_CASE( SessionShowsResolvedPadInPreview )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().front() == fixture.m_PadA );
}

BOOST_AUTO_TEST_CASE( MovePreviewShowsMovedCloneWithoutChangingOriginal )
{
    PCB_PREVIEW_FIXTURE fixture;
    VECTOR2I            original = fixture.m_PadA->GetPosition();
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view, VECTOR2I( 100, -25 ) );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().front() != fixture.m_PadA );
    BOOST_CHECK( fixture.m_PadA->GetPosition() == original );
    BOOST_CHECK( adapter.PreviewedItems().front()->GetPosition()
                 == original + VECTOR2I( 100, -25 ) );
}

BOOST_AUTO_TEST_CASE( SyntheticRouteSegmentPreviewCreatesOwnedTrack )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( routePreviewRef() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().front()->Type(), PCB_TRACE_T );

    PCB_TRACK* track = static_cast<PCB_TRACK*>( adapter.PreviewedItems().front() );
    BOOST_CHECK_EQUAL( track->GetStart().x, 100 );
    BOOST_CHECK_EQUAL( track->GetStart().y, 200 );
    BOOST_CHECK_EQUAL( track->GetEnd().x, 300 );
    BOOST_CHECK_EQUAL( track->GetEnd().y, 200 );
    BOOST_CHECK_EQUAL( track->GetLayer(), F_Cu );
    BOOST_CHECK_EQUAL( track->GetWidth(), 150000 );
    BOOST_CHECK( fixture.m_Board.Tracks().empty() );

    session.ClearPreview();

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 0 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( SyntheticViaPreviewCreatesOwnedVia )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( viaPreviewRef() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().front()->Type(), PCB_VIA_T );

    PCB_VIA* via = static_cast<PCB_VIA*>( adapter.PreviewedItems().front() );
    BOOST_CHECK_EQUAL( via->GetPosition().x, 400 );
    BOOST_CHECK_EQUAL( via->GetPosition().y, 500 );
    BOOST_CHECK_EQUAL( via->GetWidth( F_Cu ), 600000 );
    BOOST_CHECK_EQUAL( via->GetPrimaryDrillSize().x, 300000 );
    BOOST_CHECK_EQUAL( via->GetPrimaryDrillSize().y, 300000 );
    BOOST_CHECK( fixture.m_Board.Tracks().empty() );
}


BOOST_AUTO_TEST_CASE( SyntheticShapePreviewCreatesOwnedShape )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( shapePreviewRef() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().front()->Type(), PCB_SHAPE_T );

    PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( adapter.PreviewedItems().front() );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::RECTANGLE );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
    BOOST_CHECK_EQUAL( shape->GetStart().x, 10 );
    BOOST_CHECK_EQUAL( shape->GetStart().y, 20 );
    BOOST_CHECK_EQUAL( shape->GetEnd().x, 110 );
    BOOST_CHECK_EQUAL( shape->GetEnd().y, 220 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 120000 );
    BOOST_CHECK( fixture.m_Board.Drawings().empty() );
}


BOOST_AUTO_TEST_CASE( SyntheticZonePreviewCreatesOwnedZone )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( zonePreviewRef() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().front()->Type(), PCB_ZONE_T );

    ZONE* zone = static_cast<ZONE*>( adapter.PreviewedItems().front() );
    BOOST_CHECK_EQUAL( zone->GetLayer(), F_Cu );
    BOOST_REQUIRE( zone->Outline() );
    BOOST_REQUIRE_EQUAL( zone->Outline()->OutlineCount(), 1 );
    BOOST_CHECK_EQUAL( zone->Outline()->COutline( 0 ).PointCount(), 4 );
    BOOST_CHECK_EQUAL( fixture.m_Board.Zones().size(), 0 );
}


BOOST_AUTO_TEST_CASE( SyntheticZonePreviewCreatesOwnedZoneWithHole )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( zonePreviewRefWithHole() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().front()->Type(), PCB_ZONE_T );

    ZONE* zone = static_cast<ZONE*>( adapter.PreviewedItems().front() );
    BOOST_REQUIRE( zone->Outline() );
    BOOST_REQUIRE_EQUAL( zone->Outline()->OutlineCount(), 1 );
    BOOST_REQUIRE_EQUAL( zone->Outline()->HoleCount( 0 ), 1 );
    BOOST_CHECK_EQUAL( zone->Outline()->CHole( 0, 0 ).PointCount(), 4 );
}


BOOST_AUTO_TEST_CASE( AnchorFocusOperationPreviewCreatesCrosshairMarker )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );

    adapter.BeginPreview( 42 );
    adapter.ShowOperation( 42, anchorFocusOperation() );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 2 );

    for( BOARD_ITEM* item : adapter.PreviewedItems() )
    {
        BOOST_REQUIRE_EQUAL( item->Type(), PCB_SHAPE_T );
        PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( item );
        BOOST_CHECK_EQUAL( shape->GetLayer(), F_Cu );
        BOOST_CHECK( shape->GetShape() == SHAPE_T::SEGMENT );
    }

    PCB_SHAPE* horizontal = static_cast<PCB_SHAPE*>( adapter.PreviewedItems().at( 0 ) );
    PCB_SHAPE* vertical = static_cast<PCB_SHAPE*>( adapter.PreviewedItems().at( 1 ) );

    BOOST_CHECK_EQUAL( horizontal->GetStart().y, 7000 );
    BOOST_CHECK_EQUAL( horizontal->GetEnd().y, 7000 );
    BOOST_CHECK_LT( horizontal->GetStart().x, 5000 );
    BOOST_CHECK_GT( horizontal->GetEnd().x, 5000 );

    BOOST_CHECK_EQUAL( vertical->GetStart().x, 5000 );
    BOOST_CHECK_EQUAL( vertical->GetEnd().x, 5000 );
    BOOST_CHECK_LT( vertical->GetStart().y, 7000 );
    BOOST_CHECK_GT( vertical->GetEnd().y, 7000 );

    BOOST_CHECK( fixture.m_Board.Drawings().empty() );
}

BOOST_AUTO_TEST_CASE( AnchorFocusOperationPreviewFallsBackToFrontCopperLayer )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );

    adapter.BeginPreview( 42 );
    adapter.ShowOperation( 42, anchorFocusOperation( wxS( "unknown-layer" ) ) );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 2 );
    BOOST_CHECK_EQUAL( adapter.PreviewedItems().front()->GetLayer(), F_Cu );
}

BOOST_AUTO_TEST_CASE( MalformedSyntheticPreviewIsSkipped )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( AI_OBJECT_REF( KIID(), PCB_TRACE_T, wxS( "preview:bad" ),
                                 wxS( "{\"operation\":\"route_segment_preview\"}" ) ) );

    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( UnknownReferenceIsSkipped )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    AI_OBJECT_REF missing( KIID(), PCB_PAD_T, wxS( "missing" ) );

    session.ShowObject( missing );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( StalePreviewIdIsIgnored )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( ref );

    adapter.BeginPreview( 42 );
    adapter.ShowObject( 7, *ref );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 42 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( StaleAnchorFocusOperationPreviewIdIsIgnored )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );

    adapter.BeginPreview( 42 );
    adapter.ShowOperation( 7, anchorFocusOperation() );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 42 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( ClearPreviewResetsActivePreview )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );
    session.ClearPreview();

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 0 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}


BOOST_AUTO_TEST_CASE( ShowItemOverlayAddsCanvasMarkerForMatchingPreviewLabel )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    session.ShowObject( viaPreviewRef() );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );

    session.ShowItemOverlay( wxS( "preview:via" ), wxS( "validation" ),
                             wxS( "warning" ), wxS( "clearance too tight" ) );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 2 );
    BOOST_CHECK( dynamic_cast<PCB_SHAPE*>( adapter.PreviewedItems().back() ) != nullptr );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersShadowBoardViaAndClearsBySession )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 17;
    options.m_BoardId = wxS( "pcb-session-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview via" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"session-preview-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result = previewService.RenderPreview(
            session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_CHECK( result.m_PreviewId != 0 );

    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "preview_rendered" );
    BOOST_CHECK( payload["native_preview"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["rendered_item_count"].get<size_t>(), 1 );
    BOOST_REQUIRE( payload.contains( "preview_frame" ) );
    BOOST_CHECK_EQUAL( payload["preview_frame"]["frame_kind"].get<std::string>(),
                       "preview_after" );
    BOOST_CHECK_EQUAL( payload["preview_frame"]["source"].get<std::string>(),
                       "pcbnew.native_preview_scene" );
    BOOST_CHECK_EQUAL( payload["preview_frame"]["preview_id"].get<uint64_t>(),
                       result.m_PreviewId );
    BOOST_CHECK( !payload["preview_frame"]["has_pixels"].get<bool>() );

    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );
    BOOST_CHECK( dynamic_cast<PCB_VIA*>( previewService.PreviewedItems().front() ) != nullptr );

    previewService.ClearPreview( session.SessionId() );

    BOOST_CHECK( previewService.PreviewedItems().empty() );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceCapturesPreviewAfterVisualFrame )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    previewService.SetPreviewFrameCaptureProvider(
            []( uint64_t aPreviewId, const AI_EXECUTION_SESSION& aSession )
            {
                wxImage image( 10, 8, false );
                image.SetRGB( wxRect( 0, 0, 10, 8 ), 32, 64, 96 );

                AI_VISUAL_CONTEXT_FRAME_REQUEST request;
                request.m_FrameId = wxString::Format(
                        wxS( "preview_after_%llu" ),
                        static_cast<unsigned long long>( aPreviewId ) );
                request.m_FrameKind = wxS( "preview_after" );
                request.m_Source = wxS( "pcbnew.native_preview_scene" );
                request.m_PreviewId = wxString::Format(
                        wxS( "%llu" ),
                        static_cast<unsigned long long>( aPreviewId ) );
                request.m_DocumentRevision =
                        aSession.ContextVersion().m_DocumentRevision;
                request.m_PreviewRevision =
                        aSession.ContextVersion().m_ViewRevision;
                request.m_PixelBounds = AI_VISUAL_BOUNDS{ 0.0, 0.0, 10.0, 8.0 };
                request.m_PixelWorldTransform.m_WorldXPerPixelX = 100000.0;
                request.m_PixelWorldTransform.m_WorldYPerPixelY = 100000.0;

                return BuildAiVisualContextFrameFromImage( image, request ).m_Snapshot;
            } );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 117;
    options.m_BoardId = wxS( "pcb-session-preview-frame" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion.m_DocumentRevision = 44;
    options.m_ContextVersion.m_ViewRevision = 9;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview frame via" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"session-preview-frame-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );

    BOOST_REQUIRE( payload.contains( "preview_frame" ) );
    const nlohmann::json& frame = payload["preview_frame"];
    BOOST_CHECK( frame["has_pixels"].get<bool>() );
    BOOST_CHECK_EQUAL( frame["frame_kind"].get<std::string>(), "preview_after" );
    BOOST_CHECK_EQUAL( frame["source"].get<std::string>(),
                       "pcbnew.native_preview_scene.preview_after" );
    BOOST_CHECK_EQUAL( frame["frame_id"].get<std::string>(),
                       "preview_after_1" );
    BOOST_CHECK_EQUAL( frame["width_px"].get<int>(), 10 );
    BOOST_CHECK_EQUAL( frame["height_px"].get<int>(), 8 );
    BOOST_CHECK_EQUAL( frame["document_revision"].get<uint64_t>(), 44 );
    BOOST_CHECK_EQUAL( frame["preview_revision"].get<uint64_t>(), 9 );
    BOOST_CHECK( frame.contains( "sidecar" ) );
    BOOST_REQUIRE( frame["sidecar"].contains( "anchors" ) );
    BOOST_REQUIRE_EQUAL( frame["sidecar"]["anchors"].size(), 1 );
    const nlohmann::json& anchor = frame["sidecar"]["anchors"][0];
    BOOST_CHECK_EQUAL( anchor["anchor_id"].get<std::string>(),
                       "preview_item:session-preview-frame-via" );
    BOOST_CHECK_EQUAL( anchor["object_id"].get<std::string>(),
                       "session-preview-frame-via" );
    BOOST_CHECK( anchor["handle"].get<std::string>().find( "session:117/handle:" )
                 != std::string::npos );
    BOOST_CHECK_EQUAL( anchor["net_name"].get<std::string>(), "GND" );
    BOOST_CHECK_EQUAL( anchor["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( anchor["world_xy"]["x"].get<double>(), 400.0 );
    BOOST_CHECK_EQUAL( anchor["world_xy"]["y"].get<double>(), 500.0 );
    BOOST_CHECK_CLOSE( anchor["pixel_bounds"]["left"].get<double>(), -2.996, 0.001 );
    BOOST_CHECK_CLOSE( anchor["pixel_bounds"]["top"].get<double>(), -2.995, 0.001 );
    BOOST_CHECK_CLOSE( anchor["pixel_bounds"]["right"].get<double>(), 3.004, 0.001 );
    BOOST_CHECK_CLOSE( anchor["pixel_bounds"]["bottom"].get<double>(), 3.005, 0.001 );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersShadowBoardCircleShape )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 18;
    options.m_BoardId = wxS( "pcb-session-circle-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview circle" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateShape,
            wxS( "{\"alias\":\"circle-preview\",\"shape_type\":\"circle\","
                 "\"layer\":\"F.SilkS\",\"width\":50000,"
                 "\"geometry\":{\"center\":{\"x\":400,\"y\":500},"
                 "\"radius\":125000}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );

    PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().front() );
    BOOST_REQUIRE( shape );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::CIRCLE );
    BOOST_CHECK_EQUAL( shape->GetCenter().x, 400 );
    BOOST_CHECK_EQUAL( shape->GetCenter().y, 500 );
    BOOST_CHECK_EQUAL( shape->GetRadius(), 125000 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 50000 );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersShadowBoardArcShape )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 19;
    options.m_BoardId = wxS( "pcb-session-arc-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview arc" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateShape,
            wxS( "{\"alias\":\"arc-preview\",\"shape_type\":\"arc\","
                 "\"layer\":\"F.SilkS\",\"width\":50000,"
                 "\"geometry\":{\"start\":{\"x\":0,\"y\":0},"
                 "\"mid\":{\"x\":50,\"y\":100},"
                 "\"end\":{\"x\":100,\"y\":0}}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );

    PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().front() );
    BOOST_REQUIRE( shape );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::ARC );
    BOOST_CHECK( shape->EndsSwapped() );
    BOOST_CHECK_EQUAL( shape->GetStart().x, 100 );
    BOOST_CHECK_EQUAL( shape->GetStart().y, 0 );
    BOOST_CHECK_EQUAL( shape->GetArcMid().x, 50 );
    BOOST_CHECK_LE( std::abs( shape->GetArcMid().y - 100 ), 1 );
    BOOST_CHECK_EQUAL( shape->GetEnd().x, 0 );
    BOOST_CHECK_EQUAL( shape->GetEnd().y, 0 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 50000 );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersShadowBoardPolygonShape )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 20;
    options.m_BoardId = wxS( "pcb-session-polygon-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview polygon" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateShape,
            wxS( "{\"alias\":\"polygon-preview\",\"shape_type\":\"polygon\","
                 "\"layer\":\"F.SilkS\",\"width\":50000,\"fill\":true,"
                 "\"geometry\":{\"points\":[{\"x\":0,\"y\":0},"
                 "{\"x\":100,\"y\":0},{\"x\":100,\"y\":100},"
                 "{\"x\":0,\"y\":100}]}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );

    PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().front() );
    BOOST_REQUIRE( shape );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::POLY );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 50000 );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
    BOOST_REQUIRE_EQUAL( shape->GetPolyShape().OutlineCount(), 1 );
    BOOST_CHECK_EQUAL( shape->GetPolyShape().Outline( 0 ).PointCount(), 4 );
    BOOST_CHECK_EQUAL( shape->GetPolyShape().Outline( 0 ).CPoint( 2 ).x, 100 );
    BOOST_CHECK_EQUAL( shape->GetPolyShape().Outline( 0 ).CPoint( 2 ).y, 100 );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersSeededFootprintTransform )
{
    PCB_PREVIEW_FIXTURE fixture;
    fixture.m_Footprint->SetPosition( VECTOR2I( 1000, 2000 ) );
    fixture.m_Footprint->SetLayer( F_Cu );
    fixture.m_Footprint->SetOrientation( EDA_ANGLE( 0.0, DEGREES_T ) );
    fixture.m_Footprint->SetReference( wxS( "U1" ) );
    fixture.m_Footprint->SetValue( wxS( "MCU" ) );

    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 21;
    options.m_BoardId = wxS( "pcb-session-footprint-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( fixture.m_Board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> footprints =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"footprint\"}" ) );
    BOOST_REQUIRE_EQUAL( footprints.size(), 1 );

    const uint64_t stepId = session.BeginStep( wxS( "preview footprint transform" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    nlohmann::json handle = {
        { "session_id", footprints.front().m_Handle.m_SessionId },
        { "handle_id", footprints.front().m_Handle.m_HandleId },
        { "generation", footprints.front().m_Handle.m_Generation }
    };

    nlohmann::json moveArgs = {
        { "handles", nlohmann::json::array( { handle } ) },
        { "target_positions", { { "x", 7000 }, { "y", 8000 } } }
    };
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxString::FromUTF8( moveArgs.dump().c_str() ) )
                           .m_Ok );

    nlohmann::json propsArgs = {
        { "handle", handle },
        { "typed_props",
          { { "orientation_degrees", 90.0 },
            { "side", "B.Cu" },
            { "reference", "U42" },
            { "value", "STM32F4" } } }
    };
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxString::FromUTF8( propsArgs.dump().c_str() ) )
                           .m_Ok );
    session.EndStep( stepId );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );

    FOOTPRINT* previewFootprint =
            dynamic_cast<FOOTPRINT*>( previewService.PreviewedItems().front() );
    BOOST_REQUIRE( previewFootprint != nullptr );
    BOOST_CHECK( previewFootprint != fixture.m_Footprint );
    BOOST_CHECK_EQUAL( previewFootprint->GetPosition().x, 7000 );
    BOOST_CHECK_EQUAL( previewFootprint->GetPosition().y, 8000 );
    BOOST_CHECK_CLOSE( previewFootprint->GetOrientation().AsDegrees(), 90.0, 1e-6 );
    BOOST_CHECK_EQUAL( previewFootprint->GetLayer(), B_Cu );
    BOOST_CHECK_EQUAL( previewFootprint->GetReference(), wxString( wxS( "U42" ) ) );
    BOOST_CHECK_EQUAL( previewFootprint->GetValue(), wxString( wxS( "STM32F4" ) ) );

    BOOST_CHECK_EQUAL( fixture.m_Footprint->GetPosition().x, 1000 );
    BOOST_CHECK_EQUAL( fixture.m_Footprint->GetPosition().y, 2000 );
    BOOST_CHECK_EQUAL( fixture.m_Footprint->GetLayer(), F_Cu );
    BOOST_CHECK_EQUAL( fixture.m_Footprint->GetReference(), wxString( wxS( "U1" ) ) );
    BOOST_CHECK_EQUAL( fixture.m_Footprint->GetValue(), wxString( wxS( "MCU" ) ) );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersValidationMetadataAsOverlayMarker )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 19;
    options.m_BoardId = wxS( "pcb-session-overlay-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview via warning" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT createResult = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"warn-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
    BOOST_REQUIRE( createResult.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT metadataResult = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetMetadata,
            wxS( "{\"handle\":\"warn-via\",\"key_values\":{"
                 "\"validation_status\":\"warning\","
                 "\"validation_message\":\"clearance too tight\","
                 "\"validation_geometry\":\"{\\\"position\\\":{\\\"x\\\":900,"
                 "\\\"y\\\":1100}}\","
                 "\"validation_layer\":\"B.Cu\"}}" ) );
    BOOST_REQUIRE( metadataResult.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );

    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["rendered_overlay_count"].get<size_t>(), 1 );

    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 2 );
    BOOST_CHECK( dynamic_cast<PCB_VIA*>( previewService.PreviewedItems().front() ) != nullptr );
    PCB_SHAPE* marker = dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().back() );
    BOOST_REQUIRE( marker != nullptr );
    BOOST_CHECK_EQUAL( ( marker->GetStart().x + marker->GetEnd().x ) / 2, 900 );
    BOOST_CHECK_EQUAL( ( marker->GetStart().y + marker->GetEnd().y ) / 2, 1100 );
    BOOST_CHECK( marker->GetLayer() == B_Cu );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersValidationSegmentGeometryAsErrorOverlay )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 20;
    options.m_BoardId = wxS( "pcb-session-error-overlay-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview via error" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT createResult = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"error-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
    BOOST_REQUIRE( createResult.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT metadataResult = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetMetadata,
            wxS( "{\"handle\":\"error-via\",\"key_values\":{"
                 "\"validation_status\":\"error\","
                 "\"validation_message\":\"segment collision\","
                 "\"validation_geometry\":\"{\\\"segment\\\":{"
                 "\\\"start\\\":{\\\"x\\\":900,\\\"y\\\":1100},"
                 "\\\"end\\\":{\\\"x\\\":1300,\\\"y\\\":1500}}}\","
                 "\"validation_layer\":\"B.Cu\"}}" ) );
    BOOST_REQUIRE( metadataResult.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );

    nlohmann::json payload =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["rendered_overlay_count"].get<size_t>(), 1 );

    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 3 );
    BOOST_CHECK( dynamic_cast<PCB_VIA*>( previewService.PreviewedItems().front() ) != nullptr );

    PCB_SHAPE* primaryMarker =
            dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().at( 1 ) );
    PCB_SHAPE* crossMarker =
            dynamic_cast<PCB_SHAPE*>( previewService.PreviewedItems().at( 2 ) );
    BOOST_REQUIRE( primaryMarker != nullptr );
    BOOST_REQUIRE( crossMarker != nullptr );
    BOOST_CHECK_EQUAL( primaryMarker->GetStart().x, 900 );
    BOOST_CHECK_EQUAL( primaryMarker->GetStart().y, 1100 );
    BOOST_CHECK_EQUAL( primaryMarker->GetEnd().x, 1300 );
    BOOST_CHECK_EQUAL( primaryMarker->GetEnd().y, 1500 );
    BOOST_CHECK( primaryMarker->GetLayer() == B_Cu );
    BOOST_CHECK( crossMarker->GetLayer() == B_Cu );
}


BOOST_AUTO_TEST_CASE( SessionPreviewServiceRendersAnnularZoneWithInnerHole )
{
    PCB_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE previewService( fixture.m_Board, view );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 18;
    options.m_BoardId = wxS( "pcb-session-annular-preview" );
    options.m_BaseHash = wxS( "hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    AI_EXECUTION_SESSION session( std::move( options ) );

    BOOST_CHECK( session.BeginStep( wxS( "preview annular zone" ) ) != 0 );
    AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"annular-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"outer\":["
                 "{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}],"
                 "\"inner\":[{\"x\":250,\"y\":250},{\"x\":750,\"y\":250},"
                 "{\"x\":750,\"y\":750},{\"x\":250,\"y\":750}]}}" ) );
    BOOST_REQUIRE( execution.m_Ok );
    session.EndStep( 1 );

    AI_SESSION_PREVIEW_RESULT result =
            previewService.RenderPreview( session, wxS( "{\"mode\":\"native\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_RenderedItemCount, 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().size(), 1 );
    BOOST_REQUIRE_EQUAL( previewService.PreviewedItems().front()->Type(), PCB_ZONE_T );

    ZONE* zone = static_cast<ZONE*>( previewService.PreviewedItems().front() );
    BOOST_REQUIRE( zone->Outline() );
    BOOST_REQUIRE_EQUAL( zone->Outline()->OutlineCount(), 1 );
    BOOST_REQUIRE_EQUAL( zone->Outline()->HoleCount( 0 ), 1 );
    BOOST_CHECK_EQUAL( zone->Outline()->CHole( 0, 0 ).PointCount(), 4 );
}

BOOST_AUTO_TEST_SUITE_END()
