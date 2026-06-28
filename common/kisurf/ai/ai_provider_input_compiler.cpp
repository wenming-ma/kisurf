#include <kisurf/ai/ai_provider_input_compiler.h>

#include <kisurf/ai/ai_token_budget_manager.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr size_t PROVIDER_CONTRACT_INLINE_CHAR_LIMIT = 4096;
constexpr size_t PROVIDER_CONTRACT_MAX_SUMMARY_NAMES = 96;

std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string stableHashText( const wxString& aText )
{
    const std::string text = toUtf8String( aText );
    uint64_t          hash = 1469598103934665603ull;

    for( unsigned char ch : text )
    {
        hash ^= ch;
        hash *= 1099511628211ull;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw( 16 ) << std::setfill( '0' ) << hash;
    return stream.str();
}


nlohmann::json contextVersionJson( const AI_CONTEXT_VERSION& aVersion )
{
    return {
        { "document_revision", aVersion.m_DocumentRevision },
        { "selection_revision", aVersion.m_SelectionRevision },
        { "view_revision", aVersion.m_ViewRevision }
    };
}


AI_CONTEXT_SNAPSHOT boundedSnapshot( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                     size_t aMaxActivityRecords )
{
    AI_CONTEXT_SNAPSHOT snapshot = aSnapshot;

    if( aMaxActivityRecords > 0
        && snapshot.m_RecentActivity.size() > aMaxActivityRecords )
    {
        const auto first = snapshot.m_RecentActivity.end()
                           - static_cast<std::ptrdiff_t>( aMaxActivityRecords );
        snapshot.m_RecentActivity =
                std::vector<AI_ACTIVITY_RECORD>( first, snapshot.m_RecentActivity.end() );
    }

    return snapshot;
}


nlohmann::json blockTraceJson( const AI_PROVIDER_INPUT_BLOCK& aBlock )
{
    nlohmann::json block = {
        { "id", toUtf8String( aBlock.m_Id ) },
        { "kind", toUtf8String( aBlock.m_Kind ) },
        { "source", toUtf8String( aBlock.m_Source ) },
        { "included", aBlock.m_Included },
        { "original_chars", aBlock.m_OriginalChars },
        { "sent_chars", aBlock.m_SentChars }
    };

    if( !aBlock.m_OmissionReason.IsEmpty() )
        block["omission_reason"] = toUtf8String( aBlock.m_OmissionReason );

    if( !aBlock.m_MetadataJson.IsEmpty() )
    {
        nlohmann::json metadata = nlohmann::json::parse(
                toUtf8String( aBlock.m_MetadataJson ), nullptr, false );

        if( metadata.is_discarded() )
            block["metadata"] = toUtf8String( aBlock.m_MetadataJson );
        else
            block["metadata"] = metadata;
    }

    return block;
}


void appendContextBlock( AI_PROVIDER_REQUEST& aRequest, wxString& aText,
                         const wxString& aId, const wxString& aKind,
                         const wxString& aSource, const wxString& aBlockText,
                         bool aRequired,
                         const wxString& aMetadataJson = wxString() )
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = aId;
    block.m_Kind = aKind;
    block.m_Source = aSource;
    block.m_OriginalChars = aBlockText.length();
    block.m_MetadataJson = aMetadataJson;

    const size_t maxChars = aRequest.m_MaxProviderInputChars;
    const size_t currentChars = aText.length();
    const size_t remaining = maxChars > currentChars ? maxChars - currentChars : 0;

    if( remaining == 0 )
    {
        block.m_Included = false;
        block.m_OmissionReason = wxS( "budget_exhausted" );
        aRequest.m_ProviderInputWasShrunk = true;
        aRequest.m_ProviderInputBlocks.push_back( block );
        return;
    }

    wxString sent = aBlockText;

    if( sent.length() > remaining )
    {
        if( !aRequired && remaining < 256 )
        {
            block.m_Included = false;
            block.m_OmissionReason = wxS( "omitted_budget" );
            aRequest.m_ProviderInputWasShrunk = true;
            aRequest.m_ProviderInputBlocks.push_back( block );
            return;
        }

        const size_t prefixChars = remaining > 180 ? remaining - 180 : remaining;
        sent = sent.Left( prefixChars );
        sent << wxS( "\n[truncated input block id=" ) << aId
             << wxS( " original_chars=" ) << block.m_OriginalChars
             << wxS( " hash=" ) << fromUtf8String( stableHashText( aBlockText ) )
             << wxS( "]" );
        block.m_OmissionReason = wxS( "truncated_budget" );
        aRequest.m_ProviderInputWasShrunk = true;
    }

    if( !aText.IsEmpty() && !aText.EndsWith( wxS( "\n" ) ) )
        aText << wxS( "\n" );

    if( !aText.IsEmpty() )
        aText << wxS( "\n" );

    aText << sent;
    block.m_Text = sent;
    block.m_SentChars = sent.length();
    aRequest.m_ProviderInputBlocks.push_back( block );
}


