/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_PHY_MAC_HELPER_H
#define CF_PHY_MAC_HELPER_H

#include "cf-ap-mac.h"
#include "cf-ap-phy.h"
#include "cf-mimo-common.h"
#include "cf-ue-mac.h"
#include "cf-ue-phy.h"

#include "ns3/ptr.h"

#include <map>

namespace ns3
{

class CfPhyMacHelper
{
  public:
    static void BindUeMeasurementProvider(Ptr<CfUeMac> ueMac,
                                          Ptr<CfUePhy> uePhy,
                                          uint8_t topN = CfMimoConstants::TOP_N_AP);

    static void BindApGrantPath(Ptr<CfApMac> apMac,
                                Ptr<CfApPhy> apPhy,
                                const std::map<UE_ID, Ptr<CfUeMac>>& ueById,
                                const std::map<UE_ID, Ptr<CfUePhy>>& uePhyById = {});

    static void BindApGrantPath(Ptr<CfApMac> apMac,
                                Ptr<CfApPhy> apPhy,
                                Ptr<CfUeMac> ueMac,
                                Ptr<CfUePhy> uePhy = nullptr);
};

} // namespace ns3

#endif // CF_PHY_MAC_HELPER_H
