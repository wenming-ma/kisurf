#include <kisurf_ai_pcb_context_adapter.h>

#include <board.h>
#include <board_connected_item.h>
#include <board_design_settings.h>
#include <connectivity/connectivity_algo.h>
#include <connectivity/connectivity_data.h>
#include <core/typeinfo.h>
#include <drc/drc_engine.h>
#include <drc/drc_rule.h>
#include <footprint.h>
#include <netclass.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_barcode.h>
#include <pcb_dimension.h>
#include <pcb_field.h>
#include <pcb_shape.h>
#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_target.h>
#include <pcb_track.h>
#include <project/net_settings.h>
#include <project.h>
#include <project/project_local_settings.h>
#include <zone.h>

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace
{

wxString formatPoint( const VECTOR2I& aPoint )
{
    return wxString::Format( wxS( "%d,%d" ), aPoint.x, aPoint.y );
}


wxString jsonEscape( const wxString& aText )
{
    wxString escaped;

    for( size_t i = 0; i < aText.Length(); ++i )
    {
        wxUniChar ch = aText[i];

        switch( ch.GetValue() )
        {
        case '"':  escaped += wxS( "\\\"" ); break;
        case '\\': escaped += wxS( "\\\\" ); break;
        case '\b': escaped += wxS( "\\b" ); break;
        case '\f': escaped += wxS( "\\f" ); break;
        case '\n': escaped += wxS( "\\n" ); break;
        case '\r': escaped += wxS( "\\r" ); break;
        case '\t': escaped += wxS( "\\t" ); break;

        default:
            if( ch.GetValue() < 0x20 )
                escaped += wxString::Format( wxS( "\\u%04X" ), ch.GetValue() );
            else
                escaped += ch;

            break;
        }
    }

    return escaped;
}


wxString quotedJson( const wxString& aText )
{
    return wxS( "\"" ) + jsonEscape( aText ) + wxS( "\"" );
}


wxString boolJson( bool aValue )
{
    return aValue ? wxS( "true" ) : wxS( "false" );
}


wxString jsonArray( const std::vector<wxString>& aEntries )
{
    wxString result;

    for( size_t i = 0; i < aEntries.size(); ++i )
    {
        if( i > 0 )
            result += wxS( "," );

        result += aEntries[i];
    }

    return wxS( "[" ) + result + wxS( "]" );
}


wxString pointDetailsJson( const VECTOR2I& aPoint )
{
    return wxString::Format( wxS( "{\"x\":%d,\"y\":%d}" ), aPoint.x, aPoint.y );
}


wxString boxRectDetailsJson( const BOX2I& aBox )
{
    return wxString::Format( wxS( "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}" ),
                             static_cast<int>( aBox.GetX() ),
                             static_cast<int>( aBox.GetY() ),
                             static_cast<int>( aBox.GetWidth() ),
                             static_cast<int>( aBox.GetHeight() ) );
}


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


wxString angleDetailsJson( const EDA_ANGLE& aAngle )
{
    return wxString::Format( wxS( "%.10g" ), aAngle.AsDegrees() );
}


wxString boardLayerName( const BOARD_ITEM& aItem, PCB_LAYER_ID aLayer )
{
    if( const BOARD* board = aItem.GetBoard() )
        return board->GetLayerName( aLayer );

    return BOARD::GetStandardLayerName( aLayer );
}


wxString layerSetDetailsJson( const BOARD_ITEM& aItem, const LSET& aLayers )
{
    wxString details = wxS( "[" );
    bool     first = true;

    for( PCB_LAYER_ID layer : aLayers.UIOrder() )
    {
        if( !first )
            details += wxS( "," );

        details += quotedJson( boardLayerName( aItem, layer ) );
        first = false;
    }

    details += wxS( "]" );

    return details;
}


wxString padShapeToken( PAD_SHAPE aShape )
{
    switch( aShape )
    {
    case PAD_SHAPE::CIRCLE:          return wxS( "circle" );
    case PAD_SHAPE::RECTANGLE:       return wxS( "rect" );
    case PAD_SHAPE::OVAL:            return wxS( "oval" );
    case PAD_SHAPE::TRAPEZOID:       return wxS( "trapezoid" );
    case PAD_SHAPE::ROUNDRECT:       return wxS( "roundrect" );
    case PAD_SHAPE::CHAMFERED_RECT:  return wxS( "chamfered_rect" );
    case PAD_SHAPE::CUSTOM:          return wxS( "custom" );
    default:                         return wxS( "unknown" );
    }
}


wxString shapeToken( SHAPE_T aShape )
{
    switch( aShape )
    {
    case SHAPE_T::SEGMENT:      return wxS( "segment" );
    case SHAPE_T::RECTANGLE:    return wxS( "rect" );
    case SHAPE_T::ARC:          return wxS( "arc" );
    case SHAPE_T::CIRCLE:       return wxS( "circle" );
    case SHAPE_T::POLY:         return wxS( "poly" );
    case SHAPE_T::BEZIER:       return wxS( "bezier" );
    case SHAPE_T::ELLIPSE:      return wxS( "ellipse" );
    case SHAPE_T::ELLIPSE_ARC:  return wxS( "ellipse_arc" );
    default:                    return wxS( "unknown" );
    }
}


wxString hJustifyToken( GR_TEXT_H_ALIGN_T aJustify )
{
    switch( aJustify )
    {
    case GR_TEXT_H_ALIGN_LEFT:    return wxS( "left" );
    case GR_TEXT_H_ALIGN_RIGHT:   return wxS( "right" );
    case GR_TEXT_H_ALIGN_CENTER:  return wxS( "center" );
    default:                      return wxS( "unknown" );
    }
}


wxString vJustifyToken( GR_TEXT_V_ALIGN_T aJustify )
{
    switch( aJustify )
    {
    case GR_TEXT_V_ALIGN_TOP:     return wxS( "top" );
    case GR_TEXT_V_ALIGN_BOTTOM:  return wxS( "bottom" );
    case GR_TEXT_V_ALIGN_CENTER:  return wxS( "center" );
    default:                      return wxS( "unknown" );
    }
}


wxString barcodeKindToken( BARCODE_T aKind )
{
    switch( aKind )
    {
    case BARCODE_T::CODE_39:        return wxS( "code_39" );
    case BARCODE_T::CODE_128:       return wxS( "code_128" );
    case BARCODE_T::DATA_MATRIX:    return wxS( "data_matrix" );
    case BARCODE_T::QR_CODE:        return wxS( "qr_code" );
    case BARCODE_T::MICRO_QR_CODE:  return wxS( "micro_qr_code" );
    default:                        return wxS( "unknown" );
    }
}


wxString dimensionTypeToken( KICAD_T aType )
{
    switch( aType )
    {
    case PCB_DIM_ALIGNED_T:     return wxS( "aligned" );
    case PCB_DIM_ORTHOGONAL_T:  return wxS( "orthogonal" );
    case PCB_DIM_CENTER_T:      return wxS( "center" );
    case PCB_DIM_RADIAL_T:      return wxS( "radial" );
    case PCB_DIM_LEADER_T:      return wxS( "leader" );
    default:                    return wxS( "dimension" );
    }
}


wxString textPreviewLabel( const wxString& aPrefix, wxString aText, const KIID& aUuid )
{
    aText.Replace( wxS( "\r" ), wxS( " " ) );
    aText.Replace( wxS( "\n" ), wxS( " " ) );
    aText.Replace( wxS( "\t" ), wxS( " " ) );
    aText.Trim( true );
    aText.Trim( false );

    if( aText.Length() > 48 )
        aText = aText.Left( 48 ) + wxS( "..." );

    if( aText.IsEmpty() )
        aText = aUuid.AsString();

    return aPrefix + aText;
}


wxString footprintChildLabelPrefix( const FOOTPRINT& aFootprint )
{
    wxString subject = aFootprint.GetReference();

    if( subject.IsEmpty() )
        subject = aFootprint.m_Uuid.AsString();

    return wxS( "fp:" ) + subject + wxS( "/" );
}


wxString parentFootprintDetailsJson( const FOOTPRINT* aFootprint )
{
    if( !aFootprint )
        return wxEmptyString;

    return wxString::Format( wxS( ",\"parent_footprint_reference\":%s,"
                                  "\"parent_footprint_uuid\":%s" ),
                             quotedJson( aFootprint->GetReference() ),
                             quotedJson( aFootprint->m_Uuid.AsString() ) );
}


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


wxString makeFootprintDetailsJson( const FOOTPRINT& aFootprint )
{
    wxString details = wxString::Format(
            wxS( "{\"kind\":\"footprint\",\"reference\":%s,"
                 "\"value\":%s,\"footprint_id\":%s,\"position\":%s,"
                 "\"orientation_degrees\":%s,\"layer\":%s,"
                 "\"pad_count\":%u,\"bbox\":%s" ),
            quotedJson( aFootprint.GetReference() ),
            quotedJson( aFootprint.GetValue() ),
            quotedJson( aFootprint.GetFPID().Format() ),
            pointDetailsJson( aFootprint.GetPosition() ),
            angleDetailsJson( aFootprint.GetOrientation() ),
            quotedJson( aFootprint.GetLayerName() ),
            aFootprint.GetPadCount(),
            boxRectDetailsJson( aFootprint.GetBoundingBox( false ) ) );

    bool   hasPadsBBox = false;
    BOX2I  padsBBox;

    for( PAD* pad : aFootprint.Pads() )
    {
        if( hasPadsBBox )
            padsBBox.Merge( pad->GetBoundingBox() );
        else
        {
            padsBBox = pad->GetBoundingBox();
            hasPadsBBox = true;
        }
    }

    if( hasPadsBBox )
        details += wxS( ",\"pads_bbox\":" ) + boxRectDetailsJson( padsBBox );

    const PCB_LAYER_ID courtyardLayer = aFootprint.GetLayer() == B_Cu ? B_CrtYd : F_CrtYd;
    const SHAPE_POLY_SET& courtyard = aFootprint.GetCourtyard( courtyardLayer );

    if( !courtyard.IsEmpty() )
        details += wxS( ",\"courtyard_bbox\":" ) + boxRectDetailsJson( courtyard.BBox() );

    details += wxS( "}" );
    return details;
}


AI_OBJECT_REF makeFootprintRef( const FOOTPRINT& aFootprint )
{
    return AI_OBJECT_REF( aFootprint.m_Uuid, aFootprint.Type(),
                          footprintContextLabel( aFootprint ),
                          makeFootprintDetailsJson( aFootprint ) );
}


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


bool isRoutingObject( const PCB_TRACK& aTrack )
{
    return aTrack.Type() == PCB_TRACE_T || aTrack.Type() == PCB_ARC_T
           || aTrack.Type() == PCB_VIA_T;
}


wxString makeRoutingDetailsJson( const PCB_TRACK& aTrack )
{
    wxString details;

    if( aTrack.Type() == PCB_VIA_T )
    {
        const PCB_VIA& via = static_cast<const PCB_VIA&>( aTrack );

        details = wxString::Format( wxS( "{\"kind\":\"via\",\"position\":%s,"
                                         "\"diameter\":%d,\"net_code\":%d,\"net_name\":%s}" ),
                                    pointDetailsJson( via.GetPosition() ),
                                    via.GetWidth( PADSTACK::ALL_LAYERS ),
                                    via.GetNetCode(), quotedJson( via.GetNetname() ) );
    }
    else if( aTrack.Type() == PCB_ARC_T )
    {
        const PCB_ARC& arc = static_cast<const PCB_ARC&>( aTrack );

        details = wxString::Format( wxS( "{\"kind\":\"arc\",\"start\":%s,\"mid\":%s,"
                                         "\"end\":%s,\"layer\":%s,\"width\":%d,"
                                         "\"net_code\":%d,\"net_name\":%s}" ),
                                    pointDetailsJson( arc.GetStart() ),
                                    pointDetailsJson( arc.GetMid() ),
                                    pointDetailsJson( arc.GetEnd() ),
                                    quotedJson( arc.GetLayerName() ), arc.GetWidth(),
                                    arc.GetNetCode(), quotedJson( arc.GetNetname() ) );
    }
    else
    {
        details = wxString::Format( wxS( "{\"kind\":\"track\",\"start\":%s,\"end\":%s,"
                                         "\"layer\":%s,\"width\":%d,\"net_code\":%d,"
                                         "\"net_name\":%s}" ),
                                    pointDetailsJson( aTrack.GetStart() ),
                                    pointDetailsJson( aTrack.GetEnd() ),
                                    quotedJson( aTrack.GetLayerName() ), aTrack.GetWidth(),
                                    aTrack.GetNetCode(), quotedJson( aTrack.GetNetname() ) );
    }

    return details;
}


AI_OBJECT_REF makeRoutingRef( const PCB_TRACK& aTrack )
{
    wxString label;

    if( aTrack.Type() == PCB_VIA_T )
    {
        label = wxS( "via:" ) + formatPoint( aTrack.GetPosition() );
    }
    else if( aTrack.Type() == PCB_ARC_T )
    {
        const PCB_ARC& arc = static_cast<const PCB_ARC&>( aTrack );

        label = wxS( "arc:" ) + formatPoint( arc.GetStart() ) + wxS( "->" )
                + formatPoint( arc.GetMid() ) + wxS( "->" ) + formatPoint( arc.GetEnd() );
    }
    else
    {
        label = wxS( "track:" ) + formatPoint( aTrack.GetStart() ) + wxS( "->" )
                + formatPoint( aTrack.GetEnd() );
    }

    return AI_OBJECT_REF( aTrack.m_Uuid, aTrack.Type(), label,
                          makeRoutingDetailsJson( aTrack ) );
}


void appendRoutingAnchors( const PCB_TRACK& aTrack, std::vector<AI_CONTEXT_ANCHOR>& aAnchors )
{
    const wxString sourceLabel = makeRoutingRef( aTrack ).m_Label;

    if( aTrack.Type() == PCB_VIA_T )
    {
        const VECTOR2I position = aTrack.GetPosition();
        const wxString extra =
                wxString::Format( wxS( "\"net_code\":%d,\"net_name\":%s" ),
                                  aTrack.GetNetCode(), quotedJson( aTrack.GetNetname() ) );

        aAnchors.push_back( makePcbAnchor(
                anchorId( wxS( "via" ), aTrack.m_Uuid, wxS( "center" ) ),
                AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                wxS( "via:" ) + formatPoint( position ) + wxS( ":center" ),
                wxS( "Via center route target" ), position, -1,
                anchorDetailsJson( aTrack, sourceLabel, wxS( "center" ), position, extra ) ) );
        return;
    }

    const wxString netAndLayer =
            wxString::Format( wxS( "\"net_code\":%d,\"net_name\":%s,\"layer\":%s" ),
                              aTrack.GetNetCode(), quotedJson( aTrack.GetNetname() ),
                              quotedJson( aTrack.GetLayerName() ) );

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


wxString makePadDetailsJson( const FOOTPRINT& aFootprint, const PAD& aPad )
{
    return wxString::Format( wxS( "{\"kind\":\"pad\",\"footprint_reference\":%s,"
                                  "\"number\":%s,\"position\":%s,\"size\":%s,"
                                  "\"drill\":%s,\"shape\":%s,\"layer\":%s,"
                                  "\"orientation_degrees\":%s,\"net_code\":%d,"
                                  "\"net_name\":%s}" ),
                             quotedJson( aFootprint.GetReference() ),
                             quotedJson( aPad.GetNumber() ),
                             pointDetailsJson( aPad.GetPosition() ),
                             pointDetailsJson( aPad.GetSize( PADSTACK::ALL_LAYERS ) ),
                             pointDetailsJson( aPad.GetDrillSize() ),
                             quotedJson( padShapeToken(
                                     aPad.GetShape( PADSTACK::ALL_LAYERS ) ) ),
                             quotedJson( boardLayerName( aPad, aPad.GetPrincipalLayer() ) ),
                             angleDetailsJson( aPad.GetOrientation() ),
                             aPad.GetNetCode(), quotedJson( aPad.GetNetname() ) );
}


AI_OBJECT_REF makePadRef( const FOOTPRINT& aFootprint, const PAD& aPad )
{
    return AI_OBJECT_REF( aPad.m_Uuid, aPad.Type(), padContextLabel( aFootprint, aPad ),
                          makePadDetailsJson( aFootprint, aPad ) );
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


wxString makeShapeDetailsJson( const PCB_SHAPE& aShape, const FOOTPRINT* aParentFootprint = nullptr )
{
    wxString details = wxString::Format( wxS( "{\"kind\":\"shape\",\"shape\":%s,"
                                              "\"layer\":%s,\"width\":%d,\"net_code\":%d,"
                                              "\"net_name\":%s" ),
                                         quotedJson( shapeToken( aShape.GetShape() ) ),
                                         quotedJson( boardLayerName( aShape, aShape.GetLayer() ) ),
                                         aShape.GetWidth(), aShape.GetNetCode(),
                                         quotedJson( aShape.GetNetname() ) );

    switch( aShape.GetShape() )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
        details += wxString::Format( wxS( ",\"start\":%s,\"end\":%s" ),
                                     pointDetailsJson( aShape.GetStart() ),
                                     pointDetailsJson( aShape.GetEnd() ) );
        break;

    case SHAPE_T::ARC:
        details += wxString::Format( wxS( ",\"start\":%s,\"mid\":%s,\"end\":%s" ),
                                     pointDetailsJson( aShape.GetStart() ),
                                     pointDetailsJson( aShape.GetArcMid() ),
                                     pointDetailsJson( aShape.GetEnd() ) );
        break;

    case SHAPE_T::CIRCLE:
        details += wxString::Format( wxS( ",\"center\":%s,\"radius_point\":%s,"
                                          "\"radius\":%d" ),
                                     pointDetailsJson( aShape.GetCenter() ),
                                     pointDetailsJson( aShape.GetEnd() ), aShape.GetRadius() );
        break;

    default:
        details += wxString::Format( wxS( ",\"position\":%s" ),
                                     pointDetailsJson( aShape.GetPosition() ) );
        break;
    }

    details += parentFootprintDetailsJson( aParentFootprint );
    details += wxS( "}" );

    return details;
}


wxString shapePreviewLabel( const PCB_SHAPE& aShape )
{
    wxString label;

    switch( aShape.GetShape() )
    {
    case SHAPE_T::SEGMENT:
        label = aShape.GetLayer() == Edge_Cuts ? wxS( "edge:" ) : wxS( "shape:segment:" );
        label += formatPoint( aShape.GetStart() ) + wxS( "->" )
                 + formatPoint( aShape.GetEnd() );
        break;

    case SHAPE_T::ARC:
        label = wxS( "shape:arc:" ) + formatPoint( aShape.GetStart() ) + wxS( "->" )
                + formatPoint( aShape.GetArcMid() ) + wxS( "->" )
                + formatPoint( aShape.GetEnd() );
        break;

    case SHAPE_T::RECTANGLE:
        label = wxS( "shape:rect:" ) + formatPoint( aShape.GetStart() ) + wxS( "->" )
                + formatPoint( aShape.GetEnd() );
        break;

    case SHAPE_T::CIRCLE:
        label = wxS( "shape:circle:" ) + formatPoint( aShape.GetCenter() ) + wxS( "->" )
                + formatPoint( aShape.GetEnd() );
        break;

    default:
        label = wxS( "shape:" ) + aShape.m_Uuid.AsString();
        break;
    }

    return label;
}


AI_OBJECT_REF makeShapeRef( const PCB_SHAPE& aShape )
{
    return AI_OBJECT_REF( aShape.m_Uuid, aShape.Type(), shapePreviewLabel( aShape ),
                          makeShapeDetailsJson( aShape ) );
}


void appendShapeAnchors( const PCB_SHAPE& aShape, const wxString& aSourceLabel,
                         std::vector<AI_CONTEXT_ANCHOR>& aAnchors )
{
    const auto appendPoint =
            [&]( const wxString& aRole, const VECTOR2I& aPosition )
            {
                const wxString extra = wxString::Format(
                        wxS( "\"layer\":%s" ),
                        quotedJson( boardLayerName( aShape, aShape.GetLayer() ) ) );

                aAnchors.push_back( makePcbAnchor(
                        anchorId( wxS( "shape" ), aShape.m_Uuid, aRole ),
                        AI_CONTEXT_ANCHOR_KIND::ShapeCorner,
                        wxS( "shape:" ) + aRole, wxS( "Shape geometry anchor" ),
                        aPosition, static_cast<int>( aShape.GetLayer() ),
                        anchorDetailsJson( aShape, aSourceLabel, aRole, aPosition,
                                           extra ) ) );
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


AI_OBJECT_REF makeFootprintShapeRef( const FOOTPRINT& aFootprint, const PCB_SHAPE& aShape )
{
    return AI_OBJECT_REF( aShape.m_Uuid, aShape.Type(),
                          footprintChildLabelPrefix( aFootprint ) + shapePreviewLabel( aShape ),
                          makeShapeDetailsJson( aShape, &aFootprint ) );
}


wxString makeTextCommonDetailsJson( const BOARD_ITEM& aItem, const EDA_TEXT& aText,
                                    const wxString& aKind,
                                    const FOOTPRINT* aParentFootprint = nullptr )
{
    wxString details = wxString::Format( wxS( "{\"kind\":%s,\"text\":%s,\"shown_text\":%s,"
                                              "\"position\":%s,\"size\":%s,\"layer\":%s,"
                                              "\"angle_degrees\":%s,\"visible\":%s,"
                                              "\"mirrored\":%s,\"bold\":%s,\"italic\":%s,"
                                              "\"h_justify\":%s,\"v_justify\":%s" ),
                                         quotedJson( aKind ), quotedJson( aText.GetText() ),
                                         quotedJson( aText.GetShownText( true ) ),
                                         pointDetailsJson( aItem.GetPosition() ),
                                         pointDetailsJson( aText.GetTextSize() ),
                                         quotedJson( boardLayerName( aItem, aItem.GetLayer() ) ),
                                         angleDetailsJson( aText.GetTextAngle() ),
                                         boolJson( aText.IsVisible() ),
                                         boolJson( aText.IsMirrored() ),
                                         boolJson( aText.IsBold() ),
                                         boolJson( aText.IsItalic() ),
                                         quotedJson( hJustifyToken( aText.GetHorizJustify() ) ),
                                         quotedJson( vJustifyToken( aText.GetVertJustify() ) ) );

    details += parentFootprintDetailsJson( aParentFootprint );

    return details;
}


wxString makeTextDetailsJson( const PCB_TEXT& aText, const FOOTPRINT* aParentFootprint = nullptr )
{
    return makeTextCommonDetailsJson( aText, aText, wxS( "text" ), aParentFootprint )
           + wxS( "}" );
}


AI_OBJECT_REF makeTextRef( const PCB_TEXT& aText )
{
    return AI_OBJECT_REF( aText.m_Uuid, aText.Type(),
                          textPreviewLabel( wxS( "text:" ), aText.GetText(), aText.m_Uuid ),
                          makeTextDetailsJson( aText ) );
}


AI_OBJECT_REF makeFootprintTextRef( const FOOTPRINT& aFootprint, const PCB_TEXT& aText )
{
    return AI_OBJECT_REF( aText.m_Uuid, aText.Type(),
                          footprintChildLabelPrefix( aFootprint )
                                  + textPreviewLabel( wxS( "text:" ), aText.GetText(),
                                                      aText.m_Uuid ),
                          makeTextDetailsJson( aText, &aFootprint ) );
}


wxString makeFieldDetailsJson( const FOOTPRINT& aFootprint, const PCB_FIELD& aField )
{
    wxString details = makeTextCommonDetailsJson( aField, aField, wxS( "field" ), &aFootprint );

    details += wxString::Format( wxS( ",\"field_name\":%s,\"field_canonical_name\":%s,"
                                      "\"field_id\":%d,\"field_ordinal\":%d,"
                                      "\"is_reference\":%s,\"is_value\":%s}" ),
                                 quotedJson( aField.GetName() ),
                                 quotedJson( aField.GetCanonicalName() ),
                                 static_cast<int>( aField.GetId() ), aField.GetOrdinal(),
                                 boolJson( aField.IsReference() ),
                                 boolJson( aField.IsValue() ) );

    return details;
}


AI_OBJECT_REF makeFieldRef( const FOOTPRINT& aFootprint, const PCB_FIELD& aField )
{
    wxString fieldName = aField.GetName();

    if( fieldName.IsEmpty() )
        fieldName = aField.GetCanonicalName();

    if( fieldName.IsEmpty() )
        fieldName = aField.m_Uuid.AsString();

    return AI_OBJECT_REF( aField.m_Uuid, aField.Type(),
                          footprintChildLabelPrefix( aFootprint ) + wxS( "field:" ) + fieldName,
                          makeFieldDetailsJson( aFootprint, aField ) );
}


wxString makeTextboxDetailsJson( const PCB_TEXTBOX& aTextbox,
                                 const FOOTPRINT* aParentFootprint = nullptr )
{
    wxString details =
            makeTextCommonDetailsJson( aTextbox, aTextbox, wxS( "textbox" ), aParentFootprint );

    details += wxString::Format( wxS( ",\"start\":%s,\"end\":%s,"
                                      "\"border_enabled\":%s,\"border_width\":%d}" ),
                                 pointDetailsJson( aTextbox.GetStart() ),
                                 pointDetailsJson( aTextbox.GetEnd() ),
                                 boolJson( aTextbox.IsBorderEnabled() ),
                                 aTextbox.GetBorderWidth() );

    return details;
}


AI_OBJECT_REF makeTextboxRef( const PCB_TEXTBOX& aTextbox )
{
    return AI_OBJECT_REF( aTextbox.m_Uuid, aTextbox.Type(),
                          textPreviewLabel( wxS( "textbox:" ), aTextbox.GetText(),
                                            aTextbox.m_Uuid ),
                          makeTextboxDetailsJson( aTextbox ) );
}


AI_OBJECT_REF makeFootprintTextboxRef( const FOOTPRINT& aFootprint, const PCB_TEXTBOX& aTextbox )
{
    return AI_OBJECT_REF( aTextbox.m_Uuid, aTextbox.Type(),
                          footprintChildLabelPrefix( aFootprint )
                                  + textPreviewLabel( wxS( "textbox:" ), aTextbox.GetText(),
                                                      aTextbox.m_Uuid ),
                          makeTextboxDetailsJson( aTextbox, &aFootprint ) );
}


wxString makeTargetDetailsJson( const PCB_TARGET& aTarget )
{
    return wxString::Format( wxS( "{\"kind\":\"target\",\"position\":%s,\"layer\":%s,"
                                  "\"shape\":%d,\"size\":%d,\"width\":%d}" ),
                             pointDetailsJson( aTarget.GetPosition() ),
                             quotedJson( boardLayerName( aTarget, aTarget.GetLayer() ) ),
                             aTarget.GetShape(), aTarget.GetSize(), aTarget.GetWidth() );
}


AI_OBJECT_REF makeTargetRef( const PCB_TARGET& aTarget )
{
    return AI_OBJECT_REF( aTarget.m_Uuid, aTarget.Type(),
                          wxS( "target:" ) + formatPoint( aTarget.GetPosition() ),
                          makeTargetDetailsJson( aTarget ) );
}


wxString makeBarcodeDetailsJson( const PCB_BARCODE& aBarcode )
{
    return wxString::Format( wxS( "{\"kind\":\"barcode\",\"text\":%s,\"shown_text\":%s,"
                                  "\"barcode_kind\":%s,\"position\":%s,\"layer\":%s,"
                                  "\"width\":%d,\"height\":%d,\"show_text\":%s,"
                                  "\"angle_degrees\":%s}" ),
                             quotedJson( aBarcode.GetText() ),
                             quotedJson( aBarcode.GetShownText() ),
                             quotedJson( barcodeKindToken( aBarcode.GetKind() ) ),
                             pointDetailsJson( aBarcode.GetPosition() ),
                             quotedJson( boardLayerName( aBarcode, aBarcode.GetLayer() ) ),
                             aBarcode.GetWidth(), aBarcode.GetHeight(),
                             boolJson( aBarcode.GetShowText() ),
                             angleDetailsJson( aBarcode.GetAngle() ) );
}


AI_OBJECT_REF makeBarcodeRef( const PCB_BARCODE& aBarcode )
{
    return AI_OBJECT_REF( aBarcode.m_Uuid, aBarcode.Type(),
                          textPreviewLabel( wxS( "barcode:" ), aBarcode.GetText(),
                                            aBarcode.m_Uuid ),
                          makeBarcodeDetailsJson( aBarcode ) );
}


wxString makeTableDetailsJson( const PCB_TABLE& aTable )
{
    std::vector<PCB_TABLECELL*> cells = aTable.GetCells();

    return wxString::Format( wxS( "{\"kind\":\"table\",\"position\":%s,\"end\":%s,"
                                  "\"layer\":%s,\"columns\":%d,\"rows\":%d,"
                                  "\"cell_count\":%zu,\"border_width\":%d,"
                                  "\"separators_width\":%d}" ),
                             pointDetailsJson( aTable.GetPosition() ),
                             pointDetailsJson( aTable.GetEnd() ),
                             quotedJson( boardLayerName( aTable, aTable.GetLayer() ) ),
                             aTable.GetColCount(),
                             aTable.GetColCount() > 0 ? aTable.GetRowCount() : 0,
                             cells.size(), aTable.GetBorderWidth(),
                             aTable.GetSeparatorsWidth() );
}


AI_OBJECT_REF makeTableRef( const PCB_TABLE& aTable )
{
    return AI_OBJECT_REF( aTable.m_Uuid, aTable.Type(),
                          wxS( "table:" ) + formatPoint( aTable.GetPosition() ),
                          makeTableDetailsJson( aTable ) );
}


wxString makeTableCellDetailsJson( const PCB_TABLE& aTable, const PCB_TABLECELL& aCell )
{
    wxString details = makeTextCommonDetailsJson( aCell, aCell, wxS( "table_cell" ) );

    details += wxString::Format( wxS( ",\"address\":%s,\"row\":%d,\"column\":%d,"
                                      "\"parent_table_uuid\":%s,\"row_span\":%d,"
                                      "\"col_span\":%d,\"start\":%s,\"end\":%s}" ),
                                 quotedJson( aCell.GetAddr() ), aCell.GetRow(),
                                 aCell.GetColumn(), quotedJson( aTable.m_Uuid.AsString() ),
                                 aCell.GetRowSpan(), aCell.GetColSpan(),
                                 pointDetailsJson( aCell.GetStart() ),
                                 pointDetailsJson( aCell.GetEnd() ) );

    return details;
}


AI_OBJECT_REF makeTableCellRef( const PCB_TABLE& aTable, const PCB_TABLECELL& aCell )
{
    wxString label = aCell.GetAddr();

    if( label.IsEmpty() )
        label = aCell.m_Uuid.AsString();

    return AI_OBJECT_REF( aCell.m_Uuid, aCell.Type(), wxS( "table-cell:" ) + label,
                          makeTableCellDetailsJson( aTable, aCell ) );
}


wxString makeDimensionDetailsJson( const PCB_DIMENSION_BASE& aDimension )
{
    wxString details = makeTextCommonDetailsJson( aDimension, aDimension, wxS( "dimension" ) );

    details += wxString::Format( wxS( ",\"dimension_type\":%s,\"start\":%s,\"end\":%s,"
                                      "\"measured_value\":%d,\"value_text\":%s,"
                                      "\"line_thickness\":%d}" ),
                                 quotedJson( dimensionTypeToken( aDimension.Type() ) ),
                                 pointDetailsJson( aDimension.GetStart() ),
                                 pointDetailsJson( aDimension.GetEnd() ),
                                 aDimension.GetMeasuredValue(),
                                 quotedJson( aDimension.GetValueText() ),
                                 aDimension.GetLineThickness() );

    return details;
}


AI_OBJECT_REF makeDimensionRef( const PCB_DIMENSION_BASE& aDimension )
{
    return AI_OBJECT_REF( aDimension.m_Uuid, aDimension.Type(),
                          wxS( "dimension:" ) + formatPoint( aDimension.GetStart() )
                                  + wxS( "->" ) + formatPoint( aDimension.GetEnd() ),
                          makeDimensionDetailsJson( aDimension ) );
}


bool zoneHasKeepout( const ZONE& aZone )
{
    return aZone.GetIsRuleArea() && aZone.HasKeepoutParametersSet();
}


wxString zoneKindToken( const ZONE& aZone )
{
    if( !aZone.GetIsRuleArea() )
        return wxS( "copper" );

    return zoneHasKeepout( aZone ) ? wxS( "keepout" ) : wxS( "rule_area" );
}


wxString makeZoneDetailsJson( const ZONE& aZone )
{
    const bool isRuleArea = aZone.GetIsRuleArea();
    const bool hasKeepout = zoneHasKeepout( aZone );

    return wxString::Format( wxS( "{\"kind\":\"zone\",\"zone_kind\":%s,"
                                  "\"name\":%s,\"layers\":%s,\"first_layer\":%s,"
                                  "\"corner_count\":%d,\"position\":%s,\"bbox\":%s,"
                                  "\"net_code\":%d,\"net_name\":%s,\"priority\":%u,"
                                  "\"is_rule_area\":%s,\"has_keepout\":%s,"
                                  "\"keepout\":{\"tracks\":%s,\"vias\":%s,"
                                  "\"pads\":%s,\"footprints\":%s,\"zone_fills\":%s}}" ),
                             quotedJson( zoneKindToken( aZone ) ),
                             quotedJson( aZone.GetZoneName() ),
                             layerSetDetailsJson( aZone, aZone.GetLayerSet() ),
                             quotedJson( boardLayerName( aZone, aZone.GetFirstLayer() ) ),
                             aZone.GetNumCorners(), pointDetailsJson( aZone.GetPosition() ),
                             boxRectDetailsJson( aZone.GetBoundingBox() ),
                             aZone.GetNetCode(), quotedJson( aZone.GetNetname() ),
                             aZone.GetAssignedPriority(), boolJson( isRuleArea ),
                             boolJson( hasKeepout ),
                             boolJson( hasKeepout && aZone.GetDoNotAllowTracks() ),
                             boolJson( hasKeepout && aZone.GetDoNotAllowVias() ),
                             boolJson( hasKeepout && aZone.GetDoNotAllowPads() ),
                             boolJson( hasKeepout && aZone.GetDoNotAllowFootprints() ),
                             boolJson( hasKeepout && aZone.GetDoNotAllowZoneFills() ) );
}


AI_OBJECT_REF makeZoneRef( const ZONE& aZone )
{
    wxString label;

    if( aZone.GetIsRuleArea() )
        label = zoneHasKeepout( aZone ) ? wxS( "keepout:" ) : wxS( "rule-area:" );
    else
        label = wxS( "zone:" );

    wxString subject = aZone.GetZoneName();

    if( subject.IsEmpty() && !aZone.GetIsRuleArea() )
        subject = aZone.GetNetname();

    if( subject.IsEmpty() )
        subject = aZone.m_Uuid.AsString();

    label += subject;

    return AI_OBJECT_REF( aZone.m_Uuid, aZone.Type(), label, makeZoneDetailsJson( aZone ) );
}

wxString boxDetailsJson( const BOX2I& aBox )
{
    const bool defined = aBox.GetWidth() != 0 || aBox.GetHeight() != 0;

    return wxString::Format( wxS( "{\"defined\":%s,\"origin\":%s,\"end\":%s,"
                                  "\"width\":%d,\"height\":%d}" ),
                             boolJson( defined ), pointDetailsJson( aBox.GetOrigin() ),
                             pointDetailsJson( aBox.GetEnd() ),
                             static_cast<int>( aBox.GetWidth() ),
                             static_cast<int>( aBox.GetHeight() ) );
}


wxString makeClearanceSourcesJson( const BOARD& aBoard )
{
    const BOARD_DESIGN_SETTINGS& settings = aBoard.GetDesignSettings();
    wxString                     defaultNetclassJson = wxS( "null" );

    if( settings.m_NetSettings && settings.m_NetSettings->GetDefaultNetclass() )
    {
        std::shared_ptr<NETCLASS> defaultNetclass = settings.m_NetSettings->GetDefaultNetclass();

        defaultNetclassJson = wxString::Format(
                wxS( "{\"name\":%s,\"clearance\":%d,\"track_width\":%d,"
                     "\"via_diameter\":%d,\"via_drill\":%d}" ),
                quotedJson( defaultNetclass->GetName() ),
                static_cast<int>( defaultNetclass->GetClearance() ),
                static_cast<int>( defaultNetclass->GetTrackWidth() ),
                static_cast<int>( defaultNetclass->GetViaDiameter() ),
                static_cast<int>( defaultNetclass->GetViaDrill() ) );
    }

    return wxString::Format(
            wxS( "{\"source\":\"board_design_settings\",\"minimum_clearance\":%d,"
                 "\"copper_edge_clearance\":%d,\"hole_clearance\":%d,"
                 "\"default_netclass\":%s}" ),
            static_cast<int>( settings.m_MinClearance ),
            static_cast<int>( settings.m_CopperEdgeClearance ),
            static_cast<int>( settings.m_HoleClearance ), defaultNetclassJson );
}


wxString makeNetclassJson( const NETCLASS* aNetclass )
{
    if( !aNetclass )
        return wxS( "null" );

    return wxString::Format(
            wxS( "{\"name\":%s,\"clearance\":%d,\"track_width\":%d,"
                 "\"via_diameter\":%d,\"via_drill\":%d}" ),
            quotedJson( aNetclass->GetName() ), static_cast<int>( aNetclass->GetClearance() ),
            static_cast<int>( aNetclass->GetTrackWidth() ),
            static_cast<int>( aNetclass->GetViaDiameter() ),
            static_cast<int>( aNetclass->GetViaDrill() ) );
}


wxString connectedItemKindToken( const BOARD_CONNECTED_ITEM& aItem )
{
    switch( aItem.Type() )
    {
    case PCB_PAD_T:    return wxS( "pad" );
    case PCB_TRACE_T:  return wxS( "track" );
    case PCB_ARC_T:    return wxS( "arc" );
    case PCB_VIA_T:    return wxS( "via" );
    case PCB_ZONE_T:   return wxS( "zone" );
    case PCB_SHAPE_T:  return wxS( "shape" );
    default:           return wxS( "connected_item" );
    }
}


wxString connectedItemLabel( const BOARD_CONNECTED_ITEM& aItem )
{
    if( const PAD* pad = dynamic_cast<const PAD*>( &aItem ) )
    {
        if( FOOTPRINT* footprint = pad->GetParentFootprint() )
            return padContextLabel( *footprint, *pad );
    }

    if( const PCB_TRACK* track = dynamic_cast<const PCB_TRACK*>( &aItem ) )
        return makeRoutingRef( *track ).m_Label;

    if( const ZONE* zone = dynamic_cast<const ZONE*>( &aItem ) )
        return makeZoneRef( *zone ).m_Label;

    return connectedItemKindToken( aItem ) + wxS( ":" ) + aItem.m_Uuid.AsString();
}


wxString connectedItemGeometryJson( const BOARD_CONNECTED_ITEM& aItem )
{
    if( const PAD* pad = dynamic_cast<const PAD*>( &aItem ) )
    {
        wxString parentDetails;

        if( FOOTPRINT* footprint = pad->GetParentFootprint() )
        {
            parentDetails = wxString::Format(
                    wxS( ",\"footprint_reference\":%s,\"pad_number\":%s" ),
                    quotedJson( footprint->GetReference() ), quotedJson( pad->GetNumber() ) );
        }

        return wxString::Format(
                wxS( "\"position\":%s,\"bbox\":%s,\"shape\":%s,\"size\":%s%s" ),
                pointDetailsJson( pad->GetPosition() ), boxRectDetailsJson( pad->GetBoundingBox() ),
                quotedJson( padShapeToken( pad->GetShape( PADSTACK::ALL_LAYERS ) ) ),
                pointDetailsJson( pad->GetSize( PADSTACK::ALL_LAYERS ) ), parentDetails );
    }

    if( const PCB_TRACK* track = dynamic_cast<const PCB_TRACK*>( &aItem ) )
    {
        if( track->Type() == PCB_VIA_T )
        {
            const PCB_VIA& via = static_cast<const PCB_VIA&>( *track );

            return wxString::Format(
                    wxS( "\"position\":%s,\"bbox\":%s,\"diameter\":%d,\"drill\":%d" ),
                    pointDetailsJson( via.GetPosition() ),
                    boxRectDetailsJson( via.GetBoundingBox() ),
                    via.GetWidth( PADSTACK::ALL_LAYERS ), via.GetDrillValue() );
        }

        return wxString::Format(
                wxS( "\"position\":%s,\"bbox\":%s,\"start\":%s,\"end\":%s,\"width\":%d" ),
                pointDetailsJson( track->GetPosition() ),
                boxRectDetailsJson( track->GetBoundingBox() ),
                pointDetailsJson( track->GetStart() ), pointDetailsJson( track->GetEnd() ),
                track->GetWidth() );
    }

    return wxString::Format( wxS( "\"position\":%s,\"bbox\":%s" ),
                             pointDetailsJson( aItem.GetPosition() ),
                             boxRectDetailsJson( aItem.GetBoundingBox() ) );
}


wxString connectedItemGraphJson( const BOARD_CONNECTED_ITEM& aItem )
{
    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":%s,\"label\":%s,"
                 "\"net_code\":%d,\"net_name\":%s,\"layer\":%d,\"layer_name\":%s,"
                 "\"layers\":%s,%s}" ),
            quotedJson( aItem.m_Uuid.AsString() ), static_cast<int>( aItem.Type() ),
            quotedJson( connectedItemKindToken( aItem ) ),
            quotedJson( connectedItemLabel( aItem ) ), aItem.GetNetCode(),
            quotedJson( aItem.GetNetname() ), static_cast<int>( aItem.GetLayer() ),
            quotedJson( boardLayerName( aItem, aItem.GetLayer() ) ),
            layerSetDetailsJson( aItem, aItem.GetLayerSet() ),
            connectedItemGeometryJson( aItem ) );
}


void appendNetItemGraphEntry( std::vector<wxString>& aEntries, bool& aTruncated,
                              const BOARD_CONNECTED_ITEM* aItem, int aNetCode )
{
    constexpr size_t maxNetItemSample = 64;

    if( !aItem || aItem->GetNetCode() != aNetCode )
        return;

    if( aEntries.size() >= maxNetItemSample )
    {
        aTruncated = true;
        return;
    }

    aEntries.push_back( connectedItemGraphJson( *aItem ) );
}


std::vector<wxString> makeNetItemGraphEntries( const BOARD& aBoard, int aNetCode,
                                               bool& aTruncated )
{
    std::vector<wxString> entries;

    for( PCB_TRACK* track : aBoard.Tracks() )
        appendNetItemGraphEntry( entries, aTruncated, track, aNetCode );

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
            appendNetItemGraphEntry( entries, aTruncated, pad, aNetCode );

        for( ZONE* zone : footprint->Zones() )
            appendNetItemGraphEntry( entries, aTruncated, zone, aNetCode );

        for( BOARD_ITEM* item : footprint->GraphicalItems() )
        {
            appendNetItemGraphEntry( entries, aTruncated,
                                     dynamic_cast<BOARD_CONNECTED_ITEM*>( item ), aNetCode );
        }
    }

    for( ZONE* zone : aBoard.Zones() )
        appendNetItemGraphEntry( entries, aTruncated, zone, aNetCode );

    for( BOARD_ITEM* item : aBoard.Drawings() )
    {
        appendNetItemGraphEntry( entries, aTruncated,
                                 dynamic_cast<BOARD_CONNECTED_ITEM*>( item ), aNetCode );
    }

    return entries;
}


struct NET_COMPONENT_FACTS
{
    std::vector<wxString>                      m_ComponentEntries;
    std::map<const BOARD_CONNECTED_ITEM*, int> m_ItemToComponent;
    std::map<int, std::vector<BOARD_CONNECTED_ITEM*>> m_ComponentItems;
    size_t                                    m_ComponentCount = 0;
    bool                                      m_ComponentSampleTruncated = false;
};


wxString componentItemJson( const BOARD_CONNECTED_ITEM& aItem )
{
    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":%s,\"label\":%s}" ),
            quotedJson( aItem.m_Uuid.AsString() ), static_cast<int>( aItem.Type() ),
            quotedJson( connectedItemKindToken( aItem ) ),
            quotedJson( connectedItemLabel( aItem ) ) );
}


