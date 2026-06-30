#include <kisurf/ai/ai_runtime.h>

#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_provider_input_compiler.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <set>
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


wxString deniedToolResultJson( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = { { "action", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "dry_run", false },
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) } };

    return wxString::FromUTF8( payload.dump().c_str() );
}


void recordModelToolCall( AI_ACTIVITY_LOG& aActivityLog, const AI_PROVIDER_REQUEST& aRequest,
                          const AI_TOOL_CALL_RECORD& aToolCall )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_ToolCallId = aToolCall.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    record.m_EditorKind = aRequest.m_EditorKind;
    record.m_ActionName = aToolCall.m_ToolName;
    record.m_ArgumentsJson = aToolCall.m_ArgumentsJson;
    record.m_Message = wxS( "Provider tool call requested." );
    aActivityLog.Append( record );
}


void copyToolResult( AI_TOOL_CALL_RECORD& aToolCall,
                     const AI_TOOL_INVOCATION_RESULT& aResult )
{
    aToolCall.m_Allowed = aResult.m_Allowed;
    aToolCall.m_Executed = aResult.m_Executed;
    aToolCall.m_ErrorCode = aResult.m_ErrorCode;
    aToolCall.m_Message = aResult.m_Message;
    aToolCall.m_ResultJson = aResult.m_ResultJson;
}


void recordToolResult( AI_ACTIVITY_LOG& aActivityLog,
                       const AI_TOOL_INVOCATION_RESULT& aResult )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aResult.m_RequestId;
    record.m_ToolCallId = aResult.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    record.m_ActionName = aResult.m_ActionName;
    record.m_ResultJson = aResult.m_ResultJson;
    record.m_ErrorCode = aResult.m_ErrorCode;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    aActivityLog.Append( record );
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string trimAscii( std::string aText )
{
    auto first = std::find_if_not( aText.begin(), aText.end(),
                                  []( unsigned char ch )
                                  {
                                      return std::isspace( ch ) != 0;
                                  } );
    auto last = std::find_if_not( aText.rbegin(), aText.rend(),
                                 []( unsigned char ch )
                                 {
                                     return std::isspace( ch ) != 0;
                                 } ).base();

    if( first >= last )
        return {};

    return std::string( first, last );
}


bool isRuntimeTextToolName( const std::string& aName )
{
    static const std::set<std::string> names = {
        "kisurf_run_action",
        "kisurf_check_action",
        "kisurf_get_workspace_view",
        "kisurf_invoke_semantic_ui_action",
        "kisurf_open_session",
        "kisurf_close_session",
        "kisurf_run_cell",
        "kisurf_run_atomic_operation",
        "kisurf_begin_step",
        "kisurf_end_step",
        "kisurf_checkpoint",
        "kisurf_rollback_to",
        "kisurf_cancel_session",
        "kisurf_reject_session",
        "kisurf_accept_session",
        "kisurf_observe_step",
        "kisurf_query_board_summary",
        "kisurf_query_items",
        "kisurf_query_item",
        "kisurf_query_unplaced_footprints",
        "kisurf_query_selection",
        "kisurf_query_nets",
        "kisurf_query_layers",
        "kisurf_query_design_rules",
        "kisurf_query_viewport",
        "kisurf_query_activity_timeline",
        "kisurf_render_preview",
        "kisurf_run_validation"
    };

    return names.count( aName ) != 0;
}


bool isToolNameChar( unsigned char aChar )
{
    return std::isalnum( aChar ) != 0 || aChar == '_';
}


class LENIENT_TOOL_ARGUMENT_PARSER
{
public:
    explicit LENIENT_TOOL_ARGUMENT_PARSER( const std::string& aText ) :
            m_Text( aText )
    {
    }

    bool ParseObject( nlohmann::json& aObject )
    {
        SkipWhitespace();

        if( !ParseObjectValue( aObject ) )
            return false;

        SkipWhitespace();
        return m_Pos == m_Text.size();
    }

private:
    void SkipWhitespace()
    {
        while( m_Pos < m_Text.size()
               && std::isspace( static_cast<unsigned char>( m_Text[m_Pos] ) ) != 0 )
        {
            ++m_Pos;
        }
    }

    bool Consume( char aChar )
    {
        SkipWhitespace();

        if( m_Pos >= m_Text.size() || m_Text[m_Pos] != aChar )
            return false;

        ++m_Pos;
        return true;
    }

    bool ParseQuotedString( std::string& aValue )
    {
        SkipWhitespace();

        if( m_Pos >= m_Text.size() || m_Text[m_Pos] != '"' )
            return false;

        ++m_Pos;
        aValue.clear();

        while( m_Pos < m_Text.size() )
        {
            const char ch = m_Text[m_Pos++];

            if( ch == '"' )
                return true;

            if( ch == '\\' && m_Pos < m_Text.size() )
            {
                aValue.push_back( m_Text[m_Pos++] );
                continue;
            }

            aValue.push_back( ch );
        }

        return false;
    }

    bool ParseBareToken( std::string& aToken )
    {
        SkipWhitespace();
        const size_t start = m_Pos;

        while( m_Pos < m_Text.size() )
        {
            const char ch = m_Text[m_Pos];

            if( ch == ':' || ch == ',' || ch == '{' || ch == '}'
                || ch == '[' || ch == ']'
                || std::isspace( static_cast<unsigned char>( ch ) ) != 0 )
            {
                break;
            }

            ++m_Pos;
        }

        if( m_Pos == start )
            return false;

        aToken = m_Text.substr( start, m_Pos - start );
        return true;
    }

    bool ParseKey( std::string& aKey )
    {
        if( m_Pos < m_Text.size() && m_Text[m_Pos] == '"' )
            return ParseQuotedString( aKey );

        return ParseBareToken( aKey );
    }

    static bool TokenIsInteger( const std::string& aToken )
    {
        size_t pos = 0;

        if( pos < aToken.size() && ( aToken[pos] == '-' || aToken[pos] == '+' ) )
            ++pos;

        if( pos == aToken.size() )
            return false;

        for( ; pos < aToken.size(); ++pos )
        {
            if( std::isdigit( static_cast<unsigned char>( aToken[pos] ) ) == 0 )
                return false;
        }

        return true;
    }

    static bool TokenIsNumber( const std::string& aToken )
    {
        bool hasDigit = false;
        bool hasDecimalOrExponent = false;

        for( size_t i = 0; i < aToken.size(); ++i )
        {
            const unsigned char ch = static_cast<unsigned char>( aToken[i] );

            if( std::isdigit( ch ) != 0 )
            {
                hasDigit = true;
                continue;
            }

            if( ch == '.' || ch == 'e' || ch == 'E' )
            {
                hasDecimalOrExponent = true;
                continue;
            }

            if( ( ch == '-' || ch == '+' ) && ( i == 0 || aToken[i - 1] == 'e'
                                                 || aToken[i - 1] == 'E' ) )
            {
                continue;
            }

            return false;
        }

        return hasDigit && hasDecimalOrExponent;
    }

    static nlohmann::json BareTokenValue( const std::string& aToken )
    {
        if( aToken == "true" )
            return true;

        if( aToken == "false" )
            return false;

        if( aToken == "null" )
            return nullptr;

        if( TokenIsInteger( aToken ) )
            return std::strtoll( aToken.c_str(), nullptr, 10 );

        if( TokenIsNumber( aToken ) )
            return std::strtod( aToken.c_str(), nullptr );

        return aToken;
    }

    bool ParseArrayValue( nlohmann::json& aValue )
    {
        if( !Consume( '[' ) )
            return false;

        aValue = nlohmann::json::array();
        SkipWhitespace();

        if( Consume( ']' ) )
            return true;

        while( true )
        {
            nlohmann::json item;

            if( !ParseValue( item ) )
                return false;

            aValue.push_back( std::move( item ) );

            if( Consume( ']' ) )
                return true;

            if( !Consume( ',' ) )
                return false;
        }
    }

    bool ParseObjectValue( nlohmann::json& aValue )
    {
        if( !Consume( '{' ) )
            return false;

        aValue = nlohmann::json::object();
        SkipWhitespace();

        if( Consume( '}' ) )
            return true;

        while( true )
        {
            std::string key;

            if( !ParseKey( key ) || !Consume( ':' ) )
                return false;

            nlohmann::json item;

            if( !ParseValue( item ) )
                return false;

            aValue[key] = std::move( item );

            if( Consume( '}' ) )
                return true;

            if( !Consume( ',' ) )
                return false;
        }
    }

    bool ParseValue( nlohmann::json& aValue )
    {
        SkipWhitespace();

        if( m_Pos >= m_Text.size() )
            return false;

        if( m_Text[m_Pos] == '{' )
            return ParseObjectValue( aValue );

        if( m_Text[m_Pos] == '[' )
            return ParseArrayValue( aValue );

        if( m_Text[m_Pos] == '"' )
        {
            std::string value;

            if( !ParseQuotedString( value ) )
                return false;

            aValue = value;
            return true;
        }

        std::string token;

        if( !ParseBareToken( token ) )
            return false;

        aValue = BareTokenValue( token );
        return true;
    }

private:
    const std::string& m_Text;
    size_t             m_Pos = 0;
};