void appendOmittedBlockTrace( AI_PROVIDER_REQUEST& aRequest,
                              const AI_PROVIDER_INPUT_BLOCK& aSource,
                              const wxString& aReason )
{
    AI_PROVIDER_INPUT_BLOCK block = aSource;
    block.m_Included = false;
    block.m_OmissionReason = aReason;
    block.m_OriginalChars = aSource.m_Text.length();
    block.m_SentChars = 0;
    block.m_Text.Clear();
    aRequest.m_ProviderInputWasShrunk = true;
    aRequest.m_ProviderInputBlocks.push_back( std::move( block ) );
}


std::vector<std::string> providerToolNames( const nlohmann::json& aCatalog )
{
    const nlohmann::json* tools = nullptr;

    if( aCatalog.is_array() )
        tools = &aCatalog;
    else if( aCatalog.is_object() && aCatalog.contains( "tools" )
             && aCatalog["tools"].is_array() )
        tools = &aCatalog["tools"];

    std::vector<std::string> names;

    if( !tools )
        return names;

    for( const nlohmann::json& tool : *tools )
    {
        if( !tool.is_object() || !tool.contains( "function" )
            || !tool["function"].is_object() )
        {
            continue;
        }

        const nlohmann::json& fn = tool["function"];

        if( fn.contains( "name" ) && fn["name"].is_string() )
            names.push_back( fn["name"].get<std::string>() );
    }

    return names;
}


wxString summarizedToolCatalogBlockText( const wxString& aRawCatalog,
                                         nlohmann::json& aMetadata )
{
    const nlohmann::json catalog = nlohmann::json::parse(
            toUtf8String( aRawCatalog ), nullptr, false );
    const std::vector<std::string> names =
            catalog.is_discarded() ? std::vector<std::string>()
                                   : providerToolNames( catalog );

    nlohmann::json summary = {
        { "status", "summarized_tool_catalog" },
        { "tool_count", names.size() },
        { "original_chars", aRawCatalog.length() },
        { "hash", stableHashText( aRawCatalog ) },
        { "detail_policy",
          "full tool schemas remain in AI_PROVIDER_REQUEST.m_ToolCatalogJson and provider top-level tools" }
    };

    nlohmann::json toolNames = nlohmann::json::array();
    const size_t nameLimit =
            std::min( names.size(), PROVIDER_CONTRACT_MAX_SUMMARY_NAMES );

    for( size_t ii = 0; ii < nameLimit; ++ii )
        toolNames.push_back( names[ii] );

    summary["tool_names"] = std::move( toolNames );

    if( nameLimit < names.size() )
    {
        summary["tool_names_truncated"] = true;
        summary["omitted_tool_name_count"] = names.size() - nameLimit;
    }
    else
    {
        summary["tool_names_truncated"] = false;
    }

    aMetadata["summarized"] = true;
    aMetadata["summary_kind"] = "tool_catalog_names_hash";
    aMetadata["tool_count"] = names.size();
    aMetadata["original_chars"] = aRawCatalog.length();
    aMetadata["hash"] = stableHashText( aRawCatalog );

    wxString blockText;
    blockText << wxS( "Provider callable tool catalog summary:\n" )
              << fromUtf8String( summary.dump() );
    return blockText;
}


const nlohmann::json* responseSchemaObject( const nlohmann::json& aResponseFormat )
{
    if( aResponseFormat.is_object() && aResponseFormat.contains( "json_schema" )
        && aResponseFormat["json_schema"].is_object()
        && aResponseFormat["json_schema"].contains( "schema" )
        && aResponseFormat["json_schema"]["schema"].is_object() )
    {
        return &aResponseFormat["json_schema"]["schema"];
    }

    if( aResponseFormat.is_object() && aResponseFormat.contains( "schema" )
        && aResponseFormat["schema"].is_object() )
    {
        return &aResponseFormat["schema"];
    }

    return nullptr;
}


