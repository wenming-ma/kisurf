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

#include <core/typeinfo.h>
#include <kicommon.h>
#include <kiid.h>
#include <math/vector2d.h>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <wx/string.h>

enum class AI_EDITOR_KIND
{
    Unknown,
    Pcb,
    Schematic
};

enum class AI_SUGGESTION_KIND
{
    Chat,
    Preview,
    Edit
};

enum class AI_VALIDATION_SEVERITY
{
    None,
    Info,
    Warning,
    Error
};

enum class AI_ACTION_SAFETY
{
    ReadOnly,
    Interactive,
    Modifying,
    Destructive
};

enum class AI_TOOL_STATE_KIND
{
    Unknown,
    Idle,
    Selecting,
    RoutingTrack,
    PlacingVia,
    PlacingFootprint,
    DrawingZone,
    MovingSelection
};

enum class AI_AGENT_WORKSPACE_CONTEXT_KIND
{
    General,
    Routing,
    ViaPlacement,
    FootprintPlacement,
    ZoneCreation,
    SelectionEdit
};

struct KICOMMON_API AI_CONTEXT_VERSION
{
    uint64_t m_DocumentRevision = 0;
    uint64_t m_SelectionRevision = 0;
    uint64_t m_ViewRevision = 0;

    bool IsValid() const;
    wxString AsString() const;
};

struct KICOMMON_API AI_OBJECT_REF
{
    AI_OBJECT_REF();
    AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel );
    AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel,
                   const wxString& aDetailsJson );

    KIID     m_Uuid;
    KICAD_T  m_Type = TYPE_NOT_INIT;
    wxString m_Label;
    wxString m_DetailsJson;

    bool IsValid() const;
};

struct KICOMMON_API AI_VALIDATION_ISSUE
{
    AI_VALIDATION_SEVERITY m_Severity = AI_VALIDATION_SEVERITY::None;
    wxString               m_Message;
    bool                   m_IsNew = false;
};

struct KICOMMON_API AI_VALIDATION_SUMMARY
{
    std::vector<AI_VALIDATION_ISSUE> m_Issues;

    AI_VALIDATION_SEVERITY WorstSeverity() const;
    bool HasBlockingIssue() const;
};

struct KICOMMON_API AI_ACTION_DESCRIPTOR
{
    wxString         m_Name;
    wxString         m_FriendlyName;
    wxString         m_Description;
    AI_EDITOR_KIND   m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_ACTION_SAFETY m_Safety = AI_ACTION_SAFETY::Interactive;
    bool             m_Enabled = false;

    bool IsValid() const;
    wxString SafetyAsString() const;
};

struct KICOMMON_API AI_VISUAL_SNAPSHOT
{
    wxString m_Source;
    wxString m_MimeType;
    wxString m_DataUri;
    wxString m_FrameId;
    wxString m_FrameKind;
    wxString m_SidecarJson;
    int      m_WidthPx = 0;
    int      m_HeightPx = 0;
    size_t   m_ByteSize = 0;
    wxString m_UnavailableReason;

    bool HasPixels() const;
};

enum class AI_ACTIVITY_KIND
{
    UserAction,
    ModelToolRequest,
    PolicyDecision,
    ToolResult
};

struct KICOMMON_API AI_ACTIVITY_RECORD
{
    uint64_t         m_Sequence = 0;
    uint64_t         m_RequestId = 0;
    wxString         m_ToolCallId;
    AI_ACTIVITY_KIND m_Kind = AI_ACTIVITY_KIND::UserAction;
    AI_EDITOR_KIND   m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString         m_ActionName;
    wxString         m_ArgumentsJson;
    wxString         m_ResultJson;
    wxString         m_ErrorCode;
    bool             m_Allowed = false;
    bool             m_Executed = false;
    wxString         m_Message;
};

struct KICOMMON_API AI_TOOL_STATE_SNAPSHOT
{
    AI_EDITOR_KIND     m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_TOOL_STATE_KIND m_Kind = AI_TOOL_STATE_KIND::Unknown;
    AI_CONTEXT_VERSION m_ContextVersion;
    wxString           m_ActiveActionName;
    VECTOR2I           m_CursorBoardPosition = VECTOR2I( 0, 0 );
    bool               m_HasCursorBoardPosition = false;
    wxString           m_SharedContextJson;
    wxString           m_ModeContextJson;

    bool HasToolState() const;
    wxString KindAsString() const;
    wxString AsPromptText() const;
    wxString AsJsonText() const;
};

