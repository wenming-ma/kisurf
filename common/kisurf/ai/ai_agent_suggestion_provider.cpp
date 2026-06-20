#include <kisurf/ai/ai_agent_suggestion_provider.h>

#include <nlohmann/json.hpp>

#include <utility>

namespace
{
enum class MODEL_SUGGESTION_PARSE_STATE
{
    Parsed,
    NoSuggestion,
    Fallback
};

struct MODEL_SUGGESTION_PARSE_RESULT
{
    MODEL_SUGGESTION_PARSE_STATE       m_State = MODEL_SUGGESTION_PARSE_STATE::Fallback;
    std::optional<AI_SUGGESTION_RECORD> m_Record;
};


bool hasActivity( const AI_ACTIVITY_RECORD& aActivity )
{
    return aActivity.m_Sequence != 0 || !aActivity.m_ActionName.IsEmpty()
           || !aActivity.m_Message.IsEmpty();
}


AI_CONTEXT_VERSION effectiveVersion( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_ContextVersion.IsValid() )
        return aTrigger.m_ContextVersion;

    return aTrigger.m_ContextSnapshot.m_Version;
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


void applyTriggerContextMetadata( const AI_SUGGESTION_TRIGGER& aTrigger,
                                  const char* aReason,
                                  AI_SUGGESTION_RECORD& aSuggestion )
{
    aSuggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    aSuggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxString::FromUTF8( aReason ) );
}


std::string extractJsonObjectText( const wxString& aBody )
{
    const std::string body = toUtf8String( aBody );
    const size_t      first = body.find( '{' );
    const size_t      last = body.rfind( '}' );

    if( first == std::string::npos || last == std::string::npos || last < first )
        return std::string();

    return body.substr( first, last - first + 1 );
}


