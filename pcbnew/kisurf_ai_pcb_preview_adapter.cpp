#include <kisurf_ai_pcb_preview_adapter.h>

#include <board.h>
#include <board_item.h>
#include <footprint.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <lset.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <view/view.h>
#include <zone.h>

#include <nlohmann/json.hpp>

#include <memory>

namespace
{
constexpr int ANCHOR_FOCUS_MARKER_RADIUS_IU = 250000;
constexpr int ANCHOR_FOCUS_MARKER_WIDTH_IU = 50000;
constexpr int OVERLAY_MARKER_RADIUS_IU = 180000;
constexpr int OVERLAY_MARKER_ERROR_RADIUS_IU = 260000;
constexpr int OVERLAY_MARKER_WIDTH_IU = 45000;
constexpr int OVERLAY_MARKER_ERROR_WIDTH_IU = 65000;

std::optional<PCB_LAYER_ID> resolveLayerName( const BOARD& aBoard,
                                              const wxString& aLayerName )
{
    wxString layerName = aLayerName;
    const int standardLayer = LSET::NameToLayer( layerName );

    if( standardLayer >= 0 && standardLayer < PCB_LAYER_ID_COUNT )
        return static_cast<PCB_LAYER_ID>( standardLayer );

    for( int layer = 0; layer < PCB_LAYER_ID_COUNT; ++layer )
    {
        PCB_LAYER_ID pcbLayer = static_cast<PCB_LAYER_ID>( layer );

        if( aBoard.GetLayerName( pcbLayer ) == aLayerName )
            return pcbLayer;
    }

    return std::nullopt;
}


PCB_LAYER_ID resolveAnchorFocusLayer( const BOARD& aBoard,
                                      const AI_SUGGESTION_OPERATION& aOperation )
{
    if( !aOperation.m_FocusLayer.IsEmpty() )
    {
        std::optional<PCB_LAYER_ID> layer =
                resolveLayerName( aBoard, aOperation.m_FocusLayer );

        if( layer )
            return *layer;
    }

    return F_Cu;
}


PCB_SHAPE* buildAnchorFocusMarkerSegment( BOARD& aBoard, PCB_LAYER_ID aLayer,
                                          const VECTOR2I& aStart,
                                          const VECTOR2I& aEnd )
{
    PCB_SHAPE* shape = new PCB_SHAPE( &aBoard, SHAPE_T::SEGMENT );
    shape->SetLayer( aLayer );
    shape->SetStart( aStart );
    shape->SetEnd( aEnd );
    shape->SetWidth( ANCHOR_FOCUS_MARKER_WIDTH_IU );
    return shape;
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


nlohmann::json parseObjectJson( const wxString& aText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


std::optional<VECTOR2I> pointFromJson( const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return VECTOR2I( static_cast<int>( aPoint["x"].get<double>() ),
                     static_cast<int>( aPoint["y"].get<double>() ) );
}


FOOTPRINT* buildFootprintTransformPreview(
        const KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
        const AI_OBJECT_REF& aObject )
{
    nlohmann::json details = parseObjectJson( aObject.m_DetailsJson );

    if( !details.contains( "operation" ) || !details["operation"].is_string()
        || details["operation"].get<std::string>() != "footprint_transform_preview" )
    {
        return nullptr;
    }

    FOOTPRINT* source = dynamic_cast<FOOTPRINT*>( aResolver.Resolve( aObject ) );

    if( !source )
        return nullptr;

    std::unique_ptr<FOOTPRINT> preview(
            dynamic_cast<FOOTPRINT*>( source->Clone() ) );

    if( !preview )
        return nullptr;

    if( details.contains( "side" ) )
    {
        if( !details["side"].is_string() )
            return nullptr;

        wxString sideName =
                wxString::FromUTF8( details["side"].get<std::string>().c_str() );
        std::optional<PCB_LAYER_ID> side =
                resolveLayerName( aResolver.Board(), sideName );

        if( !side || ( *side != F_Cu && *side != B_Cu ) )
            return nullptr;

        preview->SetLayerAndFlip( *side );
    }

    if( details.contains( "position" ) )
    {
        std::optional<VECTOR2I> position = pointFromJson( details["position"] );

        if( !position )
            return nullptr;

        preview->SetPosition( *position );
    }

    if( details.contains( "orientation_degrees" ) )
    {
        if( !details["orientation_degrees"].is_number() )
            return nullptr;

        preview->SetOrientation(
                EDA_ANGLE( details["orientation_degrees"].get<double>(), DEGREES_T ) );
    }

    if( details.contains( "reference" ) )
    {
        if( !details["reference"].is_string() )
            return nullptr;

        preview->SetReference(
                wxString::FromUTF8( details["reference"].get<std::string>().c_str() ) );
    }

    if( details.contains( "value" ) )
    {
        if( !details["value"].is_string() )
            return nullptr;

        preview->SetValue(
                wxString::FromUTF8( details["value"].get<std::string>().c_str() ) );
    }

    return preview.release();
}


std::optional<VECTOR2I> overlayPositionFromGeometry( const wxString& aGeometryJson )
{
    if( aGeometryJson.IsEmpty() )
        return std::nullopt;

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( aGeometryJson ), nullptr, false );

    if( geometry.is_discarded() || !geometry.is_object() )
        return std::nullopt;

    if( geometry.contains( "position" ) )
        return pointFromJson( geometry["position"] );

    if( geometry.contains( "center" ) )
        return pointFromJson( geometry["center"] );

    return pointFromJson( geometry );
}


int overlayRadius( const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    return aOverlay.m_Severity.CmpNoCase( wxS( "error" ) ) == 0
                   ? OVERLAY_MARKER_ERROR_RADIUS_IU
                   : OVERLAY_MARKER_RADIUS_IU;
}


int overlayWidth( const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    return aOverlay.m_Severity.CmpNoCase( wxS( "error" ) ) == 0
                   ? OVERLAY_MARKER_ERROR_WIDTH_IU
                   : OVERLAY_MARKER_WIDTH_IU;
}


bool isErrorOverlay( const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    return aOverlay.m_Severity.CmpNoCase( wxS( "error" ) ) == 0;
}


PCB_LAYER_ID overlayLayer( const BOARD& aBoard, const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    if( !aOverlay.m_Layer.IsEmpty() )
    {
        std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, aOverlay.m_Layer );

        if( layer )
            return *layer;
    }

    return F_Cu;
}


PCB_SHAPE* buildOverlayMarkerSegmentItem( BOARD& aBoard, const VECTOR2I& aStart,
                                          const VECTOR2I& aEnd,
                                          const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    PCB_SHAPE* marker = new PCB_SHAPE( &aBoard, SHAPE_T::SEGMENT );
    marker->SetLayer( overlayLayer( aBoard, aOverlay ) );
    marker->SetStart( aStart );
    marker->SetEnd( aEnd );
    marker->SetWidth( overlayWidth( aOverlay ) );
    return marker;
}


std::vector<PCB_SHAPE*> buildOverlayMarkerAtPosition(
        BOARD& aBoard, const VECTOR2I& aCenter,
        const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    const int radius = overlayRadius( aOverlay );

    std::vector<PCB_SHAPE*> markers;
    markers.push_back( buildOverlayMarkerSegmentItem(
            aBoard, aCenter + VECTOR2I( -radius, -radius ),
            aCenter + VECTOR2I( radius, radius ), aOverlay ) );

    if( isErrorOverlay( aOverlay ) )
    {
        markers.push_back( buildOverlayMarkerSegmentItem(
                aBoard, aCenter + VECTOR2I( -radius, radius ),
                aCenter + VECTOR2I( radius, -radius ), aOverlay ) );
    }

    return markers;
}


std::vector<PCB_SHAPE*> buildOverlayMarkerForTarget(
        BOARD& aBoard, const BOARD_ITEM& aTarget,
        const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    return buildOverlayMarkerAtPosition( aBoard, aTarget.GetBoundingBox().GetCenter(),
                                         aOverlay );
}


bool appendSegmentFromJson( std::vector<std::pair<VECTOR2I, VECTOR2I>>& aSegments,
                            const nlohmann::json& aSegment )
{
    if( !aSegment.is_object() || !aSegment.contains( "start" )
        || !aSegment.contains( "end" ) )
    {
        return false;
    }

    std::optional<VECTOR2I> start = pointFromJson( aSegment["start"] );
    std::optional<VECTOR2I> end = pointFromJson( aSegment["end"] );

    if( !start || !end )
        return false;

    aSegments.emplace_back( *start, *end );
    return true;
}


void appendPathSegmentsFromJson( std::vector<std::pair<VECTOR2I, VECTOR2I>>& aSegments,
                                 const nlohmann::json& aPath )
{
    if( !aPath.is_array() || aPath.size() < 2 )
        return;

    std::optional<VECTOR2I> previous;

    for( const nlohmann::json& point : aPath )
    {
        std::optional<VECTOR2I> current = pointFromJson( point );

        if( !current )
        {
            previous.reset();
            continue;
        }

        if( previous )
            aSegments.emplace_back( *previous, *current );

        previous = current;
    }
}


std::vector<std::pair<VECTOR2I, VECTOR2I>> overlaySegmentsFromGeometry(
        const nlohmann::json& aGeometry )
{
    std::vector<std::pair<VECTOR2I, VECTOR2I>> segments;

    if( !aGeometry.is_object() )
        return segments;

    if( aGeometry.contains( "segment" ) )
        appendSegmentFromJson( segments, aGeometry["segment"] );

    appendSegmentFromJson( segments, aGeometry );

    if( aGeometry.contains( "path" ) )
        appendPathSegmentsFromJson( segments, aGeometry["path"] );

    if( aGeometry.contains( "points" ) )
        appendPathSegmentsFromJson( segments, aGeometry["points"] );

    return segments;
}


std::vector<PCB_SHAPE*> buildOverlayMarkersFromGeometry(
        BOARD& aBoard, const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    if( aOverlay.m_GeometryJson.IsEmpty() )
        return {};

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( aOverlay.m_GeometryJson ),
                                   nullptr, false );