struct KICOMMON_API AI_AGENT_WORKSPACE_CONTEXT_STATE
{
    AI_AGENT_WORKSPACE_CONTEXT_KIND m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
    wxString             m_Title;
    wxString             m_StateJson;
    uint64_t             m_LastActivitySequence = 0;

    bool HasState() const;
    wxString ContextAsString() const;
};

enum class AI_CONTEXT_ANCHOR_KIND
{
    Unknown,
    RouteStart,
    RouteTarget,
    RouteCandidate,
    OrthogonalBreakout,
    FortyFiveIntersection,
    PlacementCandidate,
    PatternContinuation,
    ShapeCorner,
    ZoneVertex,
    PanelCell,
    General
};

struct KICOMMON_API AI_CONTEXT_ANCHOR
{
    wxString               m_Id;
    AI_CONTEXT_ANCHOR_KIND m_Kind = AI_CONTEXT_ANCHOR_KIND::Unknown;
    AI_EDITOR_KIND         m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString               m_Label;
    wxString               m_Summary;
    VECTOR2I               m_Position = VECTOR2I( 0, 0 );
    bool                   m_HasPosition = false;
    int                    m_Layer = -1;
    wxString               m_DetailsJson;
    double                 m_Confidence = 0.0;

    bool IsValid() const;
    wxString KindAsString() const;
};

struct KICOMMON_API AI_PANEL_STATE_RECORD
{
    wxString m_Id;
    wxString m_Title;
    wxString m_FocusedControlId;
    wxString m_FocusedControlLabel;
    wxString m_SelectedText;
    wxString m_Summary;
    wxString m_StateJson;

    bool HasState() const;
};

struct KICOMMON_API AI_CONTEXT_SNAPSHOT
{
    AI_EDITOR_KIND                    m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString                          m_ProjectId;
    wxString                          m_DocumentId;
    AI_CONTEXT_VERSION                m_Version;
    std::vector<AI_OBJECT_REF>        m_VisibleObjects;
    std::vector<AI_OBJECT_REF>        m_SelectedObjects;
    std::vector<AI_ACTION_DESCRIPTOR> m_Actions;
    std::vector<AI_ACTIVITY_RECORD>   m_RecentActivity;
    AI_TOOL_STATE_SNAPSHOT            m_ToolState;
    AI_VISUAL_SNAPSHOT                m_Visual;
    std::vector<AI_CONTEXT_ANCHOR>     m_Anchors;
    std::vector<AI_PANEL_STATE_RECORD> m_PanelStates;
    wxString                          m_Summary;

    bool HasContext() const;
    wxString AsPromptText( size_t aMaxObjects = 25, size_t aMaxActions = 25,
                           size_t aMaxAnchors = 25,
                           size_t aMaxPanelStates = 10 ) const;
    wxString AsJsonText( size_t aMaxObjects = 64, size_t aMaxActions = 128,
                         size_t aMaxActivity = 64, size_t aMaxAnchors = 64,
                         size_t aMaxPanelStates = 16 ) const;
};

KICOMMON_API wxString AiToolStateKindName( AI_TOOL_STATE_KIND aKind );
KICOMMON_API wxString AiDynamicContextKind( const AI_CONTEXT_SNAPSHOT& aSnapshot );
KICOMMON_API wxString AiDynamicContextDetailsJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                                   const wxString& aReason );

enum class AI_SUGGESTION_STATUS
{
    Pending,
    Previewing,
    Accepted,
    Rejected,
    Expired,
    Superseded,
    Abandoned,
    Cancelled
};

struct KICOMMON_API AI_SUGGESTION_TRIGGER
{
    AI_EDITOR_KIND      m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION  m_ContextVersion;
    AI_CONTEXT_SNAPSHOT m_ContextSnapshot;
    AI_ACTIVITY_RECORD  m_Activity;
    wxString            m_Reason;
    bool                m_PreviewOnly = false;
};

struct KICOMMON_API AI_SUGGESTION_RECORD
{
    uint64_t                   m_Id = 0;
    uint64_t                   m_Sequence = 0;
    AI_EDITOR_KIND             m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_SUGGESTION_KIND         m_Kind = AI_SUGGESTION_KIND::Preview;
    AI_SUGGESTION_STATUS       m_Status = AI_SUGGESTION_STATUS::Pending;
    AI_CONTEXT_VERSION         m_ContextVersion;
    uint64_t                   m_TriggerActivitySequence = 0;
    wxString                   m_Fingerprint;
    wxString                   m_Title;
    wxString                   m_Body;
    wxString                   m_ContextKind;
    wxString                   m_ContextDetailsJson;
    wxString                   m_RuntimeProvenanceJson;
    wxString                   m_ArgumentsJson;
    bool                       m_PreviewOnly = false;
    std::vector<AI_OBJECT_REF> m_PreviewObjects;
    std::vector<AI_OBJECT_REF> m_EditObjects;
    AI_VALIDATION_SUMMARY      m_Validation;
};

