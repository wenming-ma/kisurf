#include <kisurf/ai/ai_semantic_tool_call_handler.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


AI_EDITOR_KIND effectiveEditorKind( const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_EditorKind != AI_EDITOR_KIND::Unknown )
        return aRequest.m_EditorKind;

    return aRequest.m_ContextSnapshot.m_EditorKind;
}


AI_TOOL_INVOCATION_RESULT makeResult( const AI_PROVIDER_REQUEST& aRequest,
                                      const AI_TOOL_CALL_RECORD& aToolCall )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aToolCall.m_ToolCallId;
    result.m_ActionName = aToolCall.m_ToolName;
    return result;
}


wxString deniedResultJson( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = { { "tool", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "ok", false },
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) },
                               { "retryable", true },
                               { "retry_hint", "" },
                               { "valid_tools",
                                 nlohmann::json::array(
                                         { "kisurf_get_workspace_view",
                                           "kisurf_invoke_semantic_ui_action" } ) } };

    const std::string tool = toUtf8String( aResult.m_ActionName );
    const std::string code = toUtf8String( aResult.m_ErrorCode );

    if( code == "unknown_tool" )
    {
        payload["retry_hint"] =
                "Use kisurf_get_workspace_view for workspace, visual, activity, "
                "and panel observations. Legacy separate observation tools are "
                "not available.";
    }
    else if( code == "malformed_arguments" )
    {
        payload["retry_hint"] =
                "Retry with a JSON object matching the selected tool schema. "
                "For kisurf_get_workspace_view, use views plus nested context, "
                "visual, activity, or panels options.";
    }
    else if( code == "handler_not_configured" )
    {
        payload["retryable"] = false;
        payload["retry_hint"] =
                "Semantic UI invocation is unavailable in this editor context; "
                "use non-UI session or observation tools instead.";
    }
    else if( code == "unknown_node" || code == "disabled_node"
             || code == "unsupported_action" )
    {
        payload["retry_hint"] =
                "Refresh the workspace or panel state, then retry with a current "
                "enabled node_id and matching action.";
    }
    else if( code == "confirmation_required" )
    {
        payload["retryable"] = false;
        payload["retry_hint"] =
                "This UI node requires direct user confirmation and cannot be "
                "invoked by the model.";
    }
    else
    {
        payload["retry_hint"] =
                "Use error_code and message to correct the tool arguments, then "
                "retry if the UI state has not changed.";
    }

    if( tool == "kisurf_get_workspace_view" || code == "unknown_tool" )
    {
        payload["expected_arguments"] =
                { { "type", "object" },
                  { "properties",
                    { { "views",
                        { { "type", "array" },
                          { "items",
                            { { "type", "string" },
                              { "enum",
                                nlohmann::json::array(
                                        { "context", "visual", "activity",
                                          "panels" } ) } } },
                          { "description",
                            "Optional list of workspace sections to return." } } },
                      { "context", { { "type", "object" } } },
                      { "visual", { { "type", "object" } } },
                      { "activity", { { "type", "object" } } },
                      { "panels", { { "type", "object" } } } } } };
    }
    else if( tool == "kisurf_invoke_semantic_ui_action" )
    {
        payload["expected_arguments"] =
                { { "type", "object" },
                  { "required",
                    nlohmann::json::array( { "node_id", "action" } ) },
                  { "properties",
                    { { "node_id", { { "type", "string" } } },
                      { "action", { { "type", "string" } } },
                      { "text", { { "type", "string" } } },
                      { "checked", { { "type", "boolean" } } } } } };
    }

    return fromJson( payload );
}


AI_TOOL_INVOCATION_RESULT deniedResult( const AI_PROVIDER_REQUEST& aRequest,
                                        const AI_TOOL_CALL_RECORD& aToolCall,
                                        const wxString& aCode,
                                        const wxString& aMessage )
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest, aToolCall );
    result.m_Allowed = false;
    result.m_Executed = false;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    result.m_ResultJson = deniedResultJson( result );
    return result;
}


bool supportedTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_get_workspace_view" )
           || aToolName == wxS( "kisurf_invoke_semantic_ui_action" );
}


std::optional<nlohmann::json> parseObjectArguments( const AI_TOOL_CALL_RECORD& aToolCall,
                                                    wxString& aError )
{
    try
    {
        nlohmann::json args = nlohmann::json::parse( toUtf8String( aToolCall.m_ArgumentsJson ) );

        if( !args.is_object() )
        {
            aError = wxS( "Tool call arguments must be a JSON object." );
            return std::nullopt;
        }

        return args;
    }
    catch( const std::exception& e )
    {
        aError = wxString::FromUTF8( e.what() );
        return std::nullopt;
    }
}


bool jsonStringValue( const nlohmann::json& aValue, wxString& aOut )
{
    if( !aValue.is_string() )
        return false;

    aOut = wxString::FromUTF8( aValue.get_ref<const std::string&>().c_str() );
    aOut.Trim( true ).Trim( false );
    return !aOut.IsEmpty();
}


bool jsonStringField( const nlohmann::json& aArgs, const char* aField, wxString& aOut )
{
    if( !aArgs.contains( aField ) )
        return false;

    return jsonStringValue( aArgs[aField], aOut );
}


bool jsonBooleanField( const nlohmann::json& aArgs, const char* aField, bool& aOut )
{
    if( !aArgs.contains( aField ) )
        return true;

    if( !aArgs[aField].is_boolean() )
        return false;

    aOut = aArgs[aField].get<bool>();
    return true;
}


const AI_CONTEXT_ANCHOR* findAnchorById( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                         const wxString& aAnchorId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( anchor.m_Id == aAnchorId )
            return &anchor;
    }

    return nullptr;
}


bool jsonPositiveIntegerValue( const nlohmann::json& aValue, int& aOut )
{
    if( aValue.is_number_unsigned() )
    {
        const uint64_t value = aValue.get<uint64_t>();

        if( value == 0 || value > static_cast<uint64_t>( std::numeric_limits<int>::max() ) )
            return false;

        aOut = static_cast<int>( value );
        return true;
    }

    if( !aValue.is_number_integer() )
        return false;

    const int64_t value = aValue.get<int64_t>();

    if( value <= 0 || value > static_cast<int64_t>( std::numeric_limits<int>::max() ) )
        return false;

    aOut = static_cast<int>( value );
    return true;
}