void mergeAdditionalOperationFields( nlohmann::json& aTarget,
                                     const nlohmann::json& aSource )
{
    if( !aTarget.is_object() || !aSource.is_object() )
        return;

    for( const auto& [key, value] : aSource.items() )
    {
        if( key == "kind" || key == "arguments" || aTarget.contains( key ) )
            continue;

        aTarget[key] = value;
    }
}


nlohmann::json unwrapNestedOperationArguments( nlohmann::json aArguments )
{
    while( aArguments.is_object() && aArguments.contains( "arguments" )
           && aArguments["arguments"].is_object()
           && ( aArguments.size() == 1
                || ( aArguments.size() == 2 && aArguments.contains( "kind" ) ) ) )
    {
        aArguments = aArguments["arguments"];
    }

    if( aArguments.is_object() && aArguments.contains( "kind" ) )
        aArguments.erase( "kind" );

    return aArguments;
}


nlohmann::json normalizedAtomicOperationToolArguments( const nlohmann::json& aArguments )
{
    if( !aArguments.is_object() )
        return aArguments;

    std::string  kind;
    nlohmann::json operationArguments = nlohmann::json::object();

    if( aArguments.contains( "kind" ) && aArguments["kind"].is_string() )
    {
        kind = aArguments["kind"].get<std::string>();

        if( aArguments.contains( "arguments" ) && aArguments["arguments"].is_object() )
            operationArguments = unwrapNestedOperationArguments( aArguments["arguments"] );
        else
            operationArguments = aArguments;

        mergeAdditionalOperationFields( operationArguments, aArguments );
    }
    else if( aArguments.contains( "arguments" ) && aArguments["arguments"].is_object() )
    {
        const nlohmann::json& wrapped = aArguments["arguments"];

        if( wrapped.contains( "kind" ) && wrapped["kind"].is_string() )
        {
            kind = wrapped["kind"].get<std::string>();

            if( wrapped.contains( "arguments" ) && wrapped["arguments"].is_object() )
                operationArguments = unwrapNestedOperationArguments( wrapped["arguments"] );
            else
                operationArguments = wrapped;

            mergeAdditionalOperationFields( operationArguments, wrapped );
            mergeAdditionalOperationFields( operationArguments, aArguments );
        }
    }

    if( kind.empty() || !operationArguments.is_object() )
        return aArguments;

    if( operationArguments.contains( "kind" ) )
        operationArguments.erase( "kind" );

    return { { "kind", kind }, { "arguments", std::move( operationArguments ) } };
}


void normalizeToolCallArguments( AI_TOOL_CALL_RECORD& aToolCall )
{
    if( aToolCall.m_ToolName != wxS( "kisurf_run_atomic_operation" )
        || aToolCall.m_ArgumentsJson.IsEmpty() )
    {
        return;
    }

    nlohmann::json arguments =
            nlohmann::json::parse( toUtf8String( aToolCall.m_ArgumentsJson ),
                                   nullptr, false );

    if( arguments.is_discarded() || !arguments.is_object() )
        return;

    const nlohmann::json normalized =
            normalizedAtomicOperationToolArguments( arguments );

    if( normalized != arguments )
        aToolCall.m_ArgumentsJson = fromUtf8String( normalized.dump() );
}


void normalizeToolCallArguments( std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    for( AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
        normalizeToolCallArguments( toolCall );
}


std::optional<AI_TOOL_CALL_RECORD> strictTextualToolCall(
        const AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aResponse.m_ToolCalls.empty() || aResponse.m_Body.IsEmpty() )
        return std::nullopt;

    const std::string text = trimAscii( toUtf8String( aResponse.m_Body ) );
    const std::string prefix = "call ";

    if( text.rfind( prefix, 0 ) != 0 )
        return std::nullopt;

    const size_t nameStart = prefix.size();
    size_t       nameEnd = nameStart;

    while( nameEnd < text.size() )
    {
        const unsigned char ch = static_cast<unsigned char>( text[nameEnd] );

        if( !isToolNameChar( ch ) )
            break;

        ++nameEnd;
    }

    if( nameEnd == nameStart )
        return std::nullopt;

    const std::string toolName = text.substr( nameStart, nameEnd - nameStart );

    if( !isRuntimeTextToolName( toolName ) )
        return std::nullopt;

    const std::string marker = "(arguments)=";

    if( text.compare( nameEnd, marker.size(), marker ) != 0 )
        return std::nullopt;

    const std::string argumentsText =
            trimAscii( text.substr( nameEnd + marker.size() ) );

    nlohmann::json arguments =
            nlohmann::json::parse( argumentsText, nullptr, false );

    if( arguments.is_discarded() || !arguments.is_object() )
        return std::nullopt;

    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = aResponse.m_RequestId;
    call.m_ToolCallId = wxS( "textual_tool_call_1" );
    call.m_ToolName = fromUtf8String( toolName );
    call.m_ArgumentsJson = fromUtf8String( arguments.dump() );
    normalizeToolCallArguments( call );
    return call;
}


std::optional<AI_TOOL_CALL_RECORD> colonTextualToolCall(
        const AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aResponse.m_ToolCalls.empty() || aResponse.m_Body.IsEmpty() )
        return std::nullopt;

    const std::string text = trimAscii( toUtf8String( aResponse.m_Body ) );
    const std::string prefix = "call:";

    if( text.rfind( prefix, 0 ) != 0 )
        return std::nullopt;

    const size_t nameStart = prefix.size();
    size_t       nameEnd = nameStart;

    while( nameEnd < text.size() )
    {
        const unsigned char ch = static_cast<unsigned char>( text[nameEnd] );

        if( !isToolNameChar( ch ) )
            break;

        ++nameEnd;
    }

    if( nameEnd == nameStart || nameEnd >= text.size()
        || text[nameEnd] != '{' )
    {
        return std::nullopt;
    }

    const std::string toolName = text.substr( nameStart, nameEnd - nameStart );

    if( !isRuntimeTextToolName( toolName ) )
        return std::nullopt;

    nlohmann::json arguments;
    LENIENT_TOOL_ARGUMENT_PARSER parser( text.substr( nameEnd ) );

    if( !parser.ParseObject( arguments ) || !arguments.is_object() )
        return std::nullopt;

    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = aResponse.m_RequestId;
    call.m_ToolCallId = wxS( "textual_tool_call_1" );
    call.m_ToolName = fromUtf8String( toolName );
    call.m_ArgumentsJson = fromUtf8String( arguments.dump() );
    normalizeToolCallArguments( call );
    return call;
}


void promoteStrictTextualToolCall( AI_PROVIDER_RESPONSE& aResponse )
{
    std::optional<AI_TOOL_CALL_RECORD> call = strictTextualToolCall( aResponse );

    if( !call )
        call = colonTextualToolCall( aResponse );

    if( !call )
        return;

    aResponse.m_ToolCalls.push_back( std::move( *call ) );
    aResponse.m_Body =
            wxS( "Executing model-requested tool call from strict text form." );
}


std::string jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return {};

    return aJson[aKey].get<std::string>();
}


void recordPythonWorkerEvents( AI_ACTIVITY_LOG& aActivityLog,
                               const AI_TOOL_INVOCATION_RESULT& aResult )
{
    if( aResult.m_ResultJson.IsEmpty() )
        return;

    nlohmann::json payload = nlohmann::json::parse( toUtf8String( aResult.m_ResultJson ),
                                                    nullptr, false );

    if( payload.is_discarded() || !payload.is_object()
        || !payload.contains( "recorded_events" )
        || !payload["recorded_events"].is_array() )
    {
        return;
    }

    for( const nlohmann::json& event : payload["recorded_events"] )
    {
        if( !event.is_object() )
            continue;

        const std::string eventKind = jsonStringValue( event, "kind" );
        const std::string actionSuffix = eventKind.empty() ? "event" : eventKind;

        AI_ACTIVITY_RECORD record;
        record.m_RequestId = aResult.m_RequestId;
        record.m_ToolCallId = aResult.m_ToolCallId;
        record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
        record.m_ActionName = aResult.m_ActionName;

        if( record.m_ActionName.IsEmpty() )
            record.m_ActionName = wxS( "python" );

        record.m_ActionName << wxS( "." ) << fromUtf8String( actionSuffix );
        record.m_ResultJson = fromUtf8String( event.dump() );
        record.m_ErrorCode = aResult.m_ErrorCode;
        record.m_Allowed = aResult.m_Allowed;
        record.m_Executed = aResult.m_Executed;
        record.m_Message = fromUtf8String( jsonStringValue( event, "message" ) );
        aActivityLog.Append( record );
    }
}


AI_PROVIDER_INPUT_BLOCK toolBudgetFinalizationBlock( size_t aToolRounds,
                                                     size_t aToolResultCount )
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "runtime.tool_budget.final_answer" );
    block.m_Kind = wxS( "runtime_instruction" );
    block.m_Source = wxS( "ai_runtime" );
    block.m_Text = wxString::Format(
            wxS( "Tool round budget was exhausted after %llu round(s) with %llu "
                 "handled tool result(s). No more tools are available for this "
                 "request. Produce the final user-facing answer now using the "
                 "available tool results. Do not request additional tools." ),
            static_cast<unsigned long long>( aToolRounds ),
            static_cast<unsigned long long>( aToolResultCount ) );
    return block;
}


