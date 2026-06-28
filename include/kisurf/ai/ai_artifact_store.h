#pragma once

#include <kicommon.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <wx/string.h>

struct AI_VISUAL_OBSERVATION_ARTIFACT;
class AI_ACCEPT_APPLY_ADAPTER;
class AI_LOCAL_TEXT_MEMORY_INDEX;

struct KICOMMON_API AI_ARTIFACT_RECORD
{
    wxString m_Uri;
    wxString m_Kind;
    wxString m_ProjectId;
    wxString m_DocumentId;
    wxString m_AgentKind;
    wxString m_Source;
    wxString m_MimeType;
    wxString m_Summary;
    wxString m_MetadataJson;
    wxString m_RetentionClass;
    wxString m_Hash;
    wxString m_BlobPath;
    size_t   m_ByteSize = 0;
    uint64_t m_Sequence = 0;
    int64_t  m_CreatedAtUnixSeconds = 0;
    int64_t  m_ExpiresAtUnixSeconds = 0;
};


struct KICOMMON_API AI_ARTIFACT_QUERY
{
    wxString m_Uri;
    wxString m_Kind;
    wxString m_ProjectId;
    wxString m_DocumentId;
    wxString m_AgentKind;
    wxString m_Source;
    wxString m_RetentionClass;
    size_t   m_Limit = 0;
};


