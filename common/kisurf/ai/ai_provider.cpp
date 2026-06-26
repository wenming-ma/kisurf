#include <kisurf/ai/ai_provider.h>

#include <curl/curl.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <nlohmann/json.hpp>

#include <utility>
#include <wx/utils.h>

namespace
{
wxString normalizedUrl( const wxString& aUrl )
{
    wxString url = aUrl;
    url.Trim( true ).Trim( false );

    while( url.EndsWith( wxS( "/" ) ) )
        url.RemoveLast();

    return url;
}


wxString joinUrl( const wxString& aBaseUrl, const wxString& aPath )
{
    wxString base = normalizedUrl( aBaseUrl );

    if( aPath.StartsWith( wxS( "/" ) ) )
        return base + aPath;

    return base + wxS( "/" ) + aPath;
}


bool envValue( const wxString& aName, wxString& aValue )
{
    if( wxGetEnv( aName, &aValue ) )
    {
        aValue.Trim( true ).Trim( false );
        return !aValue.IsEmpty();
    }

    return false;
}


bool defaultHttpHandler( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                         wxString& aError )
{
    KICAD_CURL_EASY curl;
    curl.SetUserAgent( "KiSurf-AI/1.0" );
    curl.SetFollowRedirects( true );
    curl.SetConnectTimeout( 20 );

    if( !curl.SetURL( aRequest.m_Url.ToStdString() ) )
    {
        aError = wxString::Format( wxS( "Unable to set URL '%s'." ), aRequest.m_Url );
        return false;
    }

    for( const AI_HTTP_HEADER& header : aRequest.m_Headers )
        curl.SetHeader( header.m_Name.ToStdString(), header.m_Value.ToStdString() );

    if( aRequest.m_Method == wxS( "POST" ) )
        curl.SetPostFields( aRequest.m_Body.ToStdString() );

    const int result = curl.Perform();

    if( result != CURLE_OK )
    {
        aError = wxString::FromUTF8( curl.GetErrorText( result ).c_str() );
        return false;
    }

    aResponse.m_StatusCode = curl.GetResponseStatusCode();
    aResponse.m_Body = wxString::FromUTF8( curl.GetBuffer().c_str() );
    return true;
}


AI_PROVIDER_RESPONSE providerError( const AI_PROVIDER_REQUEST& aRequest, const wxString& aMessage )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "AI Provider" );
    response.m_Body = aMessage;
    return response;
}


class UNSUPPORTED_MODEL_PROVIDER : public AI_PROVIDER
{
public:
    explicit UNSUPPORTED_MODEL_PROVIDER( wxString aProviderLabel ) :
            m_ProviderLabel( std::move( aProviderLabel ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        return providerError(
                aRequest,
                wxString::Format( wxS( "%s providers are configurable in Model Settings, "
                                        "but this runtime is not implemented yet." ),
                                  m_ProviderLabel ) );
    }

private:
    wxString m_ProviderLabel;
};


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


nlohmann::json makeUserMessageContent( const wxString& aUserContent,
                                       const AI_VISUAL_SNAPSHOT& aVisual )
{
    if( !aVisual.HasPixels() )
        return toUtf8String( aUserContent );

    return nlohmann::json::array(
            { { { "type", "text" }, { "text", toUtf8String( aUserContent ) } },
              { { "type", "image_url" },
                { "image_url", { { "url", toUtf8String( aVisual.m_DataUri ) } } } } } );
}


nlohmann::json actionToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "action",
                   { { "type", "string" },
                     { "description",
                       "Native KiCad/KiSurf action name from the current action catalog." } } },
                 { "arguments",
                   { { "type", "object" },
                     { "description", "Optional action-specific arguments." },
                     { "additionalProperties", true } } },
                 { "dry_run",
                   { { "type", "boolean" },
                     { "description",
                       "When true, check policy and preview feasibility without executing." } } } } },
             { "required", nlohmann::json::array( { "action" } ) },
             { "additionalProperties", false } };
}


nlohmann::json sessionOpenToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "board_id",
                   { { "type", "string" },
                     { "description",
                       "Optional stable board id. When omitted KiSurf uses the active "
                       "PCB editor board." } } },
                 { "base_hash",
                   { { "type", "string" },
                     { "description",
                       "Optional live-board base hash. When omitted KiSurf derives it "
                       "from the active editor context." } } },
                 { "editor_context",
                   { { "type", "object" },
                     { "description",
                       "Optional bounded editor context override for the session." },
                     { "additionalProperties", true } } } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionEmptyToolParameters()
{
    return { { "type", "object" },
             { "properties", nlohmann::json::object() },
             { "additionalProperties", false } };
}


nlohmann::json sessionRunCellToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "cell_text",
                   { { "type", "string" },
                     { "description",
                       "Python session cell. The cell uses the KiSurf SDK and cannot "
                       "access raw BOARD or BOARD_ITEM pointers." } } },
                 { "cell_id",
                   { { "type", "string" },
                     { "description", "Optional caller supplied stable cell id." } } } } },
             { "required", nlohmann::json::array( { "cell_text" } ) },
             { "additionalProperties", false } };
}


const char* sessionRunCellToolDescription()
{
    return "Run a Python-first KiSurf session cell. Python can only mutate "
           "the board through typed session SDK operations; it cannot access "
           "raw BOARD or BOARD_ITEM pointers and it cannot publish directly. "
           "Available atomic operation ids include: pcb.create_via, "
           "pcb.create_track_segment, pcb.create_track_polyline, "
           "pcb.create_zone, pcb.create_shape, pcb.move_items, "
           "pcb.delete_items, pcb.update_item_geometry, pcb.set_item_net, "
           "pcb.set_item_layer, pcb.set_item_properties, pcb.set_metadata, "
           "pcb.refill_zones, pcb.rebuild_connectivity, pcb.run_validation, "
           "surface.apply_patch.";
}


