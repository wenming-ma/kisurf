/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf_ai_pcb_tool_state_provider.h>

#include <board.h>
#include <board_connected_item.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <json_common.h>
#include <layer_pairs.h>
#include <lset.h>
#include <pcb_base_edit_frame.h>
#include <pcb_track.h>
#include <pcb_base_frame.h>
#include <project/net_settings.h>
#include <router/pns_placement_algo.h>
#include <router/pns_router.h>
#include <router/router_tool.h>
#include <tool/tool_base.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <view/view.h>
#include <zone.h>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{
bool contains( const wxString& aText, const wxString& aNeedle )
{
    return aText.Find( aNeedle ) != wxNOT_FOUND;
}


AI_TOOL_STATE_KIND classifyName( const wxString& aName )
{
    if( aName.IsEmpty() )
        return AI_TOOL_STATE_KIND::Idle;

    if( contains( aName, wxS( "InteractiveRouter" ) )
        || contains( aName, wxS( "InteractiveRoute" ) ) )
    {
        return AI_TOOL_STATE_KIND::RoutingTrack;
    }

    if( contains( aName, wxS( "InteractiveDrawing.via" ) )
            || contains( aName, wxS( "InteractiveDrawing.drawVia" ) ) )
    {
        return AI_TOOL_STATE_KIND::PlacingVia;
    }

    if( contains( aName, wxS( "placeFootprint" ) ) )
        return AI_TOOL_STATE_KIND::PlacingFootprint;

    if( contains( aName, wxS( "InteractiveDrawing.zone" ) )
            || contains( aName, wxS( "InteractiveDrawing.copperThievingZone" ) )
            || contains( aName, wxS( "InteractiveDrawing.ruleArea" ) )
            || contains( aName, wxS( "InteractiveDrawing.zoneCutout" ) )
            || contains( aName, wxS( "InteractiveDrawing.similarZone" ) )
            || contains( aName, wxS( "InteractiveDrawing.drawZone" ) ) )
    {
        return AI_TOOL_STATE_KIND::DrawingZone;
    }

    if( contains( aName, wxS( "InteractiveMove" ) ) )
        return AI_TOOL_STATE_KIND::MovingSelection;

    if( contains( aName, wxS( "InteractiveSelection" ) ) )
        return AI_TOOL_STATE_KIND::Selecting;

    return AI_TOOL_STATE_KIND::Idle;
}


