#include <kisurf_ai_pcb_suggestion_review.h>

#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf_ai_pcb_move_edit_adapter.h>
#include <kisurf_ai_pcb_operation_edit_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                            COMMIT& aCommit )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            aModel.FindSuggestion( aSuggestionId );

    if( !suggestion )
        return false;

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    if( !operation )
        return false;

    if( operation->IsMove() || operation->IsMoveSelected() )
    {
        KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( aResolver, aCommit,
                                                 operation->m_MoveDelta );
        AI_EDIT_SESSION                 session( adapter );
        return aModel.AcceptSuggestion( aSuggestionId, session );
    }

    if( operation->IsRouteSegmentPreview() || operation->IsPlaceViaPreview()
        || operation->IsCreateShapePreview()
        || operation->IsCreateCopperZonePreview() )
    {
        KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( aResolver, aCommit );
        AI_EDIT_SESSION                      session( adapter );
        return aModel.AcceptSuggestion( aSuggestionId, session );
    }

    return false;
}
