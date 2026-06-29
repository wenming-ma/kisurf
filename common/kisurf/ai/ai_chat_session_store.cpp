#include <kisurf/ai/ai_chat_session_store.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


nlohmann::json messageToJson( const AI_CHAT_SESSION_MESSAGE_RECORD& aMessage )
{
    return {
        { "role", toUtf8String( aMessage.m_Role ) },
        { "text", toUtf8String( aMessage.m_Text ) }
    };
}


AI_TOOL_CALL_RECORD toolCallFromJson( const nlohmann::json& aToolCall )
{
    AI_TOOL_CALL_RECORD record;

    if( aToolCall.is_object() )
    {
        record.m_RequestId = aToolCall.value( "request_id", 0ull );
        record.m_ToolCallId =
                fromUtf8String( aToolCall.value( "tool_call_id", std::string() ) );
        record.m_ToolName =
                fromUtf8String( aToolCall.value( "tool_name", std::string() ) );
        record.m_ArgumentsJson =
                fromUtf8String( aToolCall.value( "arguments_json", std::string() ) );
        record.m_ResultJson =
                fromUtf8String( aToolCall.value( "result_json", std::string() ) );
        record.m_Allowed = aToolCall.value( "allowed", false );
        record.m_Executed = aToolCall.value( "executed", false );
        record.m_ErrorCode =
                fromUtf8String( aToolCall.value( "error_code", std::string() ) );
        record.m_Message =
                fromUtf8String( aToolCall.value( "message", std::string() ) );
    }

    return record;
}


nlohmann::json toolCallToJson( const AI_TOOL_CALL_RECORD& aToolCall )
{
    return {
        { "request_id", aToolCall.m_RequestId },
        { "tool_call_id", toUtf8String( aToolCall.m_ToolCallId ) },
        { "tool_name", toUtf8String( aToolCall.m_ToolName ) },
        { "arguments_json", toUtf8String( aToolCall.m_ArgumentsJson ) },
        { "result_json", toUtf8String( aToolCall.m_ResultJson ) },
        { "allowed", aToolCall.m_Allowed },
        { "executed", aToolCall.m_Executed },
        { "error_code", toUtf8String( aToolCall.m_ErrorCode ) },
        { "message", toUtf8String( aToolCall.m_Message ) }
    };
}


AI_CHAT_SESSION_MESSAGE_RECORD messageFromJson( const nlohmann::json& aMessage )
{
    AI_CHAT_SESSION_MESSAGE_RECORD record;

    if( aMessage.is_object() )
    {
        record.m_Role = fromUtf8String( aMessage.value( "role", std::string() ) );
        record.m_Text = fromUtf8String( aMessage.value( "text", std::string() ) );
    }

    return record;
}
}


AI_CHAT_SESSION_STORE::AI_CHAT_SESSION_STORE() :
        AI_CHAT_SESSION_STORE( DefaultDirectory() )
{
}


AI_CHAT_SESSION_STORE::AI_CHAT_SESSION_STORE( wxString aDirectory ) :
        m_Directory( std::move( aDirectory ) )
{
}


wxString AI_CHAT_SESSION_STORE::DefaultDirectory()
{
    wxString base = wxStandardPaths::Get().GetUserLocalDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetUserDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetTempDir();

    wxFileName path;
    path.AssignDir( base );
    path.AppendDir( wxS( "ai" ) );
    path.AppendDir( wxS( "chat_sessions" ) );
    return path.GetFullPath();
}


wxString AI_CHAT_SESSION_STORE::SessionPath( uint64_t aConversationId ) const
{
    wxFileName path;
    path.AssignDir( m_Directory );
    path.SetFullName( wxString::Format(
            wxS( "chat_session_%llu.json" ),
            static_cast<unsigned long long>( aConversationId ) ) );
    return path.GetFullPath();
}


