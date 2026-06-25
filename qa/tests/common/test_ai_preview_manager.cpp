#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <vector>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx
#include <wx/string.h>

class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "begin:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override
    {
        m_Events.push_back( wxString::Format( wxS( "show:%llu:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              aObject.m_Label ) );
    }

    void ShowOperation( uint64_t aPreviewId,
                        const AI_SUGGESTION_OPERATION& aOperation ) override
    {
        m_Events.push_back( wxString::Format( wxS( "operation:%llu:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              aOperation.m_AnchorId ) );
    }

    void ShowOverlay( uint64_t aPreviewId,
                      const AI_PREVIEW_ITEM_OVERLAY& aOverlay ) override
    {
        m_Events.push_back( wxString::Format( wxS( "overlay:%llu:%s:%s:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              aOverlay.m_ItemLabel,
                                              aOverlay.m_Severity,
                                              aOverlay.m_Message ) );
    }

    void ClearPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "clear:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    std::vector<wxString> m_Events;
};

BOOST_AUTO_TEST_SUITE( AiPreviewManager )

BOOST_AUTO_TEST_CASE( PreviewBeginShowClearUsesOnePreviewId )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );
    AI_OBJECT_REF        pad( KIID(), PCB_PAD_T, wxS( "preview-pad" ) );

    const uint64_t id = session.BeginPreview();
    session.ShowObject( pad );
    session.ClearPreview();

    BOOST_CHECK_EQUAL( id, 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 3 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "show:1:preview-pad" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "clear:1" ) ) );
}

BOOST_AUTO_TEST_CASE( ShowAutomaticallyBeginsPreview )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );
    AI_OBJECT_REF        symbol( KIID(), SCH_SYMBOL_T, wxS( "R1" ) );

    session.ShowObject( symbol );

    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "show:1:R1" ) ) );
}

BOOST_AUTO_TEST_CASE( ShowOperationAutomaticallyBeginsPreview )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;
    operation.m_AnchorId = wxS( "tool.routing.anchor.1" );

    session.ShowOperation( operation );

    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ),
                       wxString( wxS( "operation:1:tool.routing.anchor.1" ) ) );
}

BOOST_AUTO_TEST_CASE( ActiveStateReflectsBeginAndClear )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );

    BOOST_CHECK( !session.HasActivePreview() );
    BOOST_CHECK_EQUAL( session.CurrentPreviewId(), 0 );

    const uint64_t id = session.BeginPreview();
    BOOST_CHECK( session.HasActivePreview() );
    BOOST_CHECK_EQUAL( session.CurrentPreviewId(), id );

    session.ClearPreview();
    BOOST_CHECK( !session.HasActivePreview() );
    BOOST_CHECK_EQUAL( session.CurrentPreviewId(), 0 );
}

BOOST_AUTO_TEST_CASE( PreviewProvenanceClearsWithPreview )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );

    session.BeginPreview( wxS( "{\"session_id\":5,\"step_id\":9}" ) );

    BOOST_CHECK_EQUAL( session.CurrentProvenanceJson(),
                       wxString( wxS( "{\"session_id\":5,\"step_id\":9}" ) ) );

    session.ClearPreview();

    BOOST_CHECK( session.CurrentProvenanceJson().IsEmpty() );
}

BOOST_AUTO_TEST_CASE( PreviewManagerRecordsItemizedPreviewProvenance )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );
    AI_OBJECT_REF        pad( KIID(), PCB_PAD_T, wxS( "preview-pad" ) );

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;
    operation.m_AnchorId = wxS( "tool.routing.anchor.1" );

    session.BeginPreview( wxS( "{\"session_id\":5,\"step_id\":9}" ) );
    session.ShowObject( pad );
    session.ShowOperation( operation );

    const std::vector<AI_PREVIEW_ITEM>& items = session.CurrentPreviewItems();

    BOOST_REQUIRE_EQUAL( items.size(), 2 );
    BOOST_CHECK_EQUAL( items[0].m_PreviewId, 1 );
    BOOST_CHECK_EQUAL( items[0].m_ItemKind, wxString( wxS( "object" ) ) );
    BOOST_CHECK_EQUAL( items[0].m_Label, wxString( wxS( "preview-pad" ) ) );
    BOOST_CHECK_EQUAL( items[0].m_ProvenanceJson,
                       wxString( wxS( "{\"session_id\":5,\"step_id\":9}" ) ) );
    BOOST_CHECK_EQUAL( items[1].m_ItemKind, wxString( wxS( "operation" ) ) );
    BOOST_CHECK_EQUAL( items[1].m_Label,
                       wxString( wxS( "tool.routing.anchor.1" ) ) );

    AI_PREVIEW_BATCH_ITEM batch = session.CurrentPreviewBatchItem();

    BOOST_CHECK_EQUAL( batch.m_PreviewId, 1 );
    BOOST_CHECK_EQUAL( batch.m_ProvenanceJson,
                       wxString( wxS( "{\"session_id\":5,\"step_id\":9}" ) ) );
    BOOST_REQUIRE_EQUAL( batch.m_Items.size(), 2 );
    BOOST_CHECK_EQUAL( batch.m_Items[0].m_Label, wxString( wxS( "preview-pad" ) ) );

    session.ClearPreview();
    BOOST_CHECK( session.CurrentPreviewItems().empty() );
    BOOST_CHECK( session.CurrentPreviewBatchItem().m_Items.empty() );
}