    if( geometry.is_discarded() || !geometry.is_object() )
        return {};

    std::vector<std::pair<VECTOR2I, VECTOR2I>> segments =
            overlaySegmentsFromGeometry( geometry );

    if( !segments.empty() )
    {
        std::vector<PCB_SHAPE*> markers;

        for( const std::pair<VECTOR2I, VECTOR2I>& segment : segments )
        {
            markers.push_back( buildOverlayMarkerSegmentItem(
                    aBoard, segment.first, segment.second, aOverlay ) );
        }

        if( isErrorOverlay( aOverlay ) )
        {
            const VECTOR2I center(
                    ( segments.front().first.x + segments.front().second.x ) / 2,
                    ( segments.front().first.y + segments.front().second.y ) / 2 );
            const int radius = overlayRadius( aOverlay ) / 2;
            markers.push_back( buildOverlayMarkerSegmentItem(
                    aBoard, center + VECTOR2I( -radius, radius ),
                    center + VECTOR2I( radius, -radius ), aOverlay ) );
        }

        return markers;
    }

    std::optional<VECTOR2I> position = overlayPositionFromGeometry( aOverlay.m_GeometryJson );

    if( !position )
        return {};

    return buildOverlayMarkerAtPosition( aBoard, *position, aOverlay );
}


