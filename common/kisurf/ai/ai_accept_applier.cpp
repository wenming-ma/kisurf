#include <kisurf/ai/ai_accept_applier.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace
{
AI_ACCEPT_APPLY_RESULT errorResult( const wxString& aCode, const wxString& aMessage )
{
    AI_ACCEPT_APPLY_RESULT result;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    return result;
}


wxString fallbackMessage( const wxString& aMessage, const wxString& aFallback )
{
    return aMessage.IsEmpty() ? aFallback : aMessage;
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


std::optional<wxString> latestInsufficientAcceptValidationReason(
        const AI_EXECUTION_SESSION& aSession )
{
    const std::vector<AI_SESSION_OPERATION_RECORD>& operations =
            aSession.Journal().Operations();

    for( auto it = operations.rbegin(); it != operations.rend(); ++it )
    {
        if( it->m_Kind != AI_SESSION_OPERATION_KIND::RunValidation )
            continue;

        nlohmann::json payload =
                nlohmann::json::parse( toUtf8String( it->m_ResultJson ), nullptr, false );

        if( payload.is_discarded() || !payload.is_object()
            || !payload.contains( "validation" )
            || !payload["validation"].is_object() )
        {
            continue;
        }

        const nlohmann::json& validation = payload["validation"];

        if( !validation.contains( "accept_validation_sufficient" )
            || !validation["accept_validation_sufficient"].is_boolean() )
        {
            continue;
        }

        if( validation["accept_validation_sufficient"].get<bool>() )
            return std::nullopt;

        if( validation.contains( "accept_validation_reason" )
            && validation["accept_validation_reason"].is_string() )
        {
            return wxString::FromUTF8(
                    validation["accept_validation_reason"].get_ref<const std::string&>().c_str() );
        }

        return wxS( "latest validation result is not sufficient for accept" );
    }

    return std::nullopt;
}
} // namespace


AI_ACCEPT_APPLY_RESULT AI_ACCEPT_APPLIER::Apply(
        AI_EXECUTION_SESSION& aSession, const wxString& aCurrentBaseHash,
        const AI_CONTEXT_VERSION& aCurrentContextVersion,
        AI_ACCEPT_APPLY_ADAPTER& aAdapter )
{
    if( !aSession.CanAccept( aCurrentBaseHash, aCurrentContextVersion ) )
    {
        if( aSession.Status() == AI_EXECUTION_SESSION_STATUS::Open
            && aSession.BaseHash() == aCurrentBaseHash
            && aSession.SelectionRevisionConflicts( aCurrentContextVersion ) )
        {
            return errorResult( wxS( "selection_conflict" ),
                                wxS( "Session selection changed after it was opened." ) );
        }

        return errorResult( wxS( "stale_session" ),
                            wxS( "Session cannot be accepted against this base hash." ) );
    }

    if( std::optional<wxString> reason =
                latestInsufficientAcceptValidationReason( aSession ) )
    {
        return errorResult(
                wxS( "validation_not_accept_grade" ),
                wxString::Format( wxS( "Latest explicit validation is not sufficient "
                                       "for Accept: %s" ), *reason ) );
    }

    wxString adapterError;

    if( !aAdapter.BeginTransaction( aSession, adapterError ) )
    {
        return errorResult( wxS( "begin_failed" ),
                            fallbackMessage( adapterError,
                                             wxS( "Adapter refused transaction." ) ) );
    }

    size_t applied = 0;

    for( const AI_SESSION_OPERATION_RECORD& operation : aSession.Journal().Operations() )
    {
        if( !operation.IsMutation() )
            continue;

        if( !aAdapter.ApplyOperation( operation, adapterError ) )
        {
            aAdapter.AbortTransaction();
            AI_ACCEPT_APPLY_RESULT result = errorResult(
                    wxS( "apply_failed" ),
                    fallbackMessage( adapterError,
                                     wxS( "Adapter refused operation." ) ) );
            result.m_AppliedOperationCount = applied;
            return result;
        }

        ++applied;
    }

    const bool boardMutated = aAdapter.HasBoardChanges();

    if( !aAdapter.CommitTransaction( adapterError ) )
    {
        aAdapter.AbortTransaction();
        AI_ACCEPT_APPLY_RESULT result = errorResult(
                wxS( "commit_failed" ),
                fallbackMessage( adapterError, wxS( "Adapter refused commit." ) ) );
        result.m_AppliedOperationCount = applied;
        return result;
    }

    if( !aSession.AcceptSession( aCurrentBaseHash, aCurrentContextVersion ) )
    {
        aAdapter.AbortTransaction();
        AI_ACCEPT_APPLY_RESULT result = errorResult(
                wxS( "accept_failed" ),
                wxS( "Session became unacceptable after adapter commit." ) );
        result.m_AppliedOperationCount = applied;
        return result;
    }

    AI_ACCEPT_APPLY_RESULT result;
    result.m_Ok = true;
    result.m_Message = wxS( "Session journal replayed and accepted." );
    result.m_AppliedOperationCount = applied;
    result.m_BoardMutated = boardMutated;
    return result;
}
