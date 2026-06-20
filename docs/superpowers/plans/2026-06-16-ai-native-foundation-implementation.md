# AI Native Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build KiSurf's native AI data model, provider boundary, and deterministic local runtime without requiring network model credentials.

**Architecture:** Foundation code lives in `include/kisurf/ai` and `common/kisurf/ai` so both PCB and schematic editors can link it through `kicommon`. The runtime is provider-agnostic, deterministic under the stub provider, and exposes cancellation and trace IDs before any editor mutation is allowed.

**Tech Stack:** C++17, wxString, KiCad `KIID`, KiCad `KICAD_T`, Boost.Test, `kicommon`, CMake QA target `qa_common`.

---

## File Structure

- Create: `include/kisurf/ai/ai_types.h`
  - Shared enums and small value types for editor kind, context version, object references, suggestion records, validation summaries, provider requests, and provider responses.
- Create: `common/kisurf/ai/ai_types.cpp`
  - Value-type helpers such as validity checks, string conversion, and severity aggregation.
- Create: `include/kisurf/ai/ai_provider.h`
  - Abstract provider interface and deterministic stub provider declaration.
- Create: `common/kisurf/ai/ai_provider.cpp`
  - Stub provider response generation.
- Create: `include/kisurf/ai/ai_runtime.h`
  - Runtime facade, request IDs, cancellation, trace record storage.
- Create: `common/kisurf/ai/ai_runtime.cpp`
  - Synchronous first-pass runtime that can later host async provider execution.
- Modify: `common/CMakeLists.txt:90`
  - Add new `common/kisurf/ai/*.cpp` files to `KICOMMON_SRCS`.
- Create: `qa/tests/common/test_ai_types.cpp`
  - Unit tests for stable value semantics and validation aggregation.
- Create: `qa/tests/common/test_ai_provider.cpp`
  - Unit tests for stub provider determinism.
- Create: `qa/tests/common/test_ai_runtime.cpp`
  - Unit tests for request IDs, trace storage, and cancellation.
- Modify: `qa/tests/common/CMakeLists.txt:24`
  - Add the three new test files to `QA_COMMON_SRCS`.

## Task 1: Shared Foundation Types

**Files:**
- Create: `include/kisurf/ai/ai_types.h`
- Create: `common/kisurf/ai/ai_types.cpp`
- Test: `qa/tests/common/test_ai_types.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write the failing type tests**

Create `qa/tests/common/test_ai_types.cpp` with:

```cpp
/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_types.h>

BOOST_AUTO_TEST_SUITE( AiNativeTypes )

BOOST_AUTO_TEST_CASE( ContextVersionStartsInvalidAndCanAdvance )
{
    AI_CONTEXT_VERSION version;

    BOOST_CHECK( !version.IsValid() );

    version.m_DocumentRevision = 1;
    version.m_SelectionRevision = 2;
    version.m_ViewRevision = 3;

    BOOST_CHECK( version.IsValid() );
    BOOST_CHECK_EQUAL( version.AsString(), wxS( "doc=1;sel=2;view=3" ) );
}

BOOST_AUTO_TEST_CASE( ObjectRefValidityDependsOnUuidAndType )
{
    AI_OBJECT_REF emptyRef;
    BOOST_CHECK( !emptyRef.IsValid() );

    AI_OBJECT_REF padRef( KIID(), PCB_PAD_T, wxS( "U1.1" ) );
    BOOST_CHECK( padRef.IsValid() );
    BOOST_CHECK_EQUAL( padRef.m_Label, wxS( "U1.1" ) );
}

BOOST_AUTO_TEST_CASE( ValidationSummaryFindsWorstSeverity )
{
    AI_VALIDATION_SUMMARY summary;
    BOOST_CHECK_EQUAL( summary.WorstSeverity(), AI_VALIDATION_SEVERITY::None );
    BOOST_CHECK( !summary.HasBlockingIssue() );

    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Warning, wxS( "near clearance" ), false } );
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK_EQUAL( summary.WorstSeverity(), AI_VALIDATION_SEVERITY::Error );
    BOOST_CHECK( summary.HasBlockingIssue() );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register the failing test file**

