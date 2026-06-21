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
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_types.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

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

    bool     IsValid() const;
    wxString AsJsonText() const;
    bool     SameSuggestionContext( const AI_CONTEXT_VERSION& aVersion ) const;
};

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
    wxString                     m_RawJson;

    bool WantsAttempt() const;
};

struct KICOMMON_API AI_NEXT_ACTION_REVIEW_DECISION
{
    AI_NEXT_ACTION_DECISION_KIND m_Kind = AI_NEXT_ACTION_DECISION_KIND::Abandon;
    wxString                     m_ReasonCode;
    uint64_t                     m_AttemptId = 0;
    wxString                     m_RawJson;

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
    uint64_t                       m_AttemptId = 0;

    bool IsValid() const;
    wxString AsJsonText() const;
};

struct KICOMMON_API AI_NEXT_ACTION_PUBLISH_DECISION
{
    bool                      m_Publish = false;
    uint64_t                  m_AttemptId = 0;
    wxString                  m_PreviewMode;
    AI_PREVIEW_LEASE          m_PreviewLease;
    AI_ACCEPT_OWNERSHIP_TOKEN m_AcceptToken;
    wxString                  m_RawJson;

    bool IsValid() const;
};

struct KICOMMON_API AI_NEXT_ACTION_ATTEMPT_RECORD
{
    uint64_t             m_Id = 0;
    uint64_t             m_RuntimeStepId = 0;
    uint64_t             m_HiddenSessionId = 0;
    uint64_t             m_HiddenStepId = 0;
    uint64_t             m_BaseCheckpointId = 0;
    AI_SUGGESTION_RECORD m_Candidate;
    wxString             m_JournalJson;
    wxString             m_RenderOutputsJson;
    wxString             m_ValidationFactsJson;
    wxString             m_RollbackJson;
    wxString             m_ProvenanceJson;
};

struct KICOMMON_API AI_NEXT_ACTION_RUNTIME_STEP
{
    uint64_t                       m_Id = 0;
    wxString                       m_SuggestionStreamId;
    AI_NEXT_ACTION_STEP_STATUS     m_Status = AI_NEXT_ACTION_STEP_STATUS::Observed;
    AI_NEXT_ACTION_CONTEXT_VERSION m_ContextVersion;
    uint64_t                       m_ObservationPacketId = 0;
    wxString                       m_LlmDecisionJson;
    std::vector<uint64_t>          m_AttemptIds;
    uint64_t                       m_PublishedSuggestionId = 0;
    wxString                       m_ReviewDecisionJson;
};

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
    wxString ToolCatalogJson() const;
    std::vector<AI_SUGGESTION_RECORD> GenerateCandidates(
            const AI_OBSERVATION_PACKET& aObservation ) const;
    wxString BuildHiddenMutationResult( const AI_SUGGESTION_RECORD& aCandidate ) const;
    wxString RenderAttempt( const AI_SUGGESTION_RECORD& aCandidate ) const;
    wxString ValidateAttempt( const AI_SUGGESTION_RECORD& aCandidate ) const;
    wxString RollbackAttempt( uint64_t aCheckpointId ) const;
};

class KICOMMON_API AI_NEXT_ACTION_RUNTIME
{
public:
    explicit AI_NEXT_ACTION_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider );

    std::optional<AI_SUGGESTION_RECORD> Update( const AI_SUGGESTION_TRIGGER& aTrigger );
    std::optional<AI_SUGGESTION_RECORD> AddPublishedSuggestion(
            AI_SUGGESTION_RECORD aSuggestion );
    std::vector<AI_SUGGESTION_RECORD> Suggestions() const;
    std::optional<AI_SUGGESTION_RECORD> FindSuggestion( uint64_t aSuggestionId ) const;
    std::optional<uint64_t> LatestActiveSuggestionId() const;
    bool CanPreview( uint64_t aSuggestionId ) const;
    bool CanAccept( uint64_t aSuggestionId ) const;
    bool BeginPreview( uint64_t aSuggestionId, AI_PREVIEW_MANAGER& aPreviewManager );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession );
    bool MarkAccepted( uint64_t aSuggestionId );
    bool Reject( uint64_t aSuggestionId );
    size_t ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion );

    const std::vector<AI_NEXT_ACTION_RUNTIME_STEP>& Steps() const { return m_Steps; }
    const std::vector<AI_NEXT_ACTION_ATTEMPT_RECORD>& Attempts() const
    {
        return m_Attempts;
    }

private:
    AI_OBSERVATION_PACKET buildObservationPacket( const AI_SEMANTIC_EVENT& aEvent );
    AI_NEXT_ACTION_LLM_DECISION runDecisionTurn(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation );
    AI_NEXT_ACTION_REVIEW_DECISION runReviewTurn(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation,
            const AI_NEXT_ACTION_ATTEMPT_RECORD& aAttempt );
    AI_NEXT_ACTION_ATTEMPT_RECORD buildAttempt(
            const AI_NEXT_ACTION_RUNTIME_STEP& aStep,
            const AI_OBSERVATION_PACKET& aObservation,
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
    uint64_t                                  m_NextStepId = 1;
    uint64_t                                  m_NextObservationId = 1;
    uint64_t                                  m_NextAttemptId = 1;
    uint64_t                                  m_NextSuggestionId = 1;
    uint64_t                                  m_NextLeaseId = 1;
    std::vector<AI_NEXT_ACTION_RUNTIME_STEP>  m_Steps;
    std::vector<AI_NEXT_ACTION_ATTEMPT_RECORD> m_Attempts;
    std::vector<AI_SUGGESTION_RECORD>         m_Suggestions;
};