VECTOR2I toVector2I( const VECTOR2D& aPosition )
{
    return VECTOR2I( static_cast<int>( aPosition.x ), static_cast<int>( aPosition.y ) );
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


bool isGenericToolActivationForSpecificAction( const wxString& aCommandName,
                                               const wxString& aLastActionName )
{
    if( aCommandName.IsEmpty() || aLastActionName.IsEmpty() )
        return false;

    const std::string commandName = toUtf8String( aCommandName );
    const std::string lastActionName = toUtf8String( aLastActionName );

    if( std::count( commandName.begin(), commandName.end(), '.' ) != 1 )
        return false;

    const std::string actionPrefix = commandName + ".";
    return lastActionName.rfind( actionPrefix, 0 ) == 0;
}


void appendPointDigest( std::ostringstream& aStream, const char* aName, const VECTOR2I& aPoint )
{
    aStream << '|' << aName << '=' << aPoint.x << ',' << aPoint.y;
}


void appendBoxDigest( std::ostringstream& aStream, const char* aName, const BOX2I& aBox )
{
    aStream << '|' << aName << '=' << aBox.GetX() << ',' << aBox.GetY() << ','
            << aBox.GetWidth() << ',' << aBox.GetHeight();
}


void appendBoardItemDigest( std::vector<std::string>& aRows, const char* aCollection,
                            const BOARD_ITEM* aItem )
{
    if( !aItem )
        return;

    std::ostringstream row;
    row << aCollection << "|uuid=" << aItem->m_Uuid.AsStdString()
        << "|type=" << static_cast<int>( aItem->Type() )
        << "|layer=" << static_cast<int>( aItem->GetLayer() );

    appendPointDigest( row, "pos", aItem->GetPosition() );
    appendBoxDigest( row, "bbox", aItem->GetBoundingBox() );

    if( const BOARD_CONNECTED_ITEM* connected = dynamic_cast<const BOARD_CONNECTED_ITEM*>( aItem ) )
    {
        row << "|net_code=" << connected->GetNetCode()
            << "|net_name=" << toUtf8String( connected->GetNetname() );
    }

    if( const PCB_TRACK* track = dynamic_cast<const PCB_TRACK*>( aItem ) )
    {
        appendPointDigest( row, "start", track->GetStart() );
        appendPointDigest( row, "end", track->GetEnd() );
        row << "|width=" << track->GetWidth();
    }

    if( const PCB_VIA* via = dynamic_cast<const PCB_VIA*>( aItem ) )
    {
        row << "|drill=" << via->GetDrillValue()
            << "|via_start_layer=" << static_cast<int>( via->GetPrimaryDrillStartLayer() )
            << "|via_end_layer=" << static_cast<int>( via->GetPrimaryDrillEndLayer() );
    }

    aRows.push_back( row.str() );
}


void updateFnv1a64( std::uint64_t& aHash, const std::string& aText )
{
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;

    for( unsigned char ch : aText )
    {
        aHash ^= ch;
        aHash *= fnvPrime;
    }
}


std::string computeBoardHash( const BOARD& aBoard )
{
    std::vector<std::string> rows;

    for( const PCB_TRACK* track : aBoard.Tracks() )
        appendBoardItemDigest( rows, "track", track );

    for( const FOOTPRINT* footprint : aBoard.Footprints() )
        appendBoardItemDigest( rows, "footprint", footprint );

    for( const ZONE* zone : aBoard.Zones() )
        appendBoardItemDigest( rows, "zone", zone );

    for( const BOARD_ITEM* drawing : aBoard.Drawings() )
        appendBoardItemDigest( rows, "drawing", drawing );

    std::sort( rows.begin(), rows.end() );

    std::uint64_t hash = 1469598103934665603ULL;

    for( const std::string& row : rows )
    {
        updateFnv1a64( hash, row );
        updateFnv1a64( hash, "\n" );
    }

    std::ostringstream result;
    result << "fnv1a64:" << std::hex;
    result.width( 16 );
    result.fill( '0' );
    result << hash;

    return result.str();
}


nlohmann::json pointJson( const VECTOR2I& aPoint )
{
    return nlohmann::json{ { "x", aPoint.x }, { "y", aPoint.y } };
}


nlohmann::json boxJson( const BOX2D& aBox )
{
    const VECTOR2D center = aBox.Centre();
    const int      x = static_cast<int>( aBox.GetX() );
    const int      y = static_cast<int>( aBox.GetY() );
    const int      width = static_cast<int>( aBox.GetWidth() );
    const int      height = static_cast<int>( aBox.GetHeight() );
    const int      right = static_cast<int>( aBox.GetX() + aBox.GetWidth() );
    const int      bottom = static_cast<int>( aBox.GetY() + aBox.GetHeight() );

    return nlohmann::json{
        { "x", x },
        { "y", y },
        { "width", width },
        { "height", height },
        { "right", right },
        { "bottom", bottom },
        { "center",
          {
                  { "x", static_cast<int>( center.x ) },
                  { "y", static_cast<int>( center.y ) },
          } },
        { "top_left", { { "x", x }, { "y", y } } },
        { "bottom_right", { { "x", right }, { "y", bottom } } },
    };
}


void addCursorIfPresent( nlohmann::json& aJson, const AI_TOOL_STATE_SNAPSHOT& aSnapshot )
{
    if( aSnapshot.m_HasCursorBoardPosition )
        aJson["cursor"] = pointJson( aSnapshot.m_CursorBoardPosition );
}


void addCursorRegionIfPresent( nlohmann::json& aJson,
                               const AI_TOOL_STATE_SNAPSHOT& aSnapshot )
{
    if( !aSnapshot.m_HasCursorBoardPosition )
        return;

    aJson["cursor_region"] = {
        { "source", "cursor" },
        { "x", aSnapshot.m_CursorBoardPosition.x },
        { "y", aSnapshot.m_CursorBoardPosition.y },
        { "width", 0 },
        { "height", 0 }
    };
}


void addViewportIfPresent( nlohmann::json& aJson, TOOL_MANAGER* aToolManager )
{
    if( !aToolManager )
        return;

    KIGFX::VIEW* view = aToolManager->GetView();

    if( !view || !view->GetGAL() )
        return;

    nlohmann::json viewport = boxJson( view->GetViewport() );
    const VECTOR2I& screenSize = view->GetScreenPixelSize();
    viewport["source"] = "KIGFX::VIEW::GetViewport";
    viewport["zoom"] = view->GetScale();
    viewport["screen_size"] = { { "width", screenSize.x }, { "height", screenSize.y } };
    aJson["viewport"] = std::move( viewport );
}


BOARD* boardFromToolManager( TOOL_MANAGER* aToolManager )
{
    if( !aToolManager )
        return nullptr;

    if( BOARD* board = dynamic_cast<BOARD*>( aToolManager->GetModel() ) )
        return board;

    if( PCB_BASE_FRAME* frame = dynamic_cast<PCB_BASE_FRAME*>( aToolManager->GetToolHolder() ) )
        return frame->GetBoard();

    return nullptr;
}


PCB_LAYER_ID activeLayerFromToolManager( TOOL_MANAGER* aToolManager )
{
    if( !aToolManager )
        return F_Cu;

    if( PCB_BASE_FRAME* frame = dynamic_cast<PCB_BASE_FRAME*>( aToolManager->GetToolHolder() ) )
        return frame->GetActiveLayer();

    return F_Cu;
}


wxString boardLayerName( const BOARD* aBoard, PCB_LAYER_ID aLayer )
{
    if( aLayer < 0 || aLayer >= PCB_LAYER_ID_COUNT )
        return wxString();

    if( aBoard )
        return aBoard->GetLayerName( aLayer );

    return LSET::Name( aLayer );
}


wxString dumpJson( const nlohmann::json& aJson )
{
    if( !aJson.is_object() || aJson.empty() )
        return wxString();

    return fromUtf8String( aJson.dump() );
}


bool hasStableRoutingSizes( const BOARD_DESIGN_SETTINGS& aSettings )
{
    return aSettings.m_NetSettings && aSettings.m_NetSettings->GetDefaultNetclass();
}


void addSharedRoutingSizes( nlohmann::json& aJson, const BOARD_DESIGN_SETTINGS& aSettings )
{
    if( !hasStableRoutingSizes( aSettings ) )
        return;

    aJson["track_width"] = aSettings.GetCurrentTrackWidth();
    aJson["via_diameter"] = aSettings.GetCurrentViaSize();
    aJson["via_drill"] = aSettings.GetCurrentViaDrill();
}


void addTrackWidth( nlohmann::json& aJson, const BOARD_DESIGN_SETTINGS& aSettings )
{
    if( hasStableRoutingSizes( aSettings ) )
        aJson["width"] = aSettings.GetCurrentTrackWidth();
}


void addViaSizes( nlohmann::json& aJson, const BOARD_DESIGN_SETTINGS& aSettings )
{
    if( !hasStableRoutingSizes( aSettings ) )
        return;

    aJson["diameter"] = aSettings.GetCurrentViaSize();
    aJson["drill"] = aSettings.GetCurrentViaDrill();
}


nlohmann::json currentLayerPairJson( const BOARD& aBoard, TOOL_MANAGER* aToolManager )
{
    LAYER_PAIR pair( F_Cu, B_Cu );

    if( aToolManager )
    {
        if( PCB_BASE_EDIT_FRAME* frame =
                    dynamic_cast<PCB_BASE_EDIT_FRAME*>( aToolManager->GetToolHolder() ) )
        {
            if( LAYER_PAIR_SETTINGS* settings = frame->GetLayerPairSettings() )
                pair = settings->GetCurrentLayerPair();
        }
    }

    wxString start = boardLayerName( &aBoard, pair.GetLayerA() );
    wxString end = boardLayerName( &aBoard, pair.GetLayerB() );

    if( start.IsEmpty() )
        start = boardLayerName( &aBoard, F_Cu );

    if( end.IsEmpty() )
        end = boardLayerName( &aBoard, B_Cu );

    return nlohmann::json{
        { "start", toUtf8String( start ) },
        { "end", toUtf8String( end ) }
    };
}


nlohmann::json buildSharedContextJson( const BOARD& aBoard, PCB_LAYER_ID aActiveLayer )
{
    const BOARD_DESIGN_SETTINGS& settings = aBoard.GetDesignSettings();

    nlohmann::json result{
        { "active_layer", toUtf8String( boardLayerName( &aBoard, aActiveLayer ) ) },
        { "board_hash", computeBoardHash( aBoard ) }
    };

    addSharedRoutingSizes( result, settings );

    return result;
}


void mergeRouterContext( nlohmann::json& aModeContext, const BOARD* aBoard,
                         TOOL_MANAGER* aToolManager )
{
    if( !aToolManager )
        return;

    ROUTER_TOOL* routerTool = aToolManager->GetTool<ROUTER_TOOL>();

    if( !routerTool )
        return;

    PNS::ROUTER* router = routerTool->Router();

    if( !router || !router->RoutingInProgress() || !router->Placer() )
        return;

    PNS::PLACEMENT_ALGO* placer = router->Placer();
    PNS::ROUTER_IFACE*   iface = router->GetInterface();

    if( iface )
    {
        const PCB_LAYER_ID layer = iface->GetBoardLayerFromPNSLayer( placer->CurrentLayer() );
        const wxString     layerName = boardLayerName( aBoard, layer );

        if( !layerName.IsEmpty() )
            aModeContext["layer"] = toUtf8String( layerName );

        const std::vector<PNS::NET_HANDLE> nets = placer->CurrentNets();

        if( !nets.empty() )
        {
            const wxString netName = iface->GetNetName( nets.front() );

            if( !netName.IsEmpty() )
                aModeContext["net"] = toUtf8String( netName );
        }
    }

    if( router->Sizes().TrackWidth() > 0 )
        aModeContext["width"] = router->Sizes().TrackWidth();

    if( router->Sizes().ViaDiameter() > 0 )
        aModeContext["via_diameter"] = router->Sizes().ViaDiameter();

    if( router->Sizes().ViaDrill() > 0 )
        aModeContext["via_drill"] = router->Sizes().ViaDrill();

    aModeContext["start"] = pointJson( placer->CurrentStart() );
    aModeContext["current_end"] = pointJson( placer->CurrentEnd() );

    if( !aModeContext.contains( "cursor" ) )
        aModeContext["cursor"] = pointJson( placer->CurrentEnd() );

    aModeContext["placing_via"] = router->IsPlacingVia();
}


nlohmann::json buildModeContextJson( const AI_TOOL_STATE_SNAPSHOT& aSnapshot, const BOARD& aBoard,
                                     PCB_LAYER_ID aActiveLayer, TOOL_MANAGER* aToolManager )
{
    const BOARD_DESIGN_SETTINGS& settings = aBoard.GetDesignSettings();
    const wxString               activeLayerName = boardLayerName( &aBoard, aActiveLayer );
    nlohmann::json               modeContext;

    switch( aSnapshot.m_Kind )
    {
    case AI_TOOL_STATE_KIND::RoutingTrack:
        modeContext = {
            { "mode", "routing_track" },
            { "layer", toUtf8String( activeLayerName ) }
        };
        addTrackWidth( modeContext, settings );
        addCursorIfPresent( modeContext, aSnapshot );
        mergeRouterContext( modeContext, &aBoard, aToolManager );
        break;

    case AI_TOOL_STATE_KIND::PlacingVia:
        modeContext = {
            { "mode", "placing_via" },
            { "layer", toUtf8String( activeLayerName ) },
            { "net", "" }
        };
        addViaSizes( modeContext, settings );
        modeContext["layer_pair"] = currentLayerPairJson( aBoard, aToolManager );
        addCursorIfPresent( modeContext, aSnapshot );
        break;

    case AI_TOOL_STATE_KIND::DrawingZone:
        modeContext = {
            { "mode", "drawing_zone" },
            { "layer", toUtf8String( activeLayerName ) }
        };
        addCursorIfPresent( modeContext, aSnapshot );
        break;

    case AI_TOOL_STATE_KIND::MovingSelection:
        modeContext = { { "mode", "moving_selection" } };
        addCursorIfPresent( modeContext, aSnapshot );
        break;

    case AI_TOOL_STATE_KIND::PlacingFootprint:
        modeContext = { { "mode", "placing_footprint" } };
        addCursorIfPresent( modeContext, aSnapshot );
        break;

    case AI_TOOL_STATE_KIND::Selecting:
        modeContext = { { "mode", "selecting" } };
        addCursorIfPresent( modeContext, aSnapshot );
        break;

    case AI_TOOL_STATE_KIND::Idle:
    case AI_TOOL_STATE_KIND::Unknown:
        break;
    }

    addViewportIfPresent( modeContext, aToolManager );
    addCursorRegionIfPresent( modeContext, aSnapshot );

    return modeContext;
}
} // namespace


