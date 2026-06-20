#include <kisurf_ai_sch_context_adapter.h>

#include <sch_field.h>
#include <sch_item.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_screen.h>
#include <sch_symbol.h>

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


wxString pointDetailsJson( const VECTOR2I& aPoint )
{
    return wxString::Format( wxS( "{\"x\":%d,\"y\":%d}" ), aPoint.x, aPoint.y );
}


wxString fieldText( const SCH_SYMBOL& aSymbol, FIELD_T aFieldId )
{
    const SCH_FIELD* field = aSymbol.GetField( aFieldId );
    return field ? field->GetText() : wxString();
}


wxString lineKind( const SCH_LINE& aLine )
{
    switch( aLine.GetLayer() )
    {
    case LAYER_WIRE: return wxS( "wire" );
    case LAYER_BUS:  return wxS( "bus" );
    default:         return wxS( "line" );
    }
}


wxString labelKind( const SCH_ITEM& aItem )
{
    switch( aItem.Type() )
    {
    case SCH_GLOBAL_LABEL_T: return wxS( "global_label" );
    case SCH_HIER_LABEL_T:   return wxS( "hier_label" );
    default:                 return wxS( "label" );
    }
}


wxString makeSchItemDetailsJson( const SCH_ITEM& aItem )
{
    switch( aItem.Type() )
    {
    case SCH_SYMBOL_T:
    {
        const SCH_SYMBOL& symbol = static_cast<const SCH_SYMBOL&>( aItem );

        return wxString::Format( wxS( "{\"kind\":\"symbol\",\"reference\":%s,"
                                      "\"value\":%s,\"footprint\":%s,\"position\":%s}" ),
                                 quotedJson( fieldText( symbol, FIELD_T::REFERENCE ) ),
                                 quotedJson( fieldText( symbol, FIELD_T::VALUE ) ),
                                 quotedJson( fieldText( symbol, FIELD_T::FOOTPRINT ) ),
                                 pointDetailsJson( symbol.GetPosition() ) );
    }

    case SCH_LINE_T:
    {
        const SCH_LINE& line = static_cast<const SCH_LINE&>( aItem );
        wxString        kind = lineKind( line );

        return wxString::Format( wxS( "{\"kind\":%s,\"start\":%s,"
                                      "\"end\":%s,\"layer\":%s}" ),
                                 quotedJson( kind ),
                                 pointDetailsJson( line.GetStartPoint() ),
                                 pointDetailsJson( line.GetEndPoint() ),
                                 quotedJson( kind ) );
    }

    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    {
        const SCH_LABEL_BASE& label = static_cast<const SCH_LABEL_BASE&>( aItem );
        wxString              kind = labelKind( aItem );

        return wxString::Format( wxS( "{\"kind\":%s,\"text\":%s,"
                                      "\"position\":%s}" ),
                                 quotedJson( kind ), quotedJson( label.GetText() ),
                                 pointDetailsJson( label.GetPosition() ) );
    }

    case SCH_JUNCTION_T:
        return wxString::Format( wxS( "{\"kind\":\"junction\",\"position\":%s}" ),
                                 pointDetailsJson( aItem.GetPosition() ) );

    case SCH_NO_CONNECT_T:
        return wxString::Format( wxS( "{\"kind\":\"no_connect\",\"position\":%s}" ),
                                 pointDetailsJson( aItem.GetPosition() ) );

    default:
        return wxString();
    }
}


AI_OBJECT_REF makeSchItemRef( const SCH_ITEM& aItem )
{
    wxString label;

    if( aItem.Type() == SCH_SYMBOL_T )
    {
        const SCH_SYMBOL& symbol = static_cast<const SCH_SYMBOL&>( aItem );
        const SCH_FIELD*  field = symbol.GetField( FIELD_T::REFERENCE );

        if( field )
            label = field->GetText();
    }
    else if( aItem.Type() == SCH_LINE_T )
    {
        const SCH_LINE& line = static_cast<const SCH_LINE&>( aItem );

        label = lineKind( line ) + wxS( ":" ) + formatPoint( line.GetStartPoint() )
                + wxS( "->" ) + formatPoint( line.GetEndPoint() );
    }
    else if( aItem.Type() == SCH_LABEL_T || aItem.Type() == SCH_GLOBAL_LABEL_T
             || aItem.Type() == SCH_HIER_LABEL_T )
    {
        const SCH_LABEL_BASE& schLabel = static_cast<const SCH_LABEL_BASE&>( aItem );

        label = labelKind( aItem ) + wxS( ":" ) + schLabel.GetText();
    }
    else if( aItem.Type() == SCH_JUNCTION_T )
    {
        label = wxS( "junction:" ) + formatPoint( aItem.GetPosition() );
    }
    else if( aItem.Type() == SCH_NO_CONNECT_T )
    {
        label = wxS( "no_connect:" ) + formatPoint( aItem.GetPosition() );
    }

    if( label.IsEmpty() )
        label = aItem.GetFriendlyName();

    if( label.IsEmpty() )
        label = wxS( "sch:" ) + aItem.m_Uuid.AsString();

    return AI_OBJECT_REF( aItem.m_Uuid, aItem.Type(), label,
                          makeSchItemDetailsJson( aItem ) );
}

} // namespace


KISURF_AI_SCH_CONTEXT_ADAPTER::KISURF_AI_SCH_CONTEXT_ADAPTER( SCH_SCREEN& aScreen ) :
        m_Screen( aScreen )
{
}


AI_CONTEXT_INDEX KISURF_AI_SCH_CONTEXT_ADAPTER::BuildIndex() const
{
    AI_CONTEXT_INDEX          index( AI_EDITOR_KIND::Schematic );
    std::vector<AI_OBJECT_REF> visibleObjects;
    std::vector<AI_OBJECT_REF> selectedObjects;

    for( SCH_ITEM* item : m_Screen.Items() )
    {
        AI_OBJECT_REF ref = makeSchItemRef( *item );

        visibleObjects.push_back( ref );

        if( item->IsSelected() )
            selectedObjects.push_back( ref );
    }

    index.SetVisibleObjects( visibleObjects );
    index.SetSelectedObjects( selectedObjects );

    return index;
}