std::string responseSchemaName( const nlohmann::json& aResponseFormat )
{
    if( aResponseFormat.is_object() && aResponseFormat.contains( "json_schema" )
        && aResponseFormat["json_schema"].is_object()
        && aResponseFormat["json_schema"].contains( "name" )
        && aResponseFormat["json_schema"]["name"].is_string() )
    {
        return aResponseFormat["json_schema"]["name"].get<std::string>();
    }

    if( aResponseFormat.is_object() && aResponseFormat.contains( "name" )
        && aResponseFormat["name"].is_string() )
    {
        return aResponseFormat["name"].get<std::string>();
    }

    return std::string();
}


std::vector<std::string> responseSchemaPropertyNames(
        const nlohmann::json& aResponseFormat )
{
    const nlohmann::json* schema = responseSchemaObject( aResponseFormat );

    if( !schema || !schema->contains( "properties" )
        || !( *schema )["properties"].is_object() )
    {
        return {};
    }

    std::vector<std::string> names;

    for( auto it = ( *schema )["properties"].begin();
         it != ( *schema )["properties"].end(); ++it )
    {
        names.push_back( it.key() );
    }

    return names;
}


size_t responseSchemaRequiredCount( const nlohmann::json& aResponseFormat )
{
    const nlohmann::json* schema = responseSchemaObject( aResponseFormat );

    if( !schema || !schema->contains( "required" )
        || !( *schema )["required"].is_array() )
    {
        return 0;
    }

    return ( *schema )["required"].size();
}


wxString summarizedResponseFormatBlockText( const wxString& aRawResponseFormat,
                                            nlohmann::json& aMetadata )
{
    const nlohmann::json responseFormat = nlohmann::json::parse(
            toUtf8String( aRawResponseFormat ), nullptr, false );

    const bool parsed = !responseFormat.is_discarded()
                        && responseFormat.is_object();
    const std::string type =
            parsed ? responseFormat.value( "type", std::string( "unknown" ) )
                   : std::string( "unknown" );
    const std::string name = parsed ? responseSchemaName( responseFormat )
                                    : std::string();
    const std::vector<std::string> propertyNames =
            parsed ? responseSchemaPropertyNames( responseFormat )
                   : std::vector<std::string>();
    const size_t requiredCount =
            parsed ? responseSchemaRequiredCount( responseFormat ) : 0;

    nlohmann::json summary = {
        { "status", "summarized_response_schema" },
        { "type", type },
        { "schema_name", name },
        { "property_count", propertyNames.size() },
        { "required_count", requiredCount },
        { "original_chars", aRawResponseFormat.length() },
        { "hash", stableHashText( aRawResponseFormat ) },
        { "detail_policy",
          "full response schema remains in AI_PROVIDER_REQUEST.m_ResponseFormatJson and provider top-level response_format" }
    };

    nlohmann::json names = nlohmann::json::array();
    const size_t nameLimit =
            std::min( propertyNames.size(), PROVIDER_CONTRACT_MAX_SUMMARY_NAMES );

    for( size_t ii = 0; ii < nameLimit; ++ii )
        names.push_back( propertyNames[ii] );

    summary["property_names"] = std::move( names );

    if( nameLimit < propertyNames.size() )
    {
        summary["property_names_truncated"] = true;
        summary["omitted_property_name_count"] = propertyNames.size() - nameLimit;
    }
    else
    {
        summary["property_names_truncated"] = false;
    }

    aMetadata["status"] = "summarized_response_schema";
    aMetadata["summarized"] = true;
    aMetadata["summary_kind"] = "response_schema_names_hash";
    aMetadata["type"] = type;
    aMetadata["schema_name"] = name;
    aMetadata["property_count"] = propertyNames.size();
    aMetadata["required_count"] = requiredCount;
    aMetadata["original_chars"] = aRawResponseFormat.length();
    aMetadata["hash"] = stableHashText( aRawResponseFormat );

    wxString blockText;
    blockText << wxS( "Provider response contract summary:\n" )
              << fromUtf8String( summary.dump() );
    return blockText;
}