KISURF_AI_PCB_TOOL_STATE_PROVIDER::KISURF_AI_PCB_TOOL_STATE_PROVIDER(
        TOOL_MANAGER* aToolManager ) :
        m_ToolManager( aToolManager )
{
    if( m_ToolManager )
    {
        m_EventObserverId = m_ToolManager->AddEventObserver(
                [this]( const TOOL_EVENT& aEvent )
                {
                    RecordToolEvent( aEvent );
                } );
    }
}


KISURF_AI_PCB_TOOL_STATE_PROVIDER::~KISURF_AI_PCB_TOOL_STATE_PROVIDER()
{
    if( m_ToolManager && m_EventObserverId != 0 )
        m_ToolManager->RemoveEventObserver( m_EventObserverId );
}


AI_TOOL_STATE_SNAPSHOT KISURF_AI_PCB_TOOL_STATE_PROVIDER::BuildToolState(
        const AI_CONTEXT_VERSION& aContextVersion ) const
{
    AI_TOOL_STATE_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ContextVersion = aContextVersion;
    snapshot.m_ActiveActionName = m_LastActionName;
    snapshot.m_Kind = classifyActiveState();

    if( m_HasLastCursorBoardPosition )
    {
        snapshot.m_CursorBoardPosition = m_LastCursorBoardPosition;
        snapshot.m_HasCursorBoardPosition = true;
    }
    else if( m_ToolManager )
    {
        snapshot.m_CursorBoardPosition = toVector2I( m_ToolManager->GetCursorPosition() );
        snapshot.m_HasCursorBoardPosition = true;
    }

    if( BOARD* board = boardFromToolManager( m_ToolManager ) )
    {
        const PCB_LAYER_ID activeLayer = activeLayerFromToolManager( m_ToolManager );
        snapshot.m_SharedContextJson = dumpJson( buildSharedContextJson( *board, activeLayer ) );
        snapshot.m_ModeContextJson = dumpJson( buildModeContextJson( snapshot, *board,
                                                                     activeLayer,
                                                                     m_ToolManager ) );
    }

    return snapshot;
}


