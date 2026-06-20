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
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <vector>

class KICOMMON_API AI_EDIT_ADAPTER
{
public:
    virtual ~AI_EDIT_ADAPTER() = default;

    virtual bool BeginApply( const AI_VALIDATION_SUMMARY&, size_t ) { return true; }
    virtual bool ApplyObject( const AI_OBJECT_REF& aObject ) = 0;
    virtual bool EndApply() { return true; }
    virtual void AbortApply() {}
};

class KICOMMON_API AI_EDIT_SESSION
{
public:
    explicit AI_EDIT_SESSION( AI_EDIT_ADAPTER& aAdapter );

    bool Apply( const std::vector<AI_OBJECT_REF>& aObjects,
                const AI_VALIDATION_SUMMARY& aValidation );

    const AI_VALIDATION_SUMMARY& LastValidation() const { return m_LastValidation; }

private:
    AI_EDIT_ADAPTER&      m_Adapter;
    AI_VALIDATION_SUMMARY m_LastValidation;
};
