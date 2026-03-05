/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-mimo-spectrum-signal-parameters.h"

namespace ns3
{

CfMimoSpectrumSignalParameters::CfMimoSpectrumSignalParameters() = default;

CfMimoSpectrumSignalParameters::~CfMimoSpectrumSignalParameters() = default;

CfMimoSpectrumSignalParameters::CfMimoSpectrumSignalParameters(
    const CfMimoSpectrumSignalParameters& p)
    : SpectrumSignalParameters(p),
      signal_type(p.signal_type),
      link_direction(p.link_direction),
      tx_ap_id(p.tx_ap_id),
      ue_id(p.ue_id),
      tti_index(p.tti_index),
      assoc_epoch(p.assoc_epoch),
      pilot_id(p.pilot_id),
      tx_power_total_w(p.tx_power_total_w),
      prb_bitmap(p.prb_bitmap),
      mcs(p.mcs),
      tx_power_idx(p.tx_power_idx)
{
}

Ptr<SpectrumSignalParameters>
CfMimoSpectrumSignalParameters::Copy() const
{
    return Create<CfMimoSpectrumSignalParameters>(*this);
}

} // namespace ns3