struct KICOMMON_API AI_TOOL_CALL_RECORD
{
    uint64_t m_RequestId = 0;
    wxString m_ToolCallId;
    wxString m_ToolName;
    wxString m_ArgumentsJson;
    wxString m_ResultJson;
    bool     m_Allowed = false;
    bool     m_Executed = false;
    wxString m_ErrorCode;
    wxString m_Message;
};

struct KICOMMON_API AI_PROVIDER_INPUT_BLOCK
{
    wxString m_Id;
    wxString m_Kind;
    wxString m_Source;
    wxString m_Text;
    wxString m_MetadataJson;
    bool     m_Included = true;
    wxString m_OmissionReason;
    size_t   m_OriginalChars = 0;
    size_t   m_SentChars = 0;
};

enum class AI_PROVIDER_REQUEST_KIND
{
    Chat,
    NextActionDecision,
    NextActionReview
};

struct KICOMMON_API AI_PROVIDER_REQUEST
{
    uint64_t                         m_RequestId = 0;
    uint64_t                         m_ConversationId = 1;
    AI_PROVIDER_REQUEST_KIND         m_RequestKind = AI_PROVIDER_REQUEST_KIND::Chat;
    AI_EDITOR_KIND                   m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION               m_ContextVersion;
    AI_CONTEXT_SNAPSHOT              m_ContextSnapshot;
    wxString                         m_UserText;
    std::vector<AI_TOOL_CALL_RECORD> m_ToolResults;
    wxString                         m_SystemPromptOverride;
    wxString                         m_ResponseFormatJson;
    wxString                         m_ToolCatalogJson;
    size_t                           m_MaxToolRounds = 1;
    bool                             m_DisableDefaultTools = false;
    size_t                           m_MaxProviderInputChars = 24000;
    size_t                           m_MaxContextActivityRecords = 24;
    size_t                           m_MaxToolResultChars = 4096;
    size_t                           m_MaxRetrievedMemoryRecords = 8;
    size_t                           m_MaxRetrievedMemoryChars = 4096;
    size_t                           m_MaxVisualDataUriChars = 1500000;
    bool                             m_AllowVisualPixels = true;
    bool                             m_ContextCompiled = false;
    bool                             m_ProviderInputWasShrunk = false;
    size_t                           m_ContextEstimatedChars = 0;
    wxString                         m_CompiledUserMessageText;
    wxString                         m_PromptTraceJson;
    std::vector<AI_PROVIDER_INPUT_BLOCK> m_ProviderInputBlocks;
    std::vector<AI_PROVIDER_INPUT_BLOCK> m_RetrievedMemoryBlocks;
};

struct KICOMMON_API AI_PROVIDER_RESPONSE
{
    uint64_t                         m_RequestId = 0;
    AI_SUGGESTION_KIND               m_Kind = AI_SUGGESTION_KIND::Chat;
    wxString                         m_Title;
    wxString                         m_Body;
    wxString                         m_ProviderTraceJson;
    std::vector<AI_TOOL_CALL_RECORD> m_ToolCalls;
};

struct KICOMMON_API AI_TRACE_RECORD
{
    uint64_t             m_RequestId = 0;
    AI_PROVIDER_REQUEST  m_Request;
    AI_PROVIDER_RESPONSE m_Response;
    bool                 m_Cancelled = false;
};

struct KICOMMON_API AI_TOOL_INVOCATION_REQUEST
{
    uint64_t             m_RequestId = 0;
    wxString             m_ToolCallId;
    AI_EDITOR_KIND       m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION   m_ContextVersion;
    AI_ACTION_DESCRIPTOR m_Action;
    wxString             m_ArgumentsJson;
    bool                 m_DryRun = false;
    bool                 m_UserAccepted = false;
};

struct KICOMMON_API AI_TOOL_INVOCATION_RESULT
{
    uint64_t m_RequestId = 0;
    wxString m_ToolCallId;
    wxString m_ActionName;
    bool     m_Allowed = false;
    bool     m_Executed = false;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_ResultJson;

    wxString AsTraceText() const;
};
