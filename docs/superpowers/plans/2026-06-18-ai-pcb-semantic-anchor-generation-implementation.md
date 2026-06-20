# AI PCB Semantic Anchor Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Populate `AI_CONTEXT_INDEX` anchors from concrete PCB board geometry so model-visible context contains stable pad, via, route, shape, and footprint semantic targets.

**Architecture:** Extend the existing PCB context adapter walk instead of creating a second board scan. The adapter collects anchors alongside visible and selected object refs, then calls `AI_CONTEXT_INDEX::SetAnchors()` so sorting and snapshot carriage remain centralized. This slice produces factual anchors only; preview navigation and prediction algorithms stay in later slices.

**Tech Stack:** KiCad PCB C++20, `BOARD`/`FOOTPRINT`/`PAD`/`PCB_TRACK` model objects, `AI_CONTEXT_INDEX`, Boost unit tests in `qa_pcbnew`, existing hand-built JSON helpers in `kisurf_ai_pcb_context_adapter.cpp`.

---

## File Structure

- Modify `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
  - Add anchor lookup helpers.
  - Add red tests for pad/via anchors and track/shape anchors.
- Modify `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
  - Add anchor id, details JSON, label helper, and anchor construction helpers.
  - Generate anchors while walking footprints, pads, routing objects, and PCB shapes.
  - Call `index.SetAnchors( anchors )`.
- Modify `docs/superpowers/plans/2026-06-18-ai-pcb-semantic-anchor-generation-implementation.md`
  - Check off completed steps after verification.

## Task 1: PCB Adapter Anchor Tests

**Files:**
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`

- [x] **Step 1: Add anchor test helpers**

Add these helpers inside the anonymous namespace after `findRefByLabel`:

```cpp
const AI_CONTEXT_ANCHOR* findAnchorById( const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                                         const wxString& aId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( anchor.m_Id == aId )
            return &anchor;
    }

    return nullptr;
}


nlohmann::json anchorDetails( const AI_CONTEXT_ANCHOR& aAnchor )
{
    BOOST_REQUIRE( !aAnchor.m_DetailsJson.IsEmpty() );
    return nlohmann::json::parse( aAnchor.m_DetailsJson.ToStdString() );
}


wxString anchorId( const wxString& aPrefix, const KIID& aUuid, const wxString& aRole )
{
    return wxS( "pcb." ) + aPrefix + wxS( "." ) + aUuid.AsString() + wxS( "." ) + aRole;
}
```

- [x] **Step 2: Add failing pad and via anchor test**

Append this test before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( AdapterAddsPadAndViaSemanticAnchors )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/GPIO" ), 1 ) );

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U9" ) );
    footprint->SetValue( wxS( "MCU" ) );
    footprint->SetFPID( LIB_ID( wxS( "Package_QFP" ), wxS( "TQFP-32" ) ) );
    footprint->SetPosition( VECTOR2I( 1000, 2000 ) );
    footprint->SetLayer( F_Cu );

    PAD* pad = new PAD( footprint );
    pad->SetNumber( wxS( "1" ) );
    pad->SetPosition( VECTOR2I( 1200, 2300 ) );
    pad->SetLayerSet( PAD::SMDMask() );
    pad->SetNetCode( 1 );
    footprint->Add( pad );
    board.Add( footprint );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 3000, 4000 ) );
    via->SetWidth( 600 );
    via->SetNetCode( 1 );
    board.Add( via );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const wxString padAnchorId = anchorId( wxS( "pad" ), pad->m_Uuid, wxS( "center" ) );
    const wxString viaAnchorId = anchorId( wxS( "via" ), via->m_Uuid, wxS( "center" ) );

    const AI_CONTEXT_ANCHOR* padAnchor = findAnchorById( index.Anchors(), padAnchorId );
    const AI_CONTEXT_ANCHOR* viaAnchor = findAnchorById( index.Anchors(), viaAnchorId );

    BOOST_REQUIRE( padAnchor );
    BOOST_REQUIRE( viaAnchor );
    BOOST_CHECK_EQUAL( static_cast<int>( padAnchor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( padAnchor->m_Label, wxString( wxS( "pad:U9.1:center" ) ) );
    BOOST_CHECK_EQUAL( padAnchor->m_Position.x, 1200 );
    BOOST_CHECK_EQUAL( padAnchor->m_Position.y, 2300 );
    BOOST_CHECK_EQUAL( padAnchor->m_Confidence, 1.0 );
    BOOST_CHECK_EQUAL( static_cast<int>( viaAnchor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( viaAnchor->m_Position.x, 3000 );
    BOOST_CHECK_EQUAL( viaAnchor->m_Layer, -1 );

    nlohmann::json padDetails = anchorDetails( *padAnchor );
    BOOST_CHECK_EQUAL( padDetails["role"].get<std::string>(), "center" );
    BOOST_CHECK_EQUAL( padDetails["source_label"].get<std::string>(), "U9.1" );
    BOOST_CHECK_EQUAL( padDetails["footprint_reference"].get<std::string>(), "U9" );
    BOOST_CHECK_EQUAL( padDetails["pad_number"].get<std::string>(), "1" );
    BOOST_CHECK_EQUAL( padDetails["net_name"].get<std::string>(), "/GPIO" );
    BOOST_CHECK_EQUAL( padDetails["position"]["x"].get<int>(), 1200 );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();
    BOOST_CHECK( findAnchorById( snapshot.m_Anchors, padAnchorId ) );
    BOOST_CHECK( findAnchorById( snapshot.m_Anchors, viaAnchorId ) );
}
```