struct CONTEXT_TOOL_OPTIONS
{
    bool   m_IncludeVisibleObjects = true;
    bool   m_IncludeSelectedObjects = true;
    bool   m_IncludeActions = true;
    bool   m_IncludeRecentActivity = true;
    bool   m_IncludeToolState = true;
    bool   m_IncludeAnchors = true;
    bool   m_IncludePanels = true;
    bool   m_IncludeVisual = true;
    size_t m_MaxObjects = 64;
    size_t m_MaxActions = 128;
    size_t m_MaxActivity = 64;
    size_t m_MaxAnchors = 64;
    size_t m_MaxPanels = 16;
};


bool isContextToolField( const std::string& aField )
{
    return aField == "include_visible_objects"
           || aField == "include_selected_objects"
           || aField == "include_actions"
           || aField == "include_recent_activity"
           || aField == "include_tool_state"
           || aField == "include_anchors"
           || aField == "include_panels"
           || aField == "include_visual"
           || aField == "max_objects"
           || aField == "max_actions"
           || aField == "max_activity"
           || aField == "max_anchors"
           || aField == "max_panels";
}


bool jsonLimitField( const nlohmann::json& aArgs, const char* aField,
                     size_t aCap, size_t& aOut )
{
    if( !aArgs.contains( aField ) )
        return true;

    int value = 0;

    if( !jsonPositiveIntegerValue( aArgs[aField], value ) )
        return false;

    aOut = std::min( static_cast<size_t>( value ), aCap );
    return true;
}


std::optional<CONTEXT_TOOL_OPTIONS> parseContextToolOptions(
        const nlohmann::json& aArgs, wxString& aError )
{
    CONTEXT_TOOL_OPTIONS options;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( !isContextToolField( it.key() ) )
        {
            aError = wxS( "get_context_snapshot received an unknown argument." );
            return std::nullopt;
        }
    }

    if( !jsonBooleanField( aArgs, "include_visible_objects",
                           options.m_IncludeVisibleObjects )
        || !jsonBooleanField( aArgs, "include_selected_objects",
                              options.m_IncludeSelectedObjects )
        || !jsonBooleanField( aArgs, "include_actions", options.m_IncludeActions )
        || !jsonBooleanField( aArgs, "include_recent_activity",
                              options.m_IncludeRecentActivity )
        || !jsonBooleanField( aArgs, "include_tool_state", options.m_IncludeToolState )
        || !jsonBooleanField( aArgs, "include_anchors", options.m_IncludeAnchors )
        || !jsonBooleanField( aArgs, "include_panels", options.m_IncludePanels )
        || !jsonBooleanField( aArgs, "include_visual", options.m_IncludeVisual ) )
    {
        aError = wxS( "get_context_snapshot include flags must be booleans." );
        return std::nullopt;
    }

    if( !jsonLimitField( aArgs, "max_objects", 128, options.m_MaxObjects )
        || !jsonLimitField( aArgs, "max_actions", 256, options.m_MaxActions )
        || !jsonLimitField( aArgs, "max_activity", 128, options.m_MaxActivity )
        || !jsonLimitField( aArgs, "max_anchors", 128, options.m_MaxAnchors )
        || !jsonLimitField( aArgs, "max_panels", 32, options.m_MaxPanels ) )
    {
        aError = wxS( "get_context_snapshot max limits must be positive integers." );
        return std::nullopt;
    }

    return options;
}


nlohmann::json contextSnapshotJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                    const CONTEXT_TOOL_OPTIONS& aOptions )
{
    nlohmann::json root = nlohmann::json::parse(
            toUtf8String( aSnapshot.AsJsonText( aOptions.m_MaxObjects,
                                                aOptions.m_MaxActions,
                                                aOptions.m_MaxActivity,
                                                aOptions.m_MaxAnchors,
                                                aOptions.m_MaxPanels ) ) );
    nlohmann::json context = std::move( root["kisurf_context"] );

    if( !aOptions.m_IncludeVisibleObjects )
        context.erase( "visible_objects" );

    if( !aOptions.m_IncludeSelectedObjects )
        context.erase( "selected_objects" );

    if( !aOptions.m_IncludeActions )
        context.erase( "actions" );

    if( !aOptions.m_IncludeRecentActivity )
        context.erase( "recent_activity" );

    if( !aOptions.m_IncludeToolState )
        context.erase( "tool_state" );

    if( !aOptions.m_IncludeAnchors )
        context.erase( "anchors" );

    if( !aOptions.m_IncludePanels )
        context.erase( "panel_states" );

    if( !aOptions.m_IncludeVisual )
        context.erase( "visual" );

    return context;
}


struct VISUAL_FRAME_TOOL_OPTIONS
{
    bool                  m_IncludePixels = false;
    size_t                m_MaxBytes = 262144;
    bool                  m_IncludeAnchorOverlays = true;
    size_t                m_MaxAnchorOverlays = 64;
    wxString              m_FocusLayer;
    wxString              m_FocusNet;
    bool                  m_DimUnfocusedLayers = false;
    bool                  m_HasExplicitDimUnfocusedLayers = false;
    bool                  m_HasExplicitHighlightAnchorIds = false;
    std::vector<wxString> m_HighlightAnchorIds;

    bool HasRenderDirectives() const
    {
        return !m_FocusLayer.IsEmpty() || !m_FocusNet.IsEmpty()
               || m_DimUnfocusedLayers || !m_HighlightAnchorIds.empty();
    }
};


bool parseHighlightAnchorIds( const nlohmann::json& aArgs,
                              std::vector<wxString>& aHighlightAnchorIds,
                              wxString& aError )
{
    if( !aArgs.contains( "highlight_anchor_ids" ) )
        return true;

    const nlohmann::json& ids = aArgs["highlight_anchor_ids"];

    if( !ids.is_array() )
    {
        aError = wxS( "get_visual_frame highlight_anchor_ids must be an array." );
        return false;
    }

    if( ids.size() > 32 )
    {
        aError =
                wxS( "get_visual_frame highlight_anchor_ids must contain at most 32 ids." );
        return false;
    }

    for( const nlohmann::json& idValue : ids )
    {
        wxString id;

        if( !jsonStringValue( idValue, id ) )
        {
            aError = wxS(
                    "get_visual_frame highlight_anchor_ids entries must be non-empty strings." );
            return false;
        }

        aHighlightAnchorIds.push_back( id );
    }

    return true;
}