wxString componentJson( int aIndex, const std::vector<BOARD_CONNECTED_ITEM*>& aItems )
{
    std::vector<wxString> itemEntries;
    BOX2I                 bbox;
    bool                  hasBBox = false;

    for( const BOARD_CONNECTED_ITEM* item : aItems )
    {
        if( !item )
            continue;

        itemEntries.push_back( componentItemJson( *item ) );

        if( hasBBox )
            bbox.Merge( item->GetBoundingBox() );
        else
        {
            bbox = item->GetBoundingBox();
            hasBBox = true;
        }
    }

    return wxString::Format(
            wxS( "{\"index\":%d,\"item_count\":%zu,\"bbox\":%s,\"items\":%s}" ),
            aIndex, aItems.size(),
            hasBBox ? boxRectDetailsJson( bbox ) : wxString( wxS( "null" ) ),
            jsonArray( itemEntries ) );
}


NET_COMPONENT_FACTS makeNetComponentFacts(
        const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity, int aNetCode )
{
    constexpr size_t maxComponentSample = 64;

    NET_COMPONENT_FACTS facts;

    if( !aConnectivity )
        return facts;

    const std::vector<KICAD_T> graphTypes = {
        PCB_PAD_T, PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T, PCB_ZONE_T, PCB_SHAPE_T
    };

    std::vector<BOARD_CONNECTED_ITEM*> netItems =
            aConnectivity->GetNetItems( aNetCode, graphTypes );
    std::set<const BOARD_CONNECTED_ITEM*> assignedItems;

    for( BOARD_CONNECTED_ITEM* item : netItems )
    {
        if( !item || assignedItems.find( item ) != assignedItems.end() )
            continue;

        std::vector<BOARD_CONNECTED_ITEM*> clusterItems =
                aConnectivity->GetConnectedItems( item );
        std::set<BOARD_CONNECTED_ITEM*> uniqueClusterItems;

        for( BOARD_CONNECTED_ITEM* clusterItem : clusterItems )
        {
            if( clusterItem && clusterItem->GetNetCode() == aNetCode )
                uniqueClusterItems.insert( clusterItem );
        }

        if( uniqueClusterItems.empty() )
            uniqueClusterItems.insert( item );

        std::vector<BOARD_CONNECTED_ITEM*> componentItems;

        for( BOARD_CONNECTED_ITEM* clusterItem : uniqueClusterItems )
        {
            componentItems.push_back( clusterItem );
            assignedItems.insert( clusterItem );
        }

        const int componentIndex = static_cast<int>( facts.m_ComponentCount );
        ++facts.m_ComponentCount;

        for( BOARD_CONNECTED_ITEM* componentItem : componentItems )
            facts.m_ItemToComponent[componentItem] = componentIndex;

        facts.m_ComponentItems[componentIndex] = componentItems;

        if( facts.m_ComponentEntries.size() >= maxComponentSample )
        {
            facts.m_ComponentSampleTruncated = true;
            continue;
        }

        facts.m_ComponentEntries.push_back( componentJson( componentIndex, componentItems ) );
    }

    return facts;
}


