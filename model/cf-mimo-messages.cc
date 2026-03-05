/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-mimo-messages.h"

#include <algorithm>

namespace ns3
{

uint32_t
CountClusterMembers(const AssocConfirm& confirm)
{
    uint32_t count = 0;
    for (const auto apId : confirm.cluster_members)
    {
        if (apId != INVALID_AP_ID)
        {
            ++count;
        }
    }
    return count;
}

bool
HasClusterMember(const AssocConfirm& confirm, AP_ID apId)
{
    return std::find(confirm.cluster_members.begin(), confirm.cluster_members.end(), apId) !=
           confirm.cluster_members.end();
}

std::vector<AP_ID>
ToClusterMembers(const AssocConfirm& confirm)
{
    std::vector<AP_ID> members;
    members.reserve(confirm.cluster_members.size());
    for (const auto apId : confirm.cluster_members)
    {
        if (apId == INVALID_AP_ID)
        {
            continue;
        }
        members.push_back(apId);
    }
    return members;
}

uint64_t
BuildPrbBitmap(uint8_t startPrb, uint8_t prbCount)
{
    if (startPrb >= CfMimoConstants::MAX_PRB || prbCount == 0)
    {
        return 0;
    }

    const uint8_t cappedCount = std::min<uint8_t>(
        prbCount,
        static_cast<uint8_t>(CfMimoConstants::MAX_PRB - startPrb));

    uint64_t bitmap = 0;
    for (uint8_t i = 0; i < cappedCount; ++i)
    {
        bitmap |= (uint64_t{1} << (startPrb + i));
    }
    return bitmap;
}

} // namespace ns3