Add the new file names near the other `test_*.cpp` entries in `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_types.cpp
```

Run:

```powershell
cmake --preset x64-release -DKICAD_BUILD_QA_TESTS=ON
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_types.h>` does not exist yet.

- [ ] **Step 3: Add the shared type header**

Create `include/kisurf/ai/ai_types.h` with:

```cpp
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
#include <import_export.h>
#include <kiid.h>

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

struct APIEXPORT AI_CONTEXT_VERSION
{
    uint64_t m_DocumentRevision = 0;
    uint64_t m_SelectionRevision = 0;
    uint64_t m_ViewRevision = 0;

    bool IsValid() const;
    wxString AsString() const;
};

struct APIEXPORT AI_OBJECT_REF
{
    AI_OBJECT_REF();
    AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel );

    KIID     m_Uuid;
    KICAD_T  m_Type = TYPE_NOT_INIT;
    wxString m_Label;

    bool IsValid() const;
};

struct APIEXPORT AI_VALIDATION_ISSUE
{
    AI_VALIDATION_SEVERITY m_Severity = AI_VALIDATION_SEVERITY::None;
    wxString               m_Message;
    bool                   m_IsNew = false;
};

struct APIEXPORT AI_VALIDATION_SUMMARY
{
    std::vector<AI_VALIDATION_ISSUE> m_Issues;

    AI_VALIDATION_SEVERITY WorstSeverity() const;
    bool HasBlockingIssue() const;
};

struct APIEXPORT AI_PROVIDER_REQUEST
{
    uint64_t           m_RequestId = 0;
    AI_EDITOR_KIND     m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION m_ContextVersion;
    wxString           m_UserText;
};

struct APIEXPORT AI_PROVIDER_RESPONSE
{
    uint64_t           m_RequestId = 0;
    AI_SUGGESTION_KIND m_Kind = AI_SUGGESTION_KIND::Chat;
    wxString           m_Title;
    wxString           m_Body;
};

struct APIEXPORT AI_TRACE_RECORD
{
    uint64_t             m_RequestId = 0;
    AI_PROVIDER_REQUEST  m_Request;
    AI_PROVIDER_RESPONSE m_Response;
    bool                 m_Cancelled = false;
};
```

- [ ] **Step 4: Add the shared type implementation**

Create `common/kisurf/ai/ai_types.cpp` with:

```cpp
/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_types.h>

bool AI_CONTEXT_VERSION::IsValid() const
{
    return m_DocumentRevision > 0 || m_SelectionRevision > 0 || m_ViewRevision > 0;
}


wxString AI_CONTEXT_VERSION::AsString() const
{
    return wxString::Format( wxS( "doc=%llu;sel=%llu;view=%llu" ),
                             static_cast<unsigned long long>( m_DocumentRevision ),
                             static_cast<unsigned long long>( m_SelectionRevision ),
                             static_cast<unsigned long long>( m_ViewRevision ) );
}


AI_OBJECT_REF::AI_OBJECT_REF() :
        m_Uuid( NilUuid() )
{
}


AI_OBJECT_REF::AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel ) :
        m_Uuid( aUuid ),
        m_Type( aType ),
        m_Label( aLabel )
{
}


bool AI_OBJECT_REF::IsValid() const
{
    return m_Uuid != NilUuid() && m_Type != TYPE_NOT_INIT && m_Type != NOT_USED;
}


AI_VALIDATION_SEVERITY AI_VALIDATION_SUMMARY::WorstSeverity() const
{
    AI_VALIDATION_SEVERITY worst = AI_VALIDATION_SEVERITY::None;

    for( const AI_VALIDATION_ISSUE& issue : m_Issues )
    {
        if( static_cast<int>( issue.m_Severity ) > static_cast<int>( worst ) )
            worst = issue.m_Severity;
    }

    return worst;
}


bool AI_VALIDATION_SUMMARY::HasBlockingIssue() const
{
    for( const AI_VALIDATION_ISSUE& issue : m_Issues )
    {
        if( issue.m_IsNew && issue.m_Severity == AI_VALIDATION_SEVERITY::Error )
            return true;
    }

    return false;
}
```

