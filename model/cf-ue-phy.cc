/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-ue-phy.h"

#include "cf-mimo-spectrum-signal-parameters.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/spectrum-value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CfUePhy");
NS_OBJECT_ENSURE_REGISTERED(CfUePhy);

namespace
{

double
DbmToW(double dbm)
{
    return std::pow(10.0, (dbm - 30.0) / 10.0);
}

double
WToDbm(double w)
{
    if (w <= 0.0)
    {
        return -std::numeric_limits<double>::infinity();
    }
    return 10.0 * std::log10(w / 1e-3);
}

int16_t
ToDbmQ10(double dbm)
{
    const double scaled = std::round(dbm * 10.0);
    if (scaled > std::numeric_limits<int16_t>::max())
    {
        return std::numeric_limits<int16_t>::max();
    }
    if (scaled < std::numeric_limits<int16_t>::min())
    {
        return std::numeric_limits<int16_t>::min();
    }
    return static_cast<int16_t>(scaled);
}

uint8_t
CountBits(uint64_t v)
{
    return static_cast<uint8_t>(__builtin_popcountll(v));
}

} // namespace

TypeId
CfUePhy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfUePhy")
                            .SetParent<SpectrumPhy>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfUePhy>();
    return tid;
}

CfUePhy::CfUePhy()
    : m_antenna(CreateObject<IsotropicAntennaModel>())
{
    EnsureSpectrumModel();
}

CfUePhy::~CfUePhy()
{
    NS_LOG_FUNCTION(this);
}

void
CfUePhy::DoDispose()
{
    for (auto& [_, window] : m_pilotWindows)
    {
        window.finalizeEvent.Cancel();
    }
    for (auto& [_, tx] : m_pendingUlTxByTti)
    {
        tx.txEvent.Cancel();
    }
    m_pilotWindows.clear();
    m_pendingUlTxByTti.clear();
    m_measurements.clear();
    m_channel = nullptr;
    m_mobility = nullptr;
    m_device = nullptr;
    m_antenna = nullptr;
    m_spectrumModel = nullptr;
    SpectrumPhy::DoDispose();
}

void
CfUePhy::SetUeId(UE_ID ueId)
{
    m_ueId = ueId;
}

UE_ID
CfUePhy::GetUeId() const
{
    return m_ueId;
}

void
CfUePhy::SetTti(Time tti)
{
    m_tti = tti;
}

void
CfUePhy::SetPrbConfig(uint8_t maxPrb, double prbBandwidthHz, double centerFrequencyHz)
{
    m_maxPrb = std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::MAX_PRB, maxPrb));
    m_prbBandwidthHz = prbBandwidthHz;
    m_centerFrequencyHz = centerFrequencyHz;
    m_spectrumModel = nullptr;
    EnsureSpectrumModel();
}

void
CfUePhy::SetNoiseModel(double thermalNoiseDbmPerHz, double noiseFigureDb)
{
    m_thermalNoiseDbmPerHz = thermalNoiseDbmPerHz;
    m_noiseFigureDb = noiseFigureDb;
}

void
CfUePhy::SetDataPowerDbmRange(double minDbm, double maxDbm)
{
    m_dataPowerMinDbm = std::min(minDbm, maxDbm);
    m_dataPowerMaxDbm = std::max(minDbm, maxDbm);
}

void
CfUePhy::SetCqiThresholdsDb(const std::array<double, 16>& thresholdsDb)
{
    m_cqiThresholdsDb = thresholdsDb;
}

void
CfUePhy::QueueUlGrant(const SchedulingGrant& grant, AP_ID targetApId)
{
    NS_LOG_FUNCTION(this << grant.ue_id << grant.tti << targetApId);
    if (grant.ue_id != m_ueId || targetApId == INVALID_AP_ID)
    {
        return;
    }

    auto& pending = m_pendingUlTxByTti[grant.tti];
    pending.grant = grant;
    pending.targetApId = targetApId;

    if (pending.txEvent.IsPending())
    {
        return;
    }

    const int64_t txNs = static_cast<int64_t>(grant.tti) * m_tti.GetNanoSeconds();
    Time txTime = NanoSeconds(std::max<int64_t>(0, txNs));
    Time delay = txTime - Simulator::Now();
    if (delay.IsNegative())
    {
        pending.txEvent = Simulator::ScheduleNow(&CfUePhy::OnUlTxTime, this, grant.tti);
        return;
    }
    if (!delay.IsStrictlyPositive())
    {
        pending.txEvent = Simulator::ScheduleNow(&CfUePhy::OnUlTxTime, this, grant.tti);
        return;
    }
    pending.txEvent = Simulator::Schedule(delay, &CfUePhy::OnUlTxTime, this, grant.tti);
}