std::optional<VISUAL_FRAME_TOOL_OPTIONS> parseVisualFrameToolOptions(
        const nlohmann::json& aArgs, wxString& aError )
{
    VISUAL_FRAME_TOOL_OPTIONS options;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( it.key() != "include_pixels" && it.key() != "max_bytes"
            && it.key() != "include_anchor_overlays"
            && it.key() != "max_anchor_overlays"
            && it.key() != "focus_layer"
            && it.key() != "focus_net"
            && it.key() != "dim_unfocused_layers"
            && it.key() != "highlight_anchor_ids" )
        {
            aError = wxS( "get_visual_frame received an unknown argument." );
            return std::nullopt;
        }
    }

    if( !jsonBooleanField( aArgs, "include_pixels", options.m_IncludePixels ) )
    {
        aError = wxS( "get_visual_frame include_pixels must be a boolean." );
        return std::nullopt;
    }

    if( !jsonLimitField( aArgs, "max_bytes", 1048576, options.m_MaxBytes ) )
    {
        aError = wxS( "get_visual_frame max_bytes must be a positive integer." );
        return std::nullopt;
    }

    if( !jsonBooleanField( aArgs, "include_anchor_overlays",
                           options.m_IncludeAnchorOverlays ) )
    {
        aError = wxS( "get_visual_frame include_anchor_overlays must be a boolean." );
        return std::nullopt;
    }

    if( !jsonLimitField( aArgs, "max_anchor_overlays", 128,
                         options.m_MaxAnchorOverlays ) )
    {
        aError = wxS( "get_visual_frame max_anchor_overlays must be a positive integer." );
        return std::nullopt;
    }

    if( aArgs.contains( "focus_layer" )
        && !jsonStringValue( aArgs["focus_layer"], options.m_FocusLayer ) )
    {
        aError = wxS( "get_visual_frame focus_layer must be a non-empty string." );
        return std::nullopt;
    }

    if( aArgs.contains( "focus_net" )
        && !jsonStringValue( aArgs["focus_net"], options.m_FocusNet ) )
    {
        aError = wxS( "get_visual_frame focus_net must be a non-empty string." );
        return std::nullopt;
    }

    if( !jsonBooleanField( aArgs, "dim_unfocused_layers",
                           options.m_DimUnfocusedLayers ) )
    {
        aError = wxS( "get_visual_frame dim_unfocused_layers must be a boolean." );
        return std::nullopt;
    }

    options.m_HasExplicitDimUnfocusedLayers =
            aArgs.contains( "dim_unfocused_layers" );
    options.m_HasExplicitHighlightAnchorIds =
            aArgs.contains( "highlight_anchor_ids" );

    if( !parseHighlightAnchorIds( aArgs, options.m_HighlightAnchorIds, aError ) )
        return std::nullopt;

    return options;
}


bool validateVisualFrameToolOptions( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                     const VISUAL_FRAME_TOOL_OPTIONS& aOptions,
                                     wxString& aError )
{
    for( const wxString& anchorId : aOptions.m_HighlightAnchorIds )
    {
        const AI_CONTEXT_ANCHOR* anchor = findAnchorById( aSnapshot, anchorId );

        if( !anchor || !anchor->m_HasPosition )
        {
            aError = wxS(
                    "get_visual_frame highlight_anchor_ids must reference positional anchors." );
            return false;
        }
    }

    return true;
}


bool routingModeContextJson( const AI_PROVIDER_REQUEST& aRequest,
                             nlohmann::json& aModeContext )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aRequest.m_ContextSnapshot.m_ToolState;

    if( effectiveEditorKind( aRequest ) != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::RoutingTrack )
    {
        return false;
    }

    aModeContext = nlohmann::json::parse(
            toUtf8String( toolState.m_ModeContextJson ), nullptr, false );

    return !aModeContext.is_discarded() && aModeContext.is_object();
}


bool placementToolStateActive( const AI_PROVIDER_REQUEST& aRequest )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aRequest.m_ContextSnapshot.m_ToolState;

    return effectiveEditorKind( aRequest ) == AI_EDITOR_KIND::Pcb
           && toolState.m_EditorKind == AI_EDITOR_KIND::Pcb
           && toolState.m_Kind == AI_TOOL_STATE_KIND::PlacingFootprint;
}


bool isRoutingVisualAnchorKind( AI_CONTEXT_ANCHOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_CONTEXT_ANCHOR_KIND::RouteStart:
    case AI_CONTEXT_ANCHOR_KIND::RouteTarget:
    case AI_CONTEXT_ANCHOR_KIND::RouteCandidate:
    case AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout:
    case AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection:
        return true;

    case AI_CONTEXT_ANCHOR_KIND::Unknown:
    case AI_CONTEXT_ANCHOR_KIND::PlacementCandidate:
    case AI_CONTEXT_ANCHOR_KIND::PatternContinuation:
    case AI_CONTEXT_ANCHOR_KIND::ShapeCorner:
    case AI_CONTEXT_ANCHOR_KIND::ZoneVertex:
    case AI_CONTEXT_ANCHOR_KIND::PanelCell:
    case AI_CONTEXT_ANCHOR_KIND::General:
        return false;
    }

    return false;
}


bool isPlacementVisualAnchorKind( AI_CONTEXT_ANCHOR_KIND aKind )
{
    return aKind == AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
}


void appendDefaultHighlightAnchors(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        bool ( *aKindPredicate )( AI_CONTEXT_ANCHOR_KIND ),
        std::vector<wxString>& aHighlightAnchorIds )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( aHighlightAnchorIds.size() >= 32 )
            break;

        if( anchor.m_HasPosition && aKindPredicate( anchor.m_Kind ) )
            aHighlightAnchorIds.push_back( anchor.m_Id );
    }
}


