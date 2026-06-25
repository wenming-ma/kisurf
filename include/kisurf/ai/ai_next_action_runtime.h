/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_types.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <wx/arrstr.h>

class AI_SESSION_PREVIEW_SERVICE;
class AI_SESSION_VALIDATION_SERVICE;

enum class AI_NEXT_ACTION_STEP_STATUS
{
    Observed,
    Reasoning,
    Attempting,
    Reviewing,
    Retrying,
    Published,
    Accepted,
    Rejected,
    Expired,
    Superseded,
    Abandoned,
    Cancelled
};

enum class AI_NEXT_ACTION_DECISION_KIND
{
    Wait,
    Gather,
    Attempt,
    Abandon,
    Retry,
    RollbackRetry,
    Publish
};

struct KICOMMON_API AI_NEXT_ACTION_CONTEXT_VERSION
{
    wxString           m_BoardBaseHash;
    AI_CONTEXT_VERSION m_ContextVersion;
    uint64_t           m_ToolModeVersion = 0;
    uint64_t           m_UiFocusVersion = 0;
    uint64_t           m_ActivitySequence = 0;
    wxString           m_ViewportFingerprint;
    wxString           m_CursorRegionFingerprint;

    bool     IsValid() const;
    wxString AsJsonText() const;
    bool     SameSuggestionContext( const AI_CONTEXT_VERSION& aVersion ) const;
};

KICOMMON_API AI_NEXT_ACTION_CONTEXT_VERSION AiNextActionContextVersionFromSnapshot(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        uint64_t aActivitySequence = 0,
        const wxString& aBoardBaseHash = wxString() );

struct KICOMMON_API AI_SEMANTIC_EVENT
{
    uint64_t                       m_Id = 0;
    wxString                       m_SlotId;
    wxString                       m_Kind;
    wxString                       m_Reason;
    AI_EDITOR_KIND                 m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_NEXT_ACTION_CONTEXT_VERSION m_ContextVersion;
    AI_CONTEXT_SNAPSHOT            m_ContextSnapshot;
    AI_ACTIVITY_RECORD             m_Activity;

    bool IsValid() const;
};

struct KICOMMON_API AI_OBSERVATION_PACKET
{
    uint64_t                       m_Id = 0;
    wxString                       m_Kind;
    AI_NEXT_ACTION_CONTEXT_VERSION m_ContextVersion;
    AI_CONTEXT_SNAPSHOT            m_ContextSnapshot;
    AI_ACTIVITY_RECORD             m_Activity;
    wxString                       m_ObservationJson;

    wxString AsJsonText() const;
};

struct KICOMMON_API AI_NEXT_ACTION_LLM_DECISION
{
    AI_NEXT_ACTION_DECISION_KIND m_Kind = AI_NEXT_ACTION_DECISION_KIND::Abandon;
    wxString                     m_OpportunityType;
    wxString                     m_ReasonCode;
    std::optional<size_t>        m_SelectedCandidateIndex;
    wxString                     m_RawJson;
    wxString                     m_ToolResultsJson;

    bool WantsAttempt() const;
};

struct KICOMMON_API AI_NEXT_ACTION_REVIEW_DECISION
{
    AI_NEXT_ACTION_DECISION_KIND m_Kind = AI_NEXT_ACTION_DECISION_KIND::Abandon;
    wxString                     m_ReasonCode;
    uint64_t                     m_AttemptId = 0;
    wxString                     m_RawJson;
    wxString                     m_ToolResultsJson;

    bool WantsPublish() const;
};

struct KICOMMON_API AI_PREVIEW_LEASE
{
    uint64_t m_Id = 0;
    wxString m_OwnerNamespace;
    uint64_t m_SuggestionId = 0;
    bool     m_Active = false;

    bool IsValid() const;
    wxString AsJsonText() const;
};

struct KICOMMON_API AI_ACCEPT_OWNERSHIP_TOKEN
{
    uint64_t                       m_PreviewId = 0;
    uint64_t                       m_LeaseId = 0;
    wxString                       m_OwnerNamespace;
    AI_NEXT_ACTION_CONTEXT_VERSION m_ContextVersion;
    wxString                       m_DependencyFingerprint;
    wxString                       m_TouchedObjectSetFingerprint;
    uint64_t                       m_AttemptId = 0;

    bool IsValid() const;
    wxString AsJsonText() const;
};

