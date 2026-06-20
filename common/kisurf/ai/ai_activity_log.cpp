#include <kisurf/ai/ai_activity_log.h>

AI_ACTIVITY_LOG::AI_ACTIVITY_LOG( size_t aCapacity ) :
        m_Capacity( aCapacity )
{
}


AI_ACTIVITY_RECORD AI_ACTIVITY_LOG::Append( AI_ACTIVITY_RECORD aRecord )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    aRecord.m_Sequence = m_NextSequence++;

    if( m_Capacity > 0 )
    {
        m_Records.push_back( aRecord );

        while( m_Records.size() > m_Capacity )
            m_Records.erase( m_Records.begin() );
    }

    return aRecord;
}


std::vector<AI_ACTIVITY_RECORD> AI_ACTIVITY_LOG::Records() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_Records;
}


void AI_ACTIVITY_LOG::Clear()
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_Records.clear();
}