nlohmann::json sessionAtomicOperationIdsJson()
{
    return nlohmann::json::array(
            { "pcb.create_via",
              "pcb.create_track_segment",
              "pcb.create_track_polyline",
              "pcb.create_zone",
              "pcb.create_shape",
              "pcb.move_items",
              "pcb.delete_items",
              "pcb.update_item_geometry",
              "pcb.set_item_net",
              "pcb.set_item_layer",
              "pcb.set_item_properties",
              "pcb.set_metadata",
              "pcb.refill_zones",
              "pcb.rebuild_connectivity",
              "pcb.run_validation",
              "surface.apply_patch" } );
}


nlohmann::json internalPointSchema( const char* aDescription )
{
    return { { "type", "object" },
             { "description", aDescription },
             { "additionalProperties", false },
             { "properties",
               { { "x",
                   { { "type", "integer" },
                     { "description", "Internal-coordinate x value." } } },
                 { "y",
                   { { "type", "integer" },
                     { "description", "Internal-coordinate y value." } } } } },
             { "required", nlohmann::json::array( { "x", "y" } ) } };
}


nlohmann::json internalPointArraySchema( const char* aDescription, int aMinItems )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", internalPointSchema( "Internal-coordinate x/y point." ) },
             { "minItems", aMinItems } };
}


nlohmann::json internalBoxSchema( const char* aDescription )
{
    return { { "description", aDescription },
             { "anyOf",
               nlohmann::json::array(
                       { { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "x", { { "type", "integer" } } },
                               { "y", { { "type", "integer" } } },
                               { "width",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "height",
                                 { { "type", "integer" }, { "minimum", 1 } } } } },
                           { "required",
                             nlohmann::json::array(
                                     { "x", "y", "width", "height" } ) } },
                         { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "min",
                                 internalPointSchema( "Minimum box corner." ) },
                               { "max",
                                 internalPointSchema( "Maximum box corner." ) } } },
                           { "required",
                             nlohmann::json::array( { "min", "max" } ) } } } ) } };
}


nlohmann::json geometryPatchSchema()
{
    nlohmann::json schema = { { "type", "object" },
                              { "additionalProperties", true },
                              { "description",
                                "Partial shape geometry patch. Segment/rectangle use "
                                "start/end; circle uses center/radius; arc uses "
                                "start/mid/end; polygon uses points." } };

    schema["properties"] = {
        { "start", internalPointSchema( "Patched start point." ) },
        { "end", internalPointSchema( "Patched end point." ) },
        { "center", internalPointSchema( "Patched circle center." ) },
        { "mid", internalPointSchema( "Patched arc midpoint." ) },
        { "radius", { { "type", "integer" }, { "minimum", 1 } } },
        { "points",
          internalPointArraySchema(
                  "Patched polygon outline points using internal coordinates.", 3 ) }
    };

    return schema;
}


nlohmann::json zoneOutlineSchema()
{
    nlohmann::json schema = { { "type", "object" },
                              { "additionalProperties", true },
                              { "description",
                                "Zone outline geometry. Use points for the ordered "
                                "polygon outline in internal coordinates." } };

    schema["properties"] = {
        { "points",
          internalPointArraySchema(
                  "Zone polygon outline points using internal coordinates.", 3 ) }
    };

    return schema;
}


nlohmann::json typedPropertiesSchema()
{
    nlohmann::json schema = {
        { "type", "object" },
        { "additionalProperties", true },
        { "description",
          "Typed property patch for existing PCB items. Supported fields are "
          "applied according to the target item type." }
    };

    schema["properties"] = {
        { "diameter", { { "type", "integer" }, { "minimum", 1 } } },
        { "drill", { { "type", "integer" }, { "minimum", 1 } } },
        { "width", { { "type", "integer" }, { "minimum", 0 } } },
        { "fill", { { "type", "boolean" } } },
        { "clearance", { { "type", "integer" }, { "minimum", 0 } } },
        { "priority", { { "type", "integer" }, { "minimum", 0 } } },
        { "fill_mode",
          { { "type", "string" },
            { "enum",
              nlohmann::json::array( { "solid", "hatch_pattern",
                                        "copper_thieving" } ) } } },
        { "reference", { { "type", "string" } } },
        { "value", { { "type", "string" } } },
        { "side", { { "type", "string" } } },
        { "orientation_degrees", { { "type", "number" } } }
    };

    return schema;
}


nlohmann::json handleRefSchema( const char* aDescription )
{
    return { { "description", aDescription },
             { "anyOf",
               nlohmann::json::array(
                       { { { "type", "string" },
                           { "description", "Session handle alias." } },
                         { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "session_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "handle_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "generation",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "alias", { { "type", "string" } } } } },
                           { "required",
                             nlohmann::json::array( { "handle_id" } ) } } } ) } };
}


nlohmann::json handleArraySchema( const char* aDescription )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", handleRefSchema( "Session handle or alias." ) },
             { "minItems", 1 } };
}


nlohmann::json stringArraySchema( const char* aDescription )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", { { "type", "string" } } },
             { "minItems", 1 } };
}


