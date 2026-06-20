#include <kisurf/ai/ai_semantic_ui.h>

#include <wx/regex.h>

const AI_SEMANTIC_UI_NODE* AI_SEMANTIC_UI_TREE::FindNode(
        const wxString& aNodeId ) const
{
    for( const AI_SEMANTIC_UI_NODE& node : m_Nodes )
    {
        if( node.m_NodeId == aNodeId )
            return &node;
    }

    return nullptr;
}


wxString RedactSemanticUiText( const wxString& aText )
{
    wxString text = aText;

    wxRegEx keyPattern( wxS( "sk-[A-Za-z0-9_-]{12,}" ) );
    keyPattern.ReplaceAll( &text, wxS( "sk-[redacted]" ) );

    wxRegEx envPattern(
            wxS( "(OPENAI_API_KEY|KISURF_AI_API_KEY)[[:space:]]*=[^[:space:]\\\"']+" ) );
    envPattern.ReplaceAll( &text, wxS( "\\1=[redacted]" ) );

    wxRegEx credentialPattern(
            wxS( "(credential|token|api key|api_key)[[:space:]]*:[^\\n\\r,}\\\"']+" ),
            wxRE_ADVANCED | wxRE_ICASE );
    credentialPattern.ReplaceAll( &text, wxS( "\\1: [redacted]" ) );

    if( text.length() > 4000 )
        text = text.Left( 4000 ) + wxS( "...[truncated]" );

    return text;
}