- [ ] **Step 5: Add source files to `kicommon`**

In `common/CMakeLists.txt`, add this block inside `KICOMMON_SRCS`, after the Git section and before Jobs:

```cmake
    # KiSurf AI Native
    kisurf/ai/ai_types.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `qa_common` builds.
- The `AiNativeTypes` tests pass.

- [ ] **Step 6: Commit shared foundation types**

Run:

```powershell
git add include/kisurf/ai/ai_types.h common/kisurf/ai/ai_types.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_types.cpp
git commit -m "feat: add ai native foundation types"
```

Expected:

- Commit succeeds.

## Task 2: Provider Boundary And Stub Provider

**Files:**
- Create: `include/kisurf/ai/ai_provider.h`
- Create: `common/kisurf/ai/ai_provider.cpp`
- Test: `qa/tests/common/test_ai_provider.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write the failing provider tests**

Create `qa/tests/common/test_ai_provider.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_provider.h>

BOOST_AUTO_TEST_SUITE( AiNativeProvider )

BOOST_AUTO_TEST_CASE( StubProviderReturnsDeterministicChatResponse )
{
    AI_STUB_PROVIDER provider;

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 42;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "route this trace" );

    AI_PROVIDER_RESPONSE first = provider.Generate( request );
    AI_PROVIDER_RESPONSE second = provider.Generate( request );

    BOOST_CHECK_EQUAL( first.m_RequestId, 42 );
    BOOST_CHECK_EQUAL( first.m_Kind, AI_SUGGESTION_KIND::Chat );
    BOOST_CHECK_EQUAL( first.m_Title, second.m_Title );
    BOOST_CHECK_EQUAL( first.m_Body, second.m_Body );
    BOOST_CHECK( first.m_Body.Contains( wxS( "route this trace" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register the failing provider test**

Add this file to `QA_COMMON_SRCS` in `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_provider.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_provider.h>` does not exist yet.

- [ ] **Step 3: Add provider interface header**

Create `include/kisurf/ai/ai_provider.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_types.h>

class APIEXPORT AI_PROVIDER
{
public:
    virtual ~AI_PROVIDER() = default;

    virtual AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) = 0;
};

class APIEXPORT AI_STUB_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override;
};
```

- [ ] **Step 4: Add deterministic provider implementation**

Create `common/kisurf/ai/ai_provider.cpp` with:

```cpp
#include <kisurf/ai/ai_provider.h>

AI_PROVIDER_RESPONSE AI_STUB_PROVIDER::Generate( const AI_PROVIDER_REQUEST& aRequest )
{
    AI_PROVIDER_RESPONSE response;
    response.m_RequestId = aRequest.m_RequestId;
    response.m_Kind = AI_SUGGESTION_KIND::Chat;
    response.m_Title = wxS( "Stub Agent" );
    response.m_Body = wxString::Format( wxS( "Stub response for %s request %llu: %s" ),
                                        aRequest.m_EditorKind == AI_EDITOR_KIND::Pcb
                                                ? wxS( "PCB" )
                                                : wxS( "schematic" ),
                                        static_cast<unsigned long long>( aRequest.m_RequestId ),
                                        aRequest.m_UserText );

    return response;
}
```

- [ ] **Step 5: Register provider source**

In `common/CMakeLists.txt`, extend the KiSurf AI Native source block:

```cmake
    kisurf/ai/ai_provider.cpp
    kisurf/ai/ai_types.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- The provider tests pass.

- [ ] **Step 6: Commit provider boundary**

Run:

```powershell
git add include/kisurf/ai/ai_provider.h common/kisurf/ai/ai_provider.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_provider.cpp
git commit -m "feat: add ai native stub provider"
```

Expected:

- Commit succeeds.

## Task 3: Runtime Facade, Trace Records, And Cancellation

