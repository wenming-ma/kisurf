#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_context_index.h>

BOOST_AUTO_TEST_SUITE( AiContextIndex )


BOOST_AUTO_TEST_CASE( EmptyIndexHasInvalidVersion )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( index.EditorKind() == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( !index.Version().IsValid() );
    BOOST_CHECK( index.VisibleObjects().empty() );
    BOOST_CHECK( index.SelectedObjects().empty() );
}


BOOST_AUTO_TEST_CASE( DocumentChangeUpdatesVersionAndVisibleObjects )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Schematic );
    AI_OBJECT_REF symbol( KIID(), SCH_SYMBOL_T, wxS( "U1" ) );

    index.SetVisibleObjects( { symbol } );

    BOOST_CHECK_EQUAL( index.Version().m_DocumentRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.VisibleObjects().size(), 1 );
    BOOST_CHECK_EQUAL( index.VisibleObjects().front().m_Label, wxString( wxS( "U1" ) ) );
}


BOOST_AUTO_TEST_CASE( SelectionChangeUsesSelectionRevision )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );
    AI_OBJECT_REF pad( KIID(), PCB_PAD_T, wxS( "U1.1" ) );

    index.SetSelectedObjects( { pad } );

    BOOST_CHECK_EQUAL( index.Version().m_DocumentRevision, 0 );
    BOOST_CHECK_EQUAL( index.Version().m_SelectionRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
}


BOOST_AUTO_TEST_CASE( BuildSnapshotCarriesIndexedObjects )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );
    AI_OBJECT_REF visiblePad( KIID(), PCB_PAD_T, wxS( "U1.1" ) );
    AI_OBJECT_REF selectedPad( KIID(), PCB_PAD_T, wxS( "U1.2" ) );

    index.SetVisibleObjects( { visiblePad, selectedPad } );
    index.SetSelectedObjects( { selectedPad } );
    index.BumpViewRevision();

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_CHECK( snapshot.m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK_EQUAL( snapshot.m_Version.m_DocumentRevision, 1 );
    BOOST_CHECK_EQUAL( snapshot.m_Version.m_SelectionRevision, 1 );
    BOOST_CHECK_EQUAL( snapshot.m_Version.m_ViewRevision, 1 );
    BOOST_REQUIRE_EQUAL( snapshot.m_VisibleObjects.size(), 2 );
    BOOST_REQUIRE_EQUAL( snapshot.m_SelectedObjects.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.m_SelectedObjects.front().m_Label, wxString( wxS( "U1.2" ) ) );
}


BOOST_AUTO_TEST_CASE( BuildSnapshotSortsObjectListsForStableContext )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );
    AI_OBJECT_REF    laterPad( KIID(), PCB_PAD_T, wxS( "U2.1" ) );
    AI_OBJECT_REF    earlierPad( KIID(), PCB_PAD_T, wxS( "U1.1" ) );

    index.SetVisibleObjects( { laterPad, earlierPad } );
    index.SetSelectedObjects( { laterPad, earlierPad } );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_REQUIRE_EQUAL( snapshot.m_VisibleObjects.size(), 2 );
    BOOST_REQUIRE_EQUAL( snapshot.m_SelectedObjects.size(), 2 );
    BOOST_CHECK_EQUAL( snapshot.m_VisibleObjects.at( 0 ).m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_VisibleObjects.at( 1 ).m_Label,
                       wxString( wxS( "U2.1" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_SelectedObjects.at( 0 ).m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_SelectedObjects.at( 1 ).m_Label,
                       wxString( wxS( "U2.1" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_Version.m_DocumentRevision, 1 );
    BOOST_CHECK_EQUAL( snapshot.m_Version.m_SelectionRevision, 1 );
}


BOOST_AUTO_TEST_CASE( VisualSnapshotIsCarriedAndBumpsViewRevision )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_VISUAL_SNAPSHOT visual;
    visual.m_Source = wxS( "test.image" );
    visual.m_MimeType = wxS( "image/png" );
    visual.m_DataUri = wxS( "data:image/png;base64,abc" );
    visual.m_WidthPx = 4;
    visual.m_HeightPx = 2;
    visual.m_ByteSize = 12;

    index.SetVisualSnapshot( visual );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_CHECK_EQUAL( snapshot.m_Visual.m_Source, wxString( wxS( "test.image" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_Visual.m_WidthPx, 4 );
    BOOST_CHECK( snapshot.m_Visual.HasPixels() );
}


BOOST_AUTO_TEST_CASE( AnchorsAreSortedAndCarriedInSnapshot )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_CONTEXT_ANCHOR second;
    second.m_Id = wxS( "route.candidate.2" );
    second.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteCandidate;

    AI_CONTEXT_ANCHOR first;
    first.m_Id = wxS( "route.candidate.1" );
    first.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteCandidate;

    index.SetAnchors( { second, first } );

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.Anchors().size(), 2 );
    BOOST_CHECK_EQUAL( index.Anchors().at( 0 ).m_Id,
                       wxString( wxS( "route.candidate.1" ) ) );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_REQUIRE_EQUAL( snapshot.m_Anchors.size(), 2 );
    BOOST_CHECK_EQUAL( snapshot.m_Anchors.at( 1 ).m_Id,
                       wxString( wxS( "route.candidate.2" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelStatesAreSortedAndCarriedInSnapshot )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_PANEL_STATE_RECORD later;
    later.m_Id = wxS( "panel.z" );
    later.m_Title = wxS( "Z panel" );

    AI_PANEL_STATE_RECORD earlier;
    earlier.m_Id = wxS( "panel.a" );
    earlier.m_Title = wxS( "A panel" );

    index.SetPanelStates( { later, earlier } );

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.PanelStates().size(), 2 );
    BOOST_CHECK_EQUAL( index.PanelStates().front().m_Id,
                       wxString( wxS( "panel.a" ) ) );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_REQUIRE_EQUAL( snapshot.m_PanelStates.size(), 2 );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates.back().m_Id,
                       wxString( wxS( "panel.z" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