int edgeNetCode( const CN_EDGE& aEdge )
{
    const std::shared_ptr<const CN_ANCHOR> sourceNode = aEdge.GetSourceNode();
    const std::shared_ptr<const CN_ANCHOR> targetNode = aEdge.GetTargetNode();

    if( sourceNode && sourceNode->Parent() )
        return sourceNode->Parent()->GetNetCode();

    if( targetNode && targetNode->Parent() )
        return targetNode->Parent()->GetNetCode();

    return NETINFO_LIST::UNCONNECTED;
}


wxString connectivityEndpointJson( const std::shared_ptr<const CN_ANCHOR>& aAnchor )
{
    if( !aAnchor )
        return wxS( "{\"position\":null,\"item_uuid\":null,\"item_type\":null}" );

    BOARD_CONNECTED_ITEM* item = aAnchor->Parent();

    if( !item )
    {
        return wxString::Format(
                wxS( "{\"position\":%s,\"item_uuid\":null,\"item_type\":null}" ),
                pointDetailsJson( aAnchor->Pos() ) );
    }

    return wxString::Format( wxS( "{\"position\":%s,\"item_uuid\":%s,\"item_type\":%d}" ),
                             pointDetailsJson( aAnchor->Pos() ),
                             quotedJson( item->m_Uuid.AsString() ),
                             static_cast<int>( item->Type() ) );
}