AI_PROVIDER_REQUEST finalAnswerRequestAfterToolBudget(
        const AI_PROVIDER_REQUEST& aOriginalRequest,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls,
        size_t aToolRounds )
{
    AI_PROVIDER_REQUEST request = aOriginalRequest;
    request.m_ToolResults = aHandledToolCalls;
    request.m_ToolCatalogJson.Clear();
    request.m_DisableDefaultTools = true;
    request.m_ContextCompiled = false;
    request.m_CompiledUserMessageText.Clear();
    request.m_PromptTraceJson.Clear();
    request.m_ProviderInputBlocks.clear();
    request.m_ProviderInputBlocks.push_back(
            toolBudgetFinalizationBlock( aToolRounds, aHandledToolCalls.size() ) );
    return request;
}


bool sameToolCallIdentity( const AI_TOOL_CALL_RECORD& aFirst,
                           const AI_TOOL_CALL_RECORD& aSecond )
{
    return !aFirst.m_ToolCallId.IsEmpty()
           && aFirst.m_ToolCallId == aSecond.m_ToolCallId
           && aFirst.m_ToolName == aSecond.m_ToolName
           && aFirst.m_ArgumentsJson == aSecond.m_ArgumentsJson;
}


bool repeatsHandledToolCall( const std::vector<AI_TOOL_CALL_RECORD>& aRoundToolCalls,
                             const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    for( const AI_TOOL_CALL_RECORD& roundCall : aRoundToolCalls )
    {
        for( const AI_TOOL_CALL_RECORD& handledCall : aHandledToolCalls )
        {
            if( sameToolCallIdentity( roundCall, handledCall ) )
                return true;
        }
    }

    return false;
}


bool wxContainsAny( const wxString& aText,
                    std::initializer_list<const wxString> aNeedles )
{
    for( const wxString& needle : aNeedles )
    {
        if( !needle.IsEmpty() && aText.Contains( needle ) )
            return true;
    }

    return false;
}


bool chatRequestNeedsAtLeastOneToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                         const AI_PROVIDER_RESPONSE& aResponse )
{
    if( aRequest.m_RequestKind != AI_PROVIDER_REQUEST_KIND::Chat
        || aRequest.m_MaxToolRounds == 0
        || aResponse.m_Title.CmpNoCase( wxS( "AI Provider Error" ) ) == 0 )
    {
        return false;
    }

    if( aRequest.m_DisableDefaultTools && aRequest.m_ToolCatalogJson.IsEmpty() )
        return false;

    const wxString text = aRequest.m_UserText.Lower();

    return wxContainsAny(
            text,
            { wxS( "删除" ), wxS( "清除" ), wxS( "移除" ), wxS( "创建" ),
              wxS( "新建" ), wxS( "添加" ), wxS( "放置" ), wxS( "移动" ),
              wxS( "修改" ), wxS( "更改" ), wxS( "改成" ), wxS( "生成" ),
              wxS( "绘制" ), wxS( "布线" ), wxS( "走线" ), wxS( "过孔" ),
              wxS( "铺铜" ), wxS( "检查" ), wxS( "查询" ), wxS( "多少" ),
              wxS( "几个" ), wxS( "delete" ), wxS( "remove" ),
              wxS( "clear" ), wxS( "create" ), wxS( "add" ), wxS( "place" ),
              wxS( "move" ), wxS( "modify" ), wxS( "change" ),
              wxS( "set " ), wxS( "draw" ), wxS( "route" ),
              wxS( "routing" ), wxS( "track" ), wxS( "tracks" ),
              wxS( "via" ), wxS( "vias" ), wxS( "zone" ), wxS( "fill" ),
              wxS( "drc" ), wxS( "check" ), wxS( "inspect" ),
              wxS( "count" ), wxS( "how many" ), wxS( "query" ),
              wxS( "list" ), wxS( "current board" ), wxS( "pcb" ) } );
}


struct BOARD_SUMMARY_COUNTS
{
    bool     m_HasTrackSegments = false;
    bool     m_HasVias = false;
    bool     m_HasPads = false;
    bool     m_HasFootprints = false;
    bool     m_HasZones = false;
    bool     m_HasNets = false;
    bool     m_HasTotalItems = false;
    uint64_t m_TrackSegments = 0;
    uint64_t m_Vias = 0;
    uint64_t m_Pads = 0;
    uint64_t m_Footprints = 0;
    uint64_t m_Zones = 0;
    uint64_t m_Nets = 0;
    uint64_t m_TotalItems = 0;

    uint64_t RoutingCount() const
    {
        return m_TrackSegments + m_Vias;
    }
};


wxString boardSummaryCountsText( const BOARD_SUMMARY_COUNTS& aSummary )
{
    std::vector<wxString> parts;

    if( aSummary.m_HasTrackSegments )
    {
        parts.push_back( wxString::Format(
                wxS( "Track Segments: %llu" ),
                static_cast<unsigned long long>( aSummary.m_TrackSegments ) ) );
    }

    if( aSummary.m_HasVias )
    {
        parts.push_back( wxString::Format(
                wxS( "Vias: %llu" ),
                static_cast<unsigned long long>( aSummary.m_Vias ) ) );
    }

    if( aSummary.m_HasPads )
    {
        parts.push_back( wxString::Format(
                wxS( "Pads: %llu" ),
                static_cast<unsigned long long>( aSummary.m_Pads ) ) );
    }

    if( aSummary.m_HasFootprints )
    {
        parts.push_back( wxString::Format(
                wxS( "Footprints: %llu" ),
                static_cast<unsigned long long>( aSummary.m_Footprints ) ) );
    }

    if( aSummary.m_HasZones )
    {
        parts.push_back( wxString::Format(
                wxS( "Zones: %llu" ),
                static_cast<unsigned long long>( aSummary.m_Zones ) ) );
    }

    if( aSummary.m_HasNets )
    {
        parts.push_back( wxString::Format(
                wxS( "Nets: %llu" ),
                static_cast<unsigned long long>( aSummary.m_Nets ) ) );
    }

    if( aSummary.m_HasTotalItems )
    {
        parts.push_back( wxString::Format(
                wxS( "Total Items: %llu" ),
                static_cast<unsigned long long>( aSummary.m_TotalItems ) ) );
    }

    if( parts.empty() )
        return wxS( "no board summary count fields" );

    wxString text = parts.front();

    for( size_t ii = 1; ii < parts.size(); ++ii )
        text += wxS( "; " ) + parts[ii];

    return text;
}


nlohmann::json boardSummaryCountsMetadata( const BOARD_SUMMARY_COUNTS& aSummary )
{
    nlohmann::json payload = nlohmann::json::object();

    if( aSummary.m_HasTrackSegments )
        payload["track_segments"] = aSummary.m_TrackSegments;

    if( aSummary.m_HasVias )
        payload["vias"] = aSummary.m_Vias;

    if( aSummary.m_HasPads )
        payload["pads"] = aSummary.m_Pads;

    if( aSummary.m_HasFootprints )
        payload["footprints"] = aSummary.m_Footprints;

    if( aSummary.m_HasZones )
        payload["zones"] = aSummary.m_Zones;

    if( aSummary.m_HasNets )
        payload["nets"] = aSummary.m_Nets;

    if( aSummary.m_HasTotalItems )
        payload["total_items"] = aSummary.m_TotalItems;

    payload["routing_count"] = aSummary.RoutingCount();
    return payload;
}


AI_PROVIDER_INPUT_BLOCK toolRequiredRetryBlock()
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "runtime.tool_required.retry" );
    block.m_Kind = wxS( "runtime_instruction" );
    block.m_Source = wxS( "ai_runtime" );
    block.m_Text =
            wxS( "The current user request asks about or modifies the live KiCad "
                 "workspace. The previous model response did not include a tool "
                 "call, so it is not acceptable as a final answer. You must call "
                 "at least one appropriate KiSurf tool now: use read-only query "
                 "tools for board facts and current-board atomic/script tools for "
                 "edits. Do not answer from assumptions, screenshots, visible "
                 "status text, or chat history." );
    return block;
}


AI_PROVIDER_INPUT_BLOCK toolGroundingConflictRetryBlock(
        uint64_t aObservedRoutingCount,
        const std::optional<BOARD_SUMMARY_COUNTS>& aObservedSummary,
        const AI_PROVIDER_RESPONSE& aRejectedResponse )
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "runtime.tool_grounding_conflict.retry" );
    block.m_Kind = wxS( "runtime_instruction" );
    block.m_Source = wxS( "ai_runtime" );

    if( aObservedSummary )
    {
        block.m_Text = wxString::Format(
                wxS( "The previous final answer contradicted a KiSurf tool result. "
                     "The already-executed current-board summary reports these exact "
                     "fields: %s. The answer claimed no routing/tracks/vias or no "
                     "action was needed. Produce a corrected final answer grounded "
                     "strictly in these exact fields. Do not collapse Track Segments "
                     "and Vias into a generic routing item count when the user asked "
                     "for those fields." ),
                boardSummaryCountsText( *aObservedSummary ).c_str() );
    }
    else
    {
        block.m_Text = wxString::Format(
                wxS( "The previous final answer contradicted a KiSurf tool result. "
                     "The already-executed current-board tool result reports %llu routing "
                     "item(s), but the answer claimed no routing/tracks/vias or no action "
                     "was needed. Produce a corrected final answer grounded strictly in "
                     "the tool result. Do not repeat the contradicted claim. If the tool "
                     "result is insufficient for the requested operation, state the exact "
                     "missing fact and retry_hint instead of inventing board state." ),
                static_cast<unsigned long long>( aObservedRoutingCount ) );
    }

    nlohmann::json metadata = {
        { "observed_routing_count", aObservedRoutingCount },
        { "rejected_title", toUtf8String( aRejectedResponse.m_Title ) }
    };

    if( aObservedSummary )
        metadata["observed_board_summary"] = boardSummaryCountsMetadata( *aObservedSummary );

    block.m_MetadataJson = fromUtf8String( metadata.dump() );
    return block;
}