struct KICOMMON_API AI_NEXT_ACTION_GATE_RESULT
{
    wxString              m_Gate;
    bool                  m_Allowed = false;
    std::vector<wxString> m_Reasons;

    wxString AsJsonText() const;
};

struct KICOMMON_API AI_NEXT_ACTION_PUBLISH_DECISION
{
    bool                      m_Publish = false;
    uint64_t                  m_AttemptId = 0;
    wxString                  m_PreviewMode;
    AI_PREVIEW_LEASE          m_PreviewLease;
    AI_ACCEPT_OWNERSHIP_TOKEN m_AcceptToken;
    AI_NEXT_ACTION_GATE_RESULT m_GateResult;
    wxString                  m_RawJson;

    bool IsValid() const;
};

struct KICOMMON_API AI_NEXT_ACTION_BUDGET_COUNTERS
{
    uint64_t m_ToolRoundCount = 0;
    uint64_t m_MutationCount = 0;
    uint64_t m_RenderCount = 0;
    uint64_t m_ValidationCount = 0;
    uint64_t m_WallTimeMs = 0;
    uint64_t m_CreatedObjectCount = 0;
    uint64_t m_TouchedObjectCount = 0;
    wxString m_TouchedObjectSetJson;

    wxString AsJsonText() const;
};

struct KICOMMON_API AI_NEXT_ACTION_ATTEMPT_RECORD
{
    uint64_t                       m_Id = 0;
    uint64_t                       m_RuntimeStepId = 0;
    size_t                         m_CandidateIndex = 0;
    uint64_t                       m_HiddenSessionId = 0;
    uint64_t                       m_HiddenStepId = 0;
    uint64_t                       m_BaseCheckpointId = 0;
    AI_SUGGESTION_RECORD           m_Candidate;
    wxString                       m_JournalJson;
    wxString                       m_RenderOutputsJson;
    wxString                       m_ValidationFactsJson;
    wxString                       m_RollbackJson;
    AI_NEXT_ACTION_BUDGET_COUNTERS m_BudgetCounters;
    wxString                       m_ProvenanceJson;
};

struct KICOMMON_API AI_NEXT_ACTION_RUNTIME_STEP
{
    uint64_t                       m_Id = 0;
    wxString                       m_SuggestionStreamId;
    AI_NEXT_ACTION_STEP_STATUS     m_Status = AI_NEXT_ACTION_STEP_STATUS::Observed;
    AI_NEXT_ACTION_CONTEXT_VERSION m_ContextVersion;
    uint64_t                       m_ObservationPacketId = 0;
    wxString                       m_SemanticEventJson;
    wxString                       m_ObservationPacketJson;
    wxString                       m_LlmDecisionJson;
    wxString                       m_LlmDecisionToolResultsJson;
    std::vector<uint64_t>          m_AttemptIds;
    uint64_t                       m_PublishedSuggestionId = 0;
    wxString                       m_ReviewDecisionJson;
    wxString                       m_ReviewToolResultsJson;
};

struct KICOMMON_API AI_NEXT_ACTION_REPLAY_TRACE_RECORD
{
    uint64_t m_Sequence = 0;
    uint64_t m_RuntimeStepId = 0;
    wxString m_Status;
    wxString m_ReplayJson;
};

inline constexpr unsigned AI_NEXT_ACTION_REPLAY_TRACE_SCHEMA_VERSION = 1;

struct KICOMMON_API AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT
{
    bool     m_Valid = false;
    wxString m_ErrorCode;
    wxString m_Message;
};

KICOMMON_API AI_NEXT_ACTION_REPLAY_TRACE_VALIDATION_RESULT
AiValidateNextActionReplayTraceJson( const wxString& aReplayTraceJson );

struct KICOMMON_API AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT
{
    bool     m_Valid = false;
    wxString m_ErrorCode;
    wxString m_Message;
    unsigned m_SchemaVersion = 0;
    uint64_t m_RuntimeStepId = 0;
    wxString m_TerminalState;
    bool     m_Published = false;
    bool     m_Accepted = false;
    bool     m_Rejected = false;
    bool     m_Expired = false;
    bool     m_Superseded = false;
    bool     m_Abandoned = false;
    size_t   m_AttemptCount = 0;
    size_t   m_HiddenOperationCount = 0;
    size_t   m_RenderResultCount = 0;
    size_t   m_ValidationResultCount = 0;
    size_t   m_ToolResultCount = 0;
    bool     m_PreviewGateAllowed = false;
    bool     m_HasBlockingValidationIssue = false;
    wxString m_QualityMetricJson;
};