uint64_t AI_CHAT_SESSION_STORE::NextConversationId(
        uint64_t aMinimumConversationId ) const
{
    uint64_t conversationId = std::max<uint64_t>( 1, aMinimumConversationId );

    while( wxFileExists( SessionPath( conversationId ) ) )
        ++conversationId;

    return conversationId;
}


bool AI_CHAT_SESSION_STORE::WriteSession( const AI_CHAT_SESSION_RECORD& aRecord,
                                          wxString& aError ) const
{
    if( aRecord.m_ConversationId == 0 )
    {
        aError = wxS( "Chat session conversation id must be non-zero." );
        return false;
    }

    wxFileName directory;
    directory.AssignDir( m_Directory );

    if( !directory.DirExists() )
    {
        if( !directory.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format(
                    wxS( "Unable to create chat session directory: %s" ),
                    m_Directory );
            return false;
        }
    }

    nlohmann::json messages = nlohmann::json::array();

    for( const AI_CHAT_SESSION_MESSAGE_RECORD& message : aRecord.m_Messages )
        messages.push_back( messageToJson( message ) );

    nlohmann::json toolCalls = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolCall : aRecord.m_ToolCalls )
        toolCalls.push_back( toolCallToJson( toolCall ) );

    nlohmann::json payload = {
        { "schema", { { "name", "kisurf.ai.chat_session" }, { "version", 1 } } },
        { "conversation_id", aRecord.m_ConversationId },
        { "project_id", toUtf8String( aRecord.m_ProjectId ) },
        { "document_id", toUtf8String( aRecord.m_DocumentId ) },
        { "message_count", aRecord.m_Messages.size() },
        { "tool_call_count", aRecord.m_ToolCalls.size() },
        { "messages", std::move( messages ) },
        { "tool_calls", std::move( toolCalls ) }
    };

    const wxString path = SessionPath( aRecord.m_ConversationId );
    wxFFile       file( path, wxS( "wb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open chat session file: %s" ),
                                   path );
        return false;
    }

    file.Write( fromUtf8String( payload.dump( 2 ) ), wxConvUTF8 );
    file.Close();

    aError.clear();
    return true;
}


AI_CHAT_SESSION_RECORD AI_CHAT_SESSION_STORE::LoadSession(
        uint64_t aConversationId, wxString& aError ) const
{
    AI_CHAT_SESSION_RECORD record;
    const wxString         path = SessionPath( aConversationId );

    if( !wxFileExists( path ) )
    {
        aError = wxString::Format( wxS( "Chat session file does not exist: %s" ),
                                   path );
        return record;
    }

    wxFFile file( path, wxS( "rb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open chat session file: %s" ),
                                   path );
        return record;
    }

    wxString content;

    if( !file.ReadAll( &content, wxConvUTF8 ) )
    {
        aError = wxString::Format( wxS( "Unable to read chat session file: %s" ),
                                   path );
        return record;
    }

    nlohmann::json payload =
            nlohmann::json::parse( toUtf8String( content ), nullptr, false );

    if( payload.is_discarded() || !payload.is_object() )
    {
        aError = wxString::Format( wxS( "Invalid chat session JSON: %s" ), path );
        return record;
    }

    record.m_ConversationId = payload.value( "conversation_id", 0ull );
    record.m_ProjectId = fromUtf8String( payload.value( "project_id", std::string() ) );
    record.m_DocumentId = fromUtf8String( payload.value( "document_id", std::string() ) );

    auto messagesIt = payload.find( "messages" );

    if( messagesIt != payload.end() && messagesIt->is_array() )
    {
        for( const nlohmann::json& message : *messagesIt )
            record.m_Messages.push_back( messageFromJson( message ) );
    }

    auto toolCallsIt = payload.find( "tool_calls" );

    if( toolCallsIt != payload.end() && toolCallsIt->is_array() )
    {
        for( const nlohmann::json& toolCall : *toolCallsIt )
            record.m_ToolCalls.push_back( toolCallFromJson( toolCall ) );
    }

    aError.clear();
    return record;
}