AI_PROVIDER_INPUT_BLOCK routingAbsenceVerificationRetryBlock()
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "runtime.routing_absence_verify.retry" );
    block.m_Kind = wxS( "runtime_instruction" );
    block.m_Source = wxS( "ai_runtime" );
    block.m_Text =
            wxS( "The previous final answer claimed there are no routing, tracks, "
                 "or vias after only inconclusive item-query results. Before making "
                 "that claim, call kisurf_query_board_summary to verify "
                 "track_segments and vias. If the summary reports nonzero routing "
                 "objects, retry with canonical filters such as {\"type\":\"tracks\"}, "
                 "{\"type\":\"vias\"}, or {\"type\":\"routing\"}; do not tell the "
                 "user that no action is needed from a narrow or ambiguous zero-count "
                 "query." );
    return block;
}


AI_PROVIDER_INPUT_BLOCK boardCountSummaryRetryBlock()
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "runtime.board_count_summary.retry" );
    block.m_Kind = wxS( "runtime_instruction" );
    block.m_Source = wxS( "ai_runtime" );
    block.m_Text =
            wxS( "The current user request asks for board object counts. "
                 "Do not answer count questions from routing item lists, visual "
                 "context, or chat history. Call kisurf_query_board_summary now "
                 "and answer with the exact summary fields such as "
                 "track_segments, vias, pads, footprints, zones, nets, and "
                 "total_items." );
    return block;
}


AI_PROVIDER_RESPONSE requiredToolCallMissingResponse(
        const AI_PROVIDER_REQUEST& aRequest, const AI_PROVIDER_RESPONSE& aRejectedResponse )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "Required tool call missing" );
    response.m_Body =
            wxS( "The AI provider did not return a required KiSurf tool call for "
                 "this concrete board request, so KiSurf did not execute or trust "
                 "the model's board-state answer. Please retry; the request must "
                 "use the current-board query or atomic/script tools." );
    wxUnusedVar( aRejectedResponse );
    return response;
}


uint64_t jsonUnsignedValue( const nlohmann::json& aObject, const char* aKey )
{
    if( !aObject.is_object() || !aObject.contains( aKey ) )
        return 0;

    const nlohmann::json& value = aObject[aKey];

    if( value.is_number_unsigned() )
        return value.get<uint64_t>();

    if( value.is_number_integer() )
    {
        const int64_t signedValue = value.get<int64_t>();
        return signedValue > 0 ? static_cast<uint64_t>( signedValue ) : 0;
    }

    if( value.is_number_float() )
    {
        const double floatValue = value.get<double>();
        return floatValue > 0 ? static_cast<uint64_t>( floatValue ) : 0;
    }

    return 0;
}


bool jsonHasUnsignedValue( const nlohmann::json& aObject, const char* aKey )
{
    if( !aObject.is_object() || !aObject.contains( aKey ) )
        return false;

    const nlohmann::json& value = aObject[aKey];

    if( value.is_number_unsigned() )
        return true;

    if( value.is_number_integer() )
        return value.get<int64_t>() >= 0;

    if( value.is_number_float() )
        return value.get<double>() >= 0;

    return false;
}


std::optional<BOARD_SUMMARY_COUNTS> boardSummaryCountsFromToolCall(
        const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !aToolCall.m_Allowed
        || aToolCall.m_ToolName != wxS( "kisurf_query_board_summary" ) )
    {
        return std::nullopt;
    }

    nlohmann::json result = nlohmann::json::parse(
            toUtf8String( aToolCall.m_ResultJson ), nullptr, false );

    if( !result.is_object() || !result.contains( "summary" )
        || !result["summary"].is_object() )
    {
        return std::nullopt;
    }

    const nlohmann::json& summary = result["summary"];
    BOARD_SUMMARY_COUNTS counts;

    counts.m_HasTrackSegments = jsonHasUnsignedValue( summary, "track_segments" );
    counts.m_HasVias = jsonHasUnsignedValue( summary, "vias" );
    counts.m_HasPads = jsonHasUnsignedValue( summary, "pads" );
    counts.m_HasFootprints = jsonHasUnsignedValue( summary, "footprints" );
    counts.m_HasZones = jsonHasUnsignedValue( summary, "zones" );
    counts.m_HasNets = jsonHasUnsignedValue( summary, "nets" );
    counts.m_HasTotalItems = jsonHasUnsignedValue( summary, "total_items" )
                             || jsonHasUnsignedValue( summary, "items_total" );

    counts.m_TrackSegments = jsonUnsignedValue( summary, "track_segments" );
    counts.m_Vias = jsonUnsignedValue( summary, "vias" );
    counts.m_Pads = jsonUnsignedValue( summary, "pads" );
    counts.m_Footprints = jsonUnsignedValue( summary, "footprints" );
    counts.m_Zones = jsonUnsignedValue( summary, "zones" );
    counts.m_Nets = jsonUnsignedValue( summary, "nets" );
    counts.m_TotalItems = jsonHasUnsignedValue( summary, "total_items" )
                          ? jsonUnsignedValue( summary, "total_items" )
                          : jsonUnsignedValue( summary, "items_total" );

    return counts;
}


std::optional<BOARD_SUMMARY_COUNTS> latestBoardSummaryCounts(
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    std::optional<BOARD_SUMMARY_COUNTS> latest;

    for( const AI_TOOL_CALL_RECORD& toolCall : aHandledToolCalls )
    {
        if( std::optional<BOARD_SUMMARY_COUNTS> counts =
                    boardSummaryCountsFromToolCall( toolCall ) )
        {
            latest = counts;
        }
    }

    return latest;
}


bool itemTypeIsRouting( const nlohmann::json& aType )
{
    if( !aType.is_string() )
        return false;

    wxString type = wxString::FromUTF8(
            aType.get_ref<const std::string&>().c_str() );
    type.MakeLower();
    type.Replace( wxS( "-" ), wxS( "_" ) );
    type.Replace( wxS( " " ), wxS( "_" ) );

    return type == wxS( "track_segment" ) || type == wxS( "track" )
           || type == wxS( "tracks" ) || type == wxS( "trace" )
           || type == wxS( "traces" ) || type == wxS( "via" )
           || type == wxS( "vias" ) || type == wxS( "route" )
           || type == wxS( "routes" ) || type == wxS( "routing" )
           || type == wxS( "routed_items" );
}


bool filterTargetsRouting( const nlohmann::json& aFilter )
{
    if( !aFilter.is_object() || !aFilter.contains( "type" ) )
        return false;

    const nlohmann::json& type = aFilter["type"];

    if( type.is_array() )
    {
        for( const nlohmann::json& entry : type )
        {
            if( itemTypeIsRouting( entry ) )
                return true;
        }

        return false;
    }

    return itemTypeIsRouting( type );
}


uint64_t routingObservationCount( const AI_TOOL_CALL_RECORD& aToolCall )
{
    nlohmann::json result = nlohmann::json::parse(
            toUtf8String( aToolCall.m_ResultJson ), nullptr, false );

    if( !result.is_object() )
        return 0;

    if( aToolCall.m_ToolName == wxS( "kisurf_query_items" ) )
    {
        const uint64_t reportedCount =
                std::max( jsonUnsignedValue( result, "returned_count" ),
                          jsonUnsignedValue( result, "total_count" ) );
        uint64_t routingItemsInPayload = 0;

        if( result.contains( "items" ) && result["items"].is_array() )
        {
            for( const nlohmann::json& item : result["items"] )
            {
                if( item.is_object() && item.contains( "type" )
                    && itemTypeIsRouting( item["type"] ) )
                {
                    ++routingItemsInPayload;
                }
            }
        }

        if( routingItemsInPayload > 0 )
            return std::max( routingItemsInPayload, reportedCount );

        if( filterTargetsRouting( result.value( "filter", nlohmann::json::object() ) ) )
            return reportedCount;
    }

    if( std::optional<BOARD_SUMMARY_COUNTS> counts =
                boardSummaryCountsFromToolCall( aToolCall ) )
    {
        return counts->RoutingCount();
    }

    return 0;
}