int anchorManhattanLength( const std::shared_ptr<const CN_ANCHOR>& aSource,
                           const std::shared_ptr<const CN_ANCHOR>& aTarget )
{
    if( !aSource || !aTarget )
        return 0;

    const VECTOR2I source = aSource->Pos();
    const VECTOR2I target = aTarget->Pos();

    return std::abs( source.x - target.x ) + std::abs( source.y - target.y );
}


std::vector<wxString> makeNetUnconnectedEdgeEntries(
        const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity, int aNetCode,
        bool& aTruncated )
{
    constexpr size_t      maxNetUnconnectedEdgeSample = 32;
    std::vector<wxString> edgeEntries;

    if( !aConnectivity )
        return edgeEntries;

    aConnectivity->RunOnUnconnectedEdges(
            [&]( CN_EDGE& aEdge ) -> bool
            {
                if( edgeNetCode( aEdge ) != aNetCode )
                    return true;

                if( edgeEntries.size() >= maxNetUnconnectedEdgeSample )
                {
                    aTruncated = true;
                    return false;
                }

                wxString netName;

                if( aConnectivity->HasNetNameForNetCode( aNetCode ) )
                    netName = aConnectivity->GetNetNameForNetCode( aNetCode );

                const std::shared_ptr<const CN_ANCHOR> sourceNode = aEdge.GetSourceNode();
                const std::shared_ptr<const CN_ANCHOR> targetNode = aEdge.GetTargetNode();

                edgeEntries.push_back( wxString::Format(
                        wxS( "{\"net_code\":%d,\"net_name\":%s,\"visible\":%s,"
                             "\"estimated_manhattan_length\":%d,"
                             "\"source\":%s,\"target\":%s}" ),
                        aNetCode, quotedJson( netName ), boolJson( aEdge.IsVisible() ),
                        anchorManhattanLength( sourceNode, targetNode ),
                        connectivityEndpointJson( sourceNode ),
                        connectivityEndpointJson( targetNode ) ) );

                return true;
            } );

    return edgeEntries;
}


int componentIndexForAnchor(
        const std::shared_ptr<const CN_ANCHOR>& aAnchor,
        const std::map<const BOARD_CONNECTED_ITEM*, int>& aItemToComponent )
{
    if( !aAnchor || !aAnchor->Parent() )
        return -1;

    const auto it = aItemToComponent.find( aAnchor->Parent() );

    if( it == aItemToComponent.end() )
        return -1;

    return it->second;
}


std::vector<wxString> makeRatsnestComponentEdgeEntries(
        const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity, int aNetCode,
        const std::map<const BOARD_CONNECTED_ITEM*, int>& aItemToComponent,
        bool& aTruncated )
{
    constexpr size_t      maxComponentEdgeSample = 64;
    std::vector<wxString> edgeEntries;

    if( !aConnectivity )
        return edgeEntries;

    aConnectivity->RunOnUnconnectedEdges(
            [&]( CN_EDGE& aEdge ) -> bool
            {
                if( edgeNetCode( aEdge ) != aNetCode )
                    return true;

                if( edgeEntries.size() >= maxComponentEdgeSample )
                {
                    aTruncated = true;
                    return false;
                }

                const std::shared_ptr<const CN_ANCHOR> sourceNode = aEdge.GetSourceNode();
                const std::shared_ptr<const CN_ANCHOR> targetNode = aEdge.GetTargetNode();

                edgeEntries.push_back( wxString::Format(
                        wxS( "{\"net_code\":%d,\"visible\":%s,"
                             "\"estimated_manhattan_length\":%d,"
                             "\"source_component\":%d,\"target_component\":%d,"
                             "\"source\":%s,\"target\":%s}" ),
                        aNetCode, boolJson( aEdge.IsVisible() ),
                        anchorManhattanLength( sourceNode, targetNode ),
                        componentIndexForAnchor( sourceNode, aItemToComponent ),
                        componentIndexForAnchor( targetNode, aItemToComponent ),
                        connectivityEndpointJson( sourceNode ), connectivityEndpointJson( targetNode ) ) );

                return true;
            } );

    return edgeEntries;
}


