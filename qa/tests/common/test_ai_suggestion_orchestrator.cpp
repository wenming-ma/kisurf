#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_suggestion_orchestrator.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <wx/arrstr.h>

namespace
{
class FAKE_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override
    {
        ++m_CallCount;
        m_LastTrigger = aTrigger;

        if( !m_NextSuggestion )
            return std::nullopt;

        AI_SUGGESTION_RECORD suggestion = *m_NextSuggestion;
        m_NextSuggestion.reset();
        return suggestion;
    }

    int                                m_CallCount = 0;
    AI_SUGGESTION_TRIGGER              m_LastTrigger;
    std::optional<AI_SUGGESTION_RECORD> m_NextSuggestion;
};


AI_SUGGESTION_TRIGGER makeTrigger( uint64_t aDocRevision = 1,
                                   uint64_t aActivitySequence = 1 )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = aDocRevision;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version.m_DocumentRevision = aDocRevision;
    trigger.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    trigger.m_Activity.m_Sequence = aActivitySequence;
    trigger.m_Activity.m_ActionName = wxS( "common.Interactive.selected" );
    trigger.m_Reason = wxS( "selection changed" );
    return trigger;
}


AI_SUGGESTION_RECORD makeSuggestion( const wxString& aTitle = wxS( "Place nearby cap" ) )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = aTitle;
    suggestion.m_Body = wxS( "Preview a safe next step." );
    suggestion.m_PreviewObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return suggestion;
}


AI_SUGGESTION_RECORD makeOperationOnlySuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview panel fill" );
    suggestion.m_Body = wxS( "Review this panel operation." );
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"panel_fill_column_preview\","
            "\"panel_id\":\"board_setup.clearance\","
            "\"table_id\":\"clearance.rules\","
            "\"column_id\":\"clearance\","
            "\"value\":\"0.20 mm\","
            "\"target_row_ids\":[\"row.power\"]}" );
    return suggestion;
}


AI_SUGGESTION_RECORD makeAnchorFocusOperationOnlySuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview anchor focus" );
    suggestion.m_Body = wxS( "Review this anchor before routing." );
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"anchor_focus_preview\","
            "\"anchor_id\":\"tool.routing.anchor.1\","
            "\"position\":{\"x\":500,\"y\":200},"
            "\"focus_layer\":\"F.Cu\","
            "\"focus_net\":\"/GPIO\","
            "\"dim_unfocused_layers\":true}" );
    return suggestion;
}


AI_SUGGESTION_RECORD makeActionPreviewSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = wxS( "Preview action" );
    suggestion.m_Body = wxS( "Run this action only after acceptance." );
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"action_preview\","
            "\"action\":\"common.Control.showAgentPanel\"}" );
    return suggestion;
}


class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "begin:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override
    {
        m_Events.push_back( wxString::Format( wxS( "show:%llu:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              aObject.m_Label ) );
    }

    void ShowOperation( uint64_t aPreviewId,
                        const AI_SUGGESTION_OPERATION& aOperation ) override
    {
        wxString label = aOperation.m_AnchorId;

        if( label.IsEmpty() )
            label = aOperation.m_PanelId;

        m_Events.push_back( wxString::Format( wxS( "operation:%llu:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              label ) );
    }

    void ClearPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "clear:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    std::vector<wxString> m_Events;
};


class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return m_ShouldApply;
    }

    bool                  m_ShouldApply = true;
    std::vector<wxString> m_Applied;
};
} // namespace

BOOST_AUTO_TEST_SUITE( AiSuggestionOrchestrator )


BOOST_AUTO_TEST_CASE( ValidTriggerStoresProviderSuggestion )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            orchestrator.Update( makeTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Id, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Sequence, 1 );
    BOOST_CHECK( suggestion->m_Status == AI_SUGGESTION_STATUS::Pending );
    BOOST_CHECK( suggestion->m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 1 );
    BOOST_CHECK( !suggestion->m_Fingerprint.IsEmpty() );
    BOOST_REQUIRE_EQUAL( orchestrator.Records().size(), 1 );
}


BOOST_AUTO_TEST_CASE( InvalidTriggerDoesNotCallProvider )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );

    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Unknown;

    BOOST_CHECK( !orchestrator.Update( trigger ).has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 0 );
}