uint64_t nonZeroRoutingToolObservationCount(
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    uint64_t count = 0;

    for( const AI_TOOL_CALL_RECORD& toolCall : aHandledToolCalls )
    {
        if( !toolCall.m_Allowed )
            continue;

        count = std::max( count, routingObservationCount( toolCall ) );
    }

    return count;
}


bool hasBoardSummaryRoutingObservation(
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    for( const AI_TOOL_CALL_RECORD& toolCall : aHandledToolCalls )
    {
        if( std::optional<BOARD_SUMMARY_COUNTS> counts =
                    boardSummaryCountsFromToolCall( toolCall ) )
        {
            if( counts->m_HasTrackSegments || counts->m_HasVias )
                return true;
        }
    }

    return false;
}


std::string canonicalUnsignedString( uint64_t aValue )
{
    return std::to_string( aValue );
}


bool textContainsUnsignedToken( const wxString& aText, uint64_t aValue )
{
    const std::string body = toUtf8String( aText );
    const std::string expected = canonicalUnsignedString( aValue );

    for( size_t ii = 0; ii < body.size(); )
    {
        if( !std::isdigit( static_cast<unsigned char>( body[ii] ) ) )
        {
            ++ii;
            continue;
        }

        size_t end = ii;

        while( end < body.size()
               && std::isdigit( static_cast<unsigned char>( body[end] ) ) )
        {
            ++end;
        }

        std::string token = body.substr( ii, end - ii );
        size_t      firstNonZero = token.find_first_not_of( '0' );

        if( firstNonZero == std::string::npos )
            token = "0";
        else if( firstNonZero > 0 )
            token.erase( 0, firstNonZero );

        if( token == expected )
            return true;

        ii = end;
    }

    return false;
}


bool responseClaimsNoRoutingItems( const wxString& aBody )
{
    wxString body = aBody;
    body.MakeLower();

    const bool mentionsRouting =
            wxContainsAny( body,
                           { wxS( "布线" ), wxS( "走线" ), wxS( "过孔" ),
                             wxS( "track" ), wxS( "tracks" ), wxS( "trace" ),
                             wxS( "traces" ), wxS( "route" ), wxS( "routing" ),
                             wxS( "via" ), wxS( "vias" ) } );

    if( !mentionsRouting )
        return false;

    return wxContainsAny( body,
                          { wxS( "未发现" ), wxS( "没有" ), wxS( "不存在" ),
                            wxS( "无需" ), wxS( "不需要" ), wxS( "无任何" ),
                            wxS( "no " ), wxS( "none" ), wxS( "not found" ),
                            wxS( "zero" ), wxS( "0 " ) } );
}


bool routingAbsenceNeedsVerification(
        const AI_PROVIDER_RESPONSE& aResponse,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    if( aHandledToolCalls.empty() || !aResponse.m_ToolCalls.empty()
        || aResponse.m_Body.IsEmpty()
        || !responseClaimsNoRoutingItems( aResponse.m_Body ) )
    {
        return false;
    }

    if( nonZeroRoutingToolObservationCount( aHandledToolCalls ) > 0 )
        return false;

    return !hasBoardSummaryRoutingObservation( aHandledToolCalls );
}


bool boardCountRequestNeedsSummary( const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_RequestKind != AI_PROVIDER_REQUEST_KIND::Chat )
        return false;

    const wxString text = aRequest.m_UserText.Lower();

    const bool asksForCount =
            wxContainsAny( text,
                           { wxS( "多少" ), wxS( "几个" ), wxS( "数量" ),
                             wxS( "count" ), wxS( "counts" ),
                             wxS( "how many" ) } );

    if( !asksForCount )
        return false;

    return wxContainsAny(
            text,
            { wxS( "track" ), wxS( "tracks" ), wxS( "track segment" ),
              wxS( "track segments" ), wxS( "布线" ), wxS( "走线" ),
              wxS( "routing" ), wxS( "via" ), wxS( "vias" ), wxS( "过孔" ),
              wxS( "pad" ), wxS( "pads" ), wxS( "焊盘" ),
              wxS( "footprint" ), wxS( "footprints" ), wxS( "封装" ),
              wxS( "zone" ), wxS( "zones" ), wxS( "铺铜" ),
              wxS( "net" ), wxS( "nets" ), wxS( "网络" ),
              wxS( "current board" ), wxS( "pcb" ), wxS( "板子" ) } );
}


bool requestMentionsTrackCount( const AI_PROVIDER_REQUEST& aRequest )
{
    const wxString text = aRequest.m_UserText.Lower();

    return wxContainsAny(
            text,
            { wxS( "track" ), wxS( "tracks" ), wxS( "track segment" ),
              wxS( "track segments" ), wxS( "布线" ), wxS( "走线" ),
              wxS( "routing" ), wxS( "route" ), wxS( "trace" ),
              wxS( "traces" ) } );
}


bool requestMentionsViaCount( const AI_PROVIDER_REQUEST& aRequest )
{
    const wxString text = aRequest.m_UserText.Lower();

    return wxContainsAny( text,
                          { wxS( "via" ), wxS( "vias" ), wxS( "过孔" ) } );
}


bool boardCountAnswerNeedsSummary(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_PROVIDER_RESPONSE& aResponse,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    return boardCountRequestNeedsSummary( aRequest )
           && !aHandledToolCalls.empty()
           && aResponse.m_ToolCalls.empty()
           && !aResponse.m_Body.IsEmpty()
           && !hasBoardSummaryRoutingObservation( aHandledToolCalls );
}


bool boardCountAnswerMissingRequestedSummaryFacts(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_PROVIDER_RESPONSE& aResponse,
        const BOARD_SUMMARY_COUNTS& aSummary )
{
    if( !boardCountRequestNeedsSummary( aRequest )
        || aResponse.m_ToolCalls.size() != 0
        || aResponse.m_Body.IsEmpty() )
    {
        return false;
    }

    const bool needsTrack = requestMentionsTrackCount( aRequest );
    const bool needsVia = requestMentionsViaCount( aRequest );

    bool missing = false;

    if( needsTrack && aSummary.m_HasTrackSegments
        && !textContainsUnsignedToken( aResponse.m_Body,
                                       aSummary.m_TrackSegments ) )
    {
        missing = true;
    }

    if( needsVia && aSummary.m_HasVias
        && !textContainsUnsignedToken( aResponse.m_Body, aSummary.m_Vias ) )
    {
        missing = true;
    }

    return missing;
}


bool toolGroundingConflictDetected(
        const AI_PROVIDER_RESPONSE& aResponse,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls,
        uint64_t& aObservedRoutingCount )
{
    aObservedRoutingCount = 0;

    if( aHandledToolCalls.empty() || !aResponse.m_ToolCalls.empty()
        || aResponse.m_Body.IsEmpty() )
    {
        return false;
    }

    aObservedRoutingCount =
            nonZeroRoutingToolObservationCount( aHandledToolCalls );

    return aObservedRoutingCount > 0
           && responseClaimsNoRoutingItems( aResponse.m_Body );
}


AI_PROVIDER_RESPONSE toolGroundingConflictResponse(
        const AI_PROVIDER_REQUEST& aRequest, const AI_PROVIDER_RESPONSE& aRejectedResponse,
        uint64_t aObservedRoutingCount,
        const std::optional<BOARD_SUMMARY_COUNTS>& aObservedSummary )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "Tool-grounding conflict" );

    if( aObservedSummary )
    {
        response.m_Body = wxString::Format(
                wxS( "工具查询结果：%s。模型最终回答声称不存在或无需操作，"
                     "所以 KiSurf 已拦截该回答。请以这些工具字段为准。" ),
                boardSummaryCountsText( *aObservedSummary ).c_str() );
    }
    else
    {
        response.m_Body = wxString::Format(
                wxS( "工具查询显示当前板子上有 %llu 个 routing item(s)，但模型最终回答"
                     "声称不存在或无需操作，所以 KiSurf 已拦截该回答。请以工具结果为准："
                     "模型必须重新调用正确的 current-board 查询或修改工具；如果工具失败，"
                     "必须报告精确的 tool result / error_code / retry_hint。" ),
                static_cast<unsigned long long>( aObservedRoutingCount ) );
    }

    wxUnusedVar( aRejectedResponse );
    return response;
}


AI_PROVIDER_RESPONSE boardSummaryGroundedCountResponse(
        const AI_PROVIDER_REQUEST& aRequest, const AI_PROVIDER_RESPONSE& aRejectedResponse,
        const BOARD_SUMMARY_COUNTS& aSummary )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "Tool-grounded board summary" );
    response.m_Body = wxString::Format( wxS( "工具查询结果：%s。" ),
                                        boardSummaryCountsText( aSummary ).c_str() );
    wxUnusedVar( aRejectedResponse );
    return response;
}


AI_PROVIDER_RESPONSE guardPostToolBoardStateAnswer(
        const AI_PROVIDER_REQUEST& aRequest, AI_PROVIDER_RESPONSE aResponse,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    uint64_t routingCount = 0;
    const std::optional<BOARD_SUMMARY_COUNTS> summary =
            latestBoardSummaryCounts( aHandledToolCalls );

    if( summary && boardCountRequestNeedsSummary( aRequest )
        && ( boardCountAnswerMissingRequestedSummaryFacts( aRequest, aResponse,
                                                           *summary )
             || responseClaimsNoRoutingItems( aResponse.m_Body ) ) )
    {
        return boardSummaryGroundedCountResponse( aRequest, aResponse, *summary );
    }

    if( toolGroundingConflictDetected( aResponse, aHandledToolCalls,
                                       routingCount ) )
    {
        return toolGroundingConflictResponse( aRequest, aResponse, routingCount,
                                              summary );
    }

    return aResponse;
}


