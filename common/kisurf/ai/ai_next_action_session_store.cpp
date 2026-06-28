#include <kisurf/ai/ai_next_action_session_store.h>

#include <nlohmann/json.hpp>

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


nlohmann::json parseJsonOrString( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() )
        return toUtf8String( aText );

    return parsed;
}


nlohmann::json stepToJson( const AI_NEXT_ACTION_SESSION_STEP_RECORD& aStep )
{
    nlohmann::json attemptIds = nlohmann::json::array();

    for( uint64_t attemptId : aStep.m_AttemptIds )
        attemptIds.push_back( attemptId );

    return {
        { "step_id", aStep.m_StepId },
        { "status", toUtf8String( aStep.m_Status ) },
        { "suggestion_stream_id", toUtf8String( aStep.m_SuggestionStreamId ) },
        { "observation_packet_id", aStep.m_ObservationPacketId },
        { "published_suggestion_id", aStep.m_PublishedSuggestionId },
        { "attempt_ids", std::move( attemptIds ) },
        { "semantic_event", parseJsonOrString( aStep.m_SemanticEventJson ) },
        { "observation_packet", parseJsonOrString( aStep.m_ObservationPacketJson ) },
        { "llm_decision", parseJsonOrString( aStep.m_LlmDecisionJson ) },
        { "llm_decision_tool_results",
          parseJsonOrString( aStep.m_LlmDecisionToolResultsJson ) },
        { "review_decision", parseJsonOrString( aStep.m_ReviewDecisionJson ) },
        { "review_tool_results",
          parseJsonOrString( aStep.m_ReviewToolResultsJson ) }
    };
}


AI_NEXT_ACTION_SESSION_STEP_RECORD stepFromJson( const nlohmann::json& aStep )
{
    AI_NEXT_ACTION_SESSION_STEP_RECORD record;

    if( !aStep.is_object() )
        return record;

    record.m_StepId = aStep.value( "step_id", 0ull );
    record.m_Status =
            fromUtf8String( aStep.value( "status", std::string() ) );
    record.m_SuggestionStreamId =
            fromUtf8String( aStep.value( "suggestion_stream_id", std::string() ) );
    record.m_ObservationPacketId =
            aStep.value( "observation_packet_id", 0ull );
    record.m_PublishedSuggestionId =
            aStep.value( "published_suggestion_id", 0ull );

    if( aStep.contains( "attempt_ids" ) && aStep["attempt_ids"].is_array() )
    {
        for( const nlohmann::json& attemptId : aStep["attempt_ids"] )
        {
            if( attemptId.is_number_unsigned() )
                record.m_AttemptIds.push_back( attemptId.get<uint64_t>() );
        }
    }

    auto jsonFieldToText =
            []( const nlohmann::json& aObject, const char* aKey ) -> wxString
            {
                if( !aObject.contains( aKey ) )
                    return wxString();

                if( aObject[aKey].is_string() )
                    return fromUtf8String( aObject[aKey].get<std::string>() );

                return fromUtf8String( aObject[aKey].dump() );
            };

    record.m_SemanticEventJson = jsonFieldToText( aStep, "semantic_event" );
    record.m_ObservationPacketJson = jsonFieldToText( aStep, "observation_packet" );
    record.m_LlmDecisionJson = jsonFieldToText( aStep, "llm_decision" );
    record.m_LlmDecisionToolResultsJson =
            jsonFieldToText( aStep, "llm_decision_tool_results" );
    record.m_ReviewDecisionJson = jsonFieldToText( aStep, "review_decision" );
    record.m_ReviewToolResultsJson = jsonFieldToText( aStep, "review_tool_results" );
    return record;
}
}


AI_NEXT_ACTION_SESSION_STORE::AI_NEXT_ACTION_SESSION_STORE() :
        AI_NEXT_ACTION_SESSION_STORE( DefaultDirectory() )
{
}


AI_NEXT_ACTION_SESSION_STORE::AI_NEXT_ACTION_SESSION_STORE( wxString aDirectory ) :
        m_Directory( std::move( aDirectory ) )
{
}


