#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_structured_surface_preview_adapter.h>

#include <type_traits>
#include <vector>
#include <wx/string.h>

namespace
{
class RECORDING_STRUCTURED_SURFACE_OVERLAY_IO :
        public AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO
{
public:
    bool ShowCellPreview( const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
                          wxString& aError ) override
    {
        if( !m_ShowOk )
        {
            aError = wxS( "show failed" );
            return false;
        }

        m_CellTargets.push_back( aTarget );
        aError.clear();
        return true;
    }

    bool ShowFieldPreview( const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
                           wxString& aError ) override
    {
        if( !m_ShowOk )
        {
            aError = wxS( "show failed" );
            return false;
        }

        m_FieldTargets.push_back( aTarget );
        aError.clear();
        return true;
    }

    void ClearPreview( uint64_t aPreviewId ) override
    {
        m_ClearPreviewIds.push_back( aPreviewId );
    }

    bool m_ShowOk = true;
    std::vector<AI_STRUCTURED_SURFACE_PREVIEW_TARGET> m_CellTargets;
    std::vector<AI_STRUCTURED_SURFACE_PREVIEW_TARGET> m_FieldTargets;
    std::vector<uint64_t>                             m_ClearPreviewIds;
};


wxString cellDiffGeometry()
{
    return wxS( "{"
                "\"kind\":\"set_cell\","
                "\"surface_id\":\"board_setup.clearance\","
                "\"table_id\":\"clearance.rules\","
                "\"row_id\":\"row.power\","
                "\"column_id\":\"class\","
                "\"value\":\"Power\","
                "\"previous_value\":\"Signal\","
                "\"proposed_value\":\"Power\","
                "\"value_changed\":true,"
                "\"target_path\":\"surfaces.board_setup.clearance.tables.clearance.rules.rows.row.power.cells.class\","
                "\"visual_target\":{"
                "\"kind\":\"table_cell\","
                "\"surface_id\":\"board_setup.clearance\","
                "\"table_id\":\"clearance.rules\","
                "\"row_id\":\"row.power\","
                "\"column_id\":\"class\""
                "}}" );
}


wxString fieldDiffGeometry()
{
    return wxS( "{"
                "\"kind\":\"set_field\","
                "\"surface_id\":\"board_setup.rules\","
                "\"field_id\":\"default_clearance\","
                "\"value\":\"0.20mm\","
                "\"previous_value\":\"0.15mm\","
                "\"proposed_value\":\"0.20mm\","
                "\"value_changed\":true,"
                "\"target_path\":\"surfaces.board_setup.rules.fields.default_clearance\","
                "\"visual_target\":{"
                "\"kind\":\"field\","
                "\"surface_id\":\"board_setup.rules\","
                "\"field_id\":\"default_clearance\""
                "}}" );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiStructuredSurfacePreviewAdapter )

BOOST_AUTO_TEST_CASE( StructuredSurfaceCellOverlayRoutesToGridTarget )
{
    RECORDING_STRUCTURED_SURFACE_OVERLAY_IO io;
    AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER   adapter(
            io, wxS( "board_setup.clearance" ), wxS( "clearance.rules" ) );
    AI_PREVIEW_MANAGER previewManager( adapter );

    previewManager.BeginPreview( wxS( "{\"runtime\":\"next_action\"}" ) );
    previewManager.ShowItemOverlay(
            wxS( "surfaces.board_setup.clearance.tables.clearance.rules.rows.row.power.cells.class" ),
            wxS( "structured_surface_patch" ), wxS( "preview" ),
            wxS( "SurfacePatch cell row.power/class -> Power" ),
            cellDiffGeometry() );

    BOOST_CHECK_EQUAL( adapter.IgnoredOverlayCount(), 0 );
    BOOST_CHECK_EQUAL( adapter.FailedOverlayCount(), 0 );
    BOOST_CHECK( adapter.LastError().IsEmpty() );
    BOOST_REQUIRE_EQUAL( io.m_CellTargets.size(), 1 );
    BOOST_CHECK( io.m_FieldTargets.empty() );

    const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& target =
            io.m_CellTargets.front();
    BOOST_CHECK_EQUAL( target.m_PreviewId, 1 );
    BOOST_CHECK_EQUAL( target.m_SurfaceId,
                       wxString( wxS( "board_setup.clearance" ) ) );
    BOOST_CHECK_EQUAL( target.m_TableId,
                       wxString( wxS( "clearance.rules" ) ) );
    BOOST_CHECK_EQUAL( target.m_RowId, wxString( wxS( "row.power" ) ) );
    BOOST_CHECK_EQUAL( target.m_ColumnId, wxString( wxS( "class" ) ) );
    BOOST_CHECK_EQUAL( target.m_PreviousValue, wxString( wxS( "Signal" ) ) );
    BOOST_CHECK_EQUAL( target.m_ProposedValue, wxString( wxS( "Power" ) ) );
    BOOST_CHECK( target.m_ValueChanged );
    BOOST_CHECK_EQUAL( target.m_Message,
                       wxString( wxS( "SurfacePatch cell row.power/class -> Power" ) ) );
    BOOST_CHECK_EQUAL( target.m_Severity, wxString( wxS( "preview" ) ) );
    BOOST_CHECK( target.m_TargetPath.Contains( wxS( "row.power.cells.class" ) ) );
    BOOST_CHECK_EQUAL( adapter.ShownTargetCount(), 1 );
}


BOOST_AUTO_TEST_CASE( StructuredSurfaceFieldOverlayRoutesToFieldTarget )
{
    RECORDING_STRUCTURED_SURFACE_OVERLAY_IO io;
    AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER   adapter(
            io, wxS( "board_setup.rules" ) );
    AI_PREVIEW_MANAGER previewManager( adapter );

    previewManager.BeginPreview();
    previewManager.ShowItemOverlay(
            wxS( "surfaces.board_setup.rules.fields.default_clearance" ),
            wxS( "structured_surface_patch" ), wxS( "preview" ),
            wxS( "SurfacePatch field default_clearance -> 0.20mm" ),
            fieldDiffGeometry() );

    BOOST_CHECK_EQUAL( adapter.IgnoredOverlayCount(), 0 );
    BOOST_CHECK_EQUAL( adapter.FailedOverlayCount(), 0 );
    BOOST_CHECK( adapter.LastError().IsEmpty() );
    BOOST_REQUIRE_EQUAL( io.m_FieldTargets.size(), 1 );
    BOOST_CHECK( io.m_CellTargets.empty() );

    const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& target =
            io.m_FieldTargets.front();
    BOOST_CHECK_EQUAL( target.m_SurfaceId,
                       wxString( wxS( "board_setup.rules" ) ) );
    BOOST_CHECK( target.m_TableId.IsEmpty() );
    BOOST_CHECK_EQUAL( target.m_FieldId,
                       wxString( wxS( "default_clearance" ) ) );
    BOOST_CHECK_EQUAL( target.m_PreviousValue, wxString( wxS( "0.15mm" ) ) );
    BOOST_CHECK_EQUAL( target.m_ProposedValue, wxString( wxS( "0.20mm" ) ) );
    BOOST_CHECK( target.m_ValueChanged );
}


BOOST_AUTO_TEST_CASE( NonStructuredOrMismatchedSurfaceOverlayIsIgnored )
{
    RECORDING_STRUCTURED_SURFACE_OVERLAY_IO io;
    AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER   adapter(
            io, wxS( "board_setup.clearance" ), wxS( "clearance.rules" ) );
    AI_PREVIEW_MANAGER previewManager( adapter );

    previewManager.BeginPreview();
    previewManager.ShowItemOverlay( wxS( "any" ), wxS( "validation" ),
                                    wxS( "warning" ), wxS( "not ours" ),
                                    cellDiffGeometry() );
    previewManager.ShowItemOverlay(
            wxS( "surfaces.board_setup.rules.fields.default_clearance" ),
            wxS( "structured_surface_patch" ), wxS( "preview" ),
            wxS( "wrong surface" ), fieldDiffGeometry() );

    BOOST_CHECK( io.m_CellTargets.empty() );
    BOOST_CHECK( io.m_FieldTargets.empty() );
    BOOST_CHECK_EQUAL( adapter.IgnoredOverlayCount(), 2 );
}


BOOST_AUTO_TEST_CASE( ClearPreviewClearsStructuredSurfaceOverlay )
{
    RECORDING_STRUCTURED_SURFACE_OVERLAY_IO io;
    AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER   adapter(
            io, wxS( "board_setup.clearance" ), wxS( "clearance.rules" ) );
    AI_PREVIEW_MANAGER previewManager( adapter );

    previewManager.BeginPreview();
    previewManager.ShowItemOverlay(
            wxS( "surfaces.board_setup.clearance.tables.clearance.rules.rows.row.power.cells.class" ),
            wxS( "structured_surface_patch" ), wxS( "preview" ),
            wxS( "SurfacePatch cell row.power/class -> Power" ),
            cellDiffGeometry() );
    previewManager.ClearPreview();

    BOOST_REQUIRE_EQUAL( io.m_ClearPreviewIds.size(), 1 );
    BOOST_CHECK_EQUAL( io.m_ClearPreviewIds.front(), 1 );
    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 0 );
}


BOOST_AUTO_TEST_CASE( WxPreviewOverlayIoTypesImplementStructuredSurfaceContract )
{
    BOOST_CHECK( ( std::is_base_of_v<AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO,
                                     AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO> ) );
    BOOST_CHECK( ( std::is_base_of_v<
            AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO,
            AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO> ) );
}

BOOST_AUTO_TEST_SUITE_END()