wxString promptTraceStatusForResponse( const AI_PROVIDER_RESPONSE& aResponse )
{
    if( aResponse.m_Title.CmpNoCase( wxS( "AI Provider Error" ) ) == 0 )
        return wxS( "provider_error" );

    return wxS( "provider_response" );
}


bool isScriptOutputTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_run_cell" )
           || aToolName == wxS( "script_run_bounded_plan" );
}


bool hasExecutedToolCall( const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( toolCall.m_Executed )
            return true;
    }

    return false;
}


void emitRuntimeEvent( const AI_RUNTIME_STREAM_EVENT_SINK& aSink,
                       const AI_RUNTIME_STREAM_EVENT& aEvent )
{
    if( aSink )
        aSink( aEvent );
}


void emitProviderResponseEvent( const AI_RUNTIME_STREAM_EVENT_SINK& aSink,
                                uint64_t aRequestId,
                                const AI_PROVIDER_RESPONSE& aResponse )
{
    AI_RUNTIME_STREAM_EVENT event;
    event.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::ProviderResponse;
    event.m_RequestId = aRequestId;
    event.m_Message = wxS( "Provider response received." );
    event.m_Response = aResponse;
    emitRuntimeEvent( aSink, event );
}


AI_PROVIDER_STREAM_EVENT_SINK providerStreamSinkForRuntime(
        const AI_RUNTIME_STREAM_EVENT_SINK& aSink, uint64_t aRequestId )
{
    if( !aSink )
        return AI_PROVIDER_STREAM_EVENT_SINK();

    return [aSink, aRequestId]( const AI_PROVIDER_STREAM_EVENT& aEvent )
    {
        AI_RUNTIME_STREAM_EVENT event;
        event.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::TextDelta;
        event.m_RequestId = aRequestId;
        event.m_Message = wxS( "Provider text delta received." );
        event.m_TextDelta = aEvent.m_TextDelta;
        emitRuntimeEvent( aSink, event );
    };
}


AI_PROVIDER_STREAM_EVENT_SINK providerStreamSinkForRequest(
        const AI_RUNTIME_STREAM_EVENT_SINK& aSink, uint64_t aRequestId,
        const AI_PROVIDER_REQUEST& aProviderRequest )
{
    wxUnusedVar( aProviderRequest );

    const char* streamingEnabled = std::getenv( "KISURF_AI_ENABLE_PROVIDER_STREAMING" );

    if( streamingEnabled && std::string( streamingEnabled ) == "0" )
        return AI_PROVIDER_STREAM_EVENT_SINK();

    return providerStreamSinkForRuntime( aSink, aRequestId );
}


nlohmann::json executedToolCallRefsJson( const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    nlohmann::json refs = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( !toolCall.m_Executed )
            continue;

        refs.push_back( {
                { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
                { "tool_name", toUtf8String( toolCall.m_ToolName ) },
                { "allowed", toolCall.m_Allowed },
                { "executed", toolCall.m_Executed },
                { "error_code", toUtf8String( toolCall.m_ErrorCode ) }
        } );
    }

    return refs;
}


void copyIfPresent( nlohmann::json& aTarget, const nlohmann::json& aSource,
                    const char* aKey )
{
    if( aSource.contains( aKey ) )
        aTarget[aKey] = aSource[aKey];
}


nlohmann::json contextVersionJson( const AI_CONTEXT_VERSION& aVersion )
{
    return {
        { "document_revision", aVersion.m_DocumentRevision },
        { "selection_revision", aVersion.m_SelectionRevision },
        { "view_revision", aVersion.m_ViewRevision }
    };
}


nlohmann::json recoveryBasisJson( const AI_PROVIDER_REQUEST& aRequest,
                                  const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    nlohmann::json basis = {
        { "requires_checkpoint_resume", true },
        { "executed_tool_result_count", 0 },
        { "board_state_version", contextVersionJson( aRequest.m_ContextVersion ) },
        { "tool_results", nlohmann::json::array() }
    };

    size_t executedCount = 0;

    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( !toolCall.m_Executed )
            continue;

        ++executedCount;

        nlohmann::json tool = {
            { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
            { "tool_name", toUtf8String( toolCall.m_ToolName ) },
            { "allowed", toolCall.m_Allowed },
            { "executed", toolCall.m_Executed }
        };

        nlohmann::json result = nlohmann::json::parse(
                toUtf8String( toolCall.m_ResultJson ), nullptr, false );

        if( result.is_object() )
        {
            copyIfPresent( tool, result, "session_id" );
            copyIfPresent( tool, result, "hidden_session_id" );
            copyIfPresent( tool, result, "checkpoint_id" );
            copyIfPresent( tool, result, "rollback_checkpoint_id" );
            copyIfPresent( tool, result, "preview_id" );
            copyIfPresent( tool, result, "attempt_id" );

            if( result.contains( "session_journal" )
                && result["session_journal"].is_object()
                && result["session_journal"].contains( "operations" )
                && result["session_journal"]["operations"].is_array() )
            {
                tool["session_journal"] = result["session_journal"];
                tool["journal_operation_count"] =
                        result["session_journal"]["operations"].size();
            }

            if( result.contains( "attempt_session_journal" )
                && result["attempt_session_journal"].is_object()
                && result["attempt_session_journal"].contains( "operations" )
                && result["attempt_session_journal"]["operations"].is_array() )
            {
                tool["attempt_session_journal"] = result["attempt_session_journal"];
                tool["attempt_journal_operation_count"] =
                        result["attempt_session_journal"]["operations"].size();
            }
        }

        basis["tool_results"].push_back( std::move( tool ) );
    }

    basis["executed_tool_result_count"] = executedCount;
    return basis;
}


void markPostSideEffectProviderFailure(
        AI_PROVIDER_RESPONSE& aResponse,
        const AI_PROVIDER_REQUEST& aRequest,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    if( promptTraceStatusForResponse( aResponse ) != wxS( "provider_error" )
        || !hasExecutedToolCall( aHandledToolCalls ) )
    {
        return;
    }

    nlohmann::json trace = nlohmann::json::parse(
            toUtf8String( aResponse.m_ProviderTraceJson ), nullptr, false );

    if( trace.is_discarded() || !trace.is_object() )
    {
        trace = nlohmann::json::object();
        trace["schema"] = { { "name", "kisurf.ai.provider_trace" },
                            { "version", 1 } };
    }

    trace["runtime_guard"] = {
        { "reason", "post_side_effect_ambiguity" },
        { "action", "checkpoint_or_journal_recovery_required" },
        { "replay_policy", "do_not_blindly_reexecute_tools" },
        { "executed_tool_calls", executedToolCallRefsJson( aHandledToolCalls ) },
        { "recovery_basis", recoveryBasisJson( aRequest, aHandledToolCalls ) }
    };

    aResponse.m_ProviderTraceJson = fromUtf8String( trace.dump() );
}


void appendPromptTrace( AI_PROMPT_TRACE_STORE* aStore,
                        const AI_PROVIDER_REQUEST& aRequest,
                        const AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aStore )
        return;

    wxString error;
    aStore->Append( aRequest, promptTraceStatusForResponse( aResponse ),
                    aResponse.m_ProviderTraceJson, error );
}


void archiveLargeToolResult( AI_ARTIFACT_STORE* aStore,
                             const AI_PROVIDER_REQUEST& aRequest,
                             const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !aStore || aToolCall.m_ResultJson.IsEmpty()
        || aToolCall.m_ResultJson.length() <= aRequest.m_MaxToolResultChars )
    {
        return;
    }

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( isScriptOutputTool( aToolCall.m_ToolName ) )
    {
        AiStoreScriptOutputArtifact( aRequest.m_ContextSnapshot.m_ProjectId,
                                     aRequest.m_ContextSnapshot.m_DocumentId,
                                     wxS( "chat" ), wxS( "trace" ),
                                     aToolCall.m_ToolCallId,
                                     aToolCall.m_ToolName,
                                     aToolCall.m_ArgumentsJson,
                                     aToolCall.m_ResultJson,
                                     *aStore, artifact, error );
        return;
    }

    AiStoreToolResultArtifact( aRequest.m_ContextSnapshot.m_ProjectId,
                               aRequest.m_ContextSnapshot.m_DocumentId,
                               wxS( "chat" ), wxS( "trace" ),
                               aToolCall.m_ToolCallId,
                               aToolCall.m_ToolName,
                               aToolCall.m_ResultJson,
                               *aStore, artifact, error );
}


