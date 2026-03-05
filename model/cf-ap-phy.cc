/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-ap-phy.h"

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

NS_LOG_COMPONENT_DEFINE("CfApPhy");
NS_OBJECT_ENSURE_REGISTERED(CfApPhy);

namespace
{

double
DbmToW(double dbm)
{
    return std::pow(10.0, (dbm - 30.0) / 10.0);
}

uint8_t
CountBits(uint64_t v)
{
    return static_cast<uint8_t>(__builtin_popcountll(v));
}

} // namespace

TypeId
CfApPhy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfApPhy")
                            .SetParent<SpectrumPhy>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfApPhy>();
    return tid;
}

CfApPhy::CfApPhy()
    : m_antenna(CreateObject<IsotropicAntennaModel>())
{
    EnsureSpectrumModel();
}

CfApPhy::~CfApPhy()
{
    NS_LOG_FUNCTION(this);
}

void
CfApPhy::DoDispose()
{
    Stop();
    for (auto& [_, window] : m_dataWindows)
    {
        window.finalizeEvent.Cancel();
    }
    m_dataWindows.clear();
    m_lastUlRxByUe.clear();
    m_pendingGrantByTti.clear();
    m_ulRxSink = nullptr;
    m_channel = nullptr;
    m_mobility = nullptr;
    m_device = nullptr;
    m_antenna = nullptr;
    m_spectrumModel = nullptr;
    SpectrumPhy::DoDispose();
}

void
CfApPhy::SetApId(AP_ID apId)
{
    m_apId = apId;
}

AP_ID
CfApPhy::GetApId() const
{
    return m_apId;
}

void
CfApPhy::SetTti(Time tti)
{
    m_tti = tti;
}

void
CfApPhy::SetCoherenceTime(Time tCoh)
{
    m_tCoh = tCoh;
}

void
CfApPhy::SetPrbConfig(uint8_t maxPrb, double prbBandwidthHz, double centerFrequencyHz)
{
    m_maxPrb = std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::MAX_PRB, maxPrb));
    m_prbBandwidthHz = prbBandwidthHz;
    m_centerFrequencyHz = centerFrequencyHz;
    m_spectrumModel = nullptr;
    EnsureSpectrumModel();
}

void
CfApPhy::SetPilotPowerW(double pilotPowerW)
{
    m_pilotPowerW = std::max(0.0, pilotPowerW);
}

void
CfApPhy::SetPilotPrbBitmap(uint64_t pilotPrbBitmap)
{
    m_pilotPrbBitmap = pilotPrbBitmap;
}

void
CfApPhy::SetDataPowerDbmRange(double minDbm, double maxDbm)
{
    m_dataPowerMinDbm = std::min(minDbm, maxDbm);
    m_dataPowerMaxDbm = std::max(minDbm, maxDbm);
}

void
CfApPhy::SetNoiseModel(double thermalNoiseDbmPerHz, double noiseFigureDb)
{
    m_thermalNoiseDbmPerHz = thermalNoiseDbmPerHz;
    m_noiseFigureDb = noiseFigureDb;
}

void
CfApPhy::SetEpochProvider(EpochProvider cb)
{
    m_epochProvider = std::move(cb);
}

void
CfApPhy::SetUlRxSink(UlRxSink cb)
{
    m_ulRxSink = std::move(cb);
}

void
CfApPhy::QueueSchedulingGrant(const SchedulingGrant& grant)
{
    m_pendingGrantByTti[grant.tti] = grant;
}

bool
CfApPhy::GetLastUlRx(UE_ID ueId, UlRxResult* out) const
{
    const auto it = m_lastUlRxByUe.find(ueId);
    if (it == m_lastUlRxByUe.end())
    {
        return false;
    }
    if (out)
    {
        *out = it->second;
    }
    return true;
}

void
CfApPhy::Start()
{
    if (m_running)
    {
        return;
    }
    m_running = true;
    m_ttiEvent = Simulator::ScheduleNow(&CfApPhy::OnTtiTick, this);
}