struct KICOMMON_API AI_ARTIFACT_RETENTION_POLICY
{
    // Zero disables count-based retention.
    size_t m_MaxRecords = 0;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_QUERY
{
    wxString m_ProjectId;
    wxString m_DocumentId;
    wxString m_AgentKind;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_POLICY
{
    bool     m_Available = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_CheckpointOrJournalResumeRequired = true;
    wxString m_ArtifactUri;
    wxString m_AgentKind;
    wxString m_Source;
    uint64_t m_RequestId = 0;
    wxString m_Reason;
    wxString m_Action;
    wxString m_RecoveryBasisJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_RESUME_PACKET
{
    bool     m_Available = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_CheckpointOrJournalResumeRequired = true;
    wxString m_ArtifactUri;
    wxString m_AgentKind;
    wxString m_Source;
    uint64_t m_RequestId = 0;
    wxString m_Reason;
    wxString m_ProviderRecoveryAction;
    wxString m_ResumeAction;
    wxString m_RecoveryBasisJson;
    wxString m_ResumePacketJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_RESUME_PLAN
{
    bool     m_Available = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_UserReviewRequired = true;
    bool     m_CheckpointOrJournalReplayRequired = true;
    wxString m_ArtifactUri;
    wxString m_AgentKind;
    wxString m_Source;
    uint64_t m_RequestId = 0;
    wxString m_ResumeAction;
    wxString m_RecoveryBasisJson;
    wxString m_PlanJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT
{
    uint64_t m_DocumentRevision = 0;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT
{
    bool     m_Allowed = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_CheckpointOrJournalReplayRequired = true;
    wxString m_Reason;
    uint64_t m_ExpectedDocumentRevision = 0;
    uint64_t m_CurrentDocumentRevision = 0;
    size_t   m_ReplayCandidateCount = 0;
    wxString m_ResultJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_REPLAY_REQUEST
{
    bool     m_Available = false;
    bool     m_Allowed = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_UserReviewRequired = true;
    bool     m_CheckpointOrJournalReplayRequired = true;
    wxString m_Reason;
    wxString m_ArtifactUri;
    wxString m_AgentKind;
    wxString m_Source;
    uint64_t m_RequestId = 0;
    uint64_t m_ExpectedDocumentRevision = 0;
    uint64_t m_CurrentDocumentRevision = 0;
    wxString m_ToolCallId;
    wxString m_ToolName;
    wxString m_SessionId;
    uint64_t m_CheckpointId = 0;
    uint64_t m_RollbackCheckpointId = 0;
    size_t   m_JournalOperationCount = 0;
    wxString m_ReplaySource;
    wxString m_JournalJson;
    wxString m_CandidateJson;
    wxString m_RequestJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_EPISODE
{
    bool     m_Available = false;
    bool     m_PreflightAllowed = false;
    bool     m_ReplayRequestAvailable = false;
    bool     m_ReadyForUserReview = false;
    bool     m_UserReviewRequired = true;
    bool     m_AutomaticExecutionAllowed = false;
    bool     m_BlindToolReplayAllowed = false;
    bool     m_CheckpointOrJournalReplayRequired = true;
    wxString m_Status;
    wxString m_Reason;
    wxString m_ArtifactUri;
    wxString m_AgentKind;
    wxString m_Source;
    uint64_t m_RequestId = 0;
    wxString m_EpisodeJson;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS
{
    bool     m_UserReviewed = false;
    wxString m_Reviewer;
    wxString m_ReviewNote;
};

struct KICOMMON_API AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
{
    bool     m_Ok = false;
    bool     m_BoardMutated = false;
    bool     m_UserReviewRequired = true;
    wxString m_ErrorCode;
    wxString m_Message;
    size_t   m_AppliedOperationCount = 0;
    wxString m_ResultJson;
};


class KICOMMON_API AI_ARTIFACT_STORE
{
public:
    AI_ARTIFACT_STORE();
    explicit AI_ARTIFACT_STORE(
            wxString aManifestPath,
            AI_ARTIFACT_RETENTION_POLICY aRetention = AI_ARTIFACT_RETENTION_POLICY() );

    bool StorePayload( AI_ARTIFACT_RECORD& aRecord, const wxString& aPayload,
                       wxString& aError );
    bool ReadPayload( const wxString& aUri, wxString& aPayload, wxString& aError ) const;

    std::vector<AI_ARTIFACT_RECORD> LoadAll( wxString& aError ) const;
    std::vector<AI_ARTIFACT_RECORD> Query( const AI_ARTIFACT_QUERY& aQuery,
                                           wxString& aError ) const;

    static wxString DefaultManifestPath();

    const wxString& ManifestPath() const { return m_ManifestPath; }
    wxString BlobDirectory() const;
    const AI_ARTIFACT_RETENTION_POLICY& RetentionPolicy() const { return m_Retention; }

private:
    bool ApplyRetention( wxString& aError ) const;

private:
    wxString                      m_ManifestPath;
    AI_ARTIFACT_RETENTION_POLICY  m_Retention;
};


KICOMMON_API bool AiStoreToolResultArtifact( const wxString& aProjectId,
                                             const wxString& aDocumentId,
                                             const wxString& aAgentKind,
                                             const wxString& aRetentionClass,
                                             const wxString& aToolCallId,
                                             const wxString& aToolName,
                                             const wxString& aResultJson,
                                             AI_ARTIFACT_STORE& aStore,
                                             AI_ARTIFACT_RECORD& aRecord,
                                             wxString& aError );

KICOMMON_API bool AiStoreScriptOutputArtifact( const wxString& aProjectId,
                                               const wxString& aDocumentId,
                                               const wxString& aAgentKind,
                                               const wxString& aRetentionClass,
                                               const wxString& aToolCallId,
                                               const wxString& aToolName,
                                               const wxString& aArgumentsJson,
                                               const wxString& aResultJson,
                                               AI_ARTIFACT_STORE& aStore,
                                               AI_ARTIFACT_RECORD& aRecord,
                                               wxString& aError );

KICOMMON_API bool AiStoreFailedHiddenAttemptArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        uint64_t aRuntimeStepId,
        uint64_t aAttemptId,
        const wxString& aTerminalStatus,
        const wxString& aReasonCode,
        const wxString& aAuditPayloadJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError );

KICOMMON_API bool AiStoreProviderRecoveryArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const wxString& aSource,
        uint64_t aRequestId,
        const wxString& aProviderTraceJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError );

KICOMMON_API AI_PROVIDER_RECOVERY_POLICY AiEvaluateLatestProviderRecovery(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError );

KICOMMON_API AI_PROVIDER_RECOVERY_RESUME_PACKET
AiBuildLatestProviderRecoveryResumePacket(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError );

KICOMMON_API AI_PROVIDER_RECOVERY_RESUME_PLAN
AiBuildLatestProviderRecoveryResumePlan(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError );

KICOMMON_API AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT
AiPreflightProviderRecoveryResumePlan(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext );

KICOMMON_API AI_PROVIDER_RECOVERY_REPLAY_REQUEST
AiBuildProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext );

KICOMMON_API AI_PROVIDER_RECOVERY_EPISODE
AiBuildProviderRecoveryEpisode(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext );

KICOMMON_API AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
AiExecuteProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_REPLAY_REQUEST& aRequest,
        const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
        AI_ACCEPT_APPLY_ADAPTER& aAdapter );

KICOMMON_API bool AiStoreVisualObservationArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const AI_VISUAL_OBSERVATION_ARTIFACT& aVisual,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError );

KICOMMON_API bool AiStoreValidationReportArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const wxString& aToolCallId,
        const wxString& aToolName,
        const wxString& aValidationJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError );

KICOMMON_API bool AiExportArtifactSummariesToLocalTextIndex(
        const AI_ARTIFACT_STORE& aStore,
        const AI_ARTIFACT_QUERY& aQuery,
        AI_LOCAL_TEXT_MEMORY_INDEX& aIndex,
        wxString& aError );
