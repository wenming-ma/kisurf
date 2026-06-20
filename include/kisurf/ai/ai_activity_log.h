#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <mutex>
#include <vector>

class KICOMMON_API AI_ACTIVITY_LOG
{
public:
    explicit AI_ACTIVITY_LOG( size_t aCapacity = 256 );

    AI_ACTIVITY_RECORD Append( AI_ACTIVITY_RECORD aRecord );
    std::vector<AI_ACTIVITY_RECORD> Records() const;
    void Clear();

private:
    size_t                          m_Capacity = 0;
    uint64_t                        m_NextSequence = 1;
    mutable std::mutex              m_Mutex;
    std::vector<AI_ACTIVITY_RECORD> m_Records;
};