nlohmann::json atomicOperationContractSchemas()
{
    return {
        { "pcb.create_via",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "position", internalPointSchema( "Via center position." ) },
                { "net", { { "type", "string" } } },
                { "diameter", { { "type", "integer" }, { "minimum", 1 } } },
                { "drill", { { "type", "integer" }, { "minimum", 1 } } },
                { "layer_pair",
                  { { "description", "Via layer pair, usually [start, end]." },
                    { "anyOf",
                      nlohmann::json::array(
                              { stringArraySchema( "Layer-pair names." ),
                                { { "type", "object" },
                                  { "additionalProperties", true } } } ) } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "position" } ) } } },
        { "pcb.create_track_segment",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "start", internalPointSchema( "Track segment start point." ) },
                { "end", internalPointSchema( "Track segment end point." ) },
                { "layer", { { "type", "string" } } },
                { "net", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 1 } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "start", "end" } ) } } },
        { "pcb.create_track_polyline",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "points",
                  internalPointArraySchema(
                          "Ordered route polyline points using internal coordinates.",
                          2 ) },
                { "layer", { { "type", "string" } } },
                { "net", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 1 } } },
                { "alias", { { "type", "string" } } } } },
            { "required", nlohmann::json::array( { "points" } ) } } },
        { "pcb.create_zone",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "outline", zoneOutlineSchema() },
                { "layer_set", stringArraySchema( "Copper layers for the zone." ) },
                { "net", { { "type", "string" } } },
                { "clearance", { { "type", "number" }, { "minimum", 0 } } },
                { "priority", { { "type", "number" }, { "minimum", 0 } } },
                { "fill_mode",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "solid", "hatch_pattern",
                                                "copper_thieving" } ) } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "outline" } ) } } },
        { "pcb.create_shape",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "shape_type",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "segment", "line", "rectangle",
                                                "circle", "arc", "polygon",
                                                "poly" } ) },
                    { "description",
                      "Shape primitive to create. Polygon/poly use geometry.points." } } },
                { "geometry",
                  { { "type", "object" },
                    { "additionalProperties", true },
                    { "description",
                      "Shape geometry. Segment/rectangle use start/end; circle uses "
                      "center/radius; arc uses start/mid/end; polygon uses points." },
                    { "properties",
                      { { "start", internalPointSchema( "Shape start point." ) },
                        { "end", internalPointSchema( "Shape end point." ) },
                        { "center", internalPointSchema( "Circle center point." ) },
                        { "mid", internalPointSchema( "Arc midpoint." ) },
                        { "radius", { { "type", "integer" }, { "minimum", 1 } } },
                        { "points",
                          internalPointArraySchema(
                                  "Polygon outline points using internal coordinates.",
                                  3 ) } } } } },
                { "layer", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 0 } } },
                { "fill", { { "type", "boolean" } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "shape_type", "geometry" } ) } } },
        { "pcb.move_items",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handles", handleArraySchema( "Items to move." ) },
                { "delta", internalPointSchema( "Movement delta." ) },
                { "target_positions",
                  { { "description",
                      "Single target point, ordered target array, or alias/handle-id keyed target map." },
                    { "anyOf",
                      nlohmann::json::array(
                              { internalPointSchema( "Single target point." ),
                                { { "type", "array" },
                                  { "items",
                                    internalPointSchema( "Ordered target point." ) } },
                                { { "type", "object" },
                                  { "additionalProperties", true } } } ) } } } } },
            { "required", nlohmann::json::array( { "handles" } ) } } },
        { "pcb.delete_items",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties", { { "handles", handleArraySchema( "Items to delete." ) } } },
            { "required", nlohmann::json::array( { "handles" } ) } } },
        { "pcb.update_item_geometry",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handle", handleRefSchema( "Item to update." ) },
                { "geometry_patch", geometryPatchSchema() } } },
            { "required", nlohmann::json::array( { "handle", "geometry_patch" } ) } } },
        { "pcb.set_item_net",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", handleRefSchema( "Item to update." ) },
                { "net", { { "type", "string" } } } } },
            { "required", nlohmann::json::array( { "handle", "net" } ) } } },
        { "pcb.set_item_layer",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", handleRefSchema( "Item to update." ) },
                { "layer", { { "type", "string" } } },
                { "layer_set", stringArraySchema( "Layer set for multilayer items." ) } } },
            { "required", nlohmann::json::array( { "handle" } ) } } },
        { "pcb.set_item_properties",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", handleRefSchema( "Item to update." ) },
                { "typed_props", typedPropertiesSchema() } } },
            { "required", nlohmann::json::array( { "handle", "typed_props" } ) } } },
        { "pcb.set_metadata",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", handleRefSchema( "Item to update." ) },
                { "key_values",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "handle", "key_values" } ) } } },
        { "pcb.refill_zones",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handles", handleArraySchema( "Zone handles to refill." ) },
                { "affected_area",
                  internalBoxSchema(
                          "Zone refill area in internal coordinates." ) },
                { "all", { { "type", "boolean" } } } } } } },
        { "pcb.rebuild_connectivity",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "scope",
                  { { "description", "Connectivity rebuild scope." },
                    { "additionalProperties", true },
                    { "type", "object" } } } } } } },
        { "pcb.run_validation",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "scope",
                  { { "description", "Validation scope." },
                    { "additionalProperties", true },
                    { "type", "object" } } },
                { "level",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array(
                              { "geometry", "drc_lite", "full_drc" } ) } } } } } } },
        { "surface.apply_patch",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "surface_id", { { "type", "string" } } },
                { "table_id", { { "type", "string" } } },
                { "target_scope",
                  { { "type", "object" }, { "additionalProperties", true } } },
                { "patch", { { "type", "object" }, { "additionalProperties", true } } },
                { "write_policy",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array(
                              { "fill_empty_only", "allow_overwrite" } ) } } },
                { "expected_surface_revision",
                  { { "description", "Surface revision expected at accept time." } } },
                { "expected_schema_version",
                  { { "description", "Surface schema version expected at accept time." } } },
                { "expected_selection_fingerprint",
                  { { "description", "Focused selection fingerprint expected at accept time." } } },
                { "expected_overlap_set",
                  { { "description", "Protected overlap set expected at accept time." } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "surface_id", "patch" } ) } } }
    };
}


nlohmann::json sessionAtomicOperationToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "kind",
                   { { "type", "string" },
                     { "enum", sessionAtomicOperationIdsJson() },
                     { "description", "Typed KiSurf atomic operation id." } } },
                 { "arguments",
                   { { "type", "object" },
                     { "description",
                       "Operation-specific JSON arguments. Handles must be session "
                       "handles or aliases returned by prior session operations. "
                       "Per-kind argument contracts are published in "
                       "$defs.operation_contracts." },
                     { "additionalProperties", true } } } } },
             { "required", nlohmann::json::array( { "kind", "arguments" } ) },
             { "$defs",
               { { "operation_contracts", atomicOperationContractSchemas() } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionBeginStepToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "label",
                   { { "type", "string" },
                     { "description", "Short human-readable step label." } } },
                 { "options",
                   { { "type", "object" },
                     { "description",
                       "Optional step options such as validation level or preview mode." },
                     { "additionalProperties", true } } } } },
             { "required", nlohmann::json::array( { "label" } ) },
             { "additionalProperties", false } };
}