KICOMMON_API AI_NEXT_ACTION_REPLAY_EVALUATION_RESULT
AiEvaluateNextActionReplayTraceJson( const wxString& aReplayTraceJson );

struct KICOMMON_API AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT
{
    bool     m_Valid = true;
    wxString m_FirstErrorCode;
    wxString m_FirstErrorMessage;
    size_t   m_TotalTraceCount = 0;
    size_t   m_ValidTraceCount = 0;
    size_t   m_InvalidTraceCount = 0;
    size_t   m_PublishedCount = 0;
    size_t   m_AcceptedCount = 0;
    size_t   m_RejectedCount = 0;
    size_t   m_ExpiredCount = 0;
    size_t   m_SupersededCount = 0;
    size_t   m_AbandonedCount = 0;
    size_t   m_AttemptCount = 0;
    size_t   m_HiddenOperationCount = 0;
    size_t   m_RenderResultCount = 0;
    size_t   m_ValidationResultCount = 0;
    size_t   m_ToolResultCount = 0;
    size_t   m_PreviewGateAllowedCount = 0;
    size_t   m_BlockingValidationCount = 0;
    wxString m_SummaryJson;
};

KICOMMON_API AI_NEXT_ACTION_REPLAY_BATCH_EVALUATION_RESULT
AiEvaluateNextActionReplayTraceBatch( const wxArrayString& aReplayTraceJsons );

class KICOMMON_API AI_NEXT_ACTION_SCHEDULER
{
public:
    std::optional<AI_SEMANTIC_EVENT> BuildSemanticEvent(
            const AI_SUGGESTION_TRIGGER& aTrigger );

private:
    uint64_t m_NextEventId = 1;
    wxString m_LastIssuedSlotId;
    std::chrono::steady_clock::time_point m_LastIssuedAt;
    bool     m_HasLastIssuedAt = false;
    uint64_t m_MinSlotIntervalMs = 500;
};

class KICOMMON_API AI_NEXT_ACTION_TOOL_REGISTRY
{
public:
    AI_NEXT_ACTION_TOOL_REGISTRY() = default;
    explicit AI_NEXT_ACTION_TOOL_REGISTRY(
            AI_SESSION_VALIDATION_SERVICE* aValidationService,
            AI_SESSION_PREVIEW_SERVICE* aPreviewService = nullptr );
    void SetServices( AI_SESSION_VALIDATION_SERVICE* aValidationService,
                      AI_SESSION_PREVIEW_SERVICE* aPreviewService );

    wxString ToolCatalogJson() const;
    wxString CallableToolCatalogJson() const;
    std::vector<AI_SUGGESTION_RECORD> GenerateCandidates(
            const AI_OBSERVATION_PACKET& aObservation ) const;
    wxString BuildHiddenMutationResult( const AI_SUGGESTION_RECORD& aCandidate ) const;
    wxString RenderAttempt( const AI_EXECUTION_SESSION& aSession,
                            const AI_SUGGESTION_RECORD& aCandidate,
                            const wxString& aRequestedRenderArgsJson =
                                    wxEmptyString ) const;
    wxString ValidateAttempt( const AI_EXECUTION_SESSION& aSession,
                              const AI_SUGGESTION_RECORD& aCandidate,
                              const wxString& aRequestedValidationArgsJson =
                                      wxEmptyString ) const;
    wxString RollbackAttempt( uint64_t aCheckpointId ) const;
    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall,
            const AI_OBSERVATION_PACKET& aObservation,
            AI_NEXT_ACTION_ATTEMPT_RECORD* aAttempt = nullptr,
            AI_EXECUTION_SESSION* aAttemptSession = nullptr ) const;

private:
    AI_SESSION_VALIDATION_SERVICE* m_ValidationService = nullptr;
    AI_SESSION_PREVIEW_SERVICE*    m_PreviewService = nullptr;
};