void
CfApPhy::Stop()
{
    if (!m_running)
    {
        return;
    }
    m_running = false;
    m_ttiEvent.Cancel();
}

bool
CfApPhy::IsPilotTti(uint32_t ttiIndex) const
{
    const uint64_t ttiNs = static_cast<uint64_t>(m_tti.GetNanoSeconds());
    if (ttiNs == 0)
    {
        return false;
    }
    const uint64_t cohTtis = static_cast<uint64_t>(m_tCoh.GetNanoSeconds()) / ttiNs;
    if (cohTtis == 0)
    {
        return false;
    }
    return (ttiIndex % cohTtis) == 0;
}

void
CfApPhy::OnTtiTick()
{
    if (!m_running)
    {
        return;
    }

    const uint32_t tti = GetTtiIndex(Simulator::Now(), m_tti);
    if (IsPilotTti(tti))
    {
        TransmitPilot(tti);
    }
    else
    {
        const auto it = m_pendingGrantByTti.find(tti);
        if (it != m_pendingGrantByTti.end())
        {
            TransmitData(it->second);
            m_pendingGrantByTti.erase(it);
        }
    }
    m_ttiEvent = Simulator::Schedule(m_tti, &CfApPhy::OnTtiTick, this);
}

void
CfApPhy::EnsureSpectrumModel()
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

Ptr<SpectrumValue>
CfApPhy::BuildPsdFromBitmap(uint64_t bitmap, double totalPowerW) const
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
CfApPhy::TransmitPilot(uint32_t ttiIndex)
{
    if (!m_channel || !m_mobility)
    {
        return;
    }
    EnsureSpectrumModel();

    auto params = Create<CfMimoSpectrumSignalParameters>();
    params->signal_type = CfSignalType::PILOT;
    params->link_direction = CfLinkDirection::DL;
    params->tx_ap_id = m_apId;
    params->tti_index = ttiIndex;
    params->assoc_epoch = m_epochProvider ? m_epochProvider() : 0;
    params->prb_bitmap = m_pilotPrbBitmap;
    params->tx_power_total_w = m_pilotPowerW;
    params->psd = BuildPsdFromBitmap(m_pilotPrbBitmap, m_pilotPowerW);
    params->duration = m_tti;
    params->txPhy = this;
    params->txAntenna = m_antenna;
    params->txMobility = m_mobility;
    m_channel->StartTx(params);
}

void
CfApPhy::TransmitData(const SchedulingGrant& grant)
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
    params->link_direction = CfLinkDirection::DL;
    params->tx_ap_id = m_apId;
    params->ue_id = grant.ue_id;
    params->tti_index = grant.tti;
    params->assoc_epoch = m_epochProvider ? m_epochProvider() : 0;
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

void
CfApPhy::SetChannel(Ptr<SpectrumChannel> c)
{
    m_channel = c;
}

void
CfApPhy::SetMobility(Ptr<MobilityModel> m)
{
    m_mobility = m;
}

void
CfApPhy::SetDevice(Ptr<NetDevice> d)
{
    m_device = d;
}

Ptr<MobilityModel>
CfApPhy::GetMobility() const
{
    return m_mobility;
}

Ptr<NetDevice>
CfApPhy::GetDevice() const
{
    return m_device;
}

Ptr<const SpectrumModel>
CfApPhy::GetRxSpectrumModel() const
{
    return m_spectrumModel;
}

Ptr<Object>
CfApPhy::GetAntenna() const
{
    return m_antenna;
}

