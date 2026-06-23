#include <kisurf_ai_pcb_context_adapter.h>

#include <board.h>
#include <board_design_settings.h>
#include <core/typeinfo.h>
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
#include <zone.h>

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
                                  "\"corner_count\":%d,\"position\":%s,"
                                  "\"net_code\":%d,\"net_name\":%s,\"priority\":%u,"
                                  "\"is_rule_area\":%s,\"has_keepout\":%s,"
                                  "\"keepout\":{\"tracks\":%s,\"vias\":%s,"
                                  "\"pads\":%s,\"footprints\":%s,\"zone_fills\":%s}}" ),
                             quotedJson( zoneKindToken( aZone ) ),
                             quotedJson( aZone.GetZoneName() ),
                             layerSetDetailsJson( aZone, aZone.GetLayerSet() ),
                             quotedJson( boardLayerName( aZone, aZone.GetFirstLayer() ) ),
                             aZone.GetNumCorners(), pointDetailsJson( aZone.GetPosition() ),
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


wxString makeLayerContextJson( const BOARD& aBoard )
{
    const LSET&           enabledLayers = aBoard.GetEnabledLayers();
    std::vector<wxString> layerEntries;
    int                   visibleLayerCount = 0;

    for( PCB_LAYER_ID layer : enabledLayers.Seq() )
    {
        const bool visible = aBoard.IsLayerVisible( layer );

        if( visible )
            ++visibleLayerCount;

        layerEntries.push_back( wxString::Format(
                wxS( "{\"id\":%d,\"name\":%s,\"enabled\":true,\"visible\":%s,"
                     "\"copper\":%s}" ),
                static_cast<int>( layer ), quotedJson( aBoard.GetLayerName( layer ) ),
                boolJson( visible ), boolJson( IsCopperLayer( layer ) ) ) );
    }

    const wxString visibleLayersSource = aBoard.GetProject()
                                                ? wxS( "project_local_settings" )
                                                : wxS( "default_all_layers_no_project" );

    return wxString::Format(
            wxS( "{\"source\":\"board\",\"visible_layers_source\":%s,"
                 "\"copper_layer_count\":%d,\"enabled_layer_count\":%d,"
                 "\"visible_layer_count\":%d,\"layers\":%s}" ),
            quotedJson( visibleLayersSource ), aBoard.GetCopperLayerCount(),
            static_cast<int>( enabledLayers.count() ), visibleLayerCount,
            jsonArray( layerEntries ) );
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
                 "\"layer_context\":%s}" ),
            netCount, footprintCount, padCount, trackCount, arcCount, viaCount,
            drawingCount, edgeCutCount, zoneCount, keepoutCount,
            boxDetailsJson( aBoard.GetBoardEdgesBoundingBox() ),
            makeClearanceSourcesJson( aBoard ), makeLayerContextJson( aBoard ) );
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