BOOST_AUTO_TEST_CASE( DuplicateActiveFingerprintIsSuppressed )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_RECORD first = makeSuggestion();
    first.m_Fingerprint = wxS( "same" );
    provider.m_NextSuggestion = first;

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger() ).has_value() );

    provider.m_NextSuggestion = first;
    BOOST_CHECK( !orchestrator.Update( makeTrigger( 1, 2 ) ).has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( orchestrator.Records().size(), 1 );
}


BOOST_AUTO_TEST_CASE( CapacityEvictsOldestTerminalRecordFirst )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider, 2 );

    provider.m_NextSuggestion = makeSuggestion( wxS( "first" ) );
    std::optional<AI_SUGGESTION_RECORD> first = orchestrator.Update( makeTrigger( 1, 1 ) );
    BOOST_REQUIRE( first.has_value() );
    BOOST_CHECK( orchestrator.Reject( first->m_Id ) );

    provider.m_NextSuggestion = makeSuggestion( wxS( "second" ) );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger( 2, 2 ) ).has_value() );

    provider.m_NextSuggestion = makeSuggestion( wxS( "third" ) );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger( 3, 3 ) ).has_value() );

    std::vector<AI_SUGGESTION_RECORD> records = orchestrator.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Title, wxString( wxS( "second" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Title, wxString( wxS( "third" ) ) );
}


BOOST_AUTO_TEST_CASE( BeginPreviewShowsPreviewObjectsAndChangesStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   preview( adapter );

    BOOST_CHECK( orchestrator.BeginPreview( suggestion->m_Id, preview ) );
    std::optional<AI_SUGGESTION_RECORD> updated = orchestrator.Find( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Previewing );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "show:1:U1.1" ) ) );
}


BOOST_AUTO_TEST_CASE( OperationOnlySuggestionCanBeginPreview )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeOperationOnlySuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   preview( adapter );

    BOOST_CHECK( orchestrator.CanPreview( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.BeginPreview( suggestion->m_Id, preview ) );

    std::optional<AI_SUGGESTION_RECORD> updated = orchestrator.Find( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Previewing );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ),
                       wxString( wxS( "operation:1:board_setup.clearance" ) ) );
}

BOOST_AUTO_TEST_CASE( AnchorFocusOperationOnlySuggestionDispatchesPreviewOperation )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeAnchorFocusOperationOnlySuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_MANAGER   preview( adapter );

    BOOST_CHECK( orchestrator.CanPreview( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.BeginPreview( suggestion->m_Id, preview ) );

    std::optional<AI_SUGGESTION_RECORD> updated = orchestrator.Find( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Previewing );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ),
                       wxString( wxS( "operation:1:tool.routing.anchor.1" ) ) );
}


BOOST_AUTO_TEST_CASE( AcceptAppliesEditObjectsAndChangesStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   edit( adapter );

    BOOST_CHECK( orchestrator.Accept( suggestion->m_Id, edit ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Applied.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_Applied.front(), wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( OperationOnlySuggestionCannotAcceptWithoutEditObjects )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeOperationOnlySuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   edit( adapter );

    BOOST_CHECK( !orchestrator.CanAccept( suggestion->m_Id ) );
    BOOST_CHECK( !orchestrator.Accept( suggestion->m_Id, edit ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Pending );
}


BOOST_AUTO_TEST_CASE( ActionPreviewSuggestionCanBeMarkedAcceptedWithoutEditObjects )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeActionPreviewSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( orchestrator.CanAccept( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.MarkAccepted( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( BlockingValidationPreventsAcceptedStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_RECORD     record = makeSuggestion();
    record.m_Validation.m_Issues.push_back(
            { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );
    provider.m_NextSuggestion = record;

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   edit( adapter );

    BOOST_CHECK( !orchestrator.Accept( suggestion->m_Id, edit ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Pending );
}


BOOST_AUTO_TEST_CASE( RejectChangesPendingSuggestionStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( orchestrator.Reject( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Rejected );
}


BOOST_AUTO_TEST_CASE( ExpireStaleChangesOnlyMismatchedActiveRecords )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger( 1, 1 ) );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current;
    current.m_DocumentRevision = 2;

    BOOST_CHECK_EQUAL( orchestrator.ExpireStale( current ), 1 );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Expired );
}


BOOST_AUTO_TEST_SUITE_END()