void appendRetrievedMemoryBlocks( AI_PROVIDER_REQUEST& aRequest, wxString& aText )
{
    size_t includedRecords = 0;
    size_t includedChars = 0;

    for( const AI_PROVIDER_INPUT_BLOCK& memory : aRequest.m_RetrievedMemoryBlocks )
    {
        AI_PROVIDER_INPUT_BLOCK normalized = memory;

        if( normalized.m_Id.IsEmpty() )
            normalized.m_Id = wxS( "memory.unknown" );

        if( normalized.m_Kind.IsEmpty() )
            normalized.m_Kind = wxS( "retrieved_memory" );

        if( normalized.m_Source.IsEmpty() )
            normalized.m_Source = wxS( "local_text_memory" );

        if( includedRecords >= aRequest.m_MaxRetrievedMemoryRecords )
        {
            appendOmittedBlockTrace( aRequest, normalized,
                                     wxS( "retrieved_memory_record_limit" ) );
            continue;
        }

        if( aRequest.m_MaxRetrievedMemoryChars > 0
            && includedChars >= aRequest.m_MaxRetrievedMemoryChars )
        {
            appendOmittedBlockTrace( aRequest, normalized,
                                     wxS( "retrieved_memory_char_limit" ) );
            continue;
        }

        wxString memoryText = normalized.m_Text;

        if( aRequest.m_MaxRetrievedMemoryChars > 0
            && includedChars + memoryText.length() > aRequest.m_MaxRetrievedMemoryChars )
        {
            const size_t remaining = aRequest.m_MaxRetrievedMemoryChars - includedChars;

            if( remaining < 96 )
            {
                appendOmittedBlockTrace( aRequest, normalized,
                                         wxS( "retrieved_memory_char_limit" ) );
                continue;
            }

            memoryText = memoryText.Left( remaining - 48 );
            memoryText << wxS( "\n[truncated retrieved memory]" );
            aRequest.m_ProviderInputWasShrunk = true;
        }

        wxString blockText;
        blockText << wxS( "Retrieved local memory:\n" )
                  << wxS( "- id: " ) << normalized.m_Id << wxS( "\n" )
                  << wxS( "- source: " ) << normalized.m_Source << wxS( "\n" )
                  << wxS( "- text: " ) << memoryText;

        appendContextBlock( aRequest, aText, normalized.m_Id, normalized.m_Kind,
                            normalized.m_Source, blockText, false,
                            normalized.m_MetadataJson );

        ++includedRecords;
        includedChars += memoryText.length();
    }
}


void appendRequestInputBlocks( AI_PROVIDER_REQUEST& aRequest, wxString& aText,
                               const std::vector<AI_PROVIDER_INPUT_BLOCK>& aBlocks )
{
    for( const AI_PROVIDER_INPUT_BLOCK& sourceBlock : aBlocks )
    {
        if( sourceBlock.m_Id == wxS( "provider.input.omissions" ) )
            continue;

        if( !sourceBlock.m_Included )
        {
            appendOmittedBlockTrace(
                    aRequest, sourceBlock,
                    sourceBlock.m_OmissionReason.IsEmpty()
                            ? wxString( wxS( "caller_omitted" ) )
                            : sourceBlock.m_OmissionReason );
            continue;
        }

        if( sourceBlock.m_Text.IsEmpty() )
            continue;

        appendContextBlock(
                aRequest, aText,
                sourceBlock.m_Id.IsEmpty() ? wxString( wxS( "provider.input" ) )
                                           : sourceBlock.m_Id,
                sourceBlock.m_Kind.IsEmpty() ? wxString( wxS( "provider_input" ) )
                                             : sourceBlock.m_Kind,
                sourceBlock.m_Source, sourceBlock.m_Text, false,
                sourceBlock.m_MetadataJson );
    }
}


void appendVisualObservationArtifactBlock( AI_PROVIDER_REQUEST& aRequest,
                                           wxString& aText )
{
    const AI_VISUAL_SNAPSHOT& visual = aRequest.m_ContextSnapshot.m_Visual;

    if( visual.m_SidecarJson.IsEmpty() )
        return;

    wxString blockText;
    blockText << wxS( "Visual observation artifact:\n" )
              << visual.m_SidecarJson;

    nlohmann::json metadata = {
        { "source", toUtf8String( visual.m_Source ) },
        { "frame_id", toUtf8String( visual.m_FrameId ) },
        { "frame_kind", toUtf8String( visual.m_FrameKind ) },
        { "has_pixels", visual.HasPixels() }
    };

    appendContextBlock( aRequest, aText, wxS( "visual.observation_artifact" ),
                        wxS( "visual_observation_artifact" ), visual.m_Source,
                        blockText, true, fromUtf8String( metadata.dump() ) );
}