- [x] **Step 3: Add failing route and shape anchor test**

Append this test before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( AdapterAddsRouteAndShapeSemanticAnchors )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/CLK" ), 2 ) );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 10, 20 ) );
    track->SetEnd( VECTOR2I( 110, 120 ) );
    track->SetLayer( F_Cu );
    track->SetWidth( 250 );
    track->SetNetCode( 2 );
    board.Add( track );

    PCB_SHAPE* shape = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    shape->SetStart( VECTOR2I( 500, 600 ) );
    shape->SetEnd( VECTOR2I( 700, 800 ) );
    shape->SetLayer( Edge_Cuts );
    shape->SetWidth( 50 );
    board.Add( shape );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const wxString trackStartId = anchorId( wxS( "track" ), track->m_Uuid, wxS( "start" ) );
    const wxString trackEndId = anchorId( wxS( "track" ), track->m_Uuid, wxS( "end" ) );
    const wxString shapeStartId = anchorId( wxS( "shape" ), shape->m_Uuid, wxS( "start" ) );
    const wxString shapeEndId = anchorId( wxS( "shape" ), shape->m_Uuid, wxS( "end" ) );

    const AI_CONTEXT_ANCHOR* trackStart = findAnchorById( index.Anchors(), trackStartId );
    const AI_CONTEXT_ANCHOR* trackEnd = findAnchorById( index.Anchors(), trackEndId );
    const AI_CONTEXT_ANCHOR* shapeStart = findAnchorById( index.Anchors(), shapeStartId );
    const AI_CONTEXT_ANCHOR* shapeEnd = findAnchorById( index.Anchors(), shapeEndId );

    BOOST_REQUIRE( trackStart );
    BOOST_REQUIRE( trackEnd );
    BOOST_REQUIRE( shapeStart );
    BOOST_REQUIRE( shapeEnd );
    BOOST_CHECK_EQUAL( static_cast<int>( trackStart->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteStart ) );
    BOOST_CHECK_EQUAL( static_cast<int>( trackEnd->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( static_cast<int>( shapeStart->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::ShapeCorner ) );
    BOOST_CHECK_EQUAL( trackStart->m_Position.x, 10 );
    BOOST_CHECK_EQUAL( trackEnd->m_Position.y, 120 );
    BOOST_CHECK_EQUAL( shapeStart->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( shapeEnd->m_Position.y, 800 );

    nlohmann::json routeDetails = anchorDetails( *trackStart );
    BOOST_CHECK_EQUAL( routeDetails["role"].get<std::string>(), "start" );
    BOOST_CHECK_EQUAL( routeDetails["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( routeDetails["net_name"].get<std::string>(), "/CLK" );
}
```

- [x] **Step 4: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2"
```

Expected: build succeeds, but the new tests fail because `index.Anchors()` is empty.

## Task 2: PCB Adapter Anchor Generation

**Files:**
- Modify: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`

- [x] **Step 1: Add source label helpers**

Add these helpers near the existing label helpers:

```cpp
wxString footprintContextLabel( const FOOTPRINT& aFootprint )
{
    wxString label = aFootprint.GetReference();

    if( label.IsEmpty() )
        label = wxS( "footprint:" ) + aFootprint.m_Uuid.AsString();

    return label;
}


wxString padContextLabel( const FOOTPRINT& aFootprint, const PAD& aPad )
{
    wxString label = aFootprint.GetReference();

    if( !label.IsEmpty() && !aPad.GetNumber().IsEmpty() )
        label += wxS( "." );

    label += aPad.GetNumber();

    if( label.IsEmpty() )
        label = wxS( "pad:" ) + aPad.m_Uuid.AsString();

    return label;
}
```

Update `makeFootprintRef()` and `makePadRef()` to use these helpers for their labels.

- [x] **Step 2: Add anchor construction helpers**

Add these helpers after `pointDetailsJson()`:

```cpp
wxString anchorId( const wxString& aPrefix, const KIID& aUuid, const wxString& aRole )
{
    return wxS( "pcb." ) + aPrefix + wxS( "." ) + aUuid.AsString() + wxS( "." ) + aRole;
}


wxString anchorDetailsJson( const BOARD_ITEM& aSource, const wxString& aSourceLabel,
                            const wxString& aRole, const VECTOR2I& aPosition,
                            const wxString& aExtraFields = wxEmptyString )
{
    wxString details = wxString::Format( wxS( "{\"source_object_uuid\":%s,"
                                              "\"source_label\":%s,\"source_type\":%d,"
                                              "\"role\":%s,\"position\":%s" ),
                                         quotedJson( aSource.m_Uuid.AsString() ),
                                         quotedJson( aSourceLabel ),
                                         static_cast<int>( aSource.Type() ),
                                         quotedJson( aRole ), pointDetailsJson( aPosition ) );

    if( !aExtraFields.IsEmpty() )
        details += wxS( "," ) + aExtraFields;

    details += wxS( "}" );
    return details;
}


AI_CONTEXT_ANCHOR makePcbAnchor( const wxString& aId, AI_CONTEXT_ANCHOR_KIND aKind,
                                 const wxString& aLabel, const wxString& aSummary,
                                 const VECTOR2I& aPosition, int aLayer,
                                 const wxString& aDetailsJson )
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aLabel;
    anchor.m_Summary = aSummary;
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = aLayer;
    anchor.m_DetailsJson = aDetailsJson;
    anchor.m_Confidence = 1.0;
    return anchor;
}
```

- [x] **Step 3: Add footprint and pad anchor helpers**

Add these helpers after `makePadRef()`:

```cpp
AI_CONTEXT_ANCHOR makeFootprintAnchor( const FOOTPRINT& aFootprint )
{
    const wxString label = footprintContextLabel( aFootprint );
    const VECTOR2I position = aFootprint.GetPosition();
    const wxString extra =
            wxString::Format( wxS( "\"reference\":%s,\"value\":%s,\"footprint_id\":%s" ),
                              quotedJson( aFootprint.GetReference() ),
                              quotedJson( aFootprint.GetValue() ),
                              quotedJson( aFootprint.GetFPID().Format() ) );

    return makePcbAnchor(
            anchorId( wxS( "footprint" ), aFootprint.m_Uuid, wxS( "position" ) ),
            AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
            wxS( "footprint:" ) + label + wxS( ":position" ),
            wxS( "Footprint placement origin" ), position,
            static_cast<int>( aFootprint.GetLayer() ),
            anchorDetailsJson( aFootprint, label, wxS( "position" ), position, extra ) );
}


AI_CONTEXT_ANCHOR makePadAnchor( const FOOTPRINT& aFootprint, const PAD& aPad )
{
    const wxString label = padContextLabel( aFootprint, aPad );
    const VECTOR2I position = aPad.GetPosition();
    const wxString extra =
            wxString::Format( wxS( "\"footprint_reference\":%s,\"pad_number\":%s,"
                                  "\"net_code\":%d,\"net_name\":%s" ),
                              quotedJson( aFootprint.GetReference() ),
                              quotedJson( aPad.GetNumber() ), aPad.GetNetCode(),
                              quotedJson( aPad.GetNetname() ) );

    return makePcbAnchor(
            anchorId( wxS( "pad" ), aPad.m_Uuid, wxS( "center" ) ),
            AI_CONTEXT_ANCHOR_KIND::RouteTarget,
            wxS( "pad:" ) + label + wxS( ":center" ),
            wxS( "Pad center route target" ), position,
            static_cast<int>( aPad.GetPrincipalLayer() ),
            anchorDetailsJson( aPad, label, wxS( "center" ), position, extra ) );
}
```

- [x] **Step 4: Add route and shape anchor helpers**

Add these helpers after `makeRoutingRef()`:

```cpp
void appendRoutingAnchors( const PCB_TRACK& aTrack, std::vector<AI_CONTEXT_ANCHOR>& aAnchors )
{
    const wxString sourceLabel = makeRoutingRef( aTrack ).m_Label;
    const wxString netAndLayer =
            wxString::Format( wxS( "\"net_code\":%d,\"net_name\":%s,\"layer\":%s" ),
                              aTrack.GetNetCode(), quotedJson( aTrack.GetNetname() ),
                              quotedJson( aTrack.GetLayerName() ) );

    if( aTrack.Type() == PCB_VIA_T )
    {
        const VECTOR2I position = aTrack.GetPosition();
        aAnchors.push_back( makePcbAnchor(
                anchorId( wxS( "via" ), aTrack.m_Uuid, wxS( "center" ) ),
                AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                wxS( "via:" ) + formatPoint( position ) + wxS( ":center" ),
                wxS( "Via center route target" ), position, -1,
                anchorDetailsJson( aTrack, sourceLabel, wxS( "center" ), position,
                                   wxString::Format( wxS( "\"net_code\":%d,\"net_name\":%s" ),
                                                     aTrack.GetNetCode(),
                                                     quotedJson( aTrack.GetNetname() ) ) ) ) );
        return;
    }

    if( aTrack.Type() == PCB_ARC_T )
    {
        const PCB_ARC& arc = static_cast<const PCB_ARC&>( aTrack );
        const std::vector<std::pair<wxString, VECTOR2I>> points = {
            { wxS( "start" ), arc.GetStart() },
            { wxS( "mid" ), arc.GetMid() },
            { wxS( "end" ), arc.GetEnd() }
        };

        for( const auto& point : points )
        {
            const AI_CONTEXT_ANCHOR_KIND kind =
                    point.first == wxS( "start" ) ? AI_CONTEXT_ANCHOR_KIND::RouteStart
                    : point.first == wxS( "mid" ) ? AI_CONTEXT_ANCHOR_KIND::RouteCandidate
                                                   : AI_CONTEXT_ANCHOR_KIND::RouteTarget;

            aAnchors.push_back( makePcbAnchor(
                    anchorId( wxS( "arc" ), aTrack.m_Uuid, point.first ), kind,
                    wxS( "arc:" ) + point.first, wxS( "Arc route anchor" ), point.second,
                    static_cast<int>( aTrack.GetLayer() ),
                    anchorDetailsJson( aTrack, sourceLabel, point.first, point.second,
                                       netAndLayer ) ) );
        }

        return;
    }

    aAnchors.push_back( makePcbAnchor(
            anchorId( wxS( "track" ), aTrack.m_Uuid, wxS( "start" ) ),
            AI_CONTEXT_ANCHOR_KIND::RouteStart,
            wxS( "track:start" ), wxS( "Track route start" ), aTrack.GetStart(),
            static_cast<int>( aTrack.GetLayer() ),
            anchorDetailsJson( aTrack, sourceLabel, wxS( "start" ), aTrack.GetStart(),
                               netAndLayer ) ) );
    aAnchors.push_back( makePcbAnchor(
            anchorId( wxS( "track" ), aTrack.m_Uuid, wxS( "end" ) ),
            AI_CONTEXT_ANCHOR_KIND::RouteTarget,
            wxS( "track:end" ), wxS( "Track route target" ), aTrack.GetEnd(),
            static_cast<int>( aTrack.GetLayer() ),
            anchorDetailsJson( aTrack, sourceLabel, wxS( "end" ), aTrack.GetEnd(),
                               netAndLayer ) ) );
}


void appendShapeAnchors( const PCB_SHAPE& aShape, const wxString& aSourceLabel,
                         std::vector<AI_CONTEXT_ANCHOR>& aAnchors )
{
    const auto appendPoint =
            [&]( const wxString& aRole, const VECTOR2I& aPosition )
            {
                aAnchors.push_back( makePcbAnchor(
                        anchorId( wxS( "shape" ), aShape.m_Uuid, aRole ),
                        AI_CONTEXT_ANCHOR_KIND::ShapeCorner,
                        wxS( "shape:" ) + aRole, wxS( "Shape geometry anchor" ),
                        aPosition, static_cast<int>( aShape.GetLayer() ),
                        anchorDetailsJson( aShape, aSourceLabel, aRole, aPosition,
                                           wxString::Format( wxS( "\"layer\":%s" ),
                                                             quotedJson( boardLayerName(
                                                                     aShape,
                                                                     aShape.GetLayer() ) ) ) ) ) );
            };

    switch( aShape.GetShape() )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
        appendPoint( wxS( "start" ), aShape.GetStart() );
        appendPoint( wxS( "end" ), aShape.GetEnd() );
        break;

    case SHAPE_T::ARC:
        appendPoint( wxS( "start" ), aShape.GetStart() );
        appendPoint( wxS( "mid" ), aShape.GetArcMid() );
        appendPoint( wxS( "end" ), aShape.GetEnd() );
        break;

    case SHAPE_T::CIRCLE:
        appendPoint( wxS( "center" ), aShape.GetCenter() );
        appendPoint( wxS( "radius_point" ), aShape.GetEnd() );
        break;

    default:
        appendPoint( wxS( "position" ), aShape.GetPosition() );
        break;
    }
}
```

- [x] **Step 5: Wire anchor generation into BuildIndex**

Update `BuildIndex()`:

```cpp
std::vector<AI_CONTEXT_ANCHOR> anchors;
```

Add footprint anchors after pushing each footprint ref:

```cpp
anchors.push_back( makeFootprintAnchor( *footprint ) );
```

Add pad anchors inside the pad loop:

```cpp
anchors.push_back( makePadAnchor( *footprint, *pad ) );
```

Add shape anchors in footprint graphical item and board drawing shape branches:

```cpp
appendShapeAnchors( shape, ref.m_Label, anchors );
```

Add routing anchors in the routing object loop:

```cpp
appendRoutingAnchors( *track, anchors );
```

Call before returning:

```cpp
index.SetAnchors( anchors );
```

- [x] **Step 6: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\pcbnew\qa_pcbnew.exe" --run_test=AiPcbContextAdapter --log_level=nothing
```

Expected: build exit code 0 and `AiPcbContextAdapter` reports no errors.

- [x] **Step 7: Commit**

```bash
git add pcbnew/kisurf_ai_pcb_context_adapter.cpp qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp
git commit -m "feat: generate pcb semantic context anchors"
```

## Task 3: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-18-ai-pcb-semantic-anchor-generation-implementation.md`

- [x] **Step 1: Run PCB adapter and common AI tests**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_pcbnew qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\pcbnew\qa_pcbnew.exe" --run_test=AiPcbContextAdapter --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeTypes --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiContextIndex --log_level=nothing
```

Expected: all commands exit 0.

- [x] **Step 2: Run editor target smoke build**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: `pcbnew` builds with exit code 0.

- [x] **Step 3: Run whitespace and secret checks**

Run:

```powershell
git diff --check
git grep -n -E "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" -- pcbnew qa docs
```

Expected: whitespace check exits 0; secret scan has no matches.

- [x] **Step 4: Update this plan status**

Check off each completed step in this file.

- [x] **Step 5: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-18-ai-pcb-semantic-anchor-generation-implementation.md
git commit -m "docs: update pcb semantic anchor plan status"
```

## Self-Review

- Spec coverage: The plan covers factual PCB anchors for footprint, pad, via, track, arc, and shape geometry, plus snapshot carriage through `AI_CONTEXT_INDEX`.
- Placeholder scan: Every step names exact files, code snippets, commands, expected failures, expected passes, and commit messages.
- Type consistency: `AI_CONTEXT_ANCHOR`, `AI_CONTEXT_ANCHOR_KIND`, `SetAnchors`, `index.Anchors()`, and `snapshot.m_Anchors` match the common anchor contract.
- Scope check: Navigation tools, preview overlays, synthetic routing candidates, panel walkers, and schematic anchors remain separate slices.