void
CfUePhy::EnsureSpectrumModel()
{
    if (m_spectrumModel)
    {
        return;
    }

    std::vector<BandInfo> bands;
    bands.reserve(m_maxPrb);
    const double totalBwHz = m_prbBandwidthHz * m_maxPrb;
    const double baseFl = m_centerFrequencyHz - 0.5 * totalBwHz;
    for (uint8_t rb = 0; rb < m_maxPrb; ++rb)
    {
        BandInfo bi;
        bi.fl = baseFl + rb * m_prbBandwidthHz;
        bi.fh = bi.fl + m_prbBandwidthHz;
        bi.fc = 0.5 * (bi.fl + bi.fh);
        bands.push_back(bi);
    }
    m_spectrumModel = Create<SpectrumModel>(bands);
}

uint8_t
CfUePhy::QuantizeCqi(double sinrDb) const
{
    uint8_t cqi = 0;
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (sinrDb >= m_cqiThresholdsDb[i])
        {
            cqi = i;
        }
    }
    return cqi;
}

void
CfUePhy::FinalizePilotTti(uint32_t ttiIndex)
{
    const auto it = m_pilotWindows.find(ttiIndex);
    if (it == m_pilotWindows.end())
    {
        return;
    }

    const PilotWindow window = it->second;
    m_pilotWindows.erase(it);
    if (window.rsrpWByAp.empty())
    {
        return;
    }

    double totalPowerW = 0.0;
    for (const auto& [_, p] : window.rsrpWByAp)
    {
        totalPowerW += p;
    }

    const uint8_t activePrb = static_cast<uint8_t>(__builtin_popcountll(window.prbBitmap));
    const uint8_t nPrb = std::max<uint8_t>(activePrb, 1);
    const double noiseWPerHz = DbmToW(m_thermalNoiseDbmPerHz + m_noiseFigureDb);
    const double noiseW = noiseWPerHz * nPrb * m_prbBandwidthHz;

    for (const auto& [apId, signalW] : window.rsrpWByAp)
    {
        const double interfW = std::max(0.0, totalPowerW - signalW);
        const double denom = std::max(1e-30, interfW + noiseW);
        const double sinrLin = signalW / denom;
        const double sinrDb = 10.0 * std::log10(sinrLin);

        Measurement m{};
        m.meas.ap_id = apId;
        m.meas.cqi = QuantizeCqi(sinrDb);
        m.meas.sinr_q = static_cast<int16_t>(std::round(sinrDb * 10.0));
        m.meas.rsrp_q = ToDbmQ10(WToDbm(signalW));
        m.sinrLin = sinrLin;
        m.sinrDb = sinrDb;
        m.lastMeasTime = Simulator::Now();
        m_measurements[apId] = m;
    }
}

Ptr<SpectrumValue>
CfUePhy::BuildPsdFromBitmap(uint64_t bitmap, double totalPowerW) const
{
    auto psd = Create<SpectrumValue>(m_spectrumModel);
    const uint8_t activeRb = CountBits(bitmap);
    if (activeRb == 0 || totalPowerW <= 0.0)
    {
        return psd;
    }

    const double rbPowerW = totalPowerW / activeRb;
    const double rbPsdWHz = rbPowerW / m_prbBandwidthHz;
    for (uint8_t rb = 0; rb < m_maxPrb; ++rb)
    {
        if (((bitmap >> rb) & 0x1ULL) == 0)
        {
            continue;
        }
        (*psd)[rb] = rbPsdWHz;
    }
    return psd;
}

void
CfUePhy::OnUlTxTime(uint32_t ttiIndex)
{
    const auto it = m_pendingUlTxByTti.find(ttiIndex);
    if (it == m_pendingUlTxByTti.end())
    {
        return;
    }
    const PendingUlTx pending = it->second;
    m_pendingUlTxByTti.erase(it);

    const uint32_t nowTti = GetTtiIndex(Simulator::Now(), m_tti);
    if (ttiIndex + 1u < nowTti)
    {
        NS_LOG_WARN("UE PHY " << m_ueId << " drops stale UL grant tti=" << ttiIndex
                              << " nowTti=" << nowTti);
        return;
    }
    TransmitUlData(pending.grant, pending.targetApId);
}