void applyRoutingVisualDefaults( const AI_PROVIDER_REQUEST& aRequest,
                                 VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    nlohmann::json modeContext;

    if( !routingModeContextJson( aRequest, modeContext ) )
        return;

    wxString net;
    wxString layer;

    if( !modeContext.contains( "net" ) || !jsonStringValue( modeContext["net"], net )
        || !modeContext.contains( "layer" )
        || !jsonStringValue( modeContext["layer"], layer ) )
    {
        return;
    }

    if( aOptions.m_FocusNet.IsEmpty() )
        aOptions.m_FocusNet = net;

    if( aOptions.m_FocusLayer.IsEmpty() )
        aOptions.m_FocusLayer = layer;

    if( !aOptions.m_HasExplicitDimUnfocusedLayers )
        aOptions.m_DimUnfocusedLayers = true;

    if( !aOptions.m_HasExplicitHighlightAnchorIds )
    {
        appendDefaultHighlightAnchors( aRequest.m_ContextSnapshot,
                                       isRoutingVisualAnchorKind,
                                       aOptions.m_HighlightAnchorIds );
    }
}


void applyPlacementVisualDefaults( const AI_PROVIDER_REQUEST& aRequest,
                                   VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    if( !placementToolStateActive( aRequest ) )
        return;

    if( !aOptions.m_HasExplicitHighlightAnchorIds )
    {
        appendDefaultHighlightAnchors( aRequest.m_ContextSnapshot,
                                       isPlacementVisualAnchorKind,
                                       aOptions.m_HighlightAnchorIds );
    }
}


void applyVisualDefaults( const AI_PROVIDER_REQUEST& aRequest,
                          VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    applyRoutingVisualDefaults( aRequest, aOptions );
    applyPlacementVisualDefaults( aRequest, aOptions );
}


size_t visualAnchorOverlayCount( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    size_t count = 0;

    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( anchor.m_HasPosition )
            ++count;
    }

    return count;
}


nlohmann::json anchorOverlayJson( const AI_CONTEXT_ANCHOR& aAnchor )
{
    return { { "id", toUtf8String( aAnchor.m_Id ) },
             { "kind", toUtf8String( aAnchor.KindAsString() ) },
             { "label", toUtf8String( aAnchor.m_Label ) },
             { "summary", toUtf8String( aAnchor.m_Summary ) },
             { "position",
               { { "x", aAnchor.m_Position.x },
                 { "y", aAnchor.m_Position.y } } },
             { "layer", aAnchor.m_Layer },
             { "confidence", aAnchor.m_Confidence } };
}


nlohmann::json anchorOverlaysJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                   size_t aMaxAnchorOverlays )
{
    nlohmann::json overlays = nlohmann::json::array();

    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( !anchor.m_HasPosition )
            continue;

        if( overlays.size() >= aMaxAnchorOverlays )
            break;

        overlays.push_back( anchorOverlayJson( anchor ) );
    }

    return overlays;
}


nlohmann::json renderDirectivesJson( const VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    nlohmann::json directives = nlohmann::json::object();

    if( !aOptions.m_FocusLayer.IsEmpty() )
        directives["focus_layer"] = toUtf8String( aOptions.m_FocusLayer );

    if( !aOptions.m_FocusNet.IsEmpty() )
        directives["focus_net"] = toUtf8String( aOptions.m_FocusNet );

    if( aOptions.m_DimUnfocusedLayers )
        directives["dim_unfocused_layers"] = true;

    if( !aOptions.m_HighlightAnchorIds.empty() )
    {
        nlohmann::json anchorIds = nlohmann::json::array();

        for( const wxString& anchorId : aOptions.m_HighlightAnchorIds )
            anchorIds.push_back( toUtf8String( anchorId ) );

        directives["highlight_anchor_ids"] = std::move( anchorIds );
    }

    return directives;
}


nlohmann::json visualFrameJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                const VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    const AI_VISUAL_SNAPSHOT& aVisual = aSnapshot.m_Visual;
    nlohmann::json visual = { { "source", toUtf8String( aVisual.m_Source ) },
                              { "mime_type", toUtf8String( aVisual.m_MimeType ) },
                              { "width_px", aVisual.m_WidthPx },
                              { "height_px", aVisual.m_HeightPx },
                              { "byte_size", aVisual.m_ByteSize },
                              { "has_pixels", aVisual.HasPixels() },
                              { "anchor_overlay_count",
                                visualAnchorOverlayCount( aSnapshot ) } };

    if( !aVisual.m_UnavailableReason.IsEmpty() )
        visual["unavailable_reason"] = toUtf8String( aVisual.m_UnavailableReason );

    if( aOptions.m_IncludeAnchorOverlays )
    {
        visual["anchor_overlays"] =
                anchorOverlaysJson( aSnapshot, aOptions.m_MaxAnchorOverlays );
    }

    if( aOptions.HasRenderDirectives() )
        visual["render_directives"] = renderDirectivesJson( aOptions );

    if( aOptions.m_IncludePixels && aVisual.HasPixels() )
        visual["data_uri"] = toUtf8String( aVisual.m_DataUri );

    return visual;
}


struct ACTIVITY_TIMELINE_TOOL_OPTIONS
{
    size_t                         m_MaxActivity = 64;
    std::optional<AI_ACTIVITY_KIND> m_Kind;
    wxString                       m_ActionContains;
};


std::string activityKindName( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::UserAction:
        return "user_action";

    case AI_ACTIVITY_KIND::ModelToolRequest:
        return "model_tool_request";

    case AI_ACTIVITY_KIND::PolicyDecision:
        return "policy_decision";

    case AI_ACTIVITY_KIND::ToolResult:
        return "tool_result";

    default:
        return "user_action";
    }
}


std::string editorKindName( AI_EDITOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return "pcb";

    case AI_EDITOR_KIND::Schematic:
        return "schematic";

    default:
        return "unknown";
    }
}


std::optional<AI_ACTIVITY_KIND> parseActivityKind( const nlohmann::json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string kind = aValue.get_ref<const std::string&>();

    if( kind == "user_action" )
        return AI_ACTIVITY_KIND::UserAction;

    if( kind == "model_tool_request" )
        return AI_ACTIVITY_KIND::ModelToolRequest;

    if( kind == "policy_decision" )
        return AI_ACTIVITY_KIND::PolicyDecision;

    if( kind == "tool_result" )
        return AI_ACTIVITY_KIND::ToolResult;

    return std::nullopt;
}