std::vector<BOARD_ITEM*> buildAnchorFocusMarkerItems(
        BOARD& aBoard, const AI_SUGGESTION_OPERATION& aOperation )
{
    std::vector<BOARD_ITEM*> items;
    const PCB_LAYER_ID layer = resolveAnchorFocusLayer( aBoard, aOperation );
    const VECTOR2I     center = aOperation.m_Position;
    const int          radius = ANCHOR_FOCUS_MARKER_RADIUS_IU;

    items.push_back( buildAnchorFocusMarkerSegment(
            aBoard, layer, center + VECTOR2I( -radius, 0 ),
            center + VECTOR2I( radius, 0 ) ) );
    items.push_back( buildAnchorFocusMarkerSegment(
            aBoard, layer, center + VECTOR2I( 0, -radius ),
            center + VECTOR2I( 0, radius ) ) );
    return items;
}


BOARD_ITEM* buildSyntheticRoutePreview( BOARD& aBoard,
                                        const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer =
            resolveLayerName( aBoard, aOperation.m_LayerName );

    if( !layer )
        return nullptr;

    PCB_TRACK* track = new PCB_TRACK( &aBoard );
    track->SetStart( aOperation.m_Start );
    track->SetEnd( aOperation.m_End );
    track->SetLayer( *layer );
    track->SetWidth( aOperation.m_Width );
    return track;
}