void
CfApPhy::StartRx(Ptr<SpectrumSignalParameters> params)
{
    EnsureSpectrumModel();

    auto cfParams = DynamicCast<CfMimoSpectrumSignalParameters>(params);
    if (!cfParams || cfParams->signal_type != CfSignalType::DATA ||
        cfParams->link_direction != CfLinkDirection::UL || !cfParams->psd)
    {
        return;
    }

    const uint64_t bitmap = cfParams->prb_bitmap;
    double rxPowerW = 0.0;
    for (uint8_t rb = 0; rb < m_maxPrb; ++rb)
    {
        if (((bitmap >> rb) & 0x1ULL) == 0)
        {
            continue;
        }
        rxPowerW += (*cfParams->psd)[rb] * m_prbBandwidthHz;
    }

    auto& window = m_dataWindows[cfParams->tti_index];
    window.rxPowerWByUe[cfParams->ue_id] += rxPowerW;
    window.prbBitmapByUe[cfParams->ue_id] |= bitmap;
    window.mcsByUe[cfParams->ue_id] = std::max(window.mcsByUe[cfParams->ue_id], cfParams->mcs);
    if (cfParams->tx_ap_id == m_apId || cfParams->tx_ap_id == INVALID_AP_ID)
    {
        window.decodeCandidateByUe[cfParams->ue_id] = true;
    }

    if (window.finalizeEvent.IsPending())
    {
        return;
    }
    window.finalizeEvent = Simulator::Schedule(cfParams->duration,
                                               &CfApPhy::FinalizeDataTti,
                                               this,
                                               cfParams->tti_index);
}

void
CfApPhy::FinalizeDataTti(uint32_t ttiIndex)
{
    const auto it = m_dataWindows.find(ttiIndex);
    if (it == m_dataWindows.end())
    {
        return;
    }
    const DataWindow window = it->second;
    m_dataWindows.erase(it);
    if (window.rxPowerWByUe.empty())
    {
        return;
    }

    double totalPowerW = 0.0;
    for (const auto& [_, p] : window.rxPowerWByUe)
    {
        totalPowerW += p;
    }

    for (const auto& [ueId, signalW] : window.rxPowerWByUe)
    {
        const auto candidateIt = window.decodeCandidateByUe.find(ueId);
        if (candidateIt == window.decodeCandidateByUe.end() || !candidateIt->second)
        {
            continue;
        }

        const auto prbIt = window.prbBitmapByUe.find(ueId);
        const uint64_t prbBitmap = (prbIt != window.prbBitmapByUe.end()) ? prbIt->second : 0;
        const uint8_t activePrb = std::max<uint8_t>(1, CountBits(prbBitmap));
        const double noiseWPerHz = DbmToW(m_thermalNoiseDbmPerHz + m_noiseFigureDb);
        const double noiseW = noiseWPerHz * activePrb * m_prbBandwidthHz;
        const double interfW = std::max(0.0, totalPowerW - signalW);
        const double denom = std::max(1e-30, interfW + noiseW);
        const double sinrDb = 10.0 * std::log10(signalW / denom);
        const auto mcsIt = window.mcsByUe.find(ueId);
        const uint8_t mcs = (mcsIt != window.mcsByUe.end()) ? mcsIt->second : 0;

        UlRxResult result{};
        result.ueId = ueId;
        result.apId = m_apId;
        result.tti = ttiIndex;
        result.mcs = mcs;
        result.prbBitmap = prbBitmap;
        result.sinrDb = sinrDb;
        result.ok = (sinrDb >= GetMcsThresholdDb(mcs));
        result.rxBytes = result.ok ? EstimateTbBytes(prbBitmap, mcs) : 0u;

        m_lastUlRxByUe[ueId] = result;
        NS_LOG_DEBUG("AP " << m_apId << " UL rx ue=" << ueId << " tti=" << ttiIndex
                           << " sinrDb=" << sinrDb << " mcs=" << static_cast<uint32_t>(mcs)
                           << " bytes=" << result.rxBytes << " ok=" << result.ok);
        if (m_ulRxSink)
        {
            m_ulRxSink(result);
        }
    }
}

uint32_t
CfApPhy::EstimateTbBytes(uint64_t prbBitmap, uint8_t mcs) const
{
    const uint32_t prbCount = static_cast<uint32_t>(CountBits(prbBitmap));
    return prbCount * (static_cast<uint32_t>(mcs) + 1u) * 4u;
}

double
CfApPhy::GetMcsThresholdDb(uint8_t mcs) const
{
    // Simple monotonic threshold model for UL decode abstraction.
    return -7.0 + 0.75 * static_cast<double>(mcs);
}

} // namespace ns3
