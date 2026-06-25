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
#include <kisurf/ai/ai_accept_applier.h>

#include <wx/arrstr.h>

#include <memory>
#include <string>
#include <vector>

class wxGrid;
class wxPropertyGrid;

class KICOMMON_API AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    virtual ~AI_STRUCTURED_SURFACE_STATE_BACKEND() = default;

    virtual bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                          wxString& aSurfaceStateJson,
                                          wxString& aError ) = 0;
    virtual bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                           bool aChanged,
                                           wxString& aError ) = 0;
    virtual void AbortSurfaceTransaction() {}
};


class KICOMMON_API AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    explicit AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND(
            wxString& aSurfaceStateJson );

    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override;
    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override;

private:
    wxString& m_SurfaceStateJson;
};


class KICOMMON_API AI_STRUCTURED_SURFACE_GRID_IO
{
public:
    virtual ~AI_STRUCTURED_SURFACE_GRID_IO() = default;

    virtual int RowCount() const = 0;
    virtual int ColumnCount() const = 0;
    virtual wxString RowLabel( int aRow ) const = 0;
    virtual wxString ColumnLabel( int aColumn ) const = 0;
    virtual wxString CellValue( int aRow, int aColumn ) const = 0;
    virtual void SetCellValue( int aRow, int aColumn,
                               const wxString& aValue ) = 0;
    virtual bool IsCellEditControlShown() const { return false; }
    virtual void SaveEditControlValue() {}
};


class KICOMMON_API AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND(
            std::unique_ptr<AI_STRUCTURED_SURFACE_GRID_IO> aGridIo,
            const wxString& aSurfaceId,
            const wxString& aTableId );

    void SetSurfaceRevision( uint64_t aRevision );
    uint64_t SurfaceRevision() const;

    void SetSchemaVersion( const wxString& aSchemaVersion );
    void SetSelectionFingerprint( const wxString& aSelectionFingerprint );
    void SetOverlapSet( const std::vector<wxString>& aOverlapSet );

    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override;
    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override;
    void AbortSurfaceTransaction() override;

private:
    std::unique_ptr<AI_STRUCTURED_SURFACE_GRID_IO> m_GridIo;
    wxString                                      m_SurfaceId;
    wxString                                      m_TableId;
    uint64_t                                      m_SurfaceRevision = 0;
    wxString                                      m_SchemaVersion;
    wxString                                      m_SelectionFingerprint;
    std::vector<wxString>                         m_OverlapSet;
    bool                                          m_InTransaction = false;
};


class KICOMMON_API AI_STRUCTURED_SURFACE_WX_GRID_BACKEND :
        public AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND
{
public:
    AI_STRUCTURED_SURFACE_WX_GRID_BACKEND( wxGrid& aGrid,
                                           const wxString& aSurfaceId,
                                           const wxString& aTableId );
};


class KICOMMON_API AI_STRUCTURED_SURFACE_FIELD_IO
{
public:
    virtual ~AI_STRUCTURED_SURFACE_FIELD_IO() = default;

    virtual wxArrayString FieldIds() const = 0;
    virtual wxString FieldValue( const wxString& aFieldId ) const = 0;
    virtual void SetFieldValue( const wxString& aFieldId,
                                const wxString& aValue ) = 0;
    virtual bool HasField( const wxString& aFieldId ) const;
    virtual bool IsFieldEditControlShown() const { return false; }
    virtual void SaveFieldEditControlValue() {}
};


class KICOMMON_API AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND(
            std::unique_ptr<AI_STRUCTURED_SURFACE_FIELD_IO> aFieldIo,
            const wxString& aSurfaceId );

    void SetSurfaceRevision( uint64_t aRevision );
    uint64_t SurfaceRevision() const;

    void SetSchemaVersion( const wxString& aSchemaVersion );
    void SetSelectionFingerprint( const wxString& aSelectionFingerprint );
    void SetOverlapSet( const std::vector<wxString>& aOverlapSet );

    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override;
    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override;
    void AbortSurfaceTransaction() override;

private:
    std::unique_ptr<AI_STRUCTURED_SURFACE_FIELD_IO> m_FieldIo;
    wxString                                       m_SurfaceId;
    uint64_t                                       m_SurfaceRevision = 0;
    wxString                                       m_SchemaVersion;
    wxString                                       m_SelectionFingerprint;
    std::vector<wxString>                          m_OverlapSet;
    bool                                           m_InTransaction = false;
};


class KICOMMON_API AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_BACKEND :
        public AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND
{
public:
    AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_BACKEND( wxPropertyGrid& aPropertyGrid,
                                                    const wxString& aSurfaceId );
};


class KICOMMON_API AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND() = default;
    AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND(
            const AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND& ) = delete;
    AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND& operator=(
            const AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND& ) = delete;

    void AddBackend( std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND> aBackend );

    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override;
    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override;
    void AbortSurfaceTransaction() override;

private:
    std::vector<std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND>> m_Backends;
    std::vector<std::vector<std::string>>                             m_BackendSurfaceIds;
    size_t                                                           m_BegunCount = 0;
    bool                                                             m_InTransaction = false;
};


class AI_STRUCTURED_SURFACE_APPLY_ADAPTER :
        public AI_ACCEPT_APPLY_ADAPTER
{
public:
    explicit KICOMMON_API AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
            wxString& aSurfaceStateJson );
    explicit KICOMMON_API AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
            AI_STRUCTURED_SURFACE_STATE_BACKEND& aBackend );

    KICOMMON_API bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                                        wxString& aError ) override;
    KICOMMON_API bool ApplyOperation(
            const AI_SESSION_OPERATION_RECORD& aOperation,
            wxString& aError ) override;
    KICOMMON_API bool CommitTransaction( wxString& aError ) override;
    KICOMMON_API bool HasBoardChanges() const override;
    KICOMMON_API void AbortTransaction() override;

    KICOMMON_API bool HasSurfaceChanges() const;

private:
    bool applySurfacePatch( const AI_SESSION_OPERATION_RECORD& aOperation,
                            wxString& aError );

    std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND> m_OwnedBackend;
    AI_STRUCTURED_SURFACE_STATE_BACKEND&                 m_Backend;
    wxString                                             m_WorkingStateJson;
    bool                                                 m_InTransaction = false;
    bool                                                 m_SurfaceChanged = false;
};
