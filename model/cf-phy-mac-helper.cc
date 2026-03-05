/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-phy-mac-helper.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace ns3
{

void
CfPhyMacHelper::BindUeMeasurementProvider(Ptr<CfUeMac> ueMac, Ptr<CfUePhy> uePhy, uint8_t topN)
{
    if (!ueMac)
    {
        return;
    }

    const uint8_t clampedTopN =
        std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::TOP_N_AP, topN));
    ueMac->SetLinkMeasurementProvider([uePhy, clampedTopN]() -> std::vector<UeApMeasurement> {
        if (!uePhy)
        {
            return {};
        }
        return uePhy->GetTopMeasurements(clampedTopN);
    });
}

void
CfPhyMacHelper::BindApGrantPath(Ptr<CfApMac> apMac,
                                Ptr<CfApPhy> apPhy,
                                const std::map<UE_ID, Ptr<CfUeMac>>& ueById,
                                const std::map<UE_ID, Ptr<CfUePhy>>& uePhyById)
{
    (void)apPhy;
    if (!apMac)
    {
        return;
    }

    const AP_ID apId = apMac->GetApId();
    apMac->SetGrantSink([apId, ueById, uePhyById](const SchedulingGrant& grant, UE_ID ueId) {
        const auto phyIt = uePhyById.find(ueId);
        if (phyIt != uePhyById.end() && phyIt->second)
        {
            phyIt->second->QueueUlGrant(grant, apId);
        }

        const auto it = ueById.find(ueId);
        if (it != ueById.end() && it->second)
        {
            it->second->ReceiveSchedulingGrant(grant);
        }
    });
}

void
CfPhyMacHelper::BindApGrantPath(Ptr<CfApMac> apMac,
                                Ptr<CfApPhy> apPhy,
                                Ptr<CfUeMac> ueMac,
                                Ptr<CfUePhy> uePhy)
{
    std::map<UE_ID, Ptr<CfUeMac>> ueById;
    std::map<UE_ID, Ptr<CfUePhy>> uePhyById;
    if (ueMac)
    {
        const UE_ID ueId = ueMac->GetUeId();
        ueById.emplace(ueId, ueMac);
        if (uePhy)
        {
            uePhyById.emplace(ueId, uePhy);
        }
    }
    BindApGrantPath(apMac, apPhy, ueById, uePhyById);
}

} // namespace ns3