BOARD_ITEM* buildSyntheticViaPreview( BOARD& aBoard,
                                      const AI_SUGGESTION_OPERATION& aOperation )
{
    PCB_VIA* via = new PCB_VIA( &aBoard );
    via->SetPosition( aOperation.m_Position );
    via->SetWidth( aOperation.m_Diameter );
    via->SetPrimaryDrillSize( VECTOR2I( aOperation.m_Drill, aOperation.m_Drill ) );
    return via;
}


std::optional<SHAPE_T> shapeTypeFromName( const wxString& aShape )
{
    if( aShape.CmpNoCase( wxS( "segment" ) ) == 0
        || aShape.CmpNoCase( wxS( "line" ) ) == 0 )
    {
        return SHAPE_T::SEGMENT;
    }

    if( aShape.CmpNoCase( wxS( "rectangle" ) ) == 0 )
        return SHAPE_T::RECTANGLE;

    if( aShape.CmpNoCase( wxS( "circle" ) ) == 0 )
        return SHAPE_T::CIRCLE;

    if( aShape.CmpNoCase( wxS( "arc" ) ) == 0 )
        return SHAPE_T::ARC;

    return std::nullopt;
}


BOARD_ITEM* buildSyntheticShapePreview( BOARD& aBoard,
                                        const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer =
            resolveLayerName( aBoard, aOperation.m_LayerName );
    std::optional<SHAPE_T> shapeType = shapeTypeFromName( aOperation.m_Shape );

    if( !layer || !shapeType )
        return nullptr;

    PCB_SHAPE* shape = new PCB_SHAPE( &aBoard, *shapeType );
    shape->SetLayer( *layer );

    if( *shapeType == SHAPE_T::CIRCLE )
    {
        if( aOperation.m_Diameter <= 0 )
        {
            delete shape;
            return nullptr;
        }

        shape->SetStart( aOperation.m_Position );
        shape->SetEnd( VECTOR2I( aOperation.m_Position.x + aOperation.m_Diameter,
                                 aOperation.m_Position.y ) );
    }
    else if( *shapeType == SHAPE_T::ARC )
    {
        shape->SetArcGeometry( aOperation.m_Start, aOperation.m_Position,
                               aOperation.m_End );
    }
    else
    {
        shape->SetStart( aOperation.m_Start );
        shape->SetEnd( aOperation.m_End );
    }

    shape->SetWidth( aOperation.m_Width );
    return shape;
}


BOARD_ITEM* buildSyntheticZonePreview( BOARD& aBoard,
                                       const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer =
            resolveLayerName( aBoard, aOperation.m_LayerName );

    if( !layer )
        return nullptr;

    ZONE* zone = new ZONE( &aBoard );
    zone->SetLayer( *layer );

    if( NETINFO_ITEM* net = aBoard.FindNet( aOperation.m_NetName ) )
        zone->SetNet( net );

    SHAPE_POLY_SET outline;
    outline.NewOutline();

    for( const VECTOR2I& point : aOperation.m_Points )
        outline.Append( point );

    zone->AddPolygon( outline.COutline( 0 ) );

    for( const std::vector<VECTOR2I>& hole : aOperation.m_Holes )
    {
        SHAPE_POLY_SET holeOutline;
        holeOutline.NewOutline();

        for( const VECTOR2I& point : hole )
            holeOutline.Append( point );

        zone->AddPolygon( holeOutline.COutline( 0 ) );
    }

    return zone;
}


BOARD_ITEM* buildSyntheticPreviewItem( BOARD& aBoard, const AI_OBJECT_REF& aObject )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aObject.m_DetailsJson );

    if( !operation )
        return nullptr;

    if( operation->IsRouteSegmentPreview() )
        return buildSyntheticRoutePreview( aBoard, *operation );

    if( operation->IsPlaceViaPreview() )
        return buildSyntheticViaPreview( aBoard, *operation );

    if( operation->IsCreateShapePreview() )
        return buildSyntheticShapePreview( aBoard, *operation );

    if( operation->IsCreateCopperZonePreview() )
        return buildSyntheticZonePreview( aBoard, *operation );

    return nullptr;
}
} // namespace

