#include <kisurf/ai/ai_context_anchor_provider.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace
{
struct ROUTING_ANCHOR_CONTEXT
{
    wxString m_NetName;
    wxString m_LayerName;
    int      m_Width = 0;
    VECTOR2I m_Start = VECTOR2I( 0, 0 );
    VECTOR2I m_Target = VECTOR2I( 0, 0 );
};


constexpr int DEFAULT_PLACEMENT_ANCHOR_PITCH_IU = 1000000;


struct PLACEMENT_ANCHOR_CONTEXT
{
    VECTOR2I m_Cursor = VECTOR2I( 0, 0 );
    int      m_Pitch = DEFAULT_PLACEMENT_ANCHOR_PITCH_IU;
};


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


bool jsonIntegerToInt( const nlohmann::json& aValue, int& aOut )
{
    if( aValue.is_number_unsigned() )
    {
        const uint64_t value = aValue.get<uint64_t>();

        if( value > static_cast<uint64_t>( std::numeric_limits<int>::max() ) )
            return false;

        aOut = static_cast<int>( value );
        return true;
    }

    if( !aValue.is_number_integer() )
        return false;

    const int64_t value = aValue.get<int64_t>();

    if( value < static_cast<int64_t>( std::numeric_limits<int>::min() )
        || value > static_cast<int64_t>( std::numeric_limits<int>::max() ) )
    {
        return false;
    }

    aOut = static_cast<int>( value );
    return true;
}


bool jsonPositiveIntegerToInt( const nlohmann::json& aValue, int& aOut )
{
    if( !jsonIntegerToInt( aValue, aOut ) )
        return false;

    return aOut > 0;
}


wxString jsonStringToWxString( const nlohmann::json& aValue )
{
    if( !aValue.is_string() )
        return wxString();

    wxString text = fromUtf8String( aValue.get<std::string>() );
    text.Trim( true ).Trim( false );
    return text;
}


bool jsonPointToVector2I( const nlohmann::json& aValue, VECTOR2I& aOut )
{
    if( !aValue.is_object() || !aValue.contains( "x" ) || !aValue.contains( "y" ) )
        return false;

    int x = 0;
    int y = 0;

    if( !jsonIntegerToInt( aValue["x"], x ) || !jsonIntegerToInt( aValue["y"], y ) )
        return false;

    aOut = VECTOR2I( x, y );
    return true;
}


bool samePoint( const VECTOR2I& aLeft, const VECTOR2I& aRight )
{
    return aLeft.x == aRight.x && aLeft.y == aRight.y;
}


int signOf( int aValue )
{
    if( aValue < 0 )
        return -1;

    return 1;
}


nlohmann::json pointJson( const VECTOR2I& aPoint )
{
    return { { "x", aPoint.x }, { "y", aPoint.y } };
}


int compareString( const wxString& aLeft, const wxString& aRight )
{
    const int caseInsensitive = aLeft.CmpNoCase( aRight );

    if( caseInsensitive != 0 )
        return caseInsensitive;

    return aLeft.Cmp( aRight );
}


bool anchorLess( const AI_CONTEXT_ANCHOR& aLeft, const AI_CONTEXT_ANCHOR& aRight )
{
    const int idCompare = compareString( aLeft.m_Id, aRight.m_Id );

    if( idCompare != 0 )
        return idCompare < 0;

    if( aLeft.m_Kind != aRight.m_Kind )
        return static_cast<int>( aLeft.m_Kind ) < static_cast<int>( aRight.m_Kind );

    return compareString( aLeft.m_Label, aRight.m_Label ) < 0;
}


bool isToolAnchorWithPrefix( const AI_CONTEXT_ANCHOR& aAnchor,
                             const wxString& aPrefix )
{
    return aAnchor.m_Id.StartsWith( aPrefix );
}


bool isRoutingToolAnchor( const AI_CONTEXT_ANCHOR& aAnchor )
{
    return isToolAnchorWithPrefix( aAnchor, wxS( "tool.routing." ) );
}


bool isPlacementToolAnchor( const AI_CONTEXT_ANCHOR& aAnchor )
{
    return isToolAnchorWithPrefix( aAnchor, wxS( "tool.placement." ) );
}


std::optional<ROUTING_ANCHOR_CONTEXT> parseRoutingContext(
        const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aSnapshot.m_ToolState;

    if( aSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::RoutingTrack )
    {
        return std::nullopt;
    }

    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( toolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object()
        || !modeContext.contains( "net" ) || !modeContext.contains( "layer" )
        || !modeContext.contains( "width" ) || !modeContext.contains( "start" ) )
    {
        return std::nullopt;
    }

    ROUTING_ANCHOR_CONTEXT context;
    context.m_NetName = jsonStringToWxString( modeContext["net"] );
    context.m_LayerName = jsonStringToWxString( modeContext["layer"] );

    if( context.m_NetName.IsEmpty() || context.m_LayerName.IsEmpty()
        || !jsonPositiveIntegerToInt( modeContext["width"], context.m_Width )
        || !jsonPointToVector2I( modeContext["start"], context.m_Start ) )
    {
        return std::nullopt;
    }

    bool hasTarget = false;

    if( modeContext.contains( "cursor" ) )
        hasTarget = jsonPointToVector2I( modeContext["cursor"], context.m_Target );

    if( !hasTarget && modeContext.contains( "current_end" ) )
        hasTarget = jsonPointToVector2I( modeContext["current_end"], context.m_Target );

    if( !hasTarget && toolState.m_HasCursorBoardPosition )
    {
        context.m_Target = toolState.m_CursorBoardPosition;
        hasTarget = true;
    }

    if( !hasTarget || samePoint( context.m_Start, context.m_Target ) )
        return std::nullopt;

    return context;
}


int placementPitchFromModeContext( const nlohmann::json& aModeContext )
{
    static constexpr const char* PITCH_KEYS[] = {
        "pitch",
        "grid_pitch",
        "placement_pitch"
    };

    for( const char* key : PITCH_KEYS )
    {
        int pitch = 0;

        if( aModeContext.contains( key )
            && jsonPositiveIntegerToInt( aModeContext[key], pitch ) )
        {
            return pitch;
        }
    }

    return DEFAULT_PLACEMENT_ANCHOR_PITCH_IU;
}


std::optional<PLACEMENT_ANCHOR_CONTEXT> parsePlacementContext(
        const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aSnapshot.m_ToolState;

    if( aSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::PlacingFootprint )
    {
        return std::nullopt;
    }

    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( toolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object() )
        return std::nullopt;

    PLACEMENT_ANCHOR_CONTEXT context;
    bool hasCursor = false;

    if( modeContext.contains( "cursor" ) )
        hasCursor = jsonPointToVector2I( modeContext["cursor"], context.m_Cursor );

    if( !hasCursor && toolState.m_HasCursorBoardPosition )
    {
        context.m_Cursor = toolState.m_CursorBoardPosition;
        hasCursor = true;
    }

    if( !hasCursor )
        return std::nullopt;

    context.m_Pitch = placementPitchFromModeContext( modeContext );
    return context;
}


AI_CONTEXT_ANCHOR makeRoutingAnchor( const ROUTING_ANCHOR_CONTEXT& aContext,
                                     const wxString& aId,
                                     AI_CONTEXT_ANCHOR_KIND aKind,
                                     const wxString& aLabel,
                                     const wxString& aRole,
                                     const VECTOR2I& aPosition,
                                     double aConfidence )
{
    nlohmann::json details = {
        { "source", "tool_state" },
        { "mode", "routing_track" },
        { "role", toUtf8String( aRole ) },
        { "net", toUtf8String( aContext.m_NetName ) },
        { "layer", toUtf8String( aContext.m_LayerName ) },
        { "width", aContext.m_Width },
        { "start", pointJson( aContext.m_Start ) },
        { "target", pointJson( aContext.m_Target ) },
        { "position", pointJson( aPosition ) }
    };

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aLabel;
    anchor.m_Summary = wxS( "Routing tool-state anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = -1;
    anchor.m_DetailsJson = fromUtf8String( details.dump() );
    anchor.m_Confidence = aConfidence;
    return anchor;
}


void appendIfDistinctCandidate( std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                                const ROUTING_ANCHOR_CONTEXT& aContext,
                                const wxString& aId,
                                AI_CONTEXT_ANCHOR_KIND aKind,
                                const wxString& aLabel,
                                const wxString& aRole,
                                const VECTOR2I& aPosition,
                                double aConfidence )
{
    if( samePoint( aPosition, aContext.m_Start ) || samePoint( aPosition, aContext.m_Target ) )
        return;

    aAnchors.push_back( makeRoutingAnchor( aContext, aId, aKind, aLabel, aRole,
                                           aPosition, aConfidence ) );
}


std::vector<AI_CONTEXT_ANCHOR> buildRoutingAnchors(
        const ROUTING_ANCHOR_CONTEXT& aContext )
{
    std::vector<AI_CONTEXT_ANCHOR> anchors;
    anchors.push_back( makeRoutingAnchor( aContext, wxS( "tool.routing.start" ),
                                          AI_CONTEXT_ANCHOR_KIND::RouteStart,
                                          wxS( "route:start" ), wxS( "route_start" ),
                                          aContext.m_Start, 1.0 ) );
    anchors.push_back( makeRoutingAnchor( aContext, wxS( "tool.routing.current_end" ),
                                          AI_CONTEXT_ANCHOR_KIND::RouteCandidate,
                                          wxS( "route:current_end" ), wxS( "current_end" ),
                                          aContext.m_Target, 0.95 ) );

    appendIfDistinctCandidate( anchors, aContext,
                               wxS( "tool.routing.orthogonal.horizontal" ),
                               AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout,
                               wxS( "route:orthogonal:horizontal" ),
                               wxS( "orthogonal_horizontal" ),
                               VECTOR2I( aContext.m_Target.x, aContext.m_Start.y ),
                               0.8 );
    appendIfDistinctCandidate( anchors, aContext,
                               wxS( "tool.routing.orthogonal.vertical" ),
                               AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout,
                               wxS( "route:orthogonal:vertical" ),
                               wxS( "orthogonal_vertical" ),
                               VECTOR2I( aContext.m_Start.x, aContext.m_Target.y ),
                               0.8 );

    const int dx = aContext.m_Target.x - aContext.m_Start.x;
    const int dy = aContext.m_Target.y - aContext.m_Start.y;
    const int absDx = std::abs( dx );
    const int absDy = std::abs( dy );

    if( absDx >= absDy )
    {
        appendIfDistinctCandidate(
                anchors, aContext, wxS( "tool.routing.fortyfive.horizontal" ),
                AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection,
                wxS( "route:fortyfive:horizontal" ), wxS( "fortyfive_horizontal" ),
                VECTOR2I( aContext.m_Target.x - signOf( dx ) * absDy,
                          aContext.m_Start.y ),
                0.7 );
    }

    if( absDy >= absDx )
    {
        appendIfDistinctCandidate(
                anchors, aContext, wxS( "tool.routing.fortyfive.vertical" ),
                AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection,
                wxS( "route:fortyfive:vertical" ), wxS( "fortyfive_vertical" ),
                VECTOR2I( aContext.m_Start.x,
                          aContext.m_Target.y - signOf( dy ) * absDx ),
                0.7 );
    }

    return anchors;
}


AI_CONTEXT_ANCHOR makePlacementAnchor( const PLACEMENT_ANCHOR_CONTEXT& aContext,
                                       const wxString& aId,
                                       const wxString& aLabel,
                                       const wxString& aRole,
                                       const VECTOR2I& aPosition,
                                       double aConfidence )
{
    nlohmann::json details = {
        { "source", "tool_state" },
        { "mode", "placing_footprint" },
        { "role", toUtf8String( aRole ) },
        { "cursor", pointJson( aContext.m_Cursor ) },
        { "position", pointJson( aPosition ) },
        { "pitch", aContext.m_Pitch }
    };

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aLabel;
    anchor.m_Summary = wxS( "Footprint placement tool-state anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = -1;
    anchor.m_DetailsJson = fromUtf8String( details.dump() );
    anchor.m_Confidence = aConfidence;
    return anchor;
}


std::vector<AI_CONTEXT_ANCHOR> buildPlacementAnchors(
        const PLACEMENT_ANCHOR_CONTEXT& aContext )
{
    std::vector<AI_CONTEXT_ANCHOR> anchors;
    const VECTOR2I cursor = aContext.m_Cursor;
    const int pitch = aContext.m_Pitch;

    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.cursor" ),
            wxS( "placement:cursor" ), wxS( "placement_cursor" ),
            cursor, 1.0 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.east" ),
            wxS( "placement:grid:east" ), wxS( "grid_east" ),
            VECTOR2I( cursor.x + pitch, cursor.y ), 0.75 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.south" ),
            wxS( "placement:grid:south" ), wxS( "grid_south" ),
            VECTOR2I( cursor.x, cursor.y + pitch ), 0.75 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.diagonal" ),
            wxS( "placement:grid:diagonal" ), wxS( "grid_diagonal" ),
            VECTOR2I( cursor.x + pitch, cursor.y + pitch ), 0.7 ) );

    return anchors;
}
} // namespace


void AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    aSnapshot.m_Anchors.erase(
            std::remove_if( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(),
                            []( const AI_CONTEXT_ANCHOR& aAnchor )
                            {
                                return isRoutingToolAnchor( aAnchor )
                                       || isPlacementToolAnchor( aAnchor );
                            } ),
            aSnapshot.m_Anchors.end() );

    std::optional<ROUTING_ANCHOR_CONTEXT> routingContext =
            parseRoutingContext( aSnapshot );

    if( routingContext )
    {
        std::vector<AI_CONTEXT_ANCHOR> anchors = buildRoutingAnchors( *routingContext );
        aSnapshot.m_Anchors.insert( aSnapshot.m_Anchors.end(), anchors.begin(), anchors.end() );
    }

    std::optional<PLACEMENT_ANCHOR_CONTEXT> placementContext =
            parsePlacementContext( aSnapshot );

    if( placementContext )
    {
        std::vector<AI_CONTEXT_ANCHOR> anchors = buildPlacementAnchors( *placementContext );
        aSnapshot.m_Anchors.insert( aSnapshot.m_Anchors.end(), anchors.begin(), anchors.end() );
    }

    std::sort( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(), anchorLess );
}