void
CfUePhy::TransmitUlData(const SchedulingGrant& grant, AP_ID targetApId)
{
    if (!m_channel || !m_mobility)
    {
        return;
    }
    EnsureSpectrumModel();

    const double ratio = static_cast<double>(grant.tx_power_idx) / 255.0;
    const double txDbm = m_dataPowerMinDbm + ratio * (m_dataPowerMaxDbm - m_dataPowerMinDbm);
    const double txW = DbmToW(txDbm);

    auto params = Create<CfMimoSpectrumSignalParameters>();
    params->signal_type = CfSignalType::DATA;
    params->link_direction = CfLinkDirection::UL;
    params->tx_ap_id = targetApId;
    params->ue_id = m_ueId;
    params->tti_index = grant.tti;
    params->prb_bitmap = grant.prb_bitmap;
    params->mcs = grant.mcs;
    params->tx_power_idx = grant.tx_power_idx;
    params->tx_power_total_w = txW;
    params->psd = BuildPsdFromBitmap(grant.prb_bitmap, txW);
    params->duration = m_tti;
    params->txPhy = this;
    params->txAntenna = m_antenna;
    params->txMobility = m_mobility;
    m_channel->StartTx(params);
}

std::vector<UeApMeasurement>
CfUePhy::GetTopMeasurements(uint8_t topN) const
{
    std::vector<UeApMeasurement> values;
    values.reserve(m_measurements.size());
    for (const auto& [_, m] : m_measurements)
    {
        values.push_back(m.meas);
    }
    std::sort(values.begin(), values.end(), [](const UeApMeasurement& lhs, const UeApMeasurement& rhs) {
        if (lhs.rsrp_q != rhs.rsrp_q)
        {
            return lhs.rsrp_q > rhs.rsrp_q;
        }
        if (lhs.sinr_q != rhs.sinr_q)
        {
            return lhs.sinr_q > rhs.sinr_q;
        }
        return lhs.cqi > rhs.cqi;
    });

    if (values.size() > topN)
    {
        values.resize(topN);
    }
    return values;
}

bool
CfUePhy::GetMeasurement(AP_ID apId, UeApMeasurement* out) const
{
    const auto it = m_measurements.find(apId);
    if (it == m_measurements.end())
    {
        return false;
    }
    if (out)
    {
        *out = it->second.meas;
    }
    return true;
}

void
CfUePhy::SetChannel(Ptr<SpectrumChannel> c)
{
    m_channel = c;
}

void
CfUePhy::SetMobility(Ptr<MobilityModel> m)
{
    m_mobility = m;
}

void
CfUePhy::SetDevice(Ptr<NetDevice> d)
{
    m_device = d;
}

Ptr<MobilityModel>
CfUePhy::GetMobility() const
{
    return m_mobility;
}

Ptr<NetDevice>
CfUePhy::GetDevice() const
{
    return m_device;
}

Ptr<const SpectrumModel>
CfUePhy::GetRxSpectrumModel() const
{
    return m_spectrumModel;
}

Ptr<Object>
CfUePhy::GetAntenna() const
{
    return m_antenna;
}

void
CfUePhy::StartRx(Ptr<SpectrumSignalParameters> params)
{
    EnsureSpectrumModel();

    auto cfParams = DynamicCast<CfMimoSpectrumSignalParameters>(params);
    if (!cfParams)
    {
        return;
    }
    if (cfParams->signal_type != CfSignalType::PILOT ||
        cfParams->link_direction != CfLinkDirection::DL || !cfParams->psd)
    {
        return;
    }

    const uint64_t bitmap = cfParams->prb_bitmap;
    double rsrpW = 0.0;
    for (uint8_t rb = 0; rb < m_maxPrb; ++rb)
    {
        if (((bitmap >> rb) & 0x1ULL) == 0)
        {
            continue;
        }
        rsrpW += (*cfParams->psd)[rb] * m_prbBandwidthHz;
    }

    auto& window = m_pilotWindows[cfParams->tti_index];
    window.rsrpWByAp[cfParams->tx_ap_id] += rsrpW;
    window.prbBitmap |= bitmap;
    if (window.finalizeEvent.IsPending())
    {
        return;
    }
    window.finalizeEvent = Simulator::Schedule(cfParams->duration,
                                               &CfUePhy::FinalizePilotTti,
                                               this,
                                               cfParams->tti_index);
}

} // namespace ns3