void appendProviderContractBlocks( AI_PROVIDER_REQUEST& aRequest, wxString& aText )
{
    if( !aRequest.m_ResponseFormatJson.IsEmpty() )
    {
        nlohmann::json metadata = nlohmann::json::object();
        nlohmann::json responseFormat = nlohmann::json::parse(
                toUtf8String( aRequest.m_ResponseFormatJson ), nullptr, false );

        if( !responseFormat.is_discarded() && responseFormat.is_object() )
        {
            metadata["type"] =
                    responseFormat.value( "type", std::string( "unknown" ) );

            if( responseFormat.contains( "json_schema" )
                && responseFormat["json_schema"].is_object() )
            {
                metadata["schema_name"] = responseFormat["json_schema"].value(
                        "name", std::string() );
            }
        }

        wxString blockText;

        if( aRequest.m_ResponseFormatJson.length()
            > PROVIDER_CONTRACT_INLINE_CHAR_LIMIT )
        {
            blockText = summarizedResponseFormatBlockText(
                    aRequest.m_ResponseFormatJson, metadata );
        }
        else
        {
            blockText << wxS( "Provider response contract:\n" )
                      << aRequest.m_ResponseFormatJson;
        }

        appendContextBlock( aRequest, aText, wxS( "provider.response_format" ),
                            wxS( "response_schema" ),
                            wxS( "provider_contract" ), blockText, false,
                            fromUtf8String( metadata.dump() ) );
    }

    if( !aRequest.m_ToolCatalogJson.IsEmpty() )
    {
        nlohmann::json metadata = nlohmann::json::object();
        nlohmann::json toolCatalog = nlohmann::json::parse(
                toUtf8String( aRequest.m_ToolCatalogJson ), nullptr, false );

        if( toolCatalog.is_array() )
            metadata["tool_count"] = toolCatalog.size();
        else if( toolCatalog.is_object() && toolCatalog.contains( "tools" )
                 && toolCatalog["tools"].is_array() )
        {
            metadata["tool_count"] = toolCatalog["tools"].size();
        }

        wxString blockText;

        if( aRequest.m_ToolCatalogJson.length()
            > PROVIDER_CONTRACT_INLINE_CHAR_LIMIT )
        {
            blockText = summarizedToolCatalogBlockText(
                    aRequest.m_ToolCatalogJson, metadata );
        }
        else
        {
            blockText << wxS( "Provider callable tool catalog:\n" )
                      << aRequest.m_ToolCatalogJson;
        }

        appendContextBlock( aRequest, aText, wxS( "provider.tool_catalog" ),
                            wxS( "tool_catalog" ), wxS( "provider_contract" ),
                            blockText, false,
                            fromUtf8String( metadata.dump() ) );
    }
}


bool isScriptOutputTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_run_cell" )
           || aToolName == wxS( "script_run_bounded_plan" );
}


wxString compressedToolResultJson( const AI_TOOL_CALL_RECORD& aToolResult,
                                   size_t aMaxChars )
{
    wxString raw = aToolResult.m_ResultJson;

    if( raw.IsEmpty() )
    {
        nlohmann::json result = {
            { "allowed", aToolResult.m_Allowed },
            { "executed", aToolResult.m_Executed },
            { "error_code", toUtf8String( aToolResult.m_ErrorCode ) },
            { "message", toUtf8String( aToolResult.m_Message ) }
        };

        raw = fromUtf8String( result.dump() );
    }

    if( raw.length() <= aMaxChars )
        return raw;

    const std::string hash = stableHashText( raw );
    const bool        scriptOutput = isScriptOutputTool( aToolResult.m_ToolName );
    const std::string artifactKind = scriptOutput ? "script_output" : "tool_result";
    const std::string artifactSlug = scriptOutput ? "script-output" : "tool-result";
    nlohmann::json artifactRef = {
        { "uri", "kisurf-artifact://" + artifactSlug + "/" + hash },
        { "kind", artifactKind },
        { "retention", "trace" },
        { "tool_call_id", toUtf8String( aToolResult.m_ToolCallId ) }
    };

    const size_t retainedChars =
            std::max<size_t>( 24, std::min<size_t>( 64, aMaxChars / 3 ) );

    nlohmann::json summary = {
        { "status", "compressed_tool_result" },
        { "tool_call_id", toUtf8String( aToolResult.m_ToolCallId ) },
        { "tool_name", toUtf8String( aToolResult.m_ToolName ) },
        { "allowed", aToolResult.m_Allowed },
        { "executed", aToolResult.m_Executed },
        { "error_code", toUtf8String( aToolResult.m_ErrorCode ) },
        { "message", toUtf8String( aToolResult.m_Message ) },
        { "original_chars", raw.length() },
        { "retained_prefix", toUtf8String( raw.Left( retainedChars ) ) },
        { "hash", hash },
        { "artifact_ref", artifactRef }
    };

    return fromUtf8String( summary.dump() );
}