void KISURF_AI_PCB_TOOL_STATE_PROVIDER::RecordToolEvent( const TOOL_EVENT& aEvent )
{
    if( aEvent.Category() == TC_COMMAND )
    {
        if( aEvent.IsCancel() )
            m_LastActionName.Clear();
        else if( !aEvent.CommandString().empty() )
        {
            const wxString commandName =
                    wxString::FromUTF8( aEvent.CommandString().c_str() );

            if( !isGenericToolActivationForSpecificAction( commandName,
                                                           m_LastActionName ) )
            {
                m_LastActionName = commandName;
            }
        }
    }

    if( aEvent.HasPosition() )
    {
        m_LastCursorBoardPosition = toVector2I( aEvent.Position() );
        m_HasLastCursorBoardPosition = true;
    }
}


AI_TOOL_STATE_KIND KISURF_AI_PCB_TOOL_STATE_PROVIDER::classifyActiveState() const
{
    if( m_ToolManager )
    {
        if( TOOL_BASE* currentTool = m_ToolManager->GetCurrentTool() )
        {
            const AI_TOOL_STATE_KIND currentKind =
                    classifyName( wxString::FromUTF8( currentTool->GetName().c_str() ) );

            if( currentKind != AI_TOOL_STATE_KIND::Idle )
                return currentKind;
        }
    }

    return classifyName( m_LastActionName );
}