nlohmann::json sessionStepIdToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "step_id",
                   { { "type", "integer" },
                     { "minimum", 1 },
                     { "description", "AI execution session step id." } } } } },
             { "required", nlohmann::json::array( { "step_id" } ) },
             { "additionalProperties", false } };
}


nlohmann::json sessionQueryItemsToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "filter",
                   { { "type", "object" },
                     { "description",
                       "Optional semantic shadow-board filter: type, net, layer, "
                       "alias, bbox, selection, or handle." },
                     { "additionalProperties", true } } } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionQueryItemToolParameters()
{
    nlohmann::json handleSchema = {
        { "description",
          "Session handle object, handle id, or alias returned by a prior session step." },
        { "oneOf",
          nlohmann::json::array(
                  { { { "type", "integer" }, { "minimum", 1 } },
                    { { "type", "string" } },
                    { { "type", "object" }, { "additionalProperties", true } } } ) }
    };

    return { { "type", "object" },
             { "properties",
               { { "handle", handleSchema },
                 { "alias",
                   { { "type", "string" },
                     { "description", "Optional session item alias to resolve." } } } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionCheckpointToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "name",
                   { { "type", "string" },
                     { "description", "Checkpoint name for later rollback." } } } } },
             { "required", nlohmann::json::array( { "name" } ) },
             { "additionalProperties", false } };
}


nlohmann::json sessionRollbackToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "checkpoint_id",
                   { { "type", "integer" },
                     { "minimum", 1 },
                     { "description", "Checkpoint id returned by kisurf_checkpoint." } } },
                 { "checkpoint_name",
                   { { "type", "string" },
                     { "description",
                       "Checkpoint name previously supplied to kisurf_checkpoint or "
                       "session.checkpoint(). Use this when the Python cell cannot know "
                       "the runtime-assigned checkpoint id yet." } } } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json sessionOptionalReasonToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "reason",
                   { { "type", "string" },
                     { "description", "Optional reason shown in the session journal." } } } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionAcceptToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "base_hash",
                   { { "type", "string" },
                     { "description",
                       "Optional live-board base hash to verify before accept. "
                       "When omitted KiSurf derives it from the active editor context." } } } } },
             { "additionalProperties", false } };
}


nlohmann::json sessionRenderPreviewToolParameters()
{
    nlohmann::json properties;
    properties["region"] = {
        { "type", "object" },
        { "description",
          "Optional board-space region to render around the shadow board preview." },
        { "additionalProperties", true }
    };
    properties["layer_mask"] = {
        { "type", "array" },
        { "description", "Optional layer names to include." },
        { "items", { { "type", "string" } } }
    };
    properties["mode"] = {
        { "type", "string" },
        { "enum", nlohmann::json::array( { "native", "visual", "semantic", "diff" } ) },
        { "description", "Preview render mode." }
    };

    return { { "type", "object" },
             { "properties", std::move( properties ) },
             { "additionalProperties", false } };
}


nlohmann::json sessionValidationToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "scope",
                   { { "type", "object" },
                     { "description",
                       "Optional validation scope such as bbox, handles, or affected "
                       "area." },
                     { "additionalProperties", true } } },
                 { "level",
                   { { "type", "string" },
                     { "enum",
                       nlohmann::json::array(
                               { "geometry", "drc_lite", "full_drc" } ) },
                     { "description", "Validation depth." } } } } },
             { "additionalProperties", false } };
}


nlohmann::json booleanParameter( const char* aDescription )
{
    return { { "type", "boolean" }, { "description", aDescription } };
}


nlohmann::json limitParameter( int aMaximum, const char* aDescription )
{
    return { { "type", "integer" },
             { "minimum", 1 },
             { "maximum", aMaximum },
             { "description", aDescription } };
}