class KICOMMON_API AI_NEXT_ACTION_RUNTIME
{
public:
    explicit AI_NEXT_ACTION_RUNTIME(
            std::unique_ptr<AI_PROVIDER> aProvider,
            AI_SESSION_VALIDATION_SERVICE* aValidationService = nullptr,
            AI_SESSION_PREVIEW_SERVICE* aPreviewService = nullptr );
    void SetServices( AI_SESSION_VALIDATION_SERVICE* aValidationService,
                      AI_SESSION_PREVIEW_SERVICE* aPreviewService );
    void SetCurrentContextSampler(
            std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> aSampler );

    std::optional<AI_SUGGESTION_RECORD> Update( const AI_SUGGESTION_TRIGGER& aTrigger );
    std::optional<AI_SUGGESTION_RECORD> AddPublishedSuggestion(
            AI_SUGGESTION_RECORD aSuggestion );
    std::vector<AI_SUGGESTION_RECORD> Suggestions() const;
    std::optional<AI_SUGGESTION_RECORD> FindSuggestion( uint64_t aSuggestionId ) const;
    std::optional<uint64_t> LatestActiveSuggestionId() const;
    bool CanPreview( uint64_t aSuggestionId ) const;
    bool CanAccept( uint64_t aSuggestionId ) const;
    bool BeginPreview( uint64_t aSuggestionId, AI_PREVIEW_MANAGER& aPreviewManager );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession,
                 const AI_CONTEXT_VERSION& aCurrentContextVersion );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession,
                 const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion );
    bool RecordSuggestionGateResult( uint64_t aSuggestionId, const wxString& aKey,
                                     const AI_NEXT_ACTION_GATE_RESULT& aGate );
    bool MarkAccepted( uint64_t aSuggestionId );
    bool Reject( uint64_t aSuggestionId );
    bool Expire( uint64_t aSuggestionId );
    size_t ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion );
    size_t ExpireStale( const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentVersion );

    const std::vector<AI_NEXT_ACTION_RUNTIME_STEP>& Steps() const { return m_Steps; }
    const std::vector<AI_NEXT_ACTION_ATTEMPT_RECORD>& Attempts() const
    {
        return m_Attempts;
    }
    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> ReplayTraceRecords() const;

private:
    AI_OBSERVATION_PACKET buildObservationPacket( const AI_SEMANTIC_EVENT& aEvent );
    AI_NEXT_ACTION_LLM_DECISION runDecisionTurn(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation );
    AI_NEXT_ACTION_REVIEW_DECISION runReviewTurn(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation,
            AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
            const std::vector<AI_TOOL_CALL_RECORD>& aInitialToolResults = {} );
    AI_PROVIDER_RESPONSE generateWithToolLoop(
            AI_PROVIDER_REQUEST aRequest,
            const AI_OBSERVATION_PACKET& aObservation,
            AI_NEXT_ACTION_ATTEMPT_RECORD* aAttempt );
    AI_NEXT_ACTION_ATTEMPT_RECORD buildAttempt(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation,
            size_t aCandidateIndex,
            const AI_SUGGESTION_RECORD& aCandidate );
    AI_NEXT_ACTION_PUBLISH_DECISION buildPublishDecision(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
            const AI_NEXT_ACTION_REVIEW_DECISION& aReview );
    AI_SUGGESTION_RECORD publishAttempt(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt,
            const AI_NEXT_ACTION_PUBLISH_DECISION& aPublish );
    std::optional<AI_SUGGESTION_RECORD> storeSuggestion(
            AI_SUGGESTION_RECORD aSuggestion );
    AI_SUGGESTION_RECORD* findMutable( uint64_t aSuggestionId );

    std::unique_ptr<AI_PROVIDER>              m_Provider;
    AI_NEXT_ACTION_SCHEDULER                  m_Scheduler;
    AI_NEXT_ACTION_TOOL_REGISTRY              m_Tools;
    std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> m_CurrentContextSampler;
    uint64_t                                  m_NextStepId = 1;
    uint64_t                                  m_NextObservationId = 1;
    uint64_t                                  m_NextAttemptId = 1;
    uint64_t                                  m_NextSuggestionId = 1;
    uint64_t                                  m_NextLeaseId = 1;
    std::vector<AI_NEXT_ACTION_RUNTIME_STEP>  m_Steps;
    std::vector<AI_NEXT_ACTION_ATTEMPT_RECORD> m_Attempts;
    std::map<uint64_t, std::unique_ptr<AI_EXECUTION_SESSION>> m_AttemptFrames;
    std::vector<AI_SUGGESTION_RECORD>         m_Suggestions;
};
