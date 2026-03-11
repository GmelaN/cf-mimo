/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_MIMO_COMMON_H
#define CF_MIMO_COMMON_H

#include "ns3/nstime.h"

#include <array>
#include <cstdint>
#include <limits>

namespace ns3
{

using UE_ID = uint16_t;
using AP_ID = uint16_t;
using CLUSTER_VER = uint32_t;

constexpr UE_ID INVALID_UE_ID = std::numeric_limits<UE_ID>::max();
constexpr AP_ID INVALID_AP_ID = std::numeric_limits<AP_ID>::max();

struct CfMimoConstants
{
    static constexpr uint8_t TOP_N_AP = 4;
    static constexpr uint8_t MAX_CLUSTER_SIZE = 8;
    static constexpr uint8_t MAX_PRB = 64;
    static constexpr uint8_t MAX_UE_PER_AP_SCHED = 16;
};

enum class CfMode : uint8_t
{
    DISTRIBUTED = 0,
    CENTRALIZED = 1
};

enum class CfRoleFlag : uint8_t
{
    NONE = 0,
    PRIMARY = 1,
    SECONDARY = 2
};

enum class CfRejectReason : uint8_t
{
    NONE = 0,
    MODE_MISMATCH = 1,
    EPOCH_MISMATCH = 2,
    NOT_IN_CLUSTER = 3,
    OVERLOAD = 4,
    TIMEOUT = 5
};

enum class UeMacState : uint8_t
{
    UE_TRACK = 0,
    UE_REPORT = 1,
    UE_ASSOC_WAIT = 2,
    UE_ASSOC_ACTIVE = 3,
    UE_TX = 4
};

enum class ApMacState : uint8_t
{
    AP_ESTIMATE = 0,
    AP_COLLECT = 1,
    AP_ASSOC_UPDATE = 2,
    AP_SCHEDULE = 3
};

enum class CpuControllerState : uint8_t
{
    CPU_AGGREGATE = 0,
    CPU_DECIDE = 1,
    CPU_DISPATCH = 2
};

inline uint32_t
GetTtiIndex(Time now, Time tti)
{
    if (!tti.IsStrictlyPositive())
    {
        return 0;
    }
    return static_cast<uint32_t>(now.GetNanoSeconds() / tti.GetNanoSeconds());
}

inline uint32_t
GetAssocEpoch(Time now, Time tAssoc)
{
    if (!tAssoc.IsStrictlyPositive())
    {
        return 0;
    }
    return static_cast<uint32_t>(now.GetNanoSeconds() / tAssoc.GetNanoSeconds());
}

inline std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE>
MakeInvalidClusterMembers()
{
    std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE> members{};
    members.fill(INVALID_AP_ID);
    return members;
}

inline std::array<UE_ID, CfMimoConstants::MAX_UE_PER_AP_SCHED>
MakeInvalidAssignedUes()
{
    std::array<UE_ID, CfMimoConstants::MAX_UE_PER_AP_SCHED> list{};
    list.fill(INVALID_UE_ID);
    return list;
}

} // namespace ns3

#endif // CF_MIMO_COMMON_H