std::optional<ACTIVITY_TIMELINE_TOOL_OPTIONS> parseActivityTimelineToolOptions(
        const nlohmann::json& aArgs, wxString& aError )
{
    ACTIVITY_TIMELINE_TOOL_OPTIONS options;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( it.key() != "max_activity" && it.key() != "kind"
            && it.key() != "action_contains" )
        {
            aError = wxS( "get_activity_timeline received an unknown argument." );
            return std::nullopt;
        }
    }

    if( !jsonLimitField( aArgs, "max_activity", 128, options.m_MaxActivity ) )
    {
        aError = wxS( "get_activity_timeline max_activity must be a positive integer." );
        return std::nullopt;
    }

    if( aArgs.contains( "kind" ) )
    {
        options.m_Kind = parseActivityKind( aArgs["kind"] );

        if( !options.m_Kind )
        {
            aError = wxS( "get_activity_timeline kind is not supported." );
            return std::nullopt;
        }
    }

    if( aArgs.contains( "action_contains" )
        && !jsonStringValue( aArgs["action_contains"], options.m_ActionContains ) )
    {
        aError = wxS( "get_activity_timeline action_contains must be a non-empty string." );
        return std::nullopt;
    }

    return options;
}


bool activityMatches( const AI_ACTIVITY_RECORD& aRecord,
                      const ACTIVITY_TIMELINE_TOOL_OPTIONS& aOptions )
{
    if( aOptions.m_Kind && aRecord.m_Kind != *aOptions.m_Kind )
        return false;

    if( !aOptions.m_ActionContains.IsEmpty()
        && aRecord.m_ActionName.Find( aOptions.m_ActionContains ) == wxNOT_FOUND )
    {
        return false;
    }

    return true;
}


nlohmann::json activityRecordJson( const AI_ACTIVITY_RECORD& aRecord )
{
    return { { "sequence", aRecord.m_Sequence },
             { "request_id", aRecord.m_RequestId },
             { "tool_call_id", toUtf8String( aRecord.m_ToolCallId ) },
             { "kind", activityKindName( aRecord.m_Kind ) },
             { "editor", editorKindName( aRecord.m_EditorKind ) },
             { "action", toUtf8String( aRecord.m_ActionName ) },
             { "arguments_json", toUtf8String( aRecord.m_ArgumentsJson ) },
             { "result_json", toUtf8String( aRecord.m_ResultJson ) },
             { "error_code", toUtf8String( aRecord.m_ErrorCode ) },
             { "allowed", aRecord.m_Allowed },
             { "executed", aRecord.m_Executed },
             { "message", toUtf8String( aRecord.m_Message ) } };
}


nlohmann::json activityTimelineJson( const std::vector<AI_ACTIVITY_RECORD>& aActivity,
                                     const ACTIVITY_TIMELINE_TOOL_OPTIONS& aOptions )
{
    nlohmann::json activity = nlohmann::json::array();
    size_t         filteredCount = 0;

    for( const AI_ACTIVITY_RECORD& record : aActivity )
    {
        if( !activityMatches( record, aOptions ) )
            continue;

        ++filteredCount;

        if( activity.size() < aOptions.m_MaxActivity )
            activity.push_back( activityRecordJson( record ) );
    }

    return { { "activity_count", filteredCount },
             { "records", std::move( activity ) } };
}


struct PANEL_STATE_TOOL_OPTIONS
{
    size_t   m_MaxPanels = 16;
    wxString m_PanelId;
    bool     m_FocusedOnly = false;
    bool     m_IncludeState = true;
};


bool isPanelStateToolField( const std::string& aField )
{
    return aField == "max_panels" || aField == "panel_id"
           || aField == "focused_only" || aField == "include_state";
}


std::optional<PANEL_STATE_TOOL_OPTIONS> parsePanelStateToolOptions(
        const nlohmann::json& aArgs, wxString& aError )
{
    PANEL_STATE_TOOL_OPTIONS options;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( !isPanelStateToolField( it.key() ) )
        {
            aError = wxS( "get_workspace_view panels received an unknown argument." );
            return std::nullopt;
        }
    }

    if( !jsonLimitField( aArgs, "max_panels", 32, options.m_MaxPanels ) )
    {
        aError = wxS( "get_workspace_view panels max_panels must be a positive integer." );
        return std::nullopt;
    }

    if( aArgs.contains( "panel_id" )
        && !jsonStringValue( aArgs["panel_id"], options.m_PanelId ) )
    {
        aError = wxS( "get_workspace_view panels panel_id must be a non-empty string." );
        return std::nullopt;
    }

    if( !jsonBooleanField( aArgs, "focused_only", options.m_FocusedOnly ) )
    {
        aError = wxS( "get_workspace_view panels focused_only must be a boolean." );
        return std::nullopt;
    }

    if( !jsonBooleanField( aArgs, "include_state", options.m_IncludeState ) )
    {
        aError = wxS( "get_workspace_view panels include_state must be a boolean." );
        return std::nullopt;
    }

    return options;
}


bool panelStateMatches( const AI_PANEL_STATE_RECORD& aPanel,
                        const PANEL_STATE_TOOL_OPTIONS& aOptions )
{
    if( !aOptions.m_PanelId.IsEmpty() && aPanel.m_Id != aOptions.m_PanelId )
        return false;

    if( aOptions.m_FocusedOnly && aPanel.m_FocusedControlId.IsEmpty() )
        return false;

    return true;
}


nlohmann::json parsePanelStateJsonOrRaw( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( !parsed.is_discarded() )
        return parsed;

    return { { "raw", toUtf8String( aText ) } };
}


