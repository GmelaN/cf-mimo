/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-ap-mac.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CfApMac");
NS_OBJECT_ENSURE_REGISTERED(CfApMac);

TypeId
CfApMac::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfApMac")
                            .SetParent<Object>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfApMac>();
    return tid;
}

CfApMac::CfApMac() = default;

CfApMac::~CfApMac()
{
    NS_LOG_FUNCTION(this);
}

void
CfApMac::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Stop();
    Object::DoDispose();
}

void
CfApMac::SetApId(AP_ID apId)
{
    NS_LOG_FUNCTION(this << apId);
    m_apId = apId;
}

AP_ID
CfApMac::GetApId() const
{
    return m_apId;
}

void
CfApMac::SetMode(CfMode mode)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(mode));
    m_mode = mode;
}

CfMode
CfApMac::GetMode() const
{
    return m_mode;
}

void
CfApMac::SetIntervals(Time tti, Time tCoh, Time tState, Time tCoord, Time tAssoc)
{
    NS_LOG_FUNCTION(this << tti.GetNanoSeconds() << tCoh.GetNanoSeconds() << tState.GetNanoSeconds()
                         << tCoord.GetNanoSeconds() << tAssoc.GetNanoSeconds());
    m_tti = tti;
    m_tCoh = tCoh;
    m_tState = tState;
    m_tCoord = tCoord;
    m_tAssoc = tAssoc;
}

void
CfApMac::SetControlDelay(Time ctrlDelay)
{
    NS_LOG_FUNCTION(this << ctrlDelay.GetNanoSeconds());
    m_ctrlDelay = ctrlDelay;
}

void
CfApMac::SetScoringWeights(double w1, double w2, double w3)
{
    NS_LOG_FUNCTION(this << w1 << w2 << w3);
    m_w1 = w1;
    m_w2 = w2;
    m_w3 = w3;
}

void
CfApMac::SetScoreThreshold(int16_t scoreThresholdQ)
{
    NS_LOG_FUNCTION(this << scoreThresholdQ);
    m_scoreThresholdQ = scoreThresholdQ;
}

void
CfApMac::SetLoadLimit(uint16_t loadMaxQ)
{
    NS_LOG_FUNCTION(this << loadMaxQ);
    m_loadMaxQ = loadMaxQ;
}

void
CfApMac::SetMaxUePerSched(uint8_t maxUePerSched)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(maxUePerSched));
    m_maxUePerSched = std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::MAX_UE_PER_AP_SCHED, maxUePerSched));
}

void
CfApMac::SetOfferSink(OfferSink cb)
{
    NS_LOG_FUNCTION(this);
    m_offerSink = std::move(cb);
}

void
CfApMac::SetGrantSink(GrantSink cb)
{
    NS_LOG_FUNCTION(this);
    m_grantSink = std::move(cb);
}

void
CfApMac::SetNeighborCoordSink(NeighborCoordSink cb)
{
    NS_LOG_FUNCTION(this);
    m_neighborCoordSink = std::move(cb);
}

void
CfApMac::SetApSummarySink(ApSummarySink cb)
{
    NS_LOG_FUNCTION(this);
    m_apSummarySink = std::move(cb);
}

void
CfApMac::Start()
{
    NS_LOG_FUNCTION(this);
    if (m_running)
    {
        NS_LOG_DEBUG("AP " << m_apId << " Start() ignored: already running");
        return;
    }
    m_running = true;
    NS_LOG_DEBUG("AP " << m_apId << " MAC start");
    SchedulePeriodicEvents();
}

void
CfApMac::Stop()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        NS_LOG_DEBUG("AP " << m_apId << " Stop() ignored: not running");
        return;
    }
    m_running = false;
    NS_LOG_DEBUG("AP " << m_apId << " MAC stop");
    CancelEvents();
}