wxString makeNetTopologyJson( const BOARD& aBoard,
                              const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity,
                              int aNetCode )
{
    if( !aConnectivity )
        return wxS( "null" );

    int unconnectedEdgeCount = 0;
    int visibleUnconnectedEdgeCount = 0;
    bool itemSampleTruncated = false;
    bool edgeSampleTruncated = false;
    bool componentEdgeSampleTruncated = false;

    std::vector<wxString> itemEntries =
            makeNetItemGraphEntries( aBoard, aNetCode, itemSampleTruncated );
    std::vector<wxString> edgeEntries =
            makeNetUnconnectedEdgeEntries( aConnectivity, aNetCode, edgeSampleTruncated );
    NET_COMPONENT_FACTS componentFacts = makeNetComponentFacts( aConnectivity, aNetCode );
    std::vector<wxString> componentEdgeEntries =
            makeRatsnestComponentEdgeEntries( aConnectivity, aNetCode,
                                              componentFacts.m_ItemToComponent,
                                              componentEdgeSampleTruncated );

    aConnectivity->RunOnUnconnectedEdges(
            [&]( CN_EDGE& aEdge ) -> bool
            {
                if( edgeNetCode( aEdge ) == aNetCode )
                {
                    ++unconnectedEdgeCount;

                    if( aEdge.IsVisible() )
                        ++visibleUnconnectedEdgeCount;
                }

                return true;
            } );

    return wxString::Format(
            wxS( "{\"node_count\":%u,\"pad_count\":%u,"
                 "\"unconnected_edge_count\":%d,"
                 "\"visible_unconnected_edge_count\":%d,"
                 "\"items\":%s,\"item_sample_truncated\":%s,"
                 "\"unconnected_edges\":%s,"
                 "\"unconnected_edge_sample_truncated\":%s,"
                 "\"component_count\":%zu,"
                 "\"components\":%s,\"component_sample_truncated\":%s,"
                 "\"ratsnest_component_edges\":%s,"
                 "\"ratsnest_component_edge_sample_truncated\":%s}" ),
            aConnectivity->GetNodeCount( aNetCode ), aConnectivity->GetPadCount( aNetCode ),
            unconnectedEdgeCount, visibleUnconnectedEdgeCount,
            jsonArray( itemEntries ), boolJson( itemSampleTruncated ),
            jsonArray( edgeEntries ), boolJson( edgeSampleTruncated ),
            componentFacts.m_ComponentCount, jsonArray( componentFacts.m_ComponentEntries ),
            boolJson( componentFacts.m_ComponentSampleTruncated ),
            jsonArray( componentEdgeEntries ), boolJson( componentEdgeSampleTruncated ) );
}


wxString makeNetFactsJson( const BOARD& aBoard )
{
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = aBoard.GetConnectivity();
    std::vector<wxString>              netEntries;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( net->GetNetCode() == NETINFO_LIST::UNCONNECTED )
            continue;

        netEntries.push_back( wxString::Format(
                wxS( "{\"code\":%d,\"name\":%s,\"netclass\":%s,\"topology\":%s}" ),
                net->GetNetCode(), quotedJson( net->GetNetname() ),
                makeNetclassJson( net->GetNetClass() ),
                makeNetTopologyJson( aBoard, connectivity, net->GetNetCode() ) ) );
    }

    return jsonArray( netEntries );
}


struct NET_COMPONENT_EDGE_COUNTS
{
    int m_Total = 0;
    int m_Visible = 0;
};


NET_COMPONENT_EDGE_COUNTS countRatsnestComponentEdges(
        const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity, int aNetCode,
        const std::map<const BOARD_CONNECTED_ITEM*, int>& aItemToComponent )
{
    NET_COMPONENT_EDGE_COUNTS counts;

    if( !aConnectivity )
        return counts;

    aConnectivity->RunOnUnconnectedEdges(
            [&]( CN_EDGE& aEdge ) -> bool
            {
                if( edgeNetCode( aEdge ) != aNetCode )
                    return true;

                const int sourceComponent =
                        componentIndexForAnchor( aEdge.GetSourceNode(), aItemToComponent );
                const int targetComponent =
                        componentIndexForAnchor( aEdge.GetTargetNode(), aItemToComponent );

                if( sourceComponent < 0 || targetComponent < 0
                    || sourceComponent == targetComponent )
                {
                    return true;
                }

                ++counts.m_Total;

                if( aEdge.IsVisible() )
                    ++counts.m_Visible;

                return true;
            } );

    return counts;
}


wxString netComponentSummaryJson( const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity,
                                  const NETINFO_ITEM& aNet )
{
    NET_COMPONENT_FACTS componentFacts =
            makeNetComponentFacts( aConnectivity, aNet.GetNetCode() );
    NET_COMPONENT_EDGE_COUNTS edgeCounts =
            countRatsnestComponentEdges( aConnectivity, aNet.GetNetCode(),
                                         componentFacts.m_ItemToComponent );

    return wxString::Format(
            wxS( "{\"net_code\":%d,\"net_name\":%s,"
                 "\"component_count\":%zu,\"sample_component_count\":%zu,"
                 "\"components\":%s,\"component_sample_truncated\":%s,"
                 "\"ratsnest_component_edge_count\":%d,"
                 "\"visible_ratsnest_component_edge_count\":%d}" ),
            aNet.GetNetCode(), quotedJson( aNet.GetNetname() ),
            componentFacts.m_ComponentCount, componentFacts.m_ComponentEntries.size(),
            jsonArray( componentFacts.m_ComponentEntries ),
            boolJson( componentFacts.m_ComponentSampleTruncated ),
            edgeCounts.m_Total, edgeCounts.m_Visible );
}


std::vector<wxString> makeNetComponentSummaryEntries(
        const BOARD& aBoard, const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity,
        bool& aTruncated )
{
    constexpr size_t      maxNetComponentSummarySample = 32;
    std::vector<wxString> entries;

    if( !aConnectivity )
        return entries;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( !net || net->GetNetCode() == NETINFO_LIST::UNCONNECTED )
            continue;

        if( entries.size() >= maxNetComponentSummarySample )
        {
            aTruncated = true;
            break;
        }

        entries.push_back( netComponentSummaryJson( aConnectivity, *net ) );
    }

    return entries;
}


wxString componentGraphNodeId( int aNetCode, int aComponentIndex )
{
    return wxString::Format( wxS( "net:%d:component:%d" ), aNetCode, aComponentIndex );
}


wxString componentGraphNodeJson( const NETINFO_ITEM& aNet, int aComponentIndex,
                                 const std::vector<BOARD_CONNECTED_ITEM*>& aItems )
{
    std::vector<wxString> itemEntries;
    BOX2I                 bbox;
    bool                  hasBBox = false;

    for( const BOARD_CONNECTED_ITEM* item : aItems )
    {
        if( !item )
            continue;

        itemEntries.push_back( componentItemJson( *item ) );

        if( hasBBox )
            bbox.Merge( item->GetBoundingBox() );
        else
        {
            bbox = item->GetBoundingBox();
            hasBBox = true;
        }
    }

    return wxString::Format(
            wxS( "{\"id\":%s,\"net_code\":%d,\"net_name\":%s,"
                 "\"component_index\":%d,\"item_count\":%zu,"
                 "\"bbox\":%s,\"items\":%s}" ),
            quotedJson( componentGraphNodeId( aNet.GetNetCode(), aComponentIndex ) ),
            aNet.GetNetCode(), quotedJson( aNet.GetNetname() ), aComponentIndex,
            aItems.size(), hasBBox ? boxRectDetailsJson( bbox ) : wxString( wxS( "null" ) ),
            jsonArray( itemEntries ) );
}


std::vector<wxString> makeBoardComponentGraphNodeEntries(
        const BOARD& aBoard, const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity,
        bool& aTruncated )
{
    constexpr size_t      maxGraphNodeSample = 64;
    std::vector<wxString> entries;

    if( !aConnectivity )
        return entries;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( !net || net->GetNetCode() == NETINFO_LIST::UNCONNECTED )
            continue;

        NET_COMPONENT_FACTS componentFacts =
                makeNetComponentFacts( aConnectivity, net->GetNetCode() );

        for( const auto& [componentIndex, componentItems] : componentFacts.m_ComponentItems )
        {
            if( entries.size() >= maxGraphNodeSample )
            {
                aTruncated = true;
                return entries;
            }

            entries.push_back( componentGraphNodeJson( *net, componentIndex,
                                                       componentItems ) );
        }
    }

    return entries;
}


std::vector<wxString> makeBoardComponentGraphEdgeEntries(
        const BOARD& aBoard, const std::shared_ptr<CONNECTIVITY_DATA>& aConnectivity,
        bool& aTruncated )
{
    constexpr size_t      maxGraphEdgeSample = 64;
    std::vector<wxString> entries;

    if( !aConnectivity )
        return entries;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( !net || net->GetNetCode() == NETINFO_LIST::UNCONNECTED )
            continue;

        NET_COMPONENT_FACTS componentFacts =
                makeNetComponentFacts( aConnectivity, net->GetNetCode() );
        int edgeIndex = 0;

        aConnectivity->RunOnUnconnectedEdges(
                [&]( CN_EDGE& aEdge ) -> bool
                {
                    if( edgeNetCode( aEdge ) != net->GetNetCode() )
                        return true;

                    const std::shared_ptr<const CN_ANCHOR> sourceNode =
                            aEdge.GetSourceNode();
                    const std::shared_ptr<const CN_ANCHOR> targetNode =
                            aEdge.GetTargetNode();
                    const int sourceComponent =
                            componentIndexForAnchor( sourceNode,
                                                     componentFacts.m_ItemToComponent );
                    const int targetComponent =
                            componentIndexForAnchor( targetNode,
                                                     componentFacts.m_ItemToComponent );

                    if( sourceComponent < 0 || targetComponent < 0
                        || sourceComponent == targetComponent )
                    {
                        return true;
                    }

                    if( entries.size() >= maxGraphEdgeSample )
                    {
                        aTruncated = true;
                        return false;
                    }

                    entries.push_back( wxString::Format(
                            wxS( "{\"id\":%s,\"net_code\":%d,\"net_name\":%s,"
                                 "\"kind\":\"ratsnest\",\"from\":%s,\"to\":%s,"
                                 "\"visible\":%s,\"estimated_manhattan_length\":%d,"
                                 "\"source_component\":%d,\"target_component\":%d,"
                                 "\"source\":%s,\"target\":%s}" ),
                            quotedJson( wxString::Format( wxS( "net:%d:ratsnest:%d" ),
                                                          net->GetNetCode(),
                                                          edgeIndex++ ) ),
                            net->GetNetCode(), quotedJson( net->GetNetname() ),
                            quotedJson( componentGraphNodeId( net->GetNetCode(),
                                                              sourceComponent ) ),
                            quotedJson( componentGraphNodeId( net->GetNetCode(),
                                                              targetComponent ) ),
                            boolJson( aEdge.IsVisible() ),
                            anchorManhattanLength( sourceNode, targetNode ),
                            sourceComponent, targetComponent,
                            connectivityEndpointJson( sourceNode ),
                            connectivityEndpointJson( targetNode ) ) );

                    return true;
                } );

        if( aTruncated )
            return entries;
    }

    return entries;
}


wxString makeLayerContextJson( const BOARD& aBoard )
{
    const LSET&           enabledLayers = aBoard.GetEnabledLayers();
    std::vector<wxString> layerEntries;
    int                   visibleLayerCount = 0;

    auto layerFactJson =
            [&]( PCB_LAYER_ID aLayer ) -> wxString
            {
                return wxString::Format(
                        wxS( "{\"id\":%d,\"name\":%s,\"enabled\":%s,\"visible\":%s,"
                             "\"copper\":%s}" ),
                        static_cast<int>( aLayer ), quotedJson( aBoard.GetLayerName( aLayer ) ),
                        boolJson( aBoard.IsLayerEnabled( aLayer ) ),
                        boolJson( aBoard.IsLayerVisible( aLayer ) ),
                        boolJson( IsCopperLayer( aLayer ) ) );
            };

    for( PCB_LAYER_ID layer : enabledLayers.Seq() )
    {
        const bool visible = aBoard.IsLayerVisible( layer );

        if( visible )
            ++visibleLayerCount;

        layerEntries.push_back( layerFactJson( layer ) );
    }

    const wxString visibleLayersSource = aBoard.GetProject()
                                                ? wxS( "project_local_settings" )
                                                : wxS( "default_all_layers_no_project" );
    wxString activeLayerJson = wxS( "null" );

    if( aBoard.GetProject() )
    {
        PCB_LAYER_ID activeLayer = aBoard.GetProject()->GetLocalSettings().m_ActiveLayer;

        if( activeLayer >= F_Cu && activeLayer < PCB_LAYER_ID_COUNT )
            activeLayerJson = layerFactJson( activeLayer );
    }

    return wxString::Format(
            wxS( "{\"source\":\"board\",\"visible_layers_source\":%s,"
                 "\"copper_layer_count\":%d,\"enabled_layer_count\":%d,"
                 "\"visible_layer_count\":%d,\"active_layer\":%s,\"layers\":%s}" ),
            quotedJson( visibleLayersSource ), aBoard.GetCopperLayerCount(),
            static_cast<int>( enabledLayers.count() ), visibleLayerCount,
            activeLayerJson, jsonArray( layerEntries ) );
}