nlohmann::json contextSnapshotToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "include_visible_objects",
                   booleanParameter( "Include the visible_objects array. Count fields remain." ) },
                 { "include_selected_objects",
                   booleanParameter( "Include the selected_objects array. Count fields remain." ) },
                 { "include_actions",
                   booleanParameter( "Include available editor action descriptors." ) },
                 { "include_recent_activity",
                   booleanParameter( "Include recent user/model/tool activity records." ) },
                 { "include_tool_state",
                   booleanParameter( "Include the active editor tool-state snapshot." ) },
                 { "include_anchors",
                   booleanParameter( "Include semantic context anchors." ) },
                 { "include_panels",
                   booleanParameter( "Include AI-visible panel state records." ) },
                 { "include_visual",
                   booleanParameter( "Include visual frame metadata, not pixel payloads." ) },
                 { "max_objects",
                   limitParameter( 128,
                                   "Maximum visible and selected objects to return." ) },
                 { "max_actions",
                   limitParameter( 256, "Maximum action descriptors to return." ) },
                 { "max_activity",
                   limitParameter( 128, "Maximum recent activity records to return." ) },
                 { "max_anchors",
                   limitParameter( 128, "Maximum semantic anchors to return." ) },
                 { "max_panels",
                   limitParameter( 32, "Maximum panel state records to return." ) } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json visualFrameToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "include_pixels",
                   booleanParameter( "When true, include the captured frame data_uri if "
                                     "pixels are present and within max_bytes." ) },
                 { "max_bytes",
                   limitParameter( 1048576,
                                   "Maximum captured frame byte size allowed when "
                                   "include_pixels is true." ) },
                 { "include_anchor_overlays",
                   booleanParameter( "When true, include bounded semantic anchor "
                                     "overlay metadata for anchors that have "
                                     "positions." ) },
                 { "max_anchor_overlays",
                   limitParameter( 128,
                                   "Maximum positional semantic anchor overlay "
                                   "records to return." ) },
                 { "focus_layer",
                   { { "type", "string" },
                     { "description",
                       "Optional PCB layer name to emphasize in the returned "
                       "render_directives contract." } } },
                 { "focus_net",
                   { { "type", "string" },
                     { "description",
                       "Optional net name to emphasize in the returned "
                       "render_directives contract." } } },
                 { "dim_unfocused_layers",
                   booleanParameter( "When true, request downstream renderers to "
                                     "dim layers outside the visual focus." ) },
                 { "highlight_anchor_ids",
                   { { "type", "array" },
                     { "description",
                       "Optional positional semantic anchor ids to highlight in "
                       "the returned render_directives contract." },
                     { "maxItems", 32 },
                     { "items", { { "type", "string" } } } } } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json activityTimelineToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "max_activity",
                   limitParameter( 128,
                                   "Maximum recent activity records to return after "
                                   "filtering." ) },
                 { "kind",
                   { { "type", "string" },
                     { "enum",
                       nlohmann::json::array( { "user_action", "model_tool_request",
                                                "policy_decision", "tool_result" } ) },
                     { "description", "Optional activity kind filter." } } },
                 { "action_contains",
                   { { "type", "string" },
                     { "description",
                       "Optional case-sensitive substring filter for action names." } } } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json panelStateToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "max_panels",
                   limitParameter( 32,
                                   "Maximum AI-visible panel state records to return." ) },
                 { "panel_id",
                   { { "type", "string" },
                     { "description",
                       "Optional exact panel id to return, such as agent.panel." } } },
                 { "focused_only",
                   booleanParameter( "When true, return only panels with a focused "
                                     "semantic control." ) },
                 { "include_state",
                   booleanParameter( "When false, omit parsed panel state payloads "
                                     "and return only identity, focus, selected text, "
                                     "and summary fields." ) } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json workspaceViewToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "views",
                   { { "type", "array" },
                     { "description",
                       "Optional sections to return. Omit for context, visual, "
                       "activity, and panels." },
                     { "items",
                       { { "type", "string" },
                         { "enum",
                           nlohmann::json::array(
                                   { "context", "visual", "activity", "panels" } ) } } } } },
                 { "context", contextSnapshotToolParameters() },
                 { "visual", visualFrameToolParameters() },
                 { "activity", activityTimelineToolParameters() },
                 { "panels", panelStateToolParameters() } } },
             { "required", nlohmann::json::array() },
             { "additionalProperties", false } };
}


nlohmann::json semanticUiActionToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "node_id",
                   { { "type", "string" },
                     { "description",
                       "Current semantic UI node id from workspace_view.context.panel_states." } } },
                 { "action",
                   { { "type", "string" },
                     { "description",
                       "Semantic action requested for the node, such as set_text, toggle, "
                       "or invoke." } } },
                 { "text",
                   { { "type", "string" },
                     { "description", "Optional text for set_text actions." } } },
                 { "checked",
                   { { "type", "boolean" },
                     { "description", "Optional desired checked state for toggle actions." } } } } },
             { "required", nlohmann::json::array( { "node_id", "action" } ) },
             { "additionalProperties", false } };
}


nlohmann::json functionTool( const char* aName, const char* aDescription,
                             nlohmann::json aParameters )
{
    return { { "type", "function" },
             { "function",
               { { "name", aName },
                 { "description", aDescription },
                 { "parameters", std::move( aParameters ) } } } };
}


std::string toolResultContent( const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !aToolCall.m_ResultJson.IsEmpty() )
        return toUtf8String( aToolCall.m_ResultJson );

    nlohmann::json result = { { "allowed", aToolCall.m_Allowed },
                              { "executed", aToolCall.m_Executed },
                              { "error_code", toUtf8String( aToolCall.m_ErrorCode ) },
                              { "message", toUtf8String( aToolCall.m_Message ) } };

    return result.dump();
}


void appendToolResultMessages( nlohmann::json& aMessages,
                               const std::vector<AI_TOOL_CALL_RECORD>& aToolResults )
{
    if( aToolResults.empty() )
        return;

    nlohmann::json toolCalls = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolResult : aToolResults )
    {
        std::string arguments = toUtf8String( toolResult.m_ArgumentsJson );

        if( arguments.empty() )
            arguments = "{}";

        toolCalls.push_back(
                { { "id", toUtf8String( toolResult.m_ToolCallId ) },
                  { "type", "function" },
                  { "function",
                    { { "name", toUtf8String( toolResult.m_ToolName ) },
                      { "arguments", arguments } } } } );
    }

    aMessages.push_back( { { "role", "assistant" },
                           { "content", nullptr },
                           { "tool_calls", toolCalls } } );

    for( const AI_TOOL_CALL_RECORD& toolResult : aToolResults )
    {
        aMessages.push_back( { { "role", "tool" },
                               { "tool_call_id", toUtf8String( toolResult.m_ToolCallId ) },
                               { "content", toolResultContent( toolResult ) } } );
    }
}


bool parseFunctionToolCall( const nlohmann::json& aToolCall, uint64_t aRequestId,
                            AI_TOOL_CALL_RECORD& aRecord, wxString& aError )
{
    if( aToolCall.contains( "type" ) && aToolCall["type"].is_string()
        && aToolCall["type"].get<std::string>() != "function" )
    {
        return false;
    }

    if( !aToolCall.contains( "id" ) || !aToolCall["id"].is_string()
        || !aToolCall.contains( "function" ) || !aToolCall["function"].is_object() )
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    const nlohmann::json& function = aToolCall["function"];

    if( !function.contains( "name" ) || !function["name"].is_string()
        || !function.contains( "arguments" ) )
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    aRecord.m_RequestId = aRequestId;
    aRecord.m_ToolCallId =
            wxString::FromUTF8( aToolCall["id"].get_ref<const std::string&>().c_str() );
    aRecord.m_ToolName =
            wxString::FromUTF8( function["name"].get_ref<const std::string&>().c_str() );

    if( function["arguments"].is_string() )
    {
        aRecord.m_ArgumentsJson = wxString::FromUTF8(
                function["arguments"].get_ref<const std::string&>().c_str() );
    }
    else if( function["arguments"].is_object() )
    {
        aRecord.m_ArgumentsJson = wxString::FromUTF8( function["arguments"].dump().c_str() );
    }
    else
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    return true;
}