void
CfApMac::SchedulePeriodicEvents()
{
    NS_LOG_FUNCTION(this);
    m_cohEvent = Simulator::Schedule(m_tCoh, &CfApMac::OnCohTimer, this);
    m_coordEvent = Simulator::Schedule(m_tCoord, &CfApMac::OnCoordTimer, this);
    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfApMac::OnAssocTimer, this);
    m_ttiEvent = Simulator::Schedule(m_tti, &CfApMac::OnTtiTimer, this);
}

void
CfApMac::CancelEvents()
{
    NS_LOG_FUNCTION(this);
    m_cohEvent.Cancel();
    m_coordEvent.Cancel();
    m_assocEvent.Cancel();
    m_ttiEvent.Cancel();
}

void
CfApMac::OnCohTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    m_state = ApMacState::AP_ESTIMATE;
    UpdateLoadAndInterference();
    m_state = ApMacState::AP_COLLECT;
    m_cohEvent = Simulator::Schedule(m_tCoh, &CfApMac::OnCohTimer, this);
}

void
CfApMac::OnCoordTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    UpdateLoadAndInterference();
    if (m_neighborCoordSink)
    {
        NeighborCoord coord{};
        coord.src_ap = m_apId;
        coord.coord_time_tti = GetCurrentTtiIndex();
        coord.load_q = m_loadQ;
        coord.interf_q = m_interfQ;
        coord.served_ue_cnt =
            static_cast<uint8_t>(std::min<size_t>(255, m_clusterMapActive.size()));
        uint32_t hash = 0;
        for (const auto& [ueId, _] : m_clusterMapActive)
        {
            hash = (hash * 131u) ^ ueId;
        }
        coord.top_ue_hash = hash;

        if (m_neighborTable.empty())
        {
            m_neighborCoordSink(coord, INVALID_AP_ID);
        }
        else
        {
            for (const auto& [neighborAp, _] : m_neighborTable)
            {
                m_neighborCoordSink(coord, neighborAp);
            }
        }
    }
    m_coordEvent = Simulator::Schedule(m_tCoord, &CfApMac::OnCoordTimer, this);
}

void
CfApMac::OnAssocTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    m_state = ApMacState::AP_ASSOC_UPDATE;
    ApplyPendingAssignmentsIfDue();
    const uint32_t epoch = GetCurrentEpoch();

    if (m_mode == CfMode::DISTRIBUTED)
    {
        NS_LOG_DEBUG("AP " << m_apId << " distributed assoc update epoch=" << epoch);
        GenerateDistributedOffers(epoch);
    }
    else
    {
        NS_LOG_DEBUG("AP " << m_apId << " centralized summary epoch=" << epoch);
        BuildAndSendApSummary(epoch);
    }

    m_state = ApMacState::AP_COLLECT;
    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfApMac::OnAssocTimer, this);
}

void
CfApMac::OnTtiTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    m_state = ApMacState::AP_SCHEDULE;
    ApplyPendingAssignmentsIfDue();
    ScheduleTtiGrants(GetCurrentTtiIndex());
    m_state = ApMacState::AP_COLLECT;
    m_ttiEvent = Simulator::Schedule(m_tti, &CfApMac::OnTtiTimer, this);
}

void
CfApMac::ApplyPendingAssignmentsIfDue()
{
    NS_LOG_FUNCTION(this);
    const uint32_t epoch = GetCurrentEpoch();
    std::vector<UE_ID> applied;
    for (const auto& [ueId, assignment] : m_clusterMapPending)
    {
        if (epoch < assignment.validFromEpoch)
        {
            continue;
        }

        if (assignment.served)
        {
            m_clusterMapActive[ueId] = assignment;
            m_clusterVerActive = std::max(m_clusterVerActive, assignment.clusterVer);
            NS_LOG_DEBUG("AP " << m_apId << " activate UE " << ueId
                               << " clusterVer=" << assignment.clusterVer
                               << " role=" << static_cast<uint32_t>(assignment.role));
        }
        else
        {
            m_clusterMapActive.erase(ueId);
            NS_LOG_DEBUG("AP " << m_apId << " remove UE " << ueId << " from active cluster map");
        }
        applied.push_back(ueId);
    }

    for (const auto ueId : applied)
    {
        m_clusterMapPending.erase(ueId);
    }
    UpdateLoadAndInterference();
}