nlohmann::json panelStateRecordJson( const AI_PANEL_STATE_RECORD& aPanel,
                                     bool aIncludeState )
{
    nlohmann::json record = {
        { "id", toUtf8String( aPanel.m_Id ) },
        { "title", toUtf8String( aPanel.m_Title ) },
        { "focused_control_id", toUtf8String( aPanel.m_FocusedControlId ) },
        { "focused_control_label", toUtf8String( aPanel.m_FocusedControlLabel ) },
        { "selected_text", toUtf8String( aPanel.m_SelectedText ) },
        { "summary", toUtf8String( aPanel.m_Summary ) }
    };

    if( aIncludeState )
    {
        nlohmann::json state = parsePanelStateJsonOrRaw( aPanel.m_StateJson );

        if( state.contains( "raw" ) )
            record["state_raw"] = state["raw"];
        else
            record["state"] = std::move( state );
    }

    return record;
}


nlohmann::json panelStatesViewJson(
        const std::vector<AI_PANEL_STATE_RECORD>& aPanelStates,
        const PANEL_STATE_TOOL_OPTIONS& aOptions )
{
    nlohmann::json records = nlohmann::json::array();
    size_t         matchedCount = 0;

    for( const AI_PANEL_STATE_RECORD& panel : aPanelStates )
    {
        if( !panelStateMatches( panel, aOptions ) )
            continue;

        ++matchedCount;

        if( records.size() < aOptions.m_MaxPanels )
            records.push_back( panelStateRecordJson( panel, aOptions.m_IncludeState ) );
    }

    return { { "panel_state_count", aPanelStates.size() },
             { "matched_panel_count", matchedCount },
             { "records", std::move( records ) } };
}


struct WORKSPACE_VIEW_TOOL_OPTIONS
{
    bool                           m_Context = true;
    bool                           m_Visual = true;
    bool                           m_Activity = true;
    bool                           m_Panels = true;
    CONTEXT_TOOL_OPTIONS           m_ContextOptions;
    VISUAL_FRAME_TOOL_OPTIONS      m_VisualOptions;
    ACTIVITY_TIMELINE_TOOL_OPTIONS m_ActivityOptions;
    PANEL_STATE_TOOL_OPTIONS       m_PanelOptions;
};


bool isWorkspaceViewField( const std::string& aField )
{
    return aField == "views" || aField == "context"
           || aField == "visual" || aField == "activity"
           || aField == "panels";
}


std::optional<WORKSPACE_VIEW_TOOL_OPTIONS> parseWorkspaceViewToolOptions(
        const nlohmann::json& aArgs, wxString& aError )
{
    WORKSPACE_VIEW_TOOL_OPTIONS options;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( !isWorkspaceViewField( it.key() ) )
        {
            aError = wxS( "get_workspace_view received an unknown argument." );
            return std::nullopt;
        }
    }

    if( aArgs.contains( "views" ) )
    {
        if( !aArgs["views"].is_array() )
        {
            aError = wxS( "get_workspace_view views must be an array." );
            return std::nullopt;
        }

        options.m_Context = false;
        options.m_Visual = false;
        options.m_Activity = false;
        options.m_Panels = false;

        for( const nlohmann::json& view : aArgs["views"] )
        {
            if( !view.is_string() )
            {
                aError = wxS( "get_workspace_view views must contain strings." );
                return std::nullopt;
            }

            const std::string name = view.get_ref<const std::string&>();

            if( name == "context" )
                options.m_Context = true;
            else if( name == "visual" )
                options.m_Visual = true;
            else if( name == "activity" )
                options.m_Activity = true;
            else if( name == "panels" )
                options.m_Panels = true;
            else
            {
                aError = wxS( "get_workspace_view view is not supported." );
                return std::nullopt;
            }
        }
    }

    if( aArgs.contains( "context" ) )
    {
        if( !aArgs["context"].is_object() )
        {
            aError = wxS( "get_workspace_view context options must be an object." );
            return std::nullopt;
        }

        std::optional<CONTEXT_TOOL_OPTIONS> context =
                parseContextToolOptions( aArgs["context"], aError );

        if( !context )
            return std::nullopt;

        options.m_ContextOptions = *context;
    }

    if( aArgs.contains( "visual" ) )
    {
        if( !aArgs["visual"].is_object() )
        {
            aError = wxS( "get_workspace_view visual options must be an object." );
            return std::nullopt;
        }

        std::optional<VISUAL_FRAME_TOOL_OPTIONS> visual =
                parseVisualFrameToolOptions( aArgs["visual"], aError );

        if( !visual )
            return std::nullopt;

        options.m_VisualOptions = *visual;
    }

    if( aArgs.contains( "activity" ) )
    {
        if( !aArgs["activity"].is_object() )
        {
            aError = wxS( "get_workspace_view activity options must be an object." );
            return std::nullopt;
        }

        std::optional<ACTIVITY_TIMELINE_TOOL_OPTIONS> activity =
                parseActivityTimelineToolOptions( aArgs["activity"], aError );

        if( !activity )
            return std::nullopt;

        options.m_ActivityOptions = *activity;
    }

    if( aArgs.contains( "panels" ) )
    {
        if( !aArgs["panels"].is_object() )
        {
            aError = wxS( "get_workspace_view panels options must be an object." );
            return std::nullopt;
        }

        std::optional<PANEL_STATE_TOOL_OPTIONS> panels =
                parsePanelStateToolOptions( aArgs["panels"], aError );

        if( !panels )
            return std::nullopt;

        options.m_PanelOptions = *panels;
    }

    return options;
}


nlohmann::json includedWorkspaceViewsJson( const WORKSPACE_VIEW_TOOL_OPTIONS& aOptions )
{
    nlohmann::json views = nlohmann::json::array();

    if( aOptions.m_Context )
        views.push_back( "context" );

    if( aOptions.m_Visual )
        views.push_back( "visual" );

    if( aOptions.m_Activity )
        views.push_back( "activity" );

    if( aOptions.m_Panels )
        views.push_back( "panels" );

    return views;
}