wxString makeConnectivitySummaryJson( const BOARD& aBoard )
{
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = aBoard.GetConnectivity();

    if( !connectivity )
    {
        return wxS( "{\"source\":\"board_connectivity\",\"present\":false,"
                    "\"net_count\":0,\"node_count\":0,\"pad_count\":0,"
                    "\"ratsnest_unconnected_count\":0,"
                    "\"visible_ratsnest_unconnected_count\":0,"
                    "\"local_ratsnest_line_count\":0,"
                    "\"net_component_summaries\":[],"
                    "\"net_component_summary_sample_truncated\":false,"
                    "\"component_graph_nodes\":[],"
                    "\"component_graph_node_sample_truncated\":false,"
                    "\"component_graph_edges\":[],"
                    "\"component_graph_edge_sample_truncated\":false,"
                    "\"unconnected_edges\":[],"
                    "\"unconnected_edge_sample_truncated\":false}" );
    }

    constexpr size_t       maxUnconnectedEdgeSample = 32;
    std::vector<wxString>  edgeEntries;
    bool                   edgeSampleTruncated = false;
    bool                   componentSummaryTruncated = false;
    bool                   graphNodeSampleTruncated = false;
    bool                   graphEdgeSampleTruncated = false;
    std::vector<wxString>  componentSummaryEntries =
            makeNetComponentSummaryEntries( aBoard, connectivity, componentSummaryTruncated );
    std::vector<wxString>  componentGraphNodeEntries =
            makeBoardComponentGraphNodeEntries( aBoard, connectivity,
                                                graphNodeSampleTruncated );
    std::vector<wxString>  componentGraphEdgeEntries =
            makeBoardComponentGraphEdgeEntries( aBoard, connectivity,
                                                graphEdgeSampleTruncated );

    auto endpointJson =
            []( const std::shared_ptr<const CN_ANCHOR>& aAnchor ) -> wxString
            {
                if( !aAnchor )
                    return wxS( "{\"position\":null,\"item_uuid\":null,\"item_type\":null}" );

                BOARD_CONNECTED_ITEM* item = aAnchor->Parent();

                if( !item )
                {
                    return wxString::Format(
                            wxS( "{\"position\":%s,\"item_uuid\":null,\"item_type\":null}" ),
                            pointDetailsJson( aAnchor->Pos() ) );
                }

                return wxString::Format(
                        wxS( "{\"position\":%s,\"item_uuid\":%s,\"item_type\":%d}" ),
                        pointDetailsJson( aAnchor->Pos() ), quotedJson( item->m_Uuid.AsString() ),
                        static_cast<int>( item->Type() ) );
            };

    connectivity->RunOnUnconnectedEdges(
            [&]( CN_EDGE& aEdge ) -> bool
            {
                if( edgeEntries.size() >= maxUnconnectedEdgeSample )
                {
                    edgeSampleTruncated = true;
                    return false;
                }

                const std::shared_ptr<const CN_ANCHOR> sourceNode = aEdge.GetSourceNode();
                const std::shared_ptr<const CN_ANCHOR> targetNode = aEdge.GetTargetNode();
                int                                    netCode = NETINFO_LIST::UNCONNECTED;

                if( sourceNode && sourceNode->Parent() )
                    netCode = sourceNode->Parent()->GetNetCode();
                else if( targetNode && targetNode->Parent() )
                    netCode = targetNode->Parent()->GetNetCode();

                wxString netName;

                if( connectivity->HasNetNameForNetCode( netCode ) )
                    netName = connectivity->GetNetNameForNetCode( netCode );

                edgeEntries.push_back( wxString::Format(
                        wxS( "{\"net_code\":%d,\"net_name\":%s,\"visible\":%s,"
                             "\"estimated_manhattan_length\":%d,"
                             "\"source\":%s,\"target\":%s}" ),
                        netCode, quotedJson( netName ), boolJson( aEdge.IsVisible() ),
                        anchorManhattanLength( sourceNode, targetNode ),
                        endpointJson( sourceNode ), endpointJson( targetNode ) ) );

                return true;
            } );

    return wxString::Format(
            wxS( "{\"source\":\"board_connectivity\",\"present\":true,"
                 "\"net_count\":%d,\"node_count\":%u,\"pad_count\":%u,"
                 "\"ratsnest_unconnected_count\":%u,"
                 "\"visible_ratsnest_unconnected_count\":%u,"
                 "\"local_ratsnest_line_count\":%zu,"
                 "\"net_component_summaries\":%s,"
                 "\"net_component_summary_sample_truncated\":%s,"
                 "\"component_graph_nodes\":%s,"
                 "\"component_graph_node_sample_truncated\":%s,"
                 "\"component_graph_edges\":%s,"
                 "\"component_graph_edge_sample_truncated\":%s,"
                 "\"unconnected_edges\":%s,"
                 "\"unconnected_edge_sample_truncated\":%s}" ),
            connectivity->GetNetCount(), connectivity->GetNodeCount(),
            connectivity->GetPadCount(), connectivity->GetUnconnectedCount( false ),
            connectivity->GetUnconnectedCount( true ), connectivity->GetLocalRatsnest().size(),
            jsonArray( componentSummaryEntries ), boolJson( componentSummaryTruncated ),
            jsonArray( componentGraphNodeEntries ), boolJson( graphNodeSampleTruncated ),
            jsonArray( componentGraphEdgeEntries ), boolJson( graphEdgeSampleTruncated ),
            jsonArray( edgeEntries ), boolJson( edgeSampleTruncated ) );
}


wxString makeConstraintMinimumsJson( const BOARD_DESIGN_SETTINGS& aSettings )
{
    return wxString::Format(
            wxS( "{\"min_clearance\":%d,\"min_groove_width\":%d,"
                 "\"min_connection_width\":%d,\"min_track_width\":%d,"
                 "\"min_via_annular_width\":%d,\"min_via_size\":%d,"
                 "\"min_through_drill\":%d,\"copper_edge_clearance\":%d,"
                 "\"hole_clearance\":%d,\"hole_to_hole_min\":%d,"
                 "\"silk_clearance\":%d}" ),
            static_cast<int>( aSettings.m_MinClearance ),
            static_cast<int>( aSettings.m_MinGrooveWidth ),
            static_cast<int>( aSettings.m_MinConn ),
            static_cast<int>( aSettings.m_TrackMinWidth ),
            static_cast<int>( aSettings.m_ViasMinAnnularWidth ),
            static_cast<int>( aSettings.m_ViasMinSize ),
            static_cast<int>( aSettings.m_MinThroughDrill ),
            static_cast<int>( aSettings.m_CopperEdgeClearance ),
            static_cast<int>( aSettings.m_HoleClearance ),
            static_cast<int>( aSettings.m_HoleToHoleMin ),
            static_cast<int>( aSettings.m_SilkClearance ) );
}


wxString makeKeepoutConstraintJson( const ZONE& aZone )
{
    wxString parentDetails;

    if( FOOTPRINT* footprint = aZone.GetParentFootprint() )
    {
        parentDetails = wxString::Format(
                wxS( ",\"parent_footprint_reference\":%s,"
                     "\"parent_footprint_uuid\":%s" ),
                quotedJson( footprint->GetReference() ),
                quotedJson( footprint->m_Uuid.AsString() ) );
    }

    return wxString::Format(
            wxS( "{\"uuid\":%s,\"name\":%s,\"zone_kind\":%s,"
                 "\"layers\":%s,\"first_layer\":%s,\"position\":%s,"
                 "\"bbox\":%s,\"corner_count\":%d,"
                 "\"blocks\":{\"tracks\":%s,\"vias\":%s,\"pads\":%s,"
                 "\"footprints\":%s,\"zone_fills\":%s}%s}" ),
            quotedJson( aZone.m_Uuid.AsString() ), quotedJson( aZone.GetZoneName() ),
            quotedJson( zoneKindToken( aZone ) ), layerSetDetailsJson( aZone, aZone.GetLayerSet() ),
            quotedJson( boardLayerName( aZone, aZone.GetFirstLayer() ) ),
            pointDetailsJson( aZone.GetPosition() ), boxRectDetailsJson( aZone.GetBoundingBox() ),
            aZone.GetNumCorners(), boolJson( aZone.GetDoNotAllowTracks() ),
            boolJson( aZone.GetDoNotAllowVias() ), boolJson( aZone.GetDoNotAllowPads() ),
            boolJson( aZone.GetDoNotAllowFootprints() ),
            boolJson( aZone.GetDoNotAllowZoneFills() ), parentDetails );
}


wxString drcConstraintTypeToken( DRC_CONSTRAINT_T aType )
{
    switch( aType )
    {
    case CLEARANCE_CONSTRAINT:                return wxS( "clearance" );
    case PHYSICAL_CLEARANCE_CONSTRAINT:       return wxS( "physical_clearance" );
    case HOLE_CLEARANCE_CONSTRAINT:           return wxS( "hole_clearance" );
    case PHYSICAL_HOLE_CLEARANCE_CONSTRAINT:  return wxS( "physical_hole_clearance" );
    case HOLE_TO_HOLE_CONSTRAINT:             return wxS( "hole_to_hole" );
    case EDGE_CLEARANCE_CONSTRAINT:           return wxS( "edge_clearance" );
    case COURTYARD_CLEARANCE_CONSTRAINT:      return wxS( "courtyard_clearance" );
    case SILK_CLEARANCE_CONSTRAINT:           return wxS( "silk_clearance" );
    case TRACK_WIDTH_CONSTRAINT:              return wxS( "track_width" );
    case CONNECTION_WIDTH_CONSTRAINT:         return wxS( "connection_width" );
    case VIA_DIAMETER_CONSTRAINT:             return wxS( "via_diameter" );
    case HOLE_SIZE_CONSTRAINT:                return wxS( "hole_size" );
    case ANNULAR_WIDTH_CONSTRAINT:            return wxS( "annular_width" );
    default:                                  return wxS( "unknown" );
    }
}


wxString minOptMaxJson( const MINOPTMAX<int>& aValue )
{
    return wxString::Format(
            wxS( "{\"has_min\":%s,\"min\":%d,"
                 "\"has_opt\":%s,\"opt\":%d,"
                 "\"has_max\":%s,\"max\":%d}" ),
            boolJson( aValue.HasMin() ), aValue.Min(),
            boolJson( aValue.HasOpt() ), aValue.Opt(),
            boolJson( aValue.HasMax() ), aValue.Max() );
}


wxString worstConstraintJson( DRC_CONSTRAINT_T aType, const DRC_CONSTRAINT& aConstraint )
{
    return wxString::Format(
            wxS( "{\"type\":%s,\"enum\":%d,\"name\":%s,\"value\":%s}" ),
            quotedJson( drcConstraintTypeToken( aType ) ), static_cast<int>( aType ),
            quotedJson( aConstraint.GetName() ), minOptMaxJson( aConstraint.GetValue() ) );
}


wxString pairConstraintItemJson( const BOARD_CONNECTED_ITEM& aItem )
{
    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":%s,\"label\":%s,"
                 "\"net_code\":%d,\"net\":%s,\"layers\":%s,\"bbox\":%s}" ),
            quotedJson( aItem.m_Uuid.AsString() ), static_cast<int>( aItem.Type() ),
            quotedJson( connectedItemKindToken( aItem ) ),
            quotedJson( connectedItemLabel( aItem ) ), aItem.GetNetCode(),
            quotedJson( aItem.GetNetname() ),
            layerSetDetailsJson( aItem, aItem.GetLayerSet() ),
            boxRectDetailsJson( aItem.GetBoundingBox() ) );
}


bool firstCommonCopperLayer( const BOARD& aBoard, const BOARD_CONNECTED_ITEM& aItemA,
                             const BOARD_CONNECTED_ITEM& aItemB, PCB_LAYER_ID& aLayer )
{
    LSET commonLayers = aItemA.GetLayerSet() & aItemB.GetLayerSet()
                        & LSET::AllCuMask( aBoard.GetCopperLayerCount() );

    for( PCB_LAYER_ID layer : commonLayers.Seq() )
    {
        aLayer = layer;
        return true;
    }

    return false;
}


wxString pairEffectiveConstraintJson( const BOARD& aBoard, const BOARD_CONNECTED_ITEM& aItemA,
                                      const BOARD_CONNECTED_ITEM& aItemB, PCB_LAYER_ID aLayer,
                                      DRC_CONSTRAINT_T aType,
                                      const DRC_CONSTRAINT& aConstraint,
                                      bool aGeometryDependentRulesPresent )
{
    return wxString::Format(
            wxS( "{\"type\":%s,\"enum\":%d,\"layer\":%s,"
                 "\"source_item\":%s,\"target_item\":%s,"
                 "\"name\":%s,\"value\":%s,"
                 "\"evaluation_source\":\"DRC_ENGINE::EvalRules\","
                 "\"geometry_dependent_rules_present\":%s}" ),
            quotedJson( drcConstraintTypeToken( aType ) ), static_cast<int>( aType ),
            quotedJson( aBoard.GetLayerName( aLayer ) ), pairConstraintItemJson( aItemA ),
            pairConstraintItemJson( aItemB ), quotedJson( aConstraint.GetName() ),
            minOptMaxJson( aConstraint.GetValue() ),
            boolJson( aGeometryDependentRulesPresent ) );
}


wxString geometryPairToken( const BOARD_CONNECTED_ITEM& aItemA,
                            const BOARD_CONNECTED_ITEM& aItemB )
{
    return connectedItemKindToken( aItemA ) + wxS( "_to_" ) + connectedItemKindToken( aItemB );
}