void appendToolResultTrace( AI_PROVIDER_REQUEST& aRequest,
                            const AI_TOOL_CALL_RECORD& aOriginal,
                            const AI_TOOL_CALL_RECORD& aCompiled )
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = aOriginal.m_ToolCallId;
    block.m_Kind = wxS( "tool_result" );
    block.m_Source = aOriginal.m_ToolName;
    block.m_Included = true;
    block.m_OriginalChars = aOriginal.m_ResultJson.length();
    block.m_SentChars = aCompiled.m_ResultJson.length();

    if( block.m_SentChars < block.m_OriginalChars )
    {
        block.m_OmissionReason = wxS( "compressed" );
        aRequest.m_ProviderInputWasShrunk = true;
    }

    nlohmann::json metadata = {
        { "tool_call_id", toUtf8String( aOriginal.m_ToolCallId ) },
        { "tool_name", toUtf8String( aOriginal.m_ToolName ) },
        { "board_state_version", contextVersionJson( aRequest.m_ContextVersion ) }
    };

    if( !block.m_OmissionReason.IsEmpty() )
    {
        nlohmann::json compiled = nlohmann::json::parse(
                toUtf8String( aCompiled.m_ResultJson ), nullptr, false );

        if( !compiled.is_discarded() && compiled.is_object()
            && compiled.contains( "artifact_ref" ) )
        {
            metadata["artifact_ref"] = compiled["artifact_ref"];
        }
    }

    block.m_MetadataJson = fromUtf8String( metadata.dump() );
    aRequest.m_ProviderInputBlocks.push_back( block );
}


void appendOmittedInputSummaryBlock( AI_PROVIDER_REQUEST& aRequest,
                                     wxString& aText )
{
    nlohmann::json omitted = nlohmann::json::array();

    for( const AI_PROVIDER_INPUT_BLOCK& block : aRequest.m_ProviderInputBlocks )
    {
        if( block.m_Id == wxS( "provider.input.omissions" ) )
            continue;

        if( block.m_Included && block.m_OmissionReason.IsEmpty() )
            continue;

        nlohmann::json item = {
            { "id", toUtf8String( block.m_Id ) },
            { "kind", toUtf8String( block.m_Kind ) },
            { "source", toUtf8String( block.m_Source ) },
            { "included", block.m_Included },
            { "omission_reason", toUtf8String( block.m_OmissionReason ) },
            { "original_chars", block.m_OriginalChars },
            { "sent_chars", block.m_SentChars }
        };

        if( !block.m_MetadataJson.IsEmpty() )
        {
            nlohmann::json metadata = nlohmann::json::parse(
                    toUtf8String( block.m_MetadataJson ), nullptr, false );

            if( !metadata.is_discarded() && metadata.is_object()
                && metadata.contains( "artifact_ref" ) )
            {
                item["artifact_ref"] = metadata["artifact_ref"];
            }
        }

        omitted.push_back( std::move( item ) );
    }

    if( omitted.empty() )
        return;

    wxString blockText;
    blockText << wxS( "Omitted provider input blocks:\n" )
              << fromUtf8String( omitted.dump() );

    appendContextBlock( aRequest, aText, wxS( "provider.input.omissions" ),
                        wxS( "omitted_input_summary" ),
                        wxS( "provider_input_compiler" ), blockText, false );
}


nlohmann::json providerUserMessageContent( const wxString& aUserContent,
                                           const AI_VISUAL_SNAPSHOT& aVisual )
{
    if( !aVisual.HasPixels() )
        return toUtf8String( aUserContent );

    return nlohmann::json::array(
            { { { "type", "text" }, { "text", toUtf8String( aUserContent ) } },
              { { "type", "image_url" },
                { "image_url", { { "url", toUtf8String( aVisual.m_DataUri ) } } } } } );
}


std::string providerToolResultContent( const AI_TOOL_CALL_RECORD& aToolCall,
                                       const AI_CONTEXT_VERSION& aContextVersion )
{
    nlohmann::json rawResult;

    if( !aToolCall.m_ResultJson.IsEmpty() )
    {
        rawResult = nlohmann::json::parse(
                toUtf8String( aToolCall.m_ResultJson ), nullptr, false );

        if( rawResult.is_discarded() )
            rawResult = toUtf8String( aToolCall.m_ResultJson );
    }
    else
    {
        rawResult = { { "allowed", aToolCall.m_Allowed },
                      { "executed", aToolCall.m_Executed },
                      { "error_code", toUtf8String( aToolCall.m_ErrorCode ) },
                      { "message", toUtf8String( aToolCall.m_Message ) } };
    }

    nlohmann::json envelope = {
        { "result", std::move( rawResult ) },
        { "provenance",
          { { "tool_call_id", toUtf8String( aToolCall.m_ToolCallId ) },
            { "tool_name", toUtf8String( aToolCall.m_ToolName ) },
            { "allowed", aToolCall.m_Allowed },
            { "executed", aToolCall.m_Executed },
            { "board_state_version", contextVersionJson( aContextVersion ) } } }
    };

    return envelope.dump();
}