BOOST_AUTO_TEST_CASE( PreviewManagerRecordsItemizedValidationOverlays )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );
    AI_OBJECT_REF        pad( KIID(), PCB_PAD_T, wxS( "preview-pad" ) );

    session.BeginPreview( wxS( "{\"session_id\":5,\"step_id\":9}" ) );
    session.ShowObject( pad );
    session.ShowItemOverlay( wxS( "preview-pad" ), wxS( "validation" ),
                             wxS( "warning" ), wxS( "clearance below preferred" ),
                             wxS( "{\"position\":{\"x\":10,\"y\":20}}" ),
                             wxS( "F.Cu" ) );

    const std::vector<AI_PREVIEW_ITEM_OVERLAY>& overlays =
            session.CurrentPreviewOverlays();

    BOOST_REQUIRE_EQUAL( overlays.size(), 1 );
    BOOST_CHECK_EQUAL( overlays[0].m_PreviewId, 1 );
    BOOST_CHECK_EQUAL( overlays[0].m_ItemLabel, wxString( wxS( "preview-pad" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_OverlayKind, wxString( wxS( "validation" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_Severity, wxString( wxS( "warning" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_Message,
                       wxString( wxS( "clearance below preferred" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_ProvenanceJson,
                       wxString( wxS( "{\"session_id\":5,\"step_id\":9}" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_GeometryJson,
                       wxString( wxS( "{\"position\":{\"x\":10,\"y\":20}}" ) ) );
    BOOST_CHECK_EQUAL( overlays[0].m_Layer, wxString( wxS( "F.Cu" ) ) );
    BOOST_REQUIRE_EQUAL( session.CurrentPreviewItems().size(), 1 );
    BOOST_CHECK_EQUAL( session.CurrentPreviewItems()[0].m_ValidationStatus,
                       wxString( wxS( "warning" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.back(),
                       wxString( wxS( "overlay:1:preview-pad:warning:clearance below preferred" ) ) );

    session.ClearPreview();
    BOOST_CHECK( session.CurrentPreviewOverlays().empty() );
}

BOOST_AUTO_TEST_CASE( ClearWithoutPreviewIsNoOp )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   session( adapter );

    session.ClearPreview();

    BOOST_CHECK( adapter.m_Events.empty() );
}


BOOST_AUTO_TEST_CASE( CompositePreviewAdapterFansOutPreviewLifecycle )
{
    FAKE_PREVIEW_ADAPTER first;
    FAKE_PREVIEW_ADAPTER second;
    AI_COMPOSITE_PREVIEW_ADAPTER composite;
    composite.AddAdapter( first );
    composite.AddAdapter( second );
    AI_PREVIEW_MANAGER session( composite );

    AI_OBJECT_REF pad( KIID(), PCB_PAD_T, wxS( "preview-pad" ) );
    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;
    operation.m_AnchorId = wxS( "tool.routing.anchor.1" );

    session.BeginPreview();
    session.ShowObject( pad );
    session.ShowOperation( operation );
    session.ShowItemOverlay( wxS( "preview-pad" ), wxS( "validation" ),
                             wxS( "warning" ), wxS( "clearance below preferred" ) );
    session.ClearPreview();

    BOOST_REQUIRE_EQUAL( composite.AdapterCount(), 2 );
    BOOST_REQUIRE_EQUAL( first.m_Events.size(), 5 );
    BOOST_REQUIRE_EQUAL( second.m_Events.size(), first.m_Events.size() );

    for( size_t i = 0; i < first.m_Events.size(); ++i )
        BOOST_CHECK_EQUAL( second.m_Events.at( i ), first.m_Events.at( i ) );

    BOOST_CHECK_EQUAL( first.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( first.m_Events.at( 1 ),
                       wxString( wxS( "show:1:preview-pad" ) ) );
    BOOST_CHECK_EQUAL( first.m_Events.at( 2 ),
                       wxString( wxS( "operation:1:tool.routing.anchor.1" ) ) );
    BOOST_CHECK_EQUAL(
            first.m_Events.at( 3 ),
            wxString( wxS( "overlay:1:preview-pad:warning:clearance below preferred" ) ) );
    BOOST_CHECK_EQUAL( first.m_Events.at( 4 ), wxString( wxS( "clear:1" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
