/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_MIMO_SPECTRUM_SIGNAL_PARAMETERS_H
#define CF_MIMO_SPECTRUM_SIGNAL_PARAMETERS_H

#include "cf-mimo-common.h"

#include "ns3/spectrum-signal-parameters.h"

namespace ns3
{

enum class CfSignalType : uint8_t
{
    DATA = 0,
    PILOT = 1
};

enum class CfLinkDirection : uint8_t
{
    DL = 0,
    UL = 1
};

struct CfMimoSpectrumSignalParameters : public SpectrumSignalParameters
{
    CfMimoSpectrumSignalParameters();
    ~CfMimoSpectrumSignalParameters() override;
    CfMimoSpectrumSignalParameters(const CfMimoSpectrumSignalParameters& p);

    Ptr<SpectrumSignalParameters> Copy() const override;

    CfSignalType signal_type{CfSignalType::DATA};
    CfLinkDirection link_direction{CfLinkDirection::DL};
    AP_ID tx_ap_id{INVALID_AP_ID};
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t tti_index{0};
    uint32_t assoc_epoch{0};
    uint16_t pilot_id{0};
    double tx_power_total_w{0.0};
    uint64_t prb_bitmap{0};
    uint8_t mcs{0};
    uint8_t tx_power_idx{0};
};

} // namespace ns3

#endif // CF_MIMO_SPECTRUM_SIGNAL_PARAMETERS_H