void archiveProviderRecoveryArtifact( AI_ARTIFACT_STORE* aStore,
                                      const AI_PROVIDER_REQUEST& aRequest,
                                      AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aStore || aResponse.m_ProviderTraceJson.IsEmpty() )
        return;

    nlohmann::json trace = nlohmann::json::parse(
            toUtf8String( aResponse.m_ProviderTraceJson ), nullptr, false );

    if( trace.is_discarded() || !trace.is_object()
        || !trace.contains( "runtime_guard" )
        || !trace["runtime_guard"].is_object()
        || !trace["runtime_guard"].contains( "recovery_basis" ) )
    {
        return;
    }

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( !AiStoreProviderRecoveryArtifact(
                aRequest.m_ContextSnapshot.m_ProjectId,
                aRequest.m_ContextSnapshot.m_DocumentId,
                wxS( "chat" ), wxS( "trace" ), wxS( "AI_RUNTIME" ),
                aRequest.m_RequestId, aResponse.m_ProviderTraceJson,
                *aStore, artifact, error ) )
    {
        return;
    }

    trace["runtime_guard"]["recovery_artifact_ref"] = {
        { "uri", toUtf8String( artifact.m_Uri ) },
        { "kind", "provider_recovery" },
        { "retention", toUtf8String( artifact.m_RetentionClass ) },
        { "request_id", aRequest.m_RequestId }
    };

    aResponse.m_ProviderTraceJson = fromUtf8String( trace.dump() );
}


nlohmann::json parseObjectOrEmpty( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ),
                                                   nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


void archiveVisualObservationForProviderInput( AI_ARTIFACT_STORE* aStore,
                                               AI_PROVIDER_REQUEST& aRequest,
                                               const wxString& aAgentKind )
{
    AI_VISUAL_SNAPSHOT& visual = aRequest.m_ContextSnapshot.m_Visual;

    if( !aStore || !visual.HasPixels() )
        return;

    AI_VISUAL_OBSERVATION_ARTIFACT visualArtifact;
    visualArtifact.m_Snapshot = visual;
    visualArtifact.m_SidecarJson = visual.m_SidecarJson;

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( !AiStoreVisualObservationArtifact(
                aRequest.m_ContextSnapshot.m_ProjectId,
                aRequest.m_ContextSnapshot.m_DocumentId,
                aAgentKind, wxS( "trace" ), visualArtifact,
                *aStore, artifact, error ) )
    {
        return;
    }

    nlohmann::json sidecar = parseObjectOrEmpty( visual.m_SidecarJson );

    sidecar["artifact_ref"] = {
        { "uri", toUtf8String( artifact.m_Uri ) },
        { "kind", "visual_observation" },
        { "retention", toUtf8String( artifact.m_RetentionClass ) },
        { "frame_id", toUtf8String( visual.m_FrameId ) },
        { "frame_kind", toUtf8String( visual.m_FrameKind ) }
    };

    visual.m_SidecarJson = fromUtf8String( sidecar.dump() );
}
} // namespace


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 256 ),
        m_ActivityLog( &m_OwnedActivityLog )
{
}


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider,
                        AI_ACTIVITY_LOG& aActivityLog ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 0 ),
        m_ActivityLog( &aActivityLog )
{
}


AI_PROVIDER_RESPONSE AI_RUNTIME::Submit( AI_PROVIDER_REQUEST aRequest )
{
    return Submit( std::move( aRequest ), AI_RUNTIME_STREAM_EVENT_SINK() );
}