bool parseToolCalls( const nlohmann::json& aMessage, uint64_t aRequestId,
                     std::vector<AI_TOOL_CALL_RECORD>& aToolCalls, wxString& aError )
{
    if( !aMessage.contains( "tool_calls" ) || aMessage["tool_calls"].is_null() )
        return true;

    if( !aMessage["tool_calls"].is_array() )
    {
        aError = wxS( "AI provider returned malformed tool calls." );
        return false;
    }

    for( const nlohmann::json& toolCallJson : aMessage["tool_calls"] )
    {
        AI_TOOL_CALL_RECORD record;
        wxString            localError;

        if( !parseFunctionToolCall( toolCallJson, aRequestId, record, localError ) )
        {
            if( !localError.IsEmpty() )
            {
                aError = localError;
                return false;
            }

            continue;
        }

        aToolCalls.push_back( record );
    }

    return true;
}
} // namespace

wxString AI_HTTP_REQUEST::HeaderValue( const wxString& aName ) const
{
    for( const AI_HTTP_HEADER& header : m_Headers )
    {
        if( header.m_Name.CmpNoCase( aName ) == 0 )
            return header.m_Value;
    }

    return wxString();
}


bool AI_PROVIDER_SETTINGS::HasApiKey() const
{
    return !m_ApiKey.IsEmpty();
}


wxString AI_PROVIDER_SETTINGS::DefaultBaseUrl()
{
    return wxS( "https://sub2api.wenming-dev.org/v1" );
}


wxString AI_PROVIDER_SETTINGS::DefaultModel()
{
    return wxS( "gpt-4.1-mini" );
}


AI_PROVIDER_SETTINGS AI_PROVIDER_SETTINGS::FromEnvironment()
{
    AI_PROVIDER_SETTINGS settings;

    wxString value;

    if( envValue( wxS( "KISURF_AI_BASE_URL" ), value )
        || envValue( wxS( "OPENAI_BASE_URL" ), value )
        || envValue( wxS( "base_url" ), value ) )
    {
        settings.m_BaseUrl = normalizedUrl( value );
    }
    else
    {
        settings.m_BaseUrl = DefaultBaseUrl();
    }

    if( envValue( wxS( "OPENAI_API_KEY" ), value ) )
        settings.m_ApiKey = value;

    if( envValue( wxS( "KISURF_AI_MODEL" ), value )
        || envValue( wxS( "OPENAI_MODEL" ), value ) )
    {
        settings.m_Model = value;
    }
    else
    {
        settings.m_Model = DefaultModel();
    }

    return settings;
}


AI_PROVIDER_RESPONSE AI_STUB_PROVIDER::Generate( const AI_PROVIDER_REQUEST& aRequest )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "Stub Agent" );

    if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionDecision )
    {
        response.m_Title = wxS( "Stub Next Action Decision" );
        response.m_Body = wxS( "{\"decision_kind\":\"attempt\","
                               "\"opportunity_type\":\"placement\","
                               "\"reason_code\":\"stub_grounded_candidate\"}" );
        return response;
    }

    if( aRequest.m_RequestKind == AI_PROVIDER_REQUEST_KIND::NextActionReview )
    {
        response.m_Title = wxS( "Stub Next Action Review" );
        response.m_Body = wxS( "{\"decision_kind\":\"publish\","
                               "\"reason_code\":\"stub_review_accept\","
                               "\"review_basis\":{\"render_valid\":true,"
                               "\"validation_passed\":true,"
                               "\"budget_within_limits\":true,"
                               "\"self_review_passed\":true}}" );
        return response;
    }

    response.m_Body = wxString::Format( wxS( "Stub response for %s request %llu: %s" ),
                                        aRequest.m_EditorKind == AI_EDITOR_KIND::Pcb
                                                ? wxS( "PCB" )
                                                : wxS( "schematic" ),
                                        static_cast<unsigned long long>( aRequest.m_RequestId ),
                                        aRequest.m_UserText );

    if( aRequest.m_ContextSnapshot.HasContext() )
    {
        response.m_Body << wxS( "\n\nContext:\n" )
                        << aRequest.m_ContextSnapshot.AsPromptText();
    }

    return response;
}


AI_OPENAI_COMPAT_PROVIDER::AI_OPENAI_COMPAT_PROVIDER( AI_PROVIDER_SETTINGS aSettings ) :
        AI_OPENAI_COMPAT_PROVIDER( std::move( aSettings ), defaultHttpHandler )
{
}


AI_OPENAI_COMPAT_PROVIDER::AI_OPENAI_COMPAT_PROVIDER( AI_PROVIDER_SETTINGS aSettings,
                                                      AI_HTTP_HANDLER aHandler ) :
        m_Settings( std::move( aSettings ) ),
        m_Handler( std::move( aHandler ) )
{
    m_Settings.m_BaseUrl = normalizedUrl( m_Settings.m_BaseUrl );
}