**Files:**
- Create: `include/kisurf/ai/ai_runtime.h`
- Create: `common/kisurf/ai/ai_runtime.cpp`
- Test: `qa/tests/common/test_ai_runtime.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write the failing runtime tests**

Create `qa/tests/common/test_ai_runtime.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_runtime.h>

BOOST_AUTO_TEST_SUITE( AiNativeRuntime )

BOOST_AUTO_TEST_CASE( RuntimeAssignsRequestIdsAndStoresTrace )
{
    AI_RUNTIME runtime( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "hello" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().front().m_Response.m_Body, response.m_Body );
}

BOOST_AUTO_TEST_CASE( RuntimeCanCancelKnownRequest )
{
    AI_RUNTIME runtime( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "cancel me" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK( runtime.Cancel( response.m_RequestId ) );
    BOOST_CHECK( runtime.TraceRecords().front().m_Cancelled );
    BOOST_CHECK( !runtime.Cancel( 999 ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register the failing runtime test**

Add this file to `QA_COMMON_SRCS`:

```cmake
    test_ai_runtime.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_runtime.h>` does not exist yet.

- [ ] **Step 3: Add runtime header**

Create `include/kisurf/ai/ai_runtime.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_types.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class APIEXPORT AI_RUNTIME
{
public:
    explicit AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider );

    AI_PROVIDER_RESPONSE Submit( AI_PROVIDER_REQUEST aRequest );
    bool Cancel( uint64_t aRequestId );

    std::vector<AI_TRACE_RECORD> TraceRecords() const;

private:
    std::unique_ptr<AI_PROVIDER> m_Provider;
    std::atomic<uint64_t>        m_NextRequestId;
    mutable std::mutex           m_Mutex;
    std::vector<AI_TRACE_RECORD> m_TraceRecords;
};
```

- [ ] **Step 4: Add runtime implementation**

Create `common/kisurf/ai/ai_runtime.cpp` with:

```cpp
#include <kisurf/ai/ai_runtime.h>

AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 )
{
}


AI_PROVIDER_RESPONSE AI_RUNTIME::Submit( AI_PROVIDER_REQUEST aRequest )
{
    aRequest.m_RequestId = m_NextRequestId.fetch_add( 1 );

    AI_PROVIDER_RESPONSE response = m_Provider->Generate( aRequest );

    AI_TRACE_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_Request = aRequest;
    record.m_Response = response;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        m_TraceRecords.push_back( record );
    }

    return response;
}


bool AI_RUNTIME::Cancel( uint64_t aRequestId )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    for( AI_TRACE_RECORD& record : m_TraceRecords )
    {
        if( record.m_RequestId == aRequestId )
        {
            record.m_Cancelled = true;
            return true;
        }
    }

    return false;
}


std::vector<AI_TRACE_RECORD> AI_RUNTIME::TraceRecords() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_TraceRecords;
}
```

- [ ] **Step 5: Register runtime source**

In `common/CMakeLists.txt`, extend the KiSurf AI Native source block:

```cmake
    kisurf/ai/ai_provider.cpp
    kisurf/ai/ai_runtime.cpp
    kisurf/ai/ai_types.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- Runtime tests pass.

- [ ] **Step 6: Commit runtime foundation**

Run:

```powershell
git add include/kisurf/ai/ai_runtime.h common/kisurf/ai/ai_runtime.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_runtime.cpp
git commit -m "feat: add ai native runtime facade"
```

Expected:

- Commit succeeds.

## Acceptance Criteria

- `AI_CONTEXT_VERSION`, `AI_OBJECT_REF`, `AI_VALIDATION_SUMMARY`, `AI_PROVIDER_REQUEST`, `AI_PROVIDER_RESPONSE`, and `AI_TRACE_RECORD` are available from `include/kisurf/ai/ai_types.h`.
- `AI_PROVIDER` supports deterministic local stub execution.
- `AI_RUNTIME` assigns request IDs, stores trace records, and supports cancellation state.
- `qa_common` passes with the three new AI foundation test files.