wxString AI_NEXT_ACTION_SESSION_STORE::DefaultDirectory()
{
    wxString base = wxStandardPaths::Get().GetUserLocalDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetUserDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetTempDir();

    wxFileName path;
    path.AssignDir( base );
    path.AppendDir( wxS( "ai" ) );
    path.AppendDir( wxS( "next_action_sessions" ) );
    return path.GetFullPath();
}


wxString AI_NEXT_ACTION_SESSION_STORE::SessionPath(
        uint64_t aConversationId ) const
{
    wxFileName path;
    path.AssignDir( m_Directory );
    path.SetFullName( wxString::Format(
            wxS( "next_action_session_%llu.json" ),
            static_cast<unsigned long long>( aConversationId ) ) );
    return path.GetFullPath();
}


bool AI_NEXT_ACTION_SESSION_STORE::WriteSession(
        const AI_NEXT_ACTION_SESSION_RECORD& aRecord,
        wxString& aError ) const
{
    if( aRecord.m_ConversationId == 0 )
    {
        aError = wxS( "Next Action session conversation id must be non-zero." );
        return false;
    }

    wxFileName directory;
    directory.AssignDir( m_Directory );

    if( !directory.DirExists()
        && !directory.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
    {
        aError = wxString::Format(
                wxS( "Unable to create Next Action session directory: %s" ),
                m_Directory );
        return false;
    }

    nlohmann::json steps = nlohmann::json::array();

    for( const AI_NEXT_ACTION_SESSION_STEP_RECORD& step : aRecord.m_Steps )
        steps.push_back( stepToJson( step ) );

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.next_action_session" },
            { "version", 1 } } },
        { "conversation_id", aRecord.m_ConversationId },
        { "session_type", toUtf8String( aRecord.m_SessionType ) },
        { "project_id", toUtf8String( aRecord.m_ProjectId ) },
        { "document_id", toUtf8String( aRecord.m_DocumentId ) },
        { "context_key", toUtf8String( aRecord.m_ContextKey ) },
        { "step_count", aRecord.m_Steps.size() },
        { "steps", std::move( steps ) }
    };

    const wxString path = SessionPath( aRecord.m_ConversationId );
    wxFFile       file( path, wxS( "wb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format(
                wxS( "Unable to open Next Action session file: %s" ), path );
        return false;
    }

    file.Write( fromUtf8String( payload.dump( 2 ) ), wxConvUTF8 );
    file.Close();

    aError.clear();
    return true;
}


AI_NEXT_ACTION_SESSION_RECORD AI_NEXT_ACTION_SESSION_STORE::LoadSession(
        uint64_t aConversationId, wxString& aError ) const
{
    AI_NEXT_ACTION_SESSION_RECORD record;
    const wxString                path = SessionPath( aConversationId );

    if( !wxFileExists( path ) )
    {
        aError = wxString::Format(
                wxS( "Next Action session file does not exist: %s" ), path );
        return record;
    }

    wxFFile file( path, wxS( "rb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format(
                wxS( "Unable to open Next Action session file: %s" ), path );
        return record;
    }

    wxString content;

    if( !file.ReadAll( &content, wxConvUTF8 ) )
    {
        aError = wxString::Format(
                wxS( "Unable to read Next Action session file: %s" ), path );
        return record;
    }

    nlohmann::json payload =
            nlohmann::json::parse( toUtf8String( content ), nullptr, false );

    if( payload.is_discarded() || !payload.is_object() )
    {
        aError = wxString::Format(
                wxS( "Invalid Next Action session JSON: %s" ), path );
        return record;
    }

    record.m_ConversationId = payload.value( "conversation_id", 0ull );
    record.m_SessionType =
            fromUtf8String( payload.value( "session_type", std::string() ) );
    record.m_ProjectId =
            fromUtf8String( payload.value( "project_id", std::string() ) );
    record.m_DocumentId =
            fromUtf8String( payload.value( "document_id", std::string() ) );
    record.m_ContextKey =
            fromUtf8String( payload.value( "context_key", std::string() ) );

    if( payload.contains( "steps" ) && payload["steps"].is_array() )
    {
        for( const nlohmann::json& step : payload["steps"] )
            record.m_Steps.push_back( stepFromJson( step ) );
    }

    aError.clear();
    return record;
}