nlohmann::json workspaceViewSummaryJson(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        const WORKSPACE_VIEW_TOOL_OPTIONS& aOptions )
{
    nlohmann::json dynamicContext = nlohmann::json::parse(
            toUtf8String( AiDynamicContextDetailsJson(
                    aSnapshot, AiDynamicContextKind( aSnapshot ) ) ),
            nullptr, false );

    nlohmann::json summary = {
        { "included_views", includedWorkspaceViewsJson( aOptions ) },
        { "editor", editorKindName( aSnapshot.m_EditorKind ) },
        { "dynamic_context_kind", toUtf8String( AiDynamicContextKind( aSnapshot ) ) },
        { "tool_state_kind",
          toUtf8String( AiToolStateKindName( aSnapshot.m_ToolState.m_Kind ) ) },
        { "selected_object_count", aSnapshot.m_SelectedObjects.size() },
        { "visible_object_count", aSnapshot.m_VisibleObjects.size() },
        { "anchor_count", aSnapshot.m_Anchors.size() },
        { "panel_state_count", aSnapshot.m_PanelStates.size() },
        { "recent_activity_count", aSnapshot.m_RecentActivity.size() },
        { "visual_source", toUtf8String( aSnapshot.m_Visual.m_Source ) },
        { "visual_has_pixels", aSnapshot.m_Visual.HasPixels() }
    };

    if( dynamicContext.is_object() && dynamicContext.contains( "source" )
        && dynamicContext["source"].is_string() )
    {
        summary["dynamic_context_source"] = dynamicContext["source"];
    }
    else
    {
        summary["dynamic_context_source"] = "unknown";
    }

    return summary;
}


AI_TOOL_INVOCATION_RESULT workspaceViewResult( const AI_PROVIDER_REQUEST& aRequest,
                                               const AI_TOOL_CALL_RECORD& aToolCall,
                                               const nlohmann::json& aArgs )
{
    wxString error;
    std::optional<WORKSPACE_VIEW_TOOL_OPTIONS> options =
            parseWorkspaceViewToolOptions( aArgs, error );

    if( !options )
        return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ), error );

    if( options->m_Visual )
        applyVisualDefaults( aRequest, options->m_VisualOptions );

    if( options->m_Visual
        && !validateVisualFrameToolOptions( aRequest.m_ContextSnapshot,
                                            options->m_VisualOptions, error ) )
    {
        return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ), error );
    }

    const AI_VISUAL_SNAPSHOT& visual = aRequest.m_ContextSnapshot.m_Visual;

    if( options->m_Visual && options->m_VisualOptions.m_IncludePixels
        && visual.HasPixels() && visual.m_ByteSize > options->m_VisualOptions.m_MaxBytes )
    {
        return deniedResult(
                aRequest, aToolCall, wxS( "visual_too_large" ),
                wxS( "Visual frame pixel payload exceeds max_bytes." ) );
    }

    nlohmann::json workspaceView = nlohmann::json::object();
    workspaceView["summary"] =
            workspaceViewSummaryJson( aRequest.m_ContextSnapshot, *options );

    if( options->m_Context )
    {
        workspaceView["context"] =
                contextSnapshotJson( aRequest.m_ContextSnapshot,
                                     options->m_ContextOptions );
    }

    if( options->m_Visual )
    {
        workspaceView["visual"] =
                visualFrameJson( aRequest.m_ContextSnapshot,
                                 options->m_VisualOptions );
    }

    if( options->m_Activity )
    {
        workspaceView["activity"] =
                activityTimelineJson( aRequest.m_ContextSnapshot.m_RecentActivity,
                                      options->m_ActivityOptions );
    }

    if( options->m_Panels )
    {
        workspaceView["panels"] =
                panelStatesViewJson( aRequest.m_ContextSnapshot.m_PanelStates,
                                     options->m_PanelOptions );
    }

    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest, aToolCall );
    result.m_Allowed = true;
    result.m_Executed = false;
    result.m_Message = wxS( "Workspace view ready." );

    nlohmann::json payload = { { "tool", toUtf8String( result.m_ActionName ) },
                               { "allowed", true },
                               { "executed", false },
                               { "ok", true },
                               { "status", "workspace_view_ready" },
                               { "workspace_view", std::move( workspaceView ) },
                               { "message", toUtf8String( result.m_Message ) } };

    result.m_ResultJson = fromJson( payload );
    return result;
}


bool isSemanticUiActionField( const std::string& aField )
{
    return aField == "node_id" || aField == "action" || aField == "text"
           || aField == "checked";
}


std::optional<AI_SEMANTIC_UI_ACTION_REQUEST> parseSemanticUiActionRequest(
        const nlohmann::json& aArgs, wxString& aError )
{
    AI_SEMANTIC_UI_ACTION_REQUEST request;

    for( auto it = aArgs.begin(); it != aArgs.end(); ++it )
    {
        if( !isSemanticUiActionField( it.key() ) )
        {
            aError = wxS( "semantic_ui_action received an unknown argument." );
            return std::nullopt;
        }
    }

    if( !jsonStringField( aArgs, "node_id", request.m_NodeId )
        || !jsonStringField( aArgs, "action", request.m_Action ) )
    {
        aError = wxS( "semantic_ui_action requires non-empty node_id and action." );
        return std::nullopt;
    }

    if( aArgs.contains( "text" ) )
    {
        if( !aArgs["text"].is_string() )
        {
            aError = wxS( "semantic_ui_action text must be a string." );
            return std::nullopt;
        }

        request.m_HasText = true;
        request.m_Text = wxString::FromUTF8(
                aArgs["text"].get_ref<const std::string&>().c_str() );
    }

    bool checked = false;

    if( !jsonBooleanField( aArgs, "checked", checked ) )
    {
        aError = wxS( "semantic_ui_action checked must be a boolean." );
        return std::nullopt;
    }

    if( aArgs.contains( "checked" ) )
        request.m_Checked = checked;

    request.m_UserConfirmed = false;
    return request;
}


wxString semanticUiActionResultJson(
        const AI_TOOL_INVOCATION_RESULT& aResult,
        const AI_SEMANTIC_UI_ACTION_REQUEST& aActionRequest,
        const AI_SEMANTIC_UI_ACTION_RESULT& aActionResult,
        const char* aStatus )
{
    nlohmann::json payload = {
        { "tool", toUtf8String( aResult.m_ActionName ) },
        { "allowed", aResult.m_Allowed },
        { "executed", aResult.m_Executed },
        { "ok", aResult.m_Allowed && aResult.m_Executed
                  && aResult.m_ErrorCode.IsEmpty() },
        { "status", aStatus },
        { "node_id", toUtf8String( aActionRequest.m_NodeId ) },
        { "action", toUtf8String( aActionRequest.m_Action ) },
        { "error_code", toUtf8String( aResult.m_ErrorCode ) },
        { "message", toUtf8String( aResult.m_Message ) }
    };

    if( !aActionResult.m_FocusedNodeId.IsEmpty() )
        payload["focused_node_id"] = toUtf8String( aActionResult.m_FocusedNodeId );

    return fromJson( payload );
}