AI_PROVIDER_REQUEST buildModelRequest( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = aTrigger.m_Activity.m_Sequence;
    request.m_EditorKind = aTrigger.m_EditorKind;
    request.m_ContextVersion = effectiveVersion( aTrigger );
    request.m_ContextSnapshot = aTrigger.m_ContextSnapshot;

    if( request.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        request.m_ContextSnapshot.m_EditorKind = aTrigger.m_EditorKind;

    if( !hasActivity( aTrigger.m_Activity ) )
        return request;

    request.m_ContextSnapshot.m_RecentActivity.push_back( aTrigger.m_Activity );

    wxString userText;
    userText << wxS( "Generate one KiSurf suggestion as a JSON object only.\n" )
             << wxS( "Accepted keys: kind, title, body, fingerprint, arguments, "
                     "preview_objects, edit_objects, no_suggestion.\n" )
             << wxS( "Use kind preview unless the suggestion is purely chat. "
                     "Use only object labels present in the supplied context. "
                     "preview_objects and edit_objects may contain strings or objects with "
                     "a label string. If there is no useful grounded suggestion, return "
                     "{\"no_suggestion\":true}.\n\n" )
             << wxS( "Trigger reason: " ) << aTrigger.m_Reason << wxS( "\n" )
             << wxS( "Activity: sequence=" ) << aTrigger.m_Activity.m_Sequence
             << wxS( " action=" ) << aTrigger.m_Activity.m_ActionName
             << wxS( " message=" ) << aTrigger.m_Activity.m_Message << wxS( "\n\n" )
             << wxS( "Context:\n" )
             << request.m_ContextSnapshot.AsPromptText();

    request.m_UserText = userText;
    return request;
}


AI_SUGGESTION_KIND parseKind( const nlohmann::json& aJson )
{
    if( !aJson.contains( "kind" ) || !aJson["kind"].is_string() )
        return AI_SUGGESTION_KIND::Preview;

    const std::string kind = aJson["kind"].get<std::string>();

    if( kind == "chat" )
        return AI_SUGGESTION_KIND::Chat;

    if( kind == "edit" )
        return AI_SUGGESTION_KIND::Edit;

    return AI_SUGGESTION_KIND::Preview;
}


wxString optionalStringField( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return wxString();

    return fromUtf8String( aJson[aKey].get<std::string>() );
}


const AI_OBJECT_REF* findObjectByLabel( const std::vector<AI_OBJECT_REF>& aObjects,
                                        const wxString& aLabel )
{
    for( const AI_OBJECT_REF& object : aObjects )
    {
        if( object.m_Label.CmpNoCase( aLabel ) == 0 )
            return &object;
    }

    return nullptr;
}


const AI_OBJECT_REF* findContextObjectByLabel( const AI_CONTEXT_SNAPSHOT& aContext,
                                               const wxString& aLabel )
{
    if( const AI_OBJECT_REF* selected =
                findObjectByLabel( aContext.m_SelectedObjects, aLabel ) )
    {
        return selected;
    }

    return findObjectByLabel( aContext.m_VisibleObjects, aLabel );
}


bool resolveObjectRefs( const nlohmann::json& aJson, const char* aKey,
                        const AI_CONTEXT_SNAPSHOT& aContext,
                        std::vector<AI_OBJECT_REF>& aResolved )
{
    if( !aJson.contains( aKey ) || !aJson[aKey].is_array() || aJson[aKey].empty() )
        return false;

    for( const nlohmann::json& item : aJson[aKey] )
    {
        wxString label;

        if( item.is_string() )
        {
            label = fromUtf8String( item.get<std::string>() );
        }
        else if( item.is_object() && item.contains( "label" ) && item["label"].is_string() )
        {
            label = fromUtf8String( item["label"].get<std::string>() );
        }
        else
        {
            return false;
        }

        label.Trim( true ).Trim( false );

        if( label.IsEmpty() )
            return false;

        const AI_OBJECT_REF* object = findContextObjectByLabel( aContext, label );

        if( !object )
            return false;

        aResolved.push_back( *object );
    }

    return !aResolved.empty();
}


bool fillArgumentsJson( const nlohmann::json& aJson, AI_SUGGESTION_RECORD& aRecord )
{
    if( !aJson.contains( "arguments" ) || aJson["arguments"].is_null() )
        return true;

    if( aJson["arguments"].is_string() )
    {
        aRecord.m_ArgumentsJson = fromUtf8String( aJson["arguments"].get<std::string>() );
        return true;
    }

    if( aJson["arguments"].is_object() )
    {
        aRecord.m_ArgumentsJson = fromUtf8String( aJson["arguments"].dump() );
        return true;
    }

    return false;
}


MODEL_SUGGESTION_PARSE_RESULT parseModelSuggestion(
        const AI_SUGGESTION_TRIGGER& aTrigger, const AI_PROVIDER_RESPONSE& aResponse )
{
    MODEL_SUGGESTION_PARSE_RESULT result;
    const std::string             jsonText = extractJsonObjectText( aResponse.m_Body );

    if( jsonText.empty() )
        return result;

    try
    {
        nlohmann::json parsed = nlohmann::json::parse( jsonText );

        if( !parsed.is_object() )
            return result;

        if( parsed.empty()
            || ( parsed.contains( "no_suggestion" ) && parsed["no_suggestion"].is_boolean()
                 && parsed["no_suggestion"].get<bool>() ) )
        {
            result.m_State = MODEL_SUGGESTION_PARSE_STATE::NoSuggestion;
            return result;
        }

        AI_SUGGESTION_RECORD suggestion;
        suggestion.m_EditorKind = aTrigger.m_EditorKind;
        suggestion.m_Kind = parseKind( parsed );
        suggestion.m_ContextVersion = effectiveVersion( aTrigger );
        suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
        suggestion.m_Title = optionalStringField( parsed, "title" );
        suggestion.m_Body = optionalStringField( parsed, "body" );
        suggestion.m_Fingerprint = optionalStringField( parsed, "fingerprint" );
        applyTriggerContextMetadata( aTrigger, "model_suggestion", suggestion );

        if( suggestion.m_Title.IsEmpty() && suggestion.m_Body.IsEmpty() )
        {
            result.m_State = MODEL_SUGGESTION_PARSE_STATE::NoSuggestion;
            return result;
        }

        if( !fillArgumentsJson( parsed, suggestion ) )
        {
            result.m_State = MODEL_SUGGESTION_PARSE_STATE::NoSuggestion;
            return result;
        }

        if( !resolveObjectRefs( parsed, "preview_objects", aTrigger.m_ContextSnapshot,
                                suggestion.m_PreviewObjects ) )
        {
            result.m_State = MODEL_SUGGESTION_PARSE_STATE::NoSuggestion;
            return result;
        }

        if( parsed.contains( "edit_objects" ) )
        {
            if( !resolveObjectRefs( parsed, "edit_objects", aTrigger.m_ContextSnapshot,
                                    suggestion.m_EditObjects ) )
            {
                result.m_State = MODEL_SUGGESTION_PARSE_STATE::NoSuggestion;
                return result;
            }
        }
        else
        {
            suggestion.m_EditObjects = suggestion.m_PreviewObjects;
        }

        result.m_State = MODEL_SUGGESTION_PARSE_STATE::Parsed;
        result.m_Record = suggestion;
        return result;
    }
    catch( const std::exception& )
    {
        return result;
    }
}


std::optional<AI_SUGGESTION_RECORD> deterministicSuggestion(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    const AI_OBJECT_REF& first = aTrigger.m_ContextSnapshot.m_SelectedObjects.front();
    const wxString       label = first.m_Label.IsEmpty() ? wxString( wxS( "selected item" ) )
                                                         : first.m_Label;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = aTrigger.m_EditorKind;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxString::Format( wxS( "Review %s" ), label );
    suggestion.m_Body = wxS( "Preview this suggestion before applying any edit." );
    applyTriggerContextMetadata( aTrigger, "deterministic_selection", suggestion );
    suggestion.m_PreviewObjects = aTrigger.m_ContextSnapshot.m_SelectedObjects;
    suggestion.m_EditObjects = aTrigger.m_ContextSnapshot.m_SelectedObjects;
    return suggestion;
}
} // namespace


AI_AGENT_SUGGESTION_PROVIDER::AI_AGENT_SUGGESTION_PROVIDER() = default;


AI_AGENT_SUGGESTION_PROVIDER::AI_AGENT_SUGGESTION_PROVIDER(
        std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) )
{
}


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_SUGGESTION_PROVIDER::Suggest(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return std::nullopt;

    if( !hasActivity( aTrigger.m_Activity ) )
        return std::nullopt;

    if( aTrigger.m_ContextSnapshot.m_SelectedObjects.empty() )
        return std::nullopt;

    if( m_Provider )
    {
        AI_PROVIDER_RESPONSE response = m_Provider->Generate( buildModelRequest( aTrigger ) );
        MODEL_SUGGESTION_PARSE_RESULT parsed = parseModelSuggestion( aTrigger, response );

        if( parsed.m_State == MODEL_SUGGESTION_PARSE_STATE::Parsed )
            return parsed.m_Record;

        if( parsed.m_State == MODEL_SUGGESTION_PARSE_STATE::NoSuggestion )
            return std::nullopt;
    }

    return deterministicSuggestion( aTrigger );
}