wxString geometrySpecificRuleCoverageJson( const BOARD& aBoard,
                                           const BOARD_CONNECTED_ITEM& aItemA,
                                           const BOARD_CONNECTED_ITEM& aItemB,
                                           PCB_LAYER_ID aLayer,
                                           DRC_CONSTRAINT_T aType,
                                           const DRC_CONSTRAINT& aConstraint )
{
    return wxString::Format(
            wxS( "{\"rule\":%s,\"constraint_type\":%s,\"enum\":%d,"
                 "\"geometry\":%s,\"layer\":%s,\"covered\":true,"
                 "\"source\":\"DRC_ENGINE::EvalRules\","
                 "\"source_item\":%s,\"target_item\":%s,\"value\":%s}" ),
            quotedJson( aConstraint.GetName() ),
            quotedJson( drcConstraintTypeToken( aType ) ), static_cast<int>( aType ),
            quotedJson( geometryPairToken( aItemA, aItemB ) ),
            quotedJson( aBoard.GetLayerName( aLayer ) ),
            pairConstraintItemJson( aItemA ), pairConstraintItemJson( aItemB ),
            minOptMaxJson( aConstraint.GetValue() ) );
}


void appendPairConstraintItem( std::vector<const BOARD_CONNECTED_ITEM*>& aItems,
                               const BOARD_CONNECTED_ITEM* aItem )
{
    constexpr size_t maxPairInputItems = 64;

    if( !aItem || aItem->GetNetCode() == NETINFO_LIST::UNCONNECTED )
        return;

    if( !aItem->IsOnCopperLayer() )
        return;

    if( aItems.size() >= maxPairInputItems )
        return;

    aItems.push_back( aItem );
}


std::vector<const BOARD_CONNECTED_ITEM*> collectPairConstraintItems( const BOARD& aBoard )
{
    std::vector<const BOARD_CONNECTED_ITEM*> items;

    for( PCB_TRACK* track : aBoard.Tracks() )
        appendPairConstraintItem( items, track );

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
            appendPairConstraintItem( items, pad );

        for( ZONE* zone : footprint->Zones() )
            appendPairConstraintItem( items, zone );
    }

    for( ZONE* zone : aBoard.Zones() )
        appendPairConstraintItem( items, zone );

    return items;
}


std::vector<wxString> makePairEffectiveConstraintEntries( const BOARD& aBoard,
                                                          DRC_ENGINE& aEngine,
                                                          bool& aTruncated )
{
    constexpr size_t      maxPairConstraintSample = 32;
    std::vector<wxString> entries;

    std::vector<const BOARD_CONNECTED_ITEM*> items = collectPairConstraintItems( aBoard );

    for( size_t i = 0; i < items.size(); ++i )
    {
        for( size_t j = i + 1; j < items.size(); ++j )
        {
            const BOARD_CONNECTED_ITEM* itemA = items[i];
            const BOARD_CONNECTED_ITEM* itemB = items[j];

            if( !itemA || !itemB || itemA->GetNetCode() == itemB->GetNetCode() )
                continue;

            PCB_LAYER_ID layer = UNDEFINED_LAYER;

            if( !firstCommonCopperLayer( aBoard, *itemA, *itemB, layer ) )
                continue;

            DRC_CONSTRAINT constraint =
                    aEngine.EvalRules( CLEARANCE_CONSTRAINT, itemA, itemB, layer );

            if( constraint.IsNull() || !constraint.GetValue().HasMin() )
                continue;

            if( entries.size() >= maxPairConstraintSample )
            {
                aTruncated = true;
                return entries;
            }

            entries.push_back( pairEffectiveConstraintJson( aBoard, *itemA, *itemB, layer,
                                                            CLEARANCE_CONSTRAINT,
                                                            constraint,
                                                            aEngine.HasGeometryDependentRules() ) );
        }
    }

    return entries;
}


std::vector<wxString> makeGeometrySpecificRuleCoverageEntries( const BOARD& aBoard,
                                                               DRC_ENGINE& aEngine,
                                                               bool& aTruncated )
{
    constexpr size_t      maxCoverageSample = 32;
    std::vector<wxString> entries;

    if( !aEngine.HasGeometryDependentRules() )
        return entries;

    static const DRC_CONSTRAINT_T coverageTypes[] = {
        CLEARANCE_CONSTRAINT,
        PHYSICAL_CLEARANCE_CONSTRAINT,
        HOLE_CLEARANCE_CONSTRAINT,
        PHYSICAL_HOLE_CLEARANCE_CONSTRAINT,
        EDGE_CLEARANCE_CONSTRAINT
    };

    std::vector<const BOARD_CONNECTED_ITEM*> items = collectPairConstraintItems( aBoard );

    for( size_t i = 0; i < items.size(); ++i )
    {
        for( size_t j = i + 1; j < items.size(); ++j )
        {
            const BOARD_CONNECTED_ITEM* itemA = items[i];
            const BOARD_CONNECTED_ITEM* itemB = items[j];

            if( !itemA || !itemB || itemA->GetNetCode() == itemB->GetNetCode() )
                continue;

            PCB_LAYER_ID layer = UNDEFINED_LAYER;

            if( !firstCommonCopperLayer( aBoard, *itemA, *itemB, layer ) )
                continue;

            for( DRC_CONSTRAINT_T type : coverageTypes )
            {
                DRC_CONSTRAINT constraint = aEngine.EvalRules( type, itemA, itemB, layer );

                if( constraint.IsNull() || constraint.GetName().IsEmpty()
                    || !constraint.GetValue().HasMin() )
                {
                    continue;
                }

                if( entries.size() >= maxCoverageSample )
                {
                    aTruncated = true;
                    return entries;
                }

                entries.push_back( geometrySpecificRuleCoverageJson( aBoard, *itemA, *itemB,
                                                                      layer, type,
                                                                      constraint ) );
            }
        }
    }

    return entries;
}


wxString makeEffectiveConstraintFactsJson( const BOARD& aBoard )
{
    const BOARD_DESIGN_SETTINGS& aSettings = aBoard.GetDesignSettings();
    constexpr size_t      maxWorstConstraintSample = 32;
    std::vector<wxString> worstConstraintEntries;
    std::vector<wxString> pairConstraintEntries;
    std::vector<wxString> geometryCoverageEntries;
    bool                  worstConstraintSampleTruncated = false;
    bool                  pairConstraintSampleTruncated = false;
    bool                  geometryCoverageTruncated = false;

    if( !aSettings.m_DRCEngine )
    {
        return wxS( "{\"drc_engine_present\":false,\"rules_valid\":false,"
                    "\"geometry_dependent_rules_present\":false,"
                    "\"worst_constraints\":[],"
                    "\"worst_constraint_sample_truncated\":false,"
                    "\"pair_effective_constraints\":[],"
                    "\"pair_effective_constraint_sample_truncated\":false,"
                    "\"geometry_specific_rule_coverage\":[],"
                    "\"geometry_specific_rule_coverage_truncated\":false}" );
    }

    static const DRC_CONSTRAINT_T queryTypes[] = {
        CLEARANCE_CONSTRAINT,
        PHYSICAL_CLEARANCE_CONSTRAINT,
        HOLE_CLEARANCE_CONSTRAINT,
        PHYSICAL_HOLE_CLEARANCE_CONSTRAINT,
        HOLE_TO_HOLE_CONSTRAINT,
        EDGE_CLEARANCE_CONSTRAINT,
        COURTYARD_CLEARANCE_CONSTRAINT,
        SILK_CLEARANCE_CONSTRAINT,
        TRACK_WIDTH_CONSTRAINT,
        CONNECTION_WIDTH_CONSTRAINT,
        VIA_DIAMETER_CONSTRAINT,
        HOLE_SIZE_CONSTRAINT,
        ANNULAR_WIDTH_CONSTRAINT
    };

    for( DRC_CONSTRAINT_T type : queryTypes )
    {
        if( worstConstraintEntries.size() >= maxWorstConstraintSample )
        {
            worstConstraintSampleTruncated = true;
            break;
        }

        DRC_CONSTRAINT constraint;

        if( aSettings.m_DRCEngine->QueryWorstConstraint( type, constraint ) )
            worstConstraintEntries.push_back( worstConstraintJson( type, constraint ) );
    }

    pairConstraintEntries =
            makePairEffectiveConstraintEntries( aBoard, *aSettings.m_DRCEngine,
                                                pairConstraintSampleTruncated );
    geometryCoverageEntries =
            makeGeometrySpecificRuleCoverageEntries( aBoard, *aSettings.m_DRCEngine,
                                                     geometryCoverageTruncated );

    return wxString::Format(
            wxS( "{\"drc_engine_present\":true,\"rules_valid\":%s,"
                 "\"geometry_dependent_rules_present\":%s,"
                 "\"worst_constraints\":%s,"
                 "\"worst_constraint_sample_truncated\":%s,"
                 "\"pair_effective_constraints\":%s,"
                 "\"pair_effective_constraint_sample_truncated\":%s,"
                 "\"geometry_specific_rule_coverage\":%s,"
                 "\"geometry_specific_rule_coverage_truncated\":%s}" ),
            boolJson( aSettings.m_DRCEngine->RulesValid() ),
            boolJson( aSettings.m_DRCEngine->HasGeometryDependentRules() ),
            jsonArray( worstConstraintEntries ),
            boolJson( worstConstraintSampleTruncated ),
            jsonArray( pairConstraintEntries ),
            boolJson( pairConstraintSampleTruncated ),
            jsonArray( geometryCoverageEntries ),
            boolJson( geometryCoverageTruncated ) );
}


wxString makeConstraintFactsJson( const BOARD& aBoard )
{
    constexpr size_t      maxKeepoutSample = 64;
    std::vector<wxString> keepoutEntries;
    bool                  keepoutSampleTruncated = false;
    size_t                ruleAreaCount = 0;
    size_t                keepoutCount = 0;

    auto visitZone =
            [&]( const ZONE& aZone )
            {
                if( !aZone.GetIsRuleArea() )
                    return;

                ++ruleAreaCount;

                if( !zoneHasKeepout( aZone ) )
                    return;

                ++keepoutCount;

                if( keepoutEntries.size() >= maxKeepoutSample )
                {
                    keepoutSampleTruncated = true;
                    return;
                }

                keepoutEntries.push_back( makeKeepoutConstraintJson( aZone ) );
            };

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        for( ZONE* zone : footprint->Zones() )
            visitZone( *zone );
    }

    for( ZONE* zone : aBoard.Zones() )
        visitZone( *zone );

    return wxString::Format(
            wxS( "{\"source\":\"board\",\"minimums\":%s,"
                 "\"rule_area_count\":%zu,\"keepout_count\":%zu,"
                 "\"keepouts\":%s,\"keepout_sample_truncated\":%s,"
                 "\"effective_constraints\":%s}" ),
            makeConstraintMinimumsJson( aBoard.GetDesignSettings() ), ruleAreaCount,
            keepoutCount, jsonArray( keepoutEntries ), boolJson( keepoutSampleTruncated ),
            makeEffectiveConstraintFactsJson( aBoard ) );
}


void appendObstacleEntry( std::vector<wxString>& aEntries, bool& aTruncated, size_t& aCount,
                          const wxString& aEntry )
{
    constexpr size_t maxObstacleSample = 128;

    ++aCount;

    if( aEntries.size() >= maxObstacleSample )
    {
        aTruncated = true;
        return;
    }

    aEntries.push_back( aEntry );
}


wxString makeFootprintObstacleJson( const FOOTPRINT& aFootprint )
{
    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":\"footprint\",\"label\":%s,"
                 "\"layer\":%s,\"position\":%s,\"bbox\":%s,\"pad_count\":%u}" ),
            quotedJson( aFootprint.m_Uuid.AsString() ), static_cast<int>( aFootprint.Type() ),
            quotedJson( footprintContextLabel( aFootprint ) ),
            quotedJson( aFootprint.GetLayerName() ), pointDetailsJson( aFootprint.GetPosition() ),
            boxRectDetailsJson( aFootprint.GetBoundingBox( false ) ), aFootprint.GetPadCount() );
}


wxString makePadObstacleJson( const FOOTPRINT& aFootprint, const PAD& aPad )
{
    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":\"pad\",\"label\":%s,"
                 "\"footprint_reference\":%s,\"pad_number\":%s,"
                 "\"net_code\":%d,\"net_name\":%s,\"layer\":%s,"
                 "\"layers\":%s,\"position\":%s,\"bbox\":%s,"
                 "\"shape\":%s,\"size\":%s}" ),
            quotedJson( aPad.m_Uuid.AsString() ), static_cast<int>( aPad.Type() ),
            quotedJson( padContextLabel( aFootprint, aPad ) ),
            quotedJson( aFootprint.GetReference() ), quotedJson( aPad.GetNumber() ),
            aPad.GetNetCode(), quotedJson( aPad.GetNetname() ),
            quotedJson( boardLayerName( aPad, aPad.GetLayer() ) ),
            layerSetDetailsJson( aPad, aPad.GetLayerSet() ),
            pointDetailsJson( aPad.GetPosition() ), boxRectDetailsJson( aPad.GetBoundingBox() ),
            quotedJson( padShapeToken( aPad.GetShape( PADSTACK::ALL_LAYERS ) ) ),
            pointDetailsJson( aPad.GetSize( PADSTACK::ALL_LAYERS ) ) );
}