AI_TOOL_INVOCATION_RESULT semanticUiActionResult(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_TOOL_CALL_RECORD& aToolCall,
        const AI_SEMANTIC_UI_ACTION_REQUEST& aActionRequest,
        const AI_SEMANTIC_UI_ACTION_RESULT& aActionResult )
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest, aToolCall );
    result.m_Allowed = true;
    result.m_Executed = aActionResult.m_Success;
    result.m_ErrorCode = aActionResult.m_Success
                                 ? wxString()
                                 : ( aActionResult.m_ErrorCode.IsEmpty()
                                             ? wxString( wxS( "action_failed" ) )
                                             : aActionResult.m_ErrorCode );

    if( aActionResult.m_Message.IsEmpty() )
    {
        result.m_Message = aActionResult.m_Success
                                   ? wxString( wxS( "Semantic UI action executed." ) )
                                   : wxString( wxS( "Semantic UI action failed." ) );
    }
    else
    {
        result.m_Message = RedactSemanticUiText( aActionResult.m_Message );
    }

    result.m_ResultJson = semanticUiActionResultJson(
            result, aActionRequest, aActionResult,
            aActionResult.m_Success ? "ui_action_executed" : "ui_action_failed" );
    return result;
}


} // namespace


AI_SEMANTIC_TOOL_CALL_HANDLER::AI_SEMANTIC_TOOL_CALL_HANDLER(
        AI_SEMANTIC_UI_TREE_PROVIDER aSemanticUiTreeProvider,
        AI_SEMANTIC_UI_ACTION_INVOKER aSemanticUiActionInvoker ) :
        m_SemanticUiTreeProvider( std::move( aSemanticUiTreeProvider ) ),
        m_SemanticUiActionInvoker( std::move( aSemanticUiActionInvoker ) )
{
}


AI_TOOL_INVOCATION_RESULT AI_SEMANTIC_TOOL_CALL_HANDLER::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !supportedTool( aToolCall.m_ToolName ) )
    {
        return deniedResult( aRequest, aToolCall, wxS( "unknown_tool" ),
                             wxS( "Unsupported KiSurf semantic tool call." ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_get_workspace_view" ) )
    {
        wxString parseError;
        std::optional<nlohmann::json> args = parseObjectArguments( aToolCall, parseError );

        if( !args )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 parseError );
        }

        return workspaceViewResult( aRequest, aToolCall, *args );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_invoke_semantic_ui_action" ) )
    {
        if( !m_SemanticUiTreeProvider || !m_SemanticUiActionInvoker )
        {
            return deniedResult( aRequest, aToolCall, wxS( "handler_not_configured" ),
                                 wxS( "No semantic UI action bridge is installed." ) );
        }

        wxString parseError;
        std::optional<nlohmann::json> args = parseObjectArguments( aToolCall, parseError );

        if( !args )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 parseError );
        }

        std::optional<AI_SEMANTIC_UI_ACTION_REQUEST> actionRequest =
                parseSemanticUiActionRequest( *args, parseError );

        if( !actionRequest )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 parseError );
        }

        const AI_SEMANTIC_UI_TREE tree = m_SemanticUiTreeProvider();
        const AI_SEMANTIC_UI_NODE* node = tree.FindNode( actionRequest->m_NodeId );

        if( !node )
        {
            return deniedResult( aRequest, aToolCall, wxS( "unknown_node" ),
                                 wxS( "semantic_ui_action node_id was not present in "
                                      "the current semantic UI tree." ) );
        }

        if( !node->m_Enabled )
        {
            return deniedResult( aRequest, aToolCall, wxS( "disabled_node" ),
                                 wxS( "semantic_ui_action target node is disabled." ) );
        }

        if( node->m_RequiresUserConfirmation )
        {
            return deniedResult( aRequest, aToolCall,
                                 wxS( "confirmation_required" ),
                                 wxS( "semantic_ui_action target node requires direct "
                                      "user confirmation." ) );
        }

        if( !node->m_ActionName.IsEmpty()
            && node->m_ActionName != actionRequest->m_Action )
        {
            return deniedResult( aRequest, aToolCall, wxS( "unsupported_action" ),
                                 wxS( "semantic_ui_action action does not match the "
                                      "current node action." ) );
        }

        actionRequest->m_UserConfirmed = false;
        AI_SEMANTIC_UI_ACTION_RESULT actionResult =
                m_SemanticUiActionInvoker( *actionRequest );

        return semanticUiActionResult( aRequest, aToolCall, *actionRequest,
                                       actionResult );
    }

    return deniedResult( aRequest, aToolCall, wxS( "unknown_tool" ),
                         wxS( "Unsupported KiSurf semantic tool call." ) );
}


void AI_TOOL_CALL_DISPATCHER::AddHandler( std::unique_ptr<AI_TOOL_CALL_HANDLER> aHandler )
{
    if( aHandler )
        m_Handlers.push_back( std::move( aHandler ) );
}


AI_TOOL_INVOCATION_RESULT AI_TOOL_CALL_DISPATCHER::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOOL_CALL_RECORD& aToolCall )
{
    std::optional<AI_TOOL_INVOCATION_RESULT> lastUnknown;

    for( const std::unique_ptr<AI_TOOL_CALL_HANDLER>& handler : m_Handlers )
    {
        AI_TOOL_INVOCATION_RESULT result = handler->HandleToolCall( aRequest, aToolCall );

        if( result.m_ErrorCode != wxS( "unknown_tool" ) )
            return result;

        lastUnknown = result;
    }

    if( lastUnknown )
        return *lastUnknown;

    return deniedResult( aRequest, aToolCall, wxS( "unknown_tool" ),
                         wxS( "No KiSurf tool handler accepted this call." ) );
}
