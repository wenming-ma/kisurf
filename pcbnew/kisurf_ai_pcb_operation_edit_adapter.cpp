#include <kisurf_ai_pcb_operation_edit_adapter.h>

#include <board.h>
#include <board_item.h>
#include <commit.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <lset.h>
#include <netinfo.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>

#include <optional>
#include <utility>

namespace
{
std::optional<PCB_LAYER_ID> resolveLayerName( const BOARD& aBoard,
                                              const wxString& aLayerName )
{
    wxString  layerName = aLayerName;
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


BOARD_ITEM* buildRouteSegment( BOARD& aBoard, const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, aOperation.m_LayerName );
    NETINFO_ITEM*              net = aBoard.FindNet( aOperation.m_NetName );

    if( !layer || !net )
        return nullptr;

    PCB_TRACK* track = new PCB_TRACK( &aBoard );
    track->SetStart( aOperation.m_Start );
    track->SetEnd( aOperation.m_End );
    track->SetLayer( *layer );
    track->SetWidth( aOperation.m_Width );
    track->SetNet( net );
    return track;
}


BOARD_ITEM* buildVia( BOARD& aBoard, const AI_SUGGESTION_OPERATION& aOperation )
{
    NETINFO_ITEM* net = aBoard.FindNet( aOperation.m_NetName );

    if( !net )
        return nullptr;

    PCB_VIA* via = new PCB_VIA( &aBoard );
    via->SetPosition( aOperation.m_Position );
    via->SetWidth( aOperation.m_Diameter );
    via->SetPrimaryDrillSize( VECTOR2I( aOperation.m_Drill, aOperation.m_Drill ) );
    via->SetNet( net );
    via->SetIsFree( via->GetNetCode() > 0 );
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

    return std::nullopt;
}


BOARD_ITEM* buildShape( BOARD& aBoard, const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, aOperation.m_LayerName );
    std::optional<SHAPE_T>      shapeType = shapeTypeFromName( aOperation.m_Shape );

    if( !layer || !shapeType )
        return nullptr;

    PCB_SHAPE* shape = new PCB_SHAPE( &aBoard, *shapeType );
    shape->SetLayer( *layer );
    shape->SetStart( aOperation.m_Start );
    shape->SetEnd( aOperation.m_End );
    shape->SetWidth( aOperation.m_Width );
    return shape;
}


BOARD_ITEM* buildCopperZone( BOARD& aBoard, const AI_SUGGESTION_OPERATION& aOperation )
{
    std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, aOperation.m_LayerName );
    NETINFO_ITEM*              net = aBoard.FindNet( aOperation.m_NetName );

    if( !layer || !net )
        return nullptr;

    ZONE* zone = new ZONE( &aBoard );
    zone->SetLayer( *layer );
    zone->SetNet( net );

    SHAPE_POLY_SET outline;
    outline.NewOutline();

    for( const VECTOR2I& point : aOperation.m_Points )
        outline.Append( point );

    zone->AddPolygon( outline.COutline( 0 ) );
    zone->SetNeedRefill( true );
    return zone;
}


BOARD_ITEM* buildOperationItem( BOARD& aBoard, const AI_OBJECT_REF& aObject )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aObject.m_DetailsJson );

    if( !operation )
        return nullptr;

    if( operation->IsRouteSegmentPreview() )
        return buildRouteSegment( aBoard, *operation );

    if( operation->IsPlaceViaPreview() )
        return buildVia( aBoard, *operation );

    if( operation->IsCreateShapePreview() )
        return buildShape( aBoard, *operation );

    if( operation->IsCreateCopperZonePreview() )
        return buildCopperZone( aBoard, *operation );

    return nullptr;
}
} // namespace


KISURF_AI_PCB_OPERATION_EDIT_ADAPTER::KISURF_AI_PCB_OPERATION_EDIT_ADAPTER(
        KISURF_AI_PCB_OBJECT_RESOLVER& aResolver, COMMIT& aCommit,
        wxString aCommitMessage ) :
        m_Resolver( aResolver ),
        m_Commit( aCommit ),
        m_CommitMessage( std::move( aCommitMessage ) )
{
}


bool KISURF_AI_PCB_OPERATION_EDIT_ADAPTER::BeginApply( const AI_VALIDATION_SUMMARY&,
                                                       size_t aObjectCount )
{
    m_AddedItems.clear();
    m_FailedObjects.clear();
    m_WasCommitted = false;
    m_WasReverted = false;
    return aObjectCount > 0;
}


bool KISURF_AI_PCB_OPERATION_EDIT_ADAPTER::ApplyObject( const AI_OBJECT_REF& aObject )
{
    BOARD_ITEM* item = buildOperationItem( m_Resolver.Board(), aObject );

    if( !item )
    {
        m_FailedObjects.push_back( aObject );
        return false;
    }

    m_Commit.Add( item );
    m_AddedItems.push_back( item );
    return true;
}


bool KISURF_AI_PCB_OPERATION_EDIT_ADAPTER::EndApply()
{
    if( m_AddedItems.empty() )
        return false;

    m_Commit.Push( m_CommitMessage );
    m_WasCommitted = true;
    return true;
}


void KISURF_AI_PCB_OPERATION_EDIT_ADAPTER::AbortApply()
{
    m_Commit.Revert();
    m_WasReverted = true;
    m_AddedItems.clear();
}