void
CfApMac::UpdateLoadAndInterference()
{
    NS_LOG_FUNCTION(this);
    const uint32_t activeUes = static_cast<uint32_t>(m_clusterMapActive.size());
    m_loadQ = static_cast<uint16_t>(std::min<uint32_t>(65535u, activeUes * 1000u));

    if (m_neighborTable.empty())
    {
        m_interfQ = 0;
        return;
    }

    uint32_t sum = 0;
    for (const auto& [_, coord] : m_neighborTable)
    {
        sum += coord.load_q;
    }
    m_interfQ = static_cast<uint16_t>(sum / m_neighborTable.size());
    NS_LOG_DEBUG("AP " << m_apId << " loadQ=" << m_loadQ << " interfQ=" << m_interfQ
                       << " activeUE=" << activeUes);
}

void
CfApMac::GenerateDistributedOffers(uint32_t assocEpoch)
{
    NS_LOG_FUNCTION(this << assocEpoch);
    if (!m_offerSink)
    {
        NS_LOG_WARN("AP " << m_apId << " has no OfferSink in distributed mode");
        return;
    }

    UpdateLoadAndInterference();
    for (const auto& [ueId, reportWithTs] : m_ueReports)
    {
        if (!IsReportFresh(reportWithTs))
        {
            NS_LOG_DEBUG("AP " << m_apId << " skip stale report ue=" << ueId);
            continue;
        }

        const int16_t scoreQ = ComputeOfferScoreQ(reportWithTs.report);
        if (scoreQ <= m_scoreThresholdQ || m_loadQ >= m_loadMaxQ)
        {
            NS_LOG_DEBUG("AP " << m_apId << " reject offer ue=" << ueId
                               << " scoreQ=" << scoreQ << " loadQ=" << m_loadQ);
            continue;
        }

        AssocOffer offer{};
        offer.ap_id = m_apId;
        offer.ue_id = ueId;
        offer.assoc_epoch = assocEpoch;
        offer.cluster_ver = m_clusterVerActive;
        offer.score_q = scoreQ;
        offer.load_q = m_loadQ;
        offer.max_prb_offer = CfMimoConstants::MAX_PRB;

        if (m_ctrlDelay.IsStrictlyPositive())
        {
            Simulator::Schedule(m_ctrlDelay, [cb = m_offerSink, offer, ueId]() { cb(offer, ueId); });
        }
        else
        {
            m_offerSink(offer, ueId);
        }
        NS_LOG_DEBUG("AP " << m_apId << " sent offer ue=" << ueId << " epoch=" << assocEpoch
                           << " scoreQ=" << scoreQ);
    }
}