wxString makeRoutingObstacleJson( const PCB_TRACK& aTrack )
{
    const wxString kind = connectedItemKindToken( aTrack );
    const wxString label = makeRoutingRef( aTrack ).m_Label;

    wxString geometry;

    if( aTrack.Type() == PCB_VIA_T )
    {
        const PCB_VIA& via = static_cast<const PCB_VIA&>( aTrack );

        geometry = wxString::Format(
                wxS( "\"position\":%s,\"diameter\":%d,\"drill\":%d" ),
                pointDetailsJson( via.GetPosition() ), via.GetWidth( PADSTACK::ALL_LAYERS ),
                via.GetDrillValue() );
    }
    else
    {
        geometry = wxString::Format(
                wxS( "\"position\":%s,\"start\":%s,\"end\":%s,\"width\":%d" ),
                pointDetailsJson( aTrack.GetPosition() ), pointDetailsJson( aTrack.GetStart() ),
                pointDetailsJson( aTrack.GetEnd() ), aTrack.GetWidth() );
    }

    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":%s,\"label\":%s,"
                 "\"net_code\":%d,\"net_name\":%s,\"layer\":%s,"
                 "\"layers\":%s,\"bbox\":%s,%s}" ),
            quotedJson( aTrack.m_Uuid.AsString() ), static_cast<int>( aTrack.Type() ),
            quotedJson( kind ), quotedJson( label ), aTrack.GetNetCode(),
            quotedJson( aTrack.GetNetname() ), quotedJson( aTrack.GetLayerName() ),
            layerSetDetailsJson( aTrack, aTrack.GetLayerSet() ),
            boxRectDetailsJson( aTrack.GetBoundingBox() ), geometry );
}


wxString makeZoneObstacleJson( const ZONE& aZone )
{
    const wxString kind = zoneKindToken( aZone );
    const wxString label = makeZoneRef( aZone ).m_Label;
    wxString       blocksJson = wxS( "null" );

    if( zoneHasKeepout( aZone ) )
    {
        blocksJson = wxString::Format(
                wxS( "{\"tracks\":%s,\"vias\":%s,\"pads\":%s,"
                     "\"footprints\":%s,\"zone_fills\":%s}" ),
                boolJson( aZone.GetDoNotAllowTracks() ), boolJson( aZone.GetDoNotAllowVias() ),
                boolJson( aZone.GetDoNotAllowPads() ),
                boolJson( aZone.GetDoNotAllowFootprints() ),
                boolJson( aZone.GetDoNotAllowZoneFills() ) );
    }

    return wxString::Format(
            wxS( "{\"uuid\":%s,\"type\":%d,\"kind\":%s,\"label\":%s,"
                 "\"net_code\":%d,\"net_name\":%s,\"layers\":%s,"
                 "\"first_layer\":%s,\"position\":%s,\"bbox\":%s,"
                 "\"is_rule_area\":%s,\"has_keepout\":%s,\"blocks\":%s}" ),
            quotedJson( aZone.m_Uuid.AsString() ), static_cast<int>( aZone.Type() ),
            quotedJson( kind ), quotedJson( label ), aZone.GetNetCode(),
            quotedJson( aZone.GetNetname() ), layerSetDetailsJson( aZone, aZone.GetLayerSet() ),
            quotedJson( boardLayerName( aZone, aZone.GetFirstLayer() ) ),
            pointDetailsJson( aZone.GetPosition() ), boxRectDetailsJson( aZone.GetBoundingBox() ),
            boolJson( aZone.GetIsRuleArea() ), boolJson( zoneHasKeepout( aZone ) ), blocksJson );
}


wxString makeObstacleFactsJson( const BOARD& aBoard )
{
    std::vector<wxString> obstacleEntries;
    bool                  obstacleSampleTruncated = false;
    size_t                obstacleCount = 0;

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        appendObstacleEntry( obstacleEntries, obstacleSampleTruncated, obstacleCount,
                             makeFootprintObstacleJson( *footprint ) );

        for( PAD* pad : footprint->Pads() )
        {
            appendObstacleEntry( obstacleEntries, obstacleSampleTruncated, obstacleCount,
                                 makePadObstacleJson( *footprint, *pad ) );
        }
    }

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( isRoutingObject( *track ) )
        {
            appendObstacleEntry( obstacleEntries, obstacleSampleTruncated, obstacleCount,
                                 makeRoutingObstacleJson( *track ) );
        }
    }

    for( ZONE* zone : aBoard.Zones() )
        appendObstacleEntry( obstacleEntries, obstacleSampleTruncated, obstacleCount,
                             makeZoneObstacleJson( *zone ) );

    return wxString::Format(
            wxS( "{\"source\":\"board\",\"obstacle_count\":%zu,"
                 "\"obstacles\":%s,\"obstacle_sample_truncated\":%s}" ),
            obstacleCount, jsonArray( obstacleEntries ), boolJson( obstacleSampleTruncated ) );
}


wxString makeBoardSummaryJson( const BOARD& aBoard )
{
    size_t netCount = 0;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( net->GetNetCode() != NETINFO_LIST::UNCONNECTED )
            ++netCount;
    }

    size_t footprintCount = 0;
    size_t padCount = 0;

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        ++footprintCount;
        padCount += footprint->GetPadCount();
    }

    size_t trackCount = 0;
    size_t arcCount = 0;
    size_t viaCount = 0;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( track->Type() == PCB_TRACE_T )
            ++trackCount;
        else if( track->Type() == PCB_ARC_T )
            ++arcCount;
        else if( track->Type() == PCB_VIA_T )
            ++viaCount;
    }

    size_t drawingCount = 0;
    size_t edgeCutCount = 0;

    for( BOARD_ITEM* drawing : aBoard.Drawings() )
    {
        ++drawingCount;

        if( drawing->Type() == PCB_SHAPE_T
            && static_cast<PCB_SHAPE*>( drawing )->GetLayer() == Edge_Cuts )
            ++edgeCutCount;
    }

    size_t zoneCount = 0;
    size_t keepoutCount = 0;

    for( ZONE* zone : aBoard.Zones() )
    {
        ++zoneCount;

        if( zoneHasKeepout( *zone ) )
            ++keepoutCount;
    }

    return wxString::Format(
            wxS( "{\"kind\":\"pcb_board_summary\",\"net_count\":%zu,"
                 "\"footprint_count\":%zu,\"pad_count\":%zu,\"track_count\":%zu,"
                 "\"arc_count\":%zu,\"via_count\":%zu,\"drawing_count\":%zu,"
                 "\"edge_cut_count\":%zu,\"zone_count\":%zu,\"keepout_count\":%zu,"
                 "\"board_edges_bbox\":%s,\"clearance_sources\":%s,"
                 "\"constraint_facts\":%s,\"obstacle_facts\":%s,"
                 "\"net_facts\":%s,\"layer_context\":%s,"
                 "\"connectivity_summary\":%s}" ),
            netCount, footprintCount, padCount, trackCount, arcCount, viaCount,
            drawingCount, edgeCutCount, zoneCount, keepoutCount,
            boxDetailsJson( aBoard.GetBoardEdgesBoundingBox() ),
            makeClearanceSourcesJson( aBoard ), makeConstraintFactsJson( aBoard ),
            makeObstacleFactsJson( aBoard ), makeNetFactsJson( aBoard ),
            makeLayerContextJson( aBoard ), makeConnectivitySummaryJson( aBoard ) );
}


} // namespace


KISURF_AI_PCB_CONTEXT_ADAPTER::KISURF_AI_PCB_CONTEXT_ADAPTER( BOARD& aBoard ) :
        m_Board( aBoard )
{
}


AI_CONTEXT_INDEX KISURF_AI_PCB_CONTEXT_ADAPTER::BuildIndex() const
{
    AI_CONTEXT_INDEX          index( AI_EDITOR_KIND::Pcb );
    std::vector<AI_OBJECT_REF> visibleObjects;
    std::vector<AI_OBJECT_REF> selectedObjects;
    std::vector<AI_CONTEXT_ANCHOR> anchors;

    for( FOOTPRINT* footprint : m_Board.Footprints() )
    {
        AI_OBJECT_REF footprintRef = makeFootprintRef( *footprint );

        visibleObjects.push_back( footprintRef );
        anchors.push_back( makeFootprintAnchor( *footprint ) );

        if( footprint->IsSelected() )
            selectedObjects.push_back( footprintRef );

        for( PCB_FIELD* field : footprint->GetFields() )
        {
            AI_OBJECT_REF ref = makeFieldRef( *footprint, *field );

            visibleObjects.push_back( ref );

            if( field->IsSelected() )
                selectedObjects.push_back( ref );
        }

        for( PAD* pad : footprint->Pads() )
        {
            AI_OBJECT_REF ref = makePadRef( *footprint, *pad );

            visibleObjects.push_back( ref );
            anchors.push_back( makePadAnchor( *footprint, *pad ) );

            if( pad->IsSelected() )
                selectedObjects.push_back( ref );
        }

        for( BOARD_ITEM* item : footprint->GraphicalItems() )
        {
            if( item->Type() == PCB_SHAPE_T )
            {
                const PCB_SHAPE& shape = static_cast<const PCB_SHAPE&>( *item );
                AI_OBJECT_REF    ref = makeFootprintShapeRef( *footprint, shape );

                visibleObjects.push_back( ref );
                appendShapeAnchors( shape, ref.m_Label, anchors );

                if( shape.IsSelected() )
                    selectedObjects.push_back( ref );

                continue;
            }

            if( item->Type() == PCB_TEXT_T )
            {
                const PCB_TEXT& text = static_cast<const PCB_TEXT&>( *item );
                AI_OBJECT_REF  ref = makeFootprintTextRef( *footprint, text );

                visibleObjects.push_back( ref );

                if( text.IsSelected() )
                    selectedObjects.push_back( ref );

                continue;
            }

            if( item->Type() == PCB_TEXTBOX_T )
            {
                const PCB_TEXTBOX& textbox = static_cast<const PCB_TEXTBOX&>( *item );
                AI_OBJECT_REF      ref = makeFootprintTextboxRef( *footprint, textbox );

                visibleObjects.push_back( ref );

                if( textbox.IsSelected() )
                    selectedObjects.push_back( ref );
            }
        }
    }

    for( PCB_TRACK* track : m_Board.Tracks() )
    {
        if( !isRoutingObject( *track ) )
            continue;

        AI_OBJECT_REF ref = makeRoutingRef( *track );

        visibleObjects.push_back( ref );
        appendRoutingAnchors( *track, anchors );

        if( track->IsSelected() )
            selectedObjects.push_back( ref );
    }

    for( BOARD_ITEM* drawing : m_Board.Drawings() )
    {
        if( drawing->Type() == PCB_SHAPE_T )
        {
            const PCB_SHAPE& shape = static_cast<const PCB_SHAPE&>( *drawing );
            AI_OBJECT_REF    ref = makeShapeRef( shape );

            visibleObjects.push_back( ref );
            appendShapeAnchors( shape, ref.m_Label, anchors );

            if( shape.IsSelected() )
                selectedObjects.push_back( ref );

            continue;
        }

        if( drawing->Type() == PCB_TEXT_T )
        {
            const PCB_TEXT& text = static_cast<const PCB_TEXT&>( *drawing );
            AI_OBJECT_REF  ref = makeTextRef( text );

            visibleObjects.push_back( ref );

            if( text.IsSelected() )
                selectedObjects.push_back( ref );

            continue;
        }

        if( drawing->Type() == PCB_TEXTBOX_T )
        {
            const PCB_TEXTBOX& textbox = static_cast<const PCB_TEXTBOX&>( *drawing );
            AI_OBJECT_REF      ref = makeTextboxRef( textbox );

            visibleObjects.push_back( ref );

            if( textbox.IsSelected() )
                selectedObjects.push_back( ref );

            continue;
        }

        if( drawing->Type() == PCB_TARGET_T )
        {
            const PCB_TARGET& target = static_cast<const PCB_TARGET&>( *drawing );
            AI_OBJECT_REF     ref = makeTargetRef( target );

            visibleObjects.push_back( ref );

            if( target.IsSelected() )
                selectedObjects.push_back( ref );

            continue;
        }

        if( drawing->Type() == PCB_BARCODE_T )
        {
            const PCB_BARCODE& barcode = static_cast<const PCB_BARCODE&>( *drawing );
            AI_OBJECT_REF      ref = makeBarcodeRef( barcode );

            visibleObjects.push_back( ref );

            if( barcode.IsSelected() )
                selectedObjects.push_back( ref );

            continue;
        }

        if( drawing->Type() == PCB_TABLE_T )
        {
            const PCB_TABLE& table = static_cast<const PCB_TABLE&>( *drawing );
            AI_OBJECT_REF    tableRef = makeTableRef( table );

            visibleObjects.push_back( tableRef );

            if( table.IsSelected() )
                selectedObjects.push_back( tableRef );

            for( PCB_TABLECELL* cell : table.GetCells() )
            {
                AI_OBJECT_REF cellRef = makeTableCellRef( table, *cell );

                visibleObjects.push_back( cellRef );

                if( cell->IsSelected() )
                    selectedObjects.push_back( cellRef );
            }

            continue;
        }

        if( BaseType( drawing->Type() ) == PCB_DIMENSION_T )
        {
            const PCB_DIMENSION_BASE& dimension =
                    static_cast<const PCB_DIMENSION_BASE&>( *drawing );
            AI_OBJECT_REF ref = makeDimensionRef( dimension );

            visibleObjects.push_back( ref );

            if( dimension.IsSelected() )
                selectedObjects.push_back( ref );
        }
    }

    for( ZONE* zone : m_Board.Zones() )
    {
        AI_OBJECT_REF ref = makeZoneRef( *zone );

        visibleObjects.push_back( ref );

        if( zone->IsSelected() )
            selectedObjects.push_back( ref );
    }

    index.SetVisibleObjects( visibleObjects );
    index.SetSelectedObjects( selectedObjects );

    if( !anchors.empty() )
        index.SetAnchors( anchors );

    index.SetSummary( makeBoardSummaryJson( m_Board ) );

    return index;
}