KISURF_AI_PCB_PREVIEW_ADAPTER::KISURF_AI_PCB_PREVIEW_ADAPTER(
        KISURF_AI_PCB_OBJECT_RESOLVER& aResolver, KIGFX::VIEW& aView,
        std::optional<VECTOR2I> aMoveDelta ) :
        m_Resolver( aResolver ),
        m_View( aView ),
        m_MoveDelta( aMoveDelta )
{
}


void KISURF_AI_PCB_PREVIEW_ADAPTER::BeginPreview( uint64_t aPreviewId )
{
    m_View.ClearPreview();
    m_ActivePreviewId = aPreviewId;
    m_PreviewedItems.clear();
    m_PreviewedItemLabels.clear();
}


void KISURF_AI_PCB_PREVIEW_ADAPTER::ShowObject( uint64_t aPreviewId,
                                                const AI_OBJECT_REF& aObject )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    if( FOOTPRINT* footprintPreview =
                buildFootprintTransformPreview( m_Resolver, aObject ) )
    {
        m_View.AddToPreview( footprintPreview, true );
        m_PreviewedItems.push_back( footprintPreview );
        m_PreviewedItemLabels.push_back( aObject.m_Label );
        return;
    }

    if( BOARD_ITEM* syntheticItem = buildSyntheticPreviewItem( m_Resolver.Board(), aObject ) )
    {
        m_View.AddToPreview( syntheticItem, true );
        m_PreviewedItems.push_back( syntheticItem );
        m_PreviewedItemLabels.push_back( aObject.m_Label );
        return;
    }

    BOARD_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
        return;

    if( m_MoveDelta )
    {
        BOARD_ITEM* previewItem = dynamic_cast<BOARD_ITEM*>( item->Clone() );

        if( !previewItem )
            return;

        previewItem->Move( *m_MoveDelta );
        m_View.AddToPreview( previewItem, true );
        m_PreviewedItems.push_back( previewItem );
        m_PreviewedItemLabels.push_back( aObject.m_Label );
        return;
    }

    m_View.AddToPreview( item, false );
    m_PreviewedItems.push_back( item );
    m_PreviewedItemLabels.push_back( aObject.m_Label );
}


void KISURF_AI_PCB_PREVIEW_ADAPTER::ShowOperation(
        uint64_t aPreviewId, const AI_SUGGESTION_OPERATION& aOperation )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    if( !aOperation.IsAnchorFocusPreview() )
        return;

    for( BOARD_ITEM* item : buildAnchorFocusMarkerItems( m_Resolver.Board(), aOperation ) )
    {
        m_View.AddToPreview( item, true );
        m_PreviewedItems.push_back( item );
        m_PreviewedItemLabels.push_back( aOperation.m_AnchorId );
    }
}


void KISURF_AI_PCB_PREVIEW_ADAPTER::ShowOverlay(
        uint64_t aPreviewId, const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    std::vector<PCB_SHAPE*> geometryMarkers =
            buildOverlayMarkersFromGeometry( m_Resolver.Board(), aOverlay );

    if( !geometryMarkers.empty() )
    {
        for( PCB_SHAPE* marker : geometryMarkers )
        {
            m_View.AddToPreview( marker, true );
            m_PreviewedItems.push_back( marker );
            m_PreviewedItemLabels.push_back( aOverlay.m_ItemLabel + wxS( ":overlay" ) );
        }

        return;
    }

    for( size_t i = 0; i < m_PreviewedItems.size()
                       && i < m_PreviewedItemLabels.size(); ++i )
    {
        if( m_PreviewedItemLabels[i] != aOverlay.m_ItemLabel || !m_PreviewedItems[i] )
            continue;

        std::vector<PCB_SHAPE*> markers = buildOverlayMarkerForTarget(
                m_Resolver.Board(), *m_PreviewedItems[i], aOverlay );

        for( PCB_SHAPE* marker : markers )
        {
            m_View.AddToPreview( marker, true );
            m_PreviewedItems.push_back( marker );
            m_PreviewedItemLabels.push_back( aOverlay.m_ItemLabel + wxS( ":overlay" ) );
        }

        return;
    }
}


void KISURF_AI_PCB_PREVIEW_ADAPTER::ClearPreview( uint64_t aPreviewId )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    m_View.ClearPreview();
    m_ActivePreviewId = 0;
    m_PreviewedItems.clear();
    m_PreviewedItemLabels.clear();
}