void
CfApMac::BuildAndSendApSummary(uint32_t assocEpoch)
{
    NS_LOG_FUNCTION(this << assocEpoch);
    if (!m_apSummarySink)
    {
        NS_LOG_WARN("AP " << m_apId << " has no ApSummarySink in centralized mode");
        return;
    }

    struct Candidate
    {
        UE_ID ueId;
        int16_t metricQ;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(m_ueReports.size());
    for (const auto& [ueId, reportWithTs] : m_ueReports)
    {
        if (!IsReportFresh(reportWithTs))
        {
            continue;
        }
        candidates.push_back({ueId, ComputeOfferScoreQ(reportWithTs.report)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.metricQ > rhs.metricQ;
    });

    APSummary summary{};
    summary.ap_id = m_apId;
    summary.assoc_epoch = assocEpoch;
    summary.load_q = m_loadQ;
    summary.interf_q = m_interfQ;

    const uint8_t count =
        std::min<uint8_t>(m_maxUePerSched, static_cast<uint8_t>(candidates.size()));
    summary.cand_ue_cnt = count;
    for (uint8_t i = 0; i < count; ++i)
    {
        summary.cand_ue_list[i] = candidates[i].ueId;
        summary.cand_metric_q[i] = candidates[i].metricQ;
    }

    if (m_ctrlDelay.IsStrictlyPositive())
    {
        Simulator::Schedule(m_ctrlDelay, [cb = m_apSummarySink, summary]() { cb(summary); });
    }
    else
    {
        m_apSummarySink(summary);
    }
    NS_LOG_DEBUG("AP " << m_apId << " summary sent epoch=" << assocEpoch
                       << " candCnt=" << static_cast<uint32_t>(summary.cand_ue_cnt)
                       << " loadQ=" << summary.load_q << " interfQ=" << summary.interf_q);
}

void
CfApMac::ScheduleTtiGrants(uint32_t tti)
{
    NS_LOG_FUNCTION(this << tti);
    if (!m_grantSink || m_clusterMapActive.empty())
    {
        if (m_clusterMapActive.size() > 0 && !m_grantSink)
        {
            NS_LOG_WARN("AP " << m_apId << " has active UE map but no GrantSink");
        }
        return;
    }

    std::vector<UE_ID> schedUes;
    for (const auto& [ueId, assignment] : m_clusterMapActive)
    {
        if (!assignment.served)
        {
            continue;
        }
        if (m_ueQueueBytes[ueId] == 0)
        {
            continue;
        }
        schedUes.push_back(ueId);
    }

    std::sort(schedUes.begin(), schedUes.end(), [this](UE_ID lhs, UE_ID rhs) {
        return m_ueQueueBytes[lhs] > m_ueQueueBytes[rhs];
    });
    if (schedUes.size() > m_maxUePerSched)
    {
        schedUes.resize(m_maxUePerSched);
    }

    if (schedUes.empty())
    {
        NS_LOG_DEBUG("AP " << m_apId << " no schedulable UE at tti=" << tti);
        return;
    }

    uint64_t queueSum = 0;
    for (const auto ueId : schedUes)
    {
        queueSum += m_ueQueueBytes[ueId];
    }
    if (queueSum == 0)
    {
        return;
    }

    uint8_t prbCursor = 0;
    for (size_t i = 0; i < schedUes.size(); ++i)
    {
        const UE_ID ueId = schedUes[i];
        const uint8_t remainingUes = static_cast<uint8_t>(schedUes.size() - i);
        const uint8_t remainingPrbs = CfMimoConstants::MAX_PRB - prbCursor;
        if (remainingPrbs == 0)
        {
            break;
        }

        uint8_t prbShare = static_cast<uint8_t>(
            std::max<uint64_t>(1, (static_cast<uint64_t>(remainingPrbs) * m_ueQueueBytes[ueId]) / queueSum));
        if (remainingUes > 1)
        {
            prbShare = std::min<uint8_t>(prbShare, remainingPrbs - (remainingUes - 1));
        }
        else
        {
            prbShare = remainingPrbs;
        }

        SchedulingGrant grant{};
        grant.ue_id = ueId;
        grant.tti = tti;
        grant.mcs = std::min<uint8_t>(31, static_cast<uint8_t>(2 * FindCqiForSelf(m_ueReports[ueId].report)));
        grant.tx_power_idx = static_cast<uint8_t>(std::min<uint16_t>(255, 64 + m_interfQ / 512));
        grant.prb_bitmap = BuildPrbBitmap(prbCursor, prbShare);

        if (m_ctrlDelay.IsStrictlyPositive())
        {
            Simulator::Schedule(m_ctrlDelay, [cb = m_grantSink, grant, ueId]() { cb(grant, ueId); });
        }
        else
        {
            m_grantSink(grant, ueId);
        }
        NS_LOG_DEBUG("AP " << m_apId << " grant ue=" << ueId << " tti=" << tti
                           << " prbShare=" << static_cast<uint32_t>(prbShare)
                           << " mcs=" << static_cast<uint32_t>(grant.mcs)
                           << " pwrIdx=" << static_cast<uint32_t>(grant.tx_power_idx));

        prbCursor = static_cast<uint8_t>(prbCursor + prbShare);
    }
}

int16_t
CfApMac::ComputeOfferScoreQ(const UEStateReport& report) const
{
    NS_LOG_FUNCTION(this << report.ue_id << static_cast<uint32_t>(report.meas_cnt));
    const double cqiScore = static_cast<double>(FindCqiForSelf(report));
    const double score = m_w1 * cqiScore - m_w2 * static_cast<double>(m_loadQ) -
                         m_w3 * static_cast<double>(m_interfQ);
    const auto clamped = std::clamp(score, -32768.0, 32767.0);
    return static_cast<int16_t>(std::lround(clamped));
}

uint8_t
CfApMac::FindCqiForSelf(const UEStateReport& report) const
{
    NS_LOG_FUNCTION(this << report.ue_id);
    for (uint8_t i = 0; i < report.meas_cnt; ++i)
    {
        if (report.meas[i].ap_id == m_apId)
        {
            return report.meas[i].cqi;
        }
    }
    NS_LOG_DEBUG("AP " << m_apId << " CQI for UE " << report.ue_id
                       << " not found in report, fallback CQI=0");
    return 0;
}

bool
CfApMac::IsReportFresh(const ReportWithTs& reportEntry) const
{
    NS_LOG_FUNCTION(this);
    return Simulator::Now() - reportEntry.ts <= (2 * m_tState);
}

void
CfApMac::ReceiveUeStateReport(const UEStateReport& report)
{
    NS_LOG_FUNCTION(this << report.ue_id << report.report_time_tti);
    ReportWithTs rec{report, Simulator::Now()};
    m_ueReports[report.ue_id] = rec;
    m_ueQueueBytes[report.ue_id] = report.queue_bytes;
    NS_LOG_DEBUG("AP " << m_apId << " UEStateReport received ue=" << report.ue_id
                       << " queue=" << report.queue_bytes
                       << " measCnt=" << static_cast<uint32_t>(report.meas_cnt));
}

void
CfApMac::ReceiveAssocConfirm(const AssocConfirm& confirm)
{
    NS_LOG_FUNCTION(this << confirm.ue_id << confirm.assoc_epoch << confirm.cluster_ver);
    if (m_mode != CfMode::DISTRIBUTED || confirm.assoc_epoch != GetCurrentEpoch())
    {
        NS_LOG_WARN("AP " << m_apId << " ignored AssocConfirm ue=" << confirm.ue_id
                          << " epoch=" << confirm.assoc_epoch
                          << " nowEpoch=" << GetCurrentEpoch()
                          << " mode=" << static_cast<uint32_t>(m_mode));
        return;
    }

    Assignment pending{};
    pending.validFromEpoch = confirm.assoc_epoch + 1;
    pending.clusterVer = confirm.cluster_ver;
    pending.served = HasClusterMember(confirm, m_apId);
    if (pending.served)
    {
        pending.role =
            (confirm.primary_ap == m_apId) ? CfRoleFlag::PRIMARY : CfRoleFlag::SECONDARY;
    }
    m_clusterMapPending[confirm.ue_id] = pending;
    NS_LOG_DEBUG("AP " << m_apId << " pending assoc ue=" << confirm.ue_id
                       << " served=" << pending.served
                       << " role=" << static_cast<uint32_t>(pending.role)
                       << " applyEpoch=" << pending.validFromEpoch);
}

void
CfApMac::ReceiveClusterDecision(const ClusterDecision& decision)
{
    NS_LOG_FUNCTION(this << decision.ap_id << decision.assoc_epoch << decision.cluster_ver);
    if (m_mode != CfMode::CENTRALIZED || decision.ap_id != m_apId ||
        decision.assoc_epoch != GetCurrentEpoch())
    {
        NS_LOG_WARN("AP " << m_apId << " ignored ClusterDecision for ap=" << decision.ap_id
                          << " epoch=" << decision.assoc_epoch
                          << " nowEpoch=" << GetCurrentEpoch()
                          << " mode=" << static_cast<uint32_t>(m_mode));
        return;
    }

    std::set<UE_ID> assigned;
    for (uint8_t i = 0; i < decision.assigned_ue_cnt; ++i)
    {
        const UE_ID ueId = decision.assigned_ue_list[i];
        if (ueId == INVALID_UE_ID)
        {
            continue;
        }
        Assignment pending{};
        pending.served = true;
        pending.validFromEpoch = decision.assoc_epoch + 1;
        pending.clusterVer = decision.cluster_ver;
        pending.role = (decision.role_flags[i] == static_cast<uint8_t>(CfRoleFlag::PRIMARY))
                           ? CfRoleFlag::PRIMARY
                           : CfRoleFlag::SECONDARY;
        m_clusterMapPending[ueId] = pending;
        assigned.insert(ueId);
        NS_LOG_DEBUG("AP " << m_apId << " pending decision ue=" << ueId
                           << " role=" << static_cast<uint32_t>(pending.role)
                           << " applyEpoch=" << pending.validFromEpoch);
    }

    std::set<UE_ID> currentlyKnown;
    for (const auto& [ueId, _] : m_clusterMapActive)
    {
        currentlyKnown.insert(ueId);
    }
    for (const auto& [ueId, _] : m_clusterMapPending)
    {
        currentlyKnown.insert(ueId);
    }

    for (const auto ueId : currentlyKnown)
    {
        if (assigned.find(ueId) != assigned.end())
        {
            continue;
        }
        Assignment remove{};
        remove.served = false;
        remove.validFromEpoch = decision.assoc_epoch + 1;
        remove.clusterVer = decision.cluster_ver;
        m_clusterMapPending[ueId] = remove;
        NS_LOG_DEBUG("AP " << m_apId << " pending removal ue=" << ueId
                           << " applyEpoch=" << remove.validFromEpoch);
    }
}

void
CfApMac::ReceiveSrBsr(const SR_BSR& srBsr)
{
    NS_LOG_FUNCTION(this << srBsr.ue_id << srBsr.tti << srBsr.queue_bytes);
    m_ueQueueBytes[srBsr.ue_id] = srBsr.queue_bytes;
}

void
CfApMac::ReceiveRxStatus(const RxStatus& status)
{
    NS_LOG_FUNCTION(this << status.ue_id << status.tti << static_cast<uint32_t>(status.ok));
    m_lastRxStatus[status.ue_id] = status;
}

void
CfApMac::ReceiveNeighborCoord(const NeighborCoord& coord)
{
    NS_LOG_FUNCTION(this << coord.src_ap << coord.coord_time_tti);
    if (coord.src_ap == m_apId)
    {
        NS_LOG_DEBUG("AP " << m_apId << " ignored self NeighborCoord");
        return;
    }
    m_neighborTable[coord.src_ap] = coord;
    NS_LOG_DEBUG("AP " << m_apId << " NeighborCoord update from AP " << coord.src_ap
                       << " loadQ=" << coord.load_q << " interfQ=" << coord.interf_q);
}

uint16_t
CfApMac::GetLoadQ() const
{
    return m_loadQ;
}

uint16_t
CfApMac::GetInterfQ() const
{
    return m_interfQ;
}

ApMacState
CfApMac::GetState() const
{
    return m_state;
}

uint32_t
CfApMac::GetClusterVerActive() const
{
    return m_clusterVerActive;
}

uint32_t
CfApMac::GetCurrentEpoch() const
{
    return GetAssocEpoch(Simulator::Now(), m_tAssoc);
}

uint32_t
CfApMac::GetCurrentTtiIndex() const
{
    return GetTtiIndex(Simulator::Now(), m_tti);
}

} // namespace ns3