void appendProviderToolResultMessages(
        nlohmann::json& aMessages,
        const std::vector<AI_TOOL_CALL_RECORD>& aToolResults,
        const AI_CONTEXT_VERSION& aContextVersion )
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
                               { "content", providerToolResultContent(
                                     toolResult, aContextVersion ) } } );
    }
}


void maybeOmitVisualPixels( AI_PROVIDER_REQUEST& aRequest )
{
    if( !aRequest.m_ContextSnapshot.m_Visual.HasPixels() )
        return;

    AI_VISUAL_FRAME_POLICY policy;
    policy.m_AllowPixels = aRequest.m_AllowVisualPixels;
    policy.m_MaxDataUriChars = aRequest.m_MaxVisualDataUriChars;

    AI_VISUAL_FRAME_POLICY_RESULT policyResult =
            ApplyAiVisualFramePolicy( aRequest.m_ContextSnapshot.m_Visual, policy );

    if( policyResult.m_PixelsIncluded )
        return;

    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "visual.frame.pixels" );
    block.m_Kind = wxS( "visual_frame" );
    block.m_Source = aRequest.m_ContextSnapshot.m_Visual.m_Source;
    block.m_Included = false;
    block.m_OmissionReason = policyResult.m_OmissionReason;
    block.m_OriginalChars = aRequest.m_ContextSnapshot.m_Visual.m_DataUri.length();
    block.m_MetadataJson = policyResult.m_SidecarJson;
    aRequest.m_ProviderInputBlocks.push_back( block );
    aRequest.m_ProviderInputWasShrunk = true;
    aRequest.m_ContextSnapshot.m_Visual = policyResult.m_Snapshot;
}


void finalizePromptTrace( AI_PROVIDER_REQUEST& aRequest )
{
    nlohmann::json blocks = nlohmann::json::array();
    nlohmann::json omitted = nlohmann::json::array();

    for( const AI_PROVIDER_INPUT_BLOCK& block : aRequest.m_ProviderInputBlocks )
    {
        nlohmann::json traceBlock = blockTraceJson( block );
        blocks.push_back( traceBlock );

        if( !block.m_Included || !block.m_OmissionReason.IsEmpty() )
            omitted.push_back( traceBlock );
    }

    nlohmann::json trace = {
        { "schema", { { "name", "kisurf.ai.prompt_trace" }, { "version", 1 } } },
        { "request_id", aRequest.m_RequestId },
        { "conversation_id", aRequest.m_ConversationId },
        { "request_kind", static_cast<int>( aRequest.m_RequestKind ) },
        { "estimated_input_chars", aRequest.m_ContextEstimatedChars },
        { "max_provider_input_chars", aRequest.m_MaxProviderInputChars },
        { "provider_input_was_shrunk", aRequest.m_ProviderInputWasShrunk },
        { "blocks", std::move( blocks ) },
        { "omitted", std::move( omitted ) }
    };

    aRequest.m_PromptTraceJson = fromUtf8String( trace.dump() );
}
} // namespace


