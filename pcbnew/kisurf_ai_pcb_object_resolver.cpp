#include <kisurf_ai_pcb_object_resolver.h>

#include <board.h>
#include <core/typeinfo.h>
#include <footprint.h>
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
#include <zone.h>

namespace
{
bool isRoutingRefType( KICAD_T aType )
{
    return aType == PCB_TRACE_T || aType == PCB_ARC_T || aType == PCB_VIA_T;
}

bool isDrawingRefType( KICAD_T aType )
{
    return aType == PCB_SHAPE_T || aType == PCB_TEXT_T || aType == PCB_TEXTBOX_T
           || aType == PCB_TARGET_T || aType == PCB_BARCODE_T || aType == PCB_TABLE_T
           || BaseType( aType ) == PCB_DIMENSION_T;
}
} // namespace

KISURF_AI_PCB_OBJECT_RESOLVER::KISURF_AI_PCB_OBJECT_RESOLVER( BOARD& aBoard ) :
        m_Board( aBoard )
{
}


BOARD_ITEM* KISURF_AI_PCB_OBJECT_RESOLVER::Resolve( const AI_OBJECT_REF& aRef ) const
{
    if( !aRef.IsValid() )
        return nullptr;

    if( isRoutingRefType( aRef.m_Type ) )
    {
        for( PCB_TRACK* track : m_Board.Tracks() )
        {
            if( track->m_Uuid == aRef.m_Uuid && track->Type() == aRef.m_Type )
                return track;
        }

        return nullptr;
    }

    if( isDrawingRefType( aRef.m_Type ) )
    {
        for( BOARD_ITEM* drawing : m_Board.Drawings() )
        {
            if( drawing->m_Uuid == aRef.m_Uuid && drawing->Type() == aRef.m_Type )
                return drawing;
        }
    }

    if( aRef.m_Type == PCB_TABLECELL_T )
    {
        for( BOARD_ITEM* drawing : m_Board.Drawings() )
        {
            if( drawing->Type() != PCB_TABLE_T )
                continue;

            PCB_TABLE* table = static_cast<PCB_TABLE*>( drawing );

            for( PCB_TABLECELL* cell : table->GetCells() )
            {
                if( cell->m_Uuid == aRef.m_Uuid && cell->Type() == aRef.m_Type )
                    return cell;
            }
        }

        return nullptr;
    }

    if( aRef.m_Type == PCB_ZONE_T )
    {
        for( ZONE* zone : m_Board.Zones() )
        {
            if( zone->m_Uuid == aRef.m_Uuid && zone->Type() == aRef.m_Type )
                return zone;
        }

        return nullptr;
    }

    if( aRef.m_Type != PCB_FOOTPRINT_T && aRef.m_Type != PCB_PAD_T
            && aRef.m_Type != PCB_FIELD_T && !isDrawingRefType( aRef.m_Type ) )
        return nullptr;

    for( FOOTPRINT* footprint : m_Board.Footprints() )
    {
        if( aRef.m_Type == PCB_FOOTPRINT_T )
        {
            if( footprint->m_Uuid == aRef.m_Uuid && footprint->Type() == aRef.m_Type )
                return footprint;

            continue;
        }

        for( PAD* pad : footprint->Pads() )
        {
            if( pad->m_Uuid == aRef.m_Uuid && pad->Type() == aRef.m_Type )
                return pad;
        }

        if( aRef.m_Type == PCB_FIELD_T )
        {
            for( PCB_FIELD* field : footprint->GetFields() )
            {
                if( field->m_Uuid == aRef.m_Uuid && field->Type() == aRef.m_Type )
                    return field;
            }

            continue;
        }

        if( isDrawingRefType( aRef.m_Type ) )
        {
            for( BOARD_ITEM* item : footprint->GraphicalItems() )
            {
                if( item->m_Uuid == aRef.m_Uuid && item->Type() == aRef.m_Type )
                    return item;
            }
        }
    }

    return nullptr;
}


std::vector<BOARD_ITEM*> KISURF_AI_PCB_OBJECT_RESOLVER::ResolveAll(
        const std::vector<AI_OBJECT_REF>& aRefs ) const
{
    std::vector<BOARD_ITEM*> resolved;

    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( BOARD_ITEM* item = Resolve( ref ) )
            resolved.push_back( item );
    }

    return resolved;
}