AI_PROVIDER_RESPONSE AI_OPENAI_COMPAT_PROVIDER::Generate( const AI_PROVIDER_REQUEST& aRequest )
{
    if( !m_Settings.HasApiKey() )
    {
        return providerError( aRequest,
                              wxS( "AI provider is not configured: open Model Settings and "
                                   "enter an API key." ) );
    }

    if( m_Settings.m_BaseUrl.IsEmpty() )
        m_Settings.m_BaseUrl = AI_PROVIDER_SETTINGS::DefaultBaseUrl();

    if( m_Settings.m_Model.IsEmpty() )
        m_Settings.m_Model = AI_PROVIDER_SETTINGS::DefaultModel();

    wxString userContent;
    userContent << wxS( "User request:\n" ) << aRequest.m_UserText << wxS( "\n\n" )
                << wxS( "Editor context:\n" ) << aRequest.m_ContextSnapshot.AsPromptText()
                << wxS( "\nStructured KiSurf context JSON:\n" )
                << aRequest.m_ContextSnapshot.AsJsonText();

    nlohmann::json body;
    body["model"] = toUtf8String( m_Settings.m_Model );
    body["temperature"] = 0.2;
    const wxString systemPrompt =
            aRequest.m_SystemPromptOverride.IsEmpty()
                    ? wxString( wxS( "You are KiSurf's native KiCad assistant. Use the "
                                      "supplied editor context and tool catalog, propose "
                                      "safe previews before edits, and never assume an edit "
                                      "has been accepted until the user accepts it." ) )
                    : aRequest.m_SystemPromptOverride;

    nlohmann::json messages = nlohmann::json::array(
            { { { "role", "system" },
                { "content", toUtf8String( systemPrompt ) } },
              { { "role", "user" },
                { "content",
                  makeUserMessageContent( userContent, aRequest.m_ContextSnapshot.m_Visual ) } } } );

    appendToolResultMessages( messages, aRequest.m_ToolResults );
    body["messages"] = std::move( messages );

    if( !aRequest.m_ResponseFormatJson.IsEmpty() )
    {
        nlohmann::json responseFormat = nlohmann::json::parse(
                toUtf8String( aRequest.m_ResponseFormatJson ), nullptr, false );

        if( responseFormat.is_object() )
            body["response_format"] = std::move( responseFormat );
    }

    if( !aRequest.m_ToolCatalogJson.IsEmpty() )
    {
        nlohmann::json toolCatalog = nlohmann::json::parse(
                toUtf8String( aRequest.m_ToolCatalogJson ), nullptr, false );

        if( toolCatalog.is_array() )
        {
            body["tools"] = std::move( toolCatalog );
        }
        else if( toolCatalog.is_object() && toolCatalog.contains( "tools" )
                 && toolCatalog["tools"].is_array() )
        {
            body["tools"] = std::move( toolCatalog["tools"] );
        }
        else
        {
            body["tools"] = nlohmann::json::array();
        }
    }
    else if( aRequest.m_DisableDefaultTools )
    {
        body["tools"] = nlohmann::json::array();
    }
    else
    {
        body["tools"] = nlohmann::json::array(
            { functionTool( "kisurf_run_action",
                            "Request a KiSurf editor action by native action name. Local KiSurf "
                            "policy decides whether the action can run.",
                            actionToolParameters() ),
              functionTool( "kisurf_check_action",
                            "Check whether a KiSurf editor action is known, available, and "
                            "allowed without executing it.",
                            actionToolParameters() ),
              functionTool( "kisurf_get_context_snapshot",
                            "Read a bounded JSON snapshot of the current KiSurf editor "
                            "context. This is read-only and never edits the board.",
                            contextSnapshotToolParameters() ),
              functionTool( "kisurf_get_workspace_view",
                            "Preferred single read-only interface for a bounded workspace "
                            "view: structured context, visual frame, and activity timeline.",
                            workspaceViewToolParameters() ),
              functionTool( "kisurf_get_visual_frame",
                            "Read current captured editor visual-frame metadata, and "
                            "optionally the bounded pixel data URI. This is read-only.",
                            visualFrameToolParameters() ),
              functionTool( "kisurf_get_activity_timeline",
                            "Read a bounded, optionally filtered timeline of recent "
                            "user/model/tool activity. This is read-only.",
                            activityTimelineToolParameters() ),
              functionTool( "kisurf_invoke_semantic_ui_action",
                            "Request an action on a current semantic UI node. KiSurf "
                            "checks the live semantic tree and refuses nodes that require "
                            "direct user confirmation.",
                            semanticUiActionToolParameters() ),
              functionTool( "kisurf_open_session",
                            "Open a KiSurf AI execution session for preview-first "
                            "Python cells, typed atomic operations, rollback, and accept.",
                            sessionOpenToolParameters() ),
              functionTool( "kisurf_close_session",
                            "Close the active KiSurf AI execution session without "
                            "accepting pending preview changes.",
                            sessionOptionalReasonToolParameters() ),
              functionTool( "kisurf_run_cell",
                            sessionRunCellToolDescription(),
                            sessionRunCellToolParameters() ),
              functionTool( "kisurf_run_atomic_operation",
                            "Run one typed KiSurf atomic operation inside the active "
                            "AI execution session. This mutates only the shadow board "
                            "and journal; it cannot publish or mutate the live board "
                            "without kisurf_accept_session.",
                            sessionAtomicOperationToolParameters() ),
              functionTool( "kisurf_begin_step",
                            "Begin a named AI execution step. Atomic operations recorded "
                            "after this call are grouped in the session journal.",
                            sessionBeginStepToolParameters() ),
              functionTool( "kisurf_end_step",
                            "End a named AI execution step and return a bounded "
                            "observation of the previewed result.",
                            sessionStepIdToolParameters() ),
              functionTool( "kisurf_checkpoint",
                            "Create a rollback checkpoint in the active AI execution "
                            "session.",
                            sessionCheckpointToolParameters() ),
              functionTool( "kisurf_rollback_to",
                            "Rollback the active AI execution session to a prior "
                            "checkpoint and mark newer handles stale.",
                            sessionRollbackToolParameters() ),
              functionTool( "kisurf_cancel_session",
                            "Cancel the active AI execution session and stop any running "
                            "Python worker.",
                            sessionOptionalReasonToolParameters() ),
              functionTool( "kisurf_reject_session",
                            "Reject the active AI execution session preview and clear "
                            "its native preview objects.",
                            sessionOptionalReasonToolParameters() ),
              functionTool( "kisurf_accept_session",
                            "Accept the active AI execution session by replaying its "
                            "journal to the live board as one undoable commit.",
                            sessionAcceptToolParameters() ),
              functionTool( "kisurf_observe_step",
                            "Observe an AI execution step without closing it. This is "
                            "used between Python cells to inspect intermediate results.",
                            sessionStepIdToolParameters() ),
              functionTool( "kisurf_query_board_summary",
                            "Query the semantic shadow-board summary for the active AI "
                            "execution session without mutating the live board.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_items",
                            "Query semantic shadow-board items for the active AI execution "
                            "session without mutating the live board.",
                            sessionQueryItemsToolParameters() ),
              functionTool( "kisurf_query_item",
                            "Query one semantic shadow-board item by session handle or alias.",
                            sessionQueryItemToolParameters() ),
              functionTool( "kisurf_query_selection",
                            "Query the current editor selection attached to the active AI "
                            "execution session observation context.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_nets",
                            "Query net names visible to the active AI execution session "
                            "from the shadow board and current editor context.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_layers",
                            "Query layer names visible to the active AI execution session "
                            "from the shadow board and current editor context.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_design_rules",
                            "Query design-rule context attached to the active AI execution "
                            "session.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_viewport",
                            "Query the current editor viewport, cursor, and visual-frame "
                            "metadata for the active AI execution session.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_query_activity_timeline",
                            "Query recent user/model/tool activity for the active AI "
                            "execution session.",
                            sessionEmptyToolParameters() ),
              functionTool( "kisurf_render_preview",
                            "Render the active AI execution session preview from the "
                            "native preview scene.",
                            sessionRenderPreviewToolParameters() ),
              functionTool( "kisurf_run_validation",
                            "Run typed validation for the active AI execution session.",
                            sessionValidationToolParameters() ) } );
    }

    body["parallel_tool_calls"] = false;

    AI_HTTP_REQUEST request;
    request.m_Method = wxS( "POST" );
    request.m_Url = joinUrl( m_Settings.m_BaseUrl, wxS( "/chat/completions" ) );
    request.m_Body = wxString::FromUTF8( body.dump().c_str() );
    request.m_Headers.push_back( { wxS( "Accept" ), wxS( "application/json" ) } );
    request.m_Headers.push_back( { wxS( "Content-Type" ), wxS( "application/json" ) } );
    request.m_Headers.push_back( { wxS( "Authorization" ), wxS( "Bearer " ) + m_Settings.m_ApiKey } );

    AI_HTTP_RESPONSE httpResponse;
    wxString         error;

    if( !m_Handler || !m_Handler( request, httpResponse, error ) )
    {
        wxString detail = error.IsEmpty() ? wxString( wxS( "request failed" ) ) : error;

        return providerError( aRequest,
                              wxString( wxS( "AI provider network error: " ) ) + detail );
    }

    if( httpResponse.m_StatusCode < 200 || httpResponse.m_StatusCode >= 300 )
    {
        return providerError( aRequest,
                              wxString::Format( wxS( "AI provider request failed with HTTP %d." ),
                                                httpResponse.m_StatusCode ) );
    }

    try
    {
        nlohmann::json parsed = nlohmann::json::parse( toUtf8String( httpResponse.m_Body ) );
        wxString                         content;
        std::vector<AI_TOOL_CALL_RECORD> toolCalls;

        if( parsed.contains( "choices" ) && parsed["choices"].is_array()
            && !parsed["choices"].empty() )
        {
            const nlohmann::json& first = parsed["choices"].front();

            if( first.contains( "message" ) && first["message"].is_object() )
            {
                const nlohmann::json& message = first["message"];

                if( message.contains( "content" ) && message["content"].is_string() )
                {
                    content = wxString::FromUTF8(
                            message["content"].get_ref<const std::string&>().c_str() );
                }

                wxString toolError;

                if( !parseToolCalls( message, aRequest.m_RequestId, toolCalls, toolError ) )
                    return providerError( aRequest, toolError );
            }
        }

        if( content.IsEmpty() && toolCalls.empty() )
            return providerError( aRequest, wxS( "AI provider returned no message content." ) );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "AI Provider" );
        response.m_Body = content.IsEmpty() ? wxString( wxS( "Tool call requested." ) ) : content;
        response.m_ToolCalls = std::move( toolCalls );
        return response;
    }
    catch( const std::exception& e )
    {
        return providerError( aRequest,
                              wxString::Format( wxS( "AI provider returned invalid JSON: %s" ),
                                                wxString::FromUTF8( e.what() ) ) );
    }
}