AI_PROVIDER_REQUEST AiCompileProviderInput( const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_ContextCompiled )
        return aRequest;

    AI_PROVIDER_REQUEST compiled = aRequest;
    compiled.m_ContextCompiled = true;
    compiled.m_ProviderInputWasShrunk = false;
    compiled.m_ContextEstimatedChars = 0;
    compiled.m_CompiledUserMessageText.Clear();
    compiled.m_PromptTraceJson.Clear();
    compiled.m_ProviderInputBlocks.clear();
    const size_t originalActivityCount = aRequest.m_ContextSnapshot.m_RecentActivity.size();
    compiled.m_ContextSnapshot = boundedSnapshot( aRequest.m_ContextSnapshot,
                                                  aRequest.m_MaxContextActivityRecords );

    if( originalActivityCount > compiled.m_ContextSnapshot.m_RecentActivity.size() )
    {
        AI_PROVIDER_INPUT_BLOCK block;
        block.m_Id = wxS( "editor.recent_activity" );
        block.m_Kind = wxS( "recent_activity" );
        block.m_Source = wxS( "activity_log" );
        block.m_Included = false;
        block.m_OmissionReason = wxS( "older_activity_omitted" );
        block.m_OriginalChars = originalActivityCount;
        block.m_SentChars = compiled.m_ContextSnapshot.m_RecentActivity.size();

        nlohmann::json metadata = {
            { "original_record_count", originalActivityCount },
            { "sent_record_count", compiled.m_ContextSnapshot.m_RecentActivity.size() }
        };

        block.m_MetadataJson = fromUtf8String( metadata.dump() );
        compiled.m_ProviderInputBlocks.push_back( block );
        compiled.m_ProviderInputWasShrunk = true;
    }

    maybeOmitVisualPixels( compiled );

    wxString userContent;
    wxString userBlock;
    userBlock << wxS( "User request:\n" ) << aRequest.m_UserText;
    appendContextBlock( compiled, userContent, wxS( "user.request" ),
                        wxS( "user_text" ), wxS( "chat" ), userBlock, true );

    appendRequestInputBlocks( compiled, userContent, aRequest.m_ProviderInputBlocks );
    appendRetrievedMemoryBlocks( compiled, userContent );
    appendVisualObservationArtifactBlock( compiled, userContent );
    appendProviderContractBlocks( compiled, userContent );

    if( compiled.m_ContextSnapshot.HasContext() )
    {
        wxString contextBlock;
        contextBlock << wxS( "Editor context summary:\n" )
                     << compiled.m_ContextSnapshot.AsPromptText( 16, 16, 16, 8 );
        appendContextBlock( compiled, userContent, wxS( "editor.context.summary" ),
                            wxS( "context_summary" ), wxS( "editor_state" ),
                            contextBlock, true );

        wxString jsonBlock;
        jsonBlock << wxS( "Structured KiSurf context JSON:\n" )
                  << compiled.m_ContextSnapshot.AsJsonText( 16, 32, 16, 32, 8 );
        appendContextBlock( compiled, userContent, wxS( "editor.context.json" ),
                            wxS( "context_json" ), wxS( "editor_state" ),
                            jsonBlock, false );
    }

    compiled.m_ToolResults.clear();
    compiled.m_ToolResults.reserve( aRequest.m_ToolResults.size() );

    for( const AI_TOOL_CALL_RECORD& toolResult : aRequest.m_ToolResults )
    {
        AI_TOOL_CALL_RECORD compiledResult = toolResult;
        compiledResult.m_ResultJson = compressedToolResultJson(
                toolResult, aRequest.m_MaxToolResultChars );
        appendToolResultTrace( compiled, toolResult, compiledResult );
        compiled.m_ToolResults.push_back( std::move( compiledResult ) );
    }

    appendOmittedInputSummaryBlock( compiled, userContent );

    compiled.m_CompiledUserMessageText = userContent;
    compiled.m_ContextEstimatedChars = userContent.length();

    for( const AI_TOOL_CALL_RECORD& toolResult : compiled.m_ToolResults )
        compiled.m_ContextEstimatedChars += toolResult.m_ResultJson.length();

    finalizePromptTrace( compiled );
    return compiled;
}


AI_PROVIDER_REQUEST AiCompileProviderInputWithBudget(
        const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_ContextCompiled )
        return aRequest;

    const AI_TOKEN_BUDGET_PLAN plan =
            AiPlanProviderInputBudgetForRequest( aRequest );

    if( !plan.m_ShouldShrink )
        return AiCompileProviderInput( aRequest );

    return AiCompileProviderInput(
            AiApplyProviderInputBudgetPlan( aRequest, plan ) );
}


wxString AiCompileProviderMessagesJson( const AI_PROVIDER_REQUEST& aRequest,
                                        const wxString& aDefaultSystemPrompt )
{
    AI_PROVIDER_REQUEST compiled = aRequest.m_ContextCompiled
                                           ? aRequest
                                           : AiCompileProviderInput( aRequest );

    const wxString systemPrompt = compiled.m_SystemPromptOverride.IsEmpty()
                                          ? aDefaultSystemPrompt
                                          : compiled.m_SystemPromptOverride;

    nlohmann::json messages = nlohmann::json::array(
            { { { "role", "system" },
                { "content", toUtf8String( systemPrompt ) } },
              { { "role", "user" },
                { "content",
                  providerUserMessageContent( compiled.m_CompiledUserMessageText,
                                              compiled.m_ContextSnapshot.m_Visual ) } } } );

    appendProviderToolResultMessages( messages, compiled.m_ToolResults,
                                      compiled.m_ContextVersion );

    return fromUtf8String( messages.dump() );
}