AI_PROVIDER_RESPONSE AI_RUNTIME::Submit( AI_PROVIDER_REQUEST aRequest,
                                         AI_RUNTIME_STREAM_EVENT_SINK aEventSink )
{
    aRequest.m_RequestId = m_NextRequestId.fetch_add( 1 );
    AI_TOOL_CALL_HANDLER* handler = nullptr;
    AI_PROMPT_TRACE_STORE* traceStore = nullptr;
    AI_ARTIFACT_STORE* artifactStore = nullptr;
    AI_RUNTIME_STREAM_EVENT_SINK eventSink = std::move( aEventSink );

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        handler = m_ToolCallHandler;
        traceStore = m_PromptTraceStore;
        artifactStore = m_ArtifactStore;

        if( !eventSink )
            eventSink = m_StreamEventSink;
    }

    AI_RUNTIME_STREAM_EVENT startedEvent;
    startedEvent.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::RequestStarted;
    startedEvent.m_RequestId = aRequest.m_RequestId;
    startedEvent.m_Message = wxS( "Request started." );
    emitRuntimeEvent( eventSink, startedEvent );

    archiveVisualObservationForProviderInput( artifactStore, aRequest, wxS( "chat" ) );

    AI_PROVIDER_REQUEST providerRequest = AiCompileProviderInputWithBudget( aRequest );

    AI_PROVIDER_RESPONSE response = m_Provider->Generate(
            providerRequest,
            providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                              providerRequest ) );
    promoteStrictTextualToolCall( response );
    normalizeToolCallArguments( response.m_ToolCalls );
    emitProviderResponseEvent( eventSink, aRequest.m_RequestId, response );

    appendPromptTrace( traceStore, providerRequest, response );

    if( response.m_ToolCalls.empty()
        && chatRequestNeedsAtLeastOneToolCall( aRequest, response ) )
    {
        AI_PROVIDER_REQUEST retryRequest = aRequest;
        retryRequest.m_ContextCompiled = false;
        retryRequest.m_CompiledUserMessageText.Clear();
        retryRequest.m_PromptTraceJson.Clear();
        retryRequest.m_RequireToolCall = true;
        retryRequest.m_ProviderInputBlocks.push_back( toolRequiredRetryBlock() );

        providerRequest = AiCompileProviderInputWithBudget( retryRequest );
        response = m_Provider->Generate(
                providerRequest,
                providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                              providerRequest ) );
        promoteStrictTextualToolCall( response );
        normalizeToolCallArguments( response.m_ToolCalls );
        emitProviderResponseEvent( eventSink, aRequest.m_RequestId, response );
        appendPromptTrace( traceStore, providerRequest, response );

        if( response.m_ToolCalls.empty()
            && chatRequestNeedsAtLeastOneToolCall( aRequest, response ) )
        {
            response = requiredToolCallMissingResponse( aRequest, response );
            emitProviderResponseEvent( eventSink, aRequest.m_RequestId, response );
            appendPromptTrace( traceStore, providerRequest, response );
        }
    }

    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls;
    size_t                           toolRounds = 0;

    while( !response.m_ToolCalls.empty() && toolRounds < aRequest.m_MaxToolRounds )
    {
        std::vector<AI_TOOL_CALL_RECORD> roundToolCalls = std::move( response.m_ToolCalls );

        if( repeatsHandledToolCall( roundToolCalls, handledToolCalls ) )
            break;

        for( AI_TOOL_CALL_RECORD& toolCall : roundToolCalls )
        {
            toolCall.m_RequestId = aRequest.m_RequestId;
            recordModelToolCall( *m_ActivityLog, providerRequest, toolCall );

            AI_RUNTIME_STREAM_EVENT toolStartedEvent;
            toolStartedEvent.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::ToolCallStarted;
            toolStartedEvent.m_RequestId = aRequest.m_RequestId;
            toolStartedEvent.m_ToolCall = toolCall;
            toolStartedEvent.m_Message =
                    wxString::Format( wxS( "Executing tool: %s" ),
                                      toolCall.m_ToolName );
            emitRuntimeEvent( eventSink, toolStartedEvent );

            AI_TOOL_INVOCATION_RESULT result;

            if( handler )
            {
                result = handler->HandleToolCall( providerRequest, toolCall );
            }
            else
            {
                result.m_RequestId = aRequest.m_RequestId;
                result.m_ToolCallId = toolCall.m_ToolCallId;
                result.m_ActionName = toolCall.m_ToolName;
                result.m_Allowed = false;
                result.m_Executed = false;
                result.m_ErrorCode = wxS( "no_tool_handler" );
                result.m_Message = wxS( "No tool handler installed." );
                result.m_ResultJson = deniedToolResultJson( result );
            }

            copyToolResult( toolCall, result );
            recordToolResult( *m_ActivityLog, result );
            recordPythonWorkerEvents( *m_ActivityLog, result );
            archiveLargeToolResult( artifactStore, aRequest, toolCall );

            AI_RUNTIME_STREAM_EVENT toolFinishedEvent;
            toolFinishedEvent.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::ToolCallFinished;
            toolFinishedEvent.m_RequestId = aRequest.m_RequestId;
            toolFinishedEvent.m_ToolCall = toolCall;
            toolFinishedEvent.m_ToolResult = result;
            toolFinishedEvent.m_Message = result.m_Message;
            emitRuntimeEvent( eventSink, toolFinishedEvent );
        }

        handledToolCalls.insert( handledToolCalls.end(),
                                 std::make_move_iterator( roundToolCalls.begin() ),
                                 std::make_move_iterator( roundToolCalls.end() ) );
        ++toolRounds;

        AI_PROVIDER_REQUEST continuationRequest = aRequest;
        continuationRequest.m_ToolResults = handledToolCalls;
        continuationRequest.m_ContextCompiled = false;
        continuationRequest.m_CompiledUserMessageText.Clear();
        continuationRequest.m_PromptTraceJson.Clear();

        providerRequest = AiCompileProviderInputWithBudget( continuationRequest );

        AI_PROVIDER_RESPONSE continuationResponse = m_Provider->Generate(
                providerRequest,
                providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                              providerRequest ) );
        promoteStrictTextualToolCall( continuationResponse );
        normalizeToolCallArguments( continuationResponse.m_ToolCalls );
        emitProviderResponseEvent( eventSink, aRequest.m_RequestId, continuationResponse );
        markPostSideEffectProviderFailure( continuationResponse, providerRequest,
                                           handledToolCalls );
        archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                         continuationResponse );
        appendPromptTrace( traceStore, providerRequest, continuationResponse );

        if( boardCountAnswerNeedsSummary( aRequest, continuationResponse,
                                          handledToolCalls )
            && toolRounds < aRequest.m_MaxToolRounds )
        {
            AI_PROVIDER_REQUEST summaryRequest = aRequest;
            summaryRequest.m_ToolResults = handledToolCalls;
            summaryRequest.m_ContextCompiled = false;
            summaryRequest.m_CompiledUserMessageText.Clear();
            summaryRequest.m_PromptTraceJson.Clear();
            summaryRequest.m_RequireToolCall = true;
            summaryRequest.m_ProviderInputBlocks.push_back(
                    boardCountSummaryRetryBlock() );

            providerRequest = AiCompileProviderInputWithBudget( summaryRequest );
            continuationResponse = m_Provider->Generate(
                    providerRequest,
                    providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                                  providerRequest ) );
            promoteStrictTextualToolCall( continuationResponse );
            normalizeToolCallArguments( continuationResponse.m_ToolCalls );
            emitProviderResponseEvent( eventSink, aRequest.m_RequestId,
                                       continuationResponse );
            markPostSideEffectProviderFailure( continuationResponse, providerRequest,
                                               handledToolCalls );
            archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                             continuationResponse );
            appendPromptTrace( traceStore, providerRequest,
                               continuationResponse );
        }
        else if( routingAbsenceNeedsVerification( continuationResponse,
                                                  handledToolCalls )
            && toolRounds < aRequest.m_MaxToolRounds )
        {
            AI_PROVIDER_REQUEST verifyRequest = aRequest;
            verifyRequest.m_ToolResults = handledToolCalls;
            verifyRequest.m_ContextCompiled = false;
            verifyRequest.m_CompiledUserMessageText.Clear();
            verifyRequest.m_PromptTraceJson.Clear();
            verifyRequest.m_RequireToolCall = true;
            verifyRequest.m_ProviderInputBlocks.push_back(
                    routingAbsenceVerificationRetryBlock() );

            providerRequest = AiCompileProviderInputWithBudget( verifyRequest );
            continuationResponse = m_Provider->Generate(
                    providerRequest,
                    providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                                  providerRequest ) );
            promoteStrictTextualToolCall( continuationResponse );
            normalizeToolCallArguments( continuationResponse.m_ToolCalls );
            emitProviderResponseEvent( eventSink, aRequest.m_RequestId,
                                       continuationResponse );
            markPostSideEffectProviderFailure( continuationResponse, providerRequest,
                                               handledToolCalls );
            archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                             continuationResponse );
            appendPromptTrace( traceStore, providerRequest,
                               continuationResponse );
        }

        continuationResponse.m_RequestId = aRequest.m_RequestId;
        response = std::move( continuationResponse );
    }

    if( !handledToolCalls.empty() && !response.m_ToolCalls.empty()
        && toolRounds >= aRequest.m_MaxToolRounds )
    {
        AI_PROVIDER_REQUEST finalRequest =
                finalAnswerRequestAfterToolBudget( aRequest, handledToolCalls,
                                                   toolRounds );

        providerRequest = AiCompileProviderInputWithBudget( finalRequest );

        AI_PROVIDER_RESPONSE finalResponse = m_Provider->Generate(
                providerRequest,
                providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                              providerRequest ) );
        promoteStrictTextualToolCall( finalResponse );
        normalizeToolCallArguments( finalResponse.m_ToolCalls );
        emitProviderResponseEvent( eventSink, aRequest.m_RequestId, finalResponse );
        markPostSideEffectProviderFailure( finalResponse, providerRequest,
                                           handledToolCalls );
        archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                         finalResponse );
        appendPromptTrace( traceStore, providerRequest, finalResponse );
        finalResponse.m_RequestId = aRequest.m_RequestId;

        if( !finalResponse.m_ToolCalls.empty() )
        {
            finalResponse.m_ToolCalls.clear();

            if( finalResponse.m_Body.IsEmpty()
                || finalResponse.m_Body == wxS( "Tool call requested." ) )
            {
                finalResponse.m_Body =
                        wxS( "Tool round budget was exhausted after running the "
                             "available tools. Review the tool results in the log." );
            }
        }

        response = std::move( finalResponse );
    }

    uint64_t groundingConflictRoutingCount = 0;
    const std::optional<BOARD_SUMMARY_COUNTS> groundingConflictSummary =
            latestBoardSummaryCounts( handledToolCalls );

    if( toolGroundingConflictDetected( response, handledToolCalls,
                                       groundingConflictRoutingCount )
        && !( groundingConflictSummary
              && boardCountRequestNeedsSummary( aRequest ) ) )
    {
        AI_PROVIDER_REQUEST groundingRetryRequest = aRequest;
        groundingRetryRequest.m_ToolResults = handledToolCalls;
        groundingRetryRequest.m_ContextCompiled = false;
        groundingRetryRequest.m_CompiledUserMessageText.Clear();
        groundingRetryRequest.m_PromptTraceJson.Clear();
        groundingRetryRequest.m_RequireToolCall = false;
        groundingRetryRequest.m_ProviderInputBlocks.push_back(
                toolGroundingConflictRetryBlock( groundingConflictRoutingCount,
                                                 groundingConflictSummary,
                                                 response ) );

        providerRequest = AiCompileProviderInputWithBudget( groundingRetryRequest );

        AI_PROVIDER_RESPONSE groundingRetryResponse = m_Provider->Generate(
                providerRequest,
                providerStreamSinkForRequest( eventSink, aRequest.m_RequestId,
                                              providerRequest ) );
        promoteStrictTextualToolCall( groundingRetryResponse );
        normalizeToolCallArguments( groundingRetryResponse.m_ToolCalls );
        emitProviderResponseEvent( eventSink, aRequest.m_RequestId,
                                   groundingRetryResponse );
        markPostSideEffectProviderFailure( groundingRetryResponse, providerRequest,
                                           handledToolCalls );
        archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                         groundingRetryResponse );
        appendPromptTrace( traceStore, providerRequest, groundingRetryResponse );
        groundingRetryResponse.m_RequestId = aRequest.m_RequestId;

        if( !groundingRetryResponse.m_ToolCalls.empty() )
        {
            groundingRetryResponse.m_ToolCalls.clear();

            if( groundingRetryResponse.m_Body.IsEmpty()
                || groundingRetryResponse.m_Body == wxS( "Tool call requested." ) )
            {
                groundingRetryResponse.m_Body =
                        wxS( "The provider requested another tool after KiSurf "
                             "reported a tool-result grounding conflict. Retry the "
                             "request; the final answer must be grounded in the "
                             "already returned tool results or call the needed tool "
                             "inside the normal tool loop." );
            }
        }

        response = std::move( groundingRetryResponse );
    }

    response = guardPostToolBoardStateAnswer( aRequest, std::move( response ),
                                              handledToolCalls );

    if( !handledToolCalls.empty() )
        response.m_ToolCalls = std::move( handledToolCalls );

    AI_TRACE_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_Request = providerRequest;
    record.m_Response = response;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        m_TraceRecords.push_back( record );
    }

    AI_RUNTIME_STREAM_EVENT finalEvent;
    finalEvent.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse;
    finalEvent.m_RequestId = aRequest.m_RequestId;
    finalEvent.m_Message = wxS( "Final response received." );
    finalEvent.m_TextDelta = response.m_Body;
    finalEvent.m_Response = response;
    emitRuntimeEvent( eventSink, finalEvent );

    return response;
}


bool AI_RUNTIME::Cancel( uint64_t aRequestId )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    for( AI_TRACE_RECORD& record : m_TraceRecords )
    {
        if( record.m_RequestId == aRequestId )
        {
            record.m_Cancelled = true;
            return true;
        }
    }

    return false;
}


void AI_RUNTIME::SetProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    if( !aProvider )
        return;

    std::lock_guard<std::mutex> lock( m_Mutex );
    m_Provider = std::move( aProvider );
}


void AI_RUNTIME::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ToolCallHandler = aHandler;
}


void AI_RUNTIME::SetStreamEventSink( AI_RUNTIME_STREAM_EVENT_SINK aSink )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_StreamEventSink = std::move( aSink );
}


void AI_RUNTIME::SetPromptTraceStore( AI_PROMPT_TRACE_STORE* aStore )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_PromptTraceStore = aStore;
}


void AI_RUNTIME::SetArtifactStore( AI_ARTIFACT_STORE* aStore )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ArtifactStore = aStore;
}


std::vector<AI_TRACE_RECORD> AI_RUNTIME::TraceRecords() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_TraceRecords;
}


std::vector<AI_ACTIVITY_RECORD> AI_RUNTIME::ActivityRecords() const
{
    return m_ActivityLog->Records();
}