std::unique_ptr<AI_PROVIDER> MakeDefaultAiProvider()
{
    wxString mode;

    if( wxGetEnv( wxS( "KISURF_AI_PROVIDER" ), &mode ) && mode.CmpNoCase( wxS( "stub" ) ) == 0 )
        return std::make_unique<AI_STUB_PROVIDER>();

    return MakeAiProviderFromModelConfig( AI_MODEL_CONFIG_STORE::LoadUserConfig() );
}


std::unique_ptr<AI_PROVIDER> MakeAiProviderFromModelConfig( const AI_MODEL_CONFIG& aConfig )
{
    return MakeAiProviderFromModelConfig( aConfig, AI_HTTP_HANDLER() );
}


std::unique_ptr<AI_PROVIDER> MakeAiProviderFromModelConfig(
        const AI_MODEL_CONFIG& aConfig, AI_HTTP_HANDLER aHandler )
{
    AI_MODEL_CONFIG config = aConfig;
    config.Normalize();

    if( config.m_ProviderKind == AI_MODEL_PROVIDER_KIND::AnthropicCompatible )
    {
        return std::make_unique<UNSUPPORTED_MODEL_PROVIDER>(
                AiModelProviderKindLabel( config.m_ProviderKind ) );
    }

    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = config.m_BaseUrl;
    settings.m_ApiKey = config.m_ApiKey;
    settings.m_Model = config.m_Model;

    if( aHandler )
        return std::make_unique<AI_OPENAI_COMPAT_PROVIDER>( std::move( settings ),
                                                            std::move( aHandler ) );

    return std::make_unique<AI_OPENAI_COMPAT_PROVIDER>( std::move( settings ) );
}
