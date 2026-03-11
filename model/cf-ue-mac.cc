/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-ue-mac.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CfUeMac");
NS_OBJECT_ENSURE_REGISTERED(CfUeMac);

TypeId
CfUeMac::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfUeMac")
                            .SetParent<Object>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfUeMac>();
    return tid;
}

CfUeMac::CfUeMac() = default;

CfUeMac::~CfUeMac()
{
    NS_LOG_FUNCTION(this);
}

void
CfUeMac::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Stop();
    Object::DoDispose();
}

void
CfUeMac::SetUeId(UE_ID ueId)
{
    NS_LOG_FUNCTION(this << ueId);
    m_ueId = ueId;
}

UE_ID
CfUeMac::GetUeId() const
{
    return m_ueId;
}

void
CfUeMac::SetMode(CfMode mode)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(mode));
    m_mode = mode;
}

CfMode
CfUeMac::GetMode() const
{
    return m_mode;
}

void
CfUeMac::SetIntervals(Time tti, Time tState, Time tAssoc)
{
    NS_LOG_FUNCTION(this << tti.GetNanoSeconds() << tState.GetNanoSeconds() << tAssoc.GetNanoSeconds());
    m_tti = tti;
    m_tState = tState;
    m_tAssoc = tAssoc;
}

void
CfUeMac::SetOfferWindow(Time offerWindow)
{
    NS_LOG_FUNCTION(this << offerWindow.GetNanoSeconds());
    m_offerWindow = offerWindow;
}

void
CfUeMac::SetAssocAckDeadline(Time ackDeadline)
{
    NS_LOG_FUNCTION(this << ackDeadline.GetNanoSeconds());
    m_assocAckDeadline = ackDeadline;
}

void
CfUeMac::SetTopNAp(uint8_t topNAp)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(topNAp));
    m_topNAp = std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::TOP_N_AP, topNAp));
}

void
CfUeMac::SetMaxClusterSize(uint8_t maxClusterSize)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(maxClusterSize));
    m_maxClusterSize =
        std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::MAX_CLUSTER_SIZE, maxClusterSize));
}

void
CfUeMac::SetLambda(double lambda)
{
    NS_LOG_FUNCTION(this << lambda);
    m_lambda = std::max(0.0, lambda);
}

void
CfUeMac::SetLoadRejectThreshold(uint16_t loadThresholdQ)
{
    NS_LOG_FUNCTION(this << loadThresholdQ);
    m_loadRejectThresholdQ = loadThresholdQ;
}

void
CfUeMac::SetQueueBytes(uint32_t queueBytes)
{
    NS_LOG_FUNCTION(this << queueBytes);
    m_queueBytes = queueBytes;
}

uint32_t
CfUeMac::GetQueueBytes() const
{
    return m_queueBytes;
}

void
CfUeMac::SetTrafficClass(uint8_t trafficClass)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(trafficClass));
    m_trafficClass = trafficClass;
}

void
CfUeMac::SetStateReportSink(StateReportSink cb)
{
    NS_LOG_FUNCTION(this);
    m_stateReportSink = std::move(cb);
}

void
CfUeMac::SetAssocConfirmSink(AssocConfirmSink cb)
{
    NS_LOG_FUNCTION(this);
    m_assocConfirmSink = std::move(cb);
}

void
CfUeMac::SetDistributedClusterPolicy(DistributedClusterPolicy policy)
{
    NS_LOG_FUNCTION(this);
    m_distributedClusterPolicy = std::move(policy);
}

void
CfUeMac::SetSrBsrSink(SrBsrSink cb)
{
    NS_LOG_FUNCTION(this);
    m_srBsrSink = std::move(cb);
}

void
CfUeMac::SetRxStatusSink(RxStatusSink cb)
{
    NS_LOG_FUNCTION(this);
    m_rxStatusSink = std::move(cb);
}

void
CfUeMac::SetLinkMeasurementProvider(LinkMeasurementProvider cb)
{
    NS_LOG_FUNCTION(this);
    m_linkMeasurementProvider = std::move(cb);
}

void
CfUeMac::Start()
{
    NS_LOG_FUNCTION(this);
    if (m_running)
    {
        NS_LOG_DEBUG("UE " << m_ueId << " Start() ignored: already running");
        return;
    }
    m_running = true;
    NS_LOG_DEBUG("UE " << m_ueId << " MAC start");
    SchedulePeriodicEvents();
}

void
CfUeMac::Stop()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        NS_LOG_DEBUG("UE " << m_ueId << " Stop() ignored: not running");
        return;
    }
    m_running = false;
    NS_LOG_DEBUG("UE " << m_ueId << " MAC stop");
    CancelEvents();
}

void
CfUeMac::SchedulePeriodicEvents()
{
    NS_LOG_FUNCTION(this);
    m_stateReportEvent = Simulator::Schedule(m_tState, &CfUeMac::OnStateReportTimer, this);
    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfUeMac::OnAssocTimer, this);
    m_ttiEvent = Simulator::Schedule(m_tti, &CfUeMac::OnTtiTimer, this);
}

void
CfUeMac::CancelEvents()
{
    NS_LOG_FUNCTION(this);
    m_stateReportEvent.Cancel();
    m_assocEvent.Cancel();
    m_assocDecisionEvent.Cancel();
    m_assocAckDeadlineEvent.Cancel();
    m_ttiEvent.Cancel();
}

void
CfUeMac::OnStateReportTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    ApplyPendingClusterIfDue();
    m_state = UeMacState::UE_REPORT;
    BuildAndSendStateReport();
    m_state = UeMacState::UE_TRACK;
    m_stateReportEvent = Simulator::Schedule(m_tState, &CfUeMac::OnStateReportTimer, this);
}

void
CfUeMac::OnAssocTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    ApplyPendingClusterIfDue();

    if (m_mode == CfMode::DISTRIBUTED)
    {
        if (m_assocAttempt.active)
        {
            RollbackAssocAttempt(static_cast<uint8_t>(CfRejectReason::TIMEOUT));
        }
        m_state = UeMacState::UE_ASSOC_WAIT;
        m_offerEpoch = GetCurrentEpoch();
        m_offerBuffer.clear();
        NS_LOG_DEBUG("UE " << m_ueId << " entering offer window for epoch " << m_offerEpoch);
        m_assocDecisionEvent =
            Simulator::Schedule(m_offerWindow, &CfUeMac::OnAssocDecisionTimer, this, m_offerEpoch);
    }
    else
    {
        m_state = UeMacState::UE_ASSOC_ACTIVE;
    }

    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfUeMac::OnAssocTimer, this);
}

void
CfUeMac::OnAssocDecisionTimer(uint32_t assocEpoch)
{
    NS_LOG_FUNCTION(this << assocEpoch);
    if (!m_running || m_mode != CfMode::DISTRIBUTED)
    {
        NS_LOG_WARN("UE " << m_ueId << " assoc decision ignored (running=" << m_running
                          << ", mode=" << static_cast<uint32_t>(m_mode) << ")");
        return;
    }
    DecideDistributedCluster(assocEpoch);
    m_state = UeMacState::UE_ASSOC_ACTIVE;
}

void
CfUeMac::OnAssocAckDeadline(uint32_t assocEpoch, uint32_t clusterVer)
{
    NS_LOG_FUNCTION(this << assocEpoch << clusterVer);
    if (!m_assocAttempt.active)
    {
        return;
    }
    if (m_assocAttempt.assocEpoch != assocEpoch || m_assocAttempt.clusterVer != clusterVer)
    {
        return;
    }
    RollbackAssocAttempt(static_cast<uint8_t>(CfRejectReason::TIMEOUT));
}

void
CfUeMac::OnTtiTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    ApplyPendingClusterIfDue();
    m_state = UeMacState::UE_TX;
    SendSrBsr();
    ConsumeGrant();
    m_state = UeMacState::UE_TRACK;

    m_ttiEvent = Simulator::Schedule(m_tti, &CfUeMac::OnTtiTimer, this);
}

std::vector<UeApMeasurement>
CfUeMac::GetSortedMeasurements() const
{
    NS_LOG_FUNCTION(this);
    std::vector<UeApMeasurement> sorted =
        m_linkMeasurementProvider ? m_linkMeasurementProvider() : m_measurementCache;
    std::sort(sorted.begin(),
              sorted.end(),
              [](const UeApMeasurement& lhs, const UeApMeasurement& rhs) {
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
    return sorted;
}

void
CfUeMac::BuildAndSendStateReport()
{
    NS_LOG_FUNCTION(this);
    auto sorted = GetSortedMeasurements();
    const uint8_t measCount = std::min<uint8_t>(m_topNAp, static_cast<uint8_t>(sorted.size()));
    if (measCount == 0)
    {
        NS_LOG_WARN("UE " << m_ueId << " has no measurements for UEStateReport");
    }

    UEStateReport report{};
    report.ue_id = m_ueId;
    report.report_time_tti = GetCurrentTtiIndex();
    report.queue_bytes = m_queueBytes;
    report.traffic_class = m_trafficClass;
    report.meas_cnt = measCount;
    for (uint8_t i = 0; i < measCount; ++i)
    {
        report.meas[i] = sorted[i];
    }

    m_measurementCache = sorted;
    NS_LOG_DEBUG("UE " << m_ueId << " UEStateReport tti=" << report.report_time_tti
                       << " meas=" << static_cast<uint32_t>(report.meas_cnt)
                       << " queue=" << report.queue_bytes);

    if (!m_stateReportSink)
    {
        NS_LOG_WARN("UE " << m_ueId << " has no StateReportSink");
        return;
    }
    for (uint8_t i = 0; i < measCount; ++i)
    {
        if (report.meas[i].ap_id == INVALID_AP_ID)
        {
            continue;
        }
        m_stateReportSink(report, report.meas[i].ap_id);
    }
}

void
CfUeMac::DecideDistributedCluster(uint32_t assocEpoch)
{
    NS_LOG_FUNCTION(this << assocEpoch);
    std::vector<AssocOffer> offers;
    offers.reserve(m_offerBuffer.size());
    for (const auto& [_, offer] : m_offerBuffer)
    {
        if (offer.ue_id != m_ueId || offer.assoc_epoch != assocEpoch)
        {
            continue;
        }
        offers.push_back(offer);
    }

    AP_ID primaryAp = INVALID_AP_ID;
    std::vector<AP_ID> members;
    members.reserve(m_maxClusterSize);
    const auto sortedMeasurements = GetSortedMeasurements();
    if (m_distributedClusterPolicy)
    {
        members = m_distributedClusterPolicy(
            offers, sortedMeasurements, primaryAp, m_maxClusterSize, m_lambda, m_loadRejectThresholdQ);
    }
    else
    {
        double bestPrimaryMetric = -1e18;
        for (const auto& offer : offers)
        {
            const double metric = static_cast<double>(offer.score_q) -
                                  m_lambda * static_cast<double>(offer.load_q);
            if (metric > bestPrimaryMetric)
            {
                bestPrimaryMetric = metric;
                primaryAp = offer.ap_id;
            }
        }

        if (primaryAp != INVALID_AP_ID)
        {
            members.push_back(primaryAp);
        }

        std::sort(offers.begin(), offers.end(), [](const AssocOffer& lhs, const AssocOffer& rhs) {
            return lhs.score_q > rhs.score_q;
        });

        for (const auto& offer : offers)
        {
            if (members.size() >= m_maxClusterSize)
            {
                break;
            }
            if (offer.ap_id == primaryAp || offer.load_q > m_loadRejectThresholdQ)
            {
                continue;
            }
            if (std::find(members.begin(), members.end(), offer.ap_id) != members.end())
            {
                continue;
            }
            members.push_back(offer.ap_id);
        }

        if (members.size() < m_maxClusterSize)
        {
            for (const auto& meas : sortedMeasurements)
            {
                if (members.size() >= m_maxClusterSize)
                {
                    break;
                }
                if (meas.ap_id == INVALID_AP_ID ||
                    std::find(members.begin(), members.end(), meas.ap_id) != members.end())
                {
                    continue;
                }
                members.push_back(meas.ap_id);
            }
        }
    }

    if (members.empty())
    {
        NS_LOG_WARN("UE " << m_ueId << " did not find cluster candidates in epoch " << assocEpoch);
        return;
    }
    if (primaryAp == INVALID_AP_ID)
    {
        primaryAp = members.front();
    }

    AssocConfirm confirm{};
    confirm.ue_id = m_ueId;
    confirm.assoc_epoch = assocEpoch;
    confirm.cluster_ver = m_nextClusterVer++;
    confirm.primary_ap = primaryAp;
    confirm.cluster_size = static_cast<uint8_t>(members.size());
    for (size_t i = 0; i < members.size(); ++i)
    {
        confirm.cluster_members[i] = members[i];
    }

    m_assocAttempt.active = true;
    m_assocAttempt.assocEpoch = assocEpoch;
    m_assocAttempt.clusterVer = confirm.cluster_ver;
    m_assocAttempt.primaryAp = confirm.primary_ap;
    m_assocAttempt.clusterSize = confirm.cluster_size;
    m_assocAttempt.members = confirm.cluster_members;
    m_assocAttempt.ackByAp.clear();
    for (uint8_t i = 0; i < confirm.cluster_size; ++i)
    {
        m_assocAttempt.ackByAp[confirm.cluster_members[i]] = false;
    }
    m_assocAckDeadlineEvent.Cancel();
    m_assocAckDeadlineEvent = Simulator::Schedule(
        m_assocAckDeadline, &CfUeMac::OnAssocAckDeadline, this, assocEpoch, confirm.cluster_ver);
    NS_LOG_DEBUG("UE " << m_ueId << " selected cluster ver=" << confirm.cluster_ver
                       << " primary=" << confirm.primary_ap
                       << " size=" << static_cast<uint32_t>(confirm.cluster_size)
                       << " waitingAckUntilMs=" << m_assocAckDeadline.GetMilliSeconds());

    if (!m_assocConfirmSink)
    {
        NS_LOG_WARN("UE " << m_ueId << " has no AssocConfirmSink");
        return;
    }
    for (uint8_t i = 0; i < confirm.cluster_size; ++i)
    {
        m_assocConfirmSink(confirm, confirm.cluster_members[i]);
    }
}

void
CfUeMac::RollbackAssocAttempt(uint8_t reasonCode)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(reasonCode));
    if (!m_assocAttempt.active)
    {
        return;
    }
    m_assocAckDeadlineEvent.Cancel();
    m_assocAttempt = AssocAttempt{};
    NS_LOG_DEBUG("UE " << m_ueId << " rollback pending association reason="
                       << static_cast<uint32_t>(reasonCode));
}

void
CfUeMac::ApplyPendingClusterIfDue()
{
    NS_LOG_FUNCTION(this);
    if (!m_pendingCluster.valid)
    {
        return;
    }

    if (GetCurrentEpoch() < m_pendingCluster.validFromEpoch)
    {
        return;
    }

    m_primaryApActive = m_pendingCluster.primaryAp;
    m_clusterVerActive = m_pendingCluster.clusterVer;
    m_clusterSizeActive = m_pendingCluster.clusterSize;
    m_clusterMembersActive = m_pendingCluster.members;
    m_pendingCluster.valid = false;
    m_state = UeMacState::UE_ASSOC_ACTIVE;
    NS_LOG_DEBUG("UE " << m_ueId << " activated cluster ver=" << m_clusterVerActive
                       << " primary=" << m_primaryApActive
                       << " size=" << static_cast<uint32_t>(m_clusterSizeActive));
}

void
CfUeMac::SendSrBsr()
{
    NS_LOG_FUNCTION(this);
    const uint8_t memberCount =
        std::min<uint8_t>(m_clusterSizeActive, CfMimoConstants::MAX_CLUSTER_SIZE);
    if (!m_srBsrSink || m_queueBytes == 0 || memberCount == 0)
    {
        if (m_queueBytes > 0 && memberCount == 0)
        {
            NS_LOG_DEBUG("UE " << m_ueId << " has queue but no active cluster yet, SR/BSR deferred");
        }
        return;
    }

    SR_BSR srBsr{};
    srBsr.ue_id = m_ueId;
    srBsr.tti = GetCurrentTtiIndex();
    srBsr.sr_flag = 1;
    srBsr.traffic_class = m_trafficClass;
    srBsr.queue_bytes = m_queueBytes;

    uint8_t fanout = 0;
    for (uint8_t i = 0; i < memberCount; ++i)
    {
        const AP_ID apId = m_clusterMembersActive[i];
        if (apId == INVALID_AP_ID)
        {
            continue;
        }
        m_srBsrSink(srBsr, apId);
        ++fanout;
    }

    if (fanout == 0 && m_primaryApActive != INVALID_AP_ID)
    {
        m_srBsrSink(srBsr, m_primaryApActive);
        fanout = 1;
    }
    NS_LOG_DEBUG("UE " << m_ueId << " SR/BSR tti=" << srBsr.tti << " queue=" << srBsr.queue_bytes
                       << " fanout=" << static_cast<uint32_t>(fanout)
                       << " primary=" << m_primaryApActive);
}

void
CfUeMac::ConsumeGrant()
{
    NS_LOG_FUNCTION(this);
    if (!m_hasGrant || m_lastGrant.tti != GetCurrentTtiIndex())
    {
        if (m_hasGrant && m_lastGrant.tti < GetCurrentTtiIndex())
        {
            NS_LOG_WARN("UE " << m_ueId << " stale grant discarded grantTti=" << m_lastGrant.tti
                              << " nowTti=" << GetCurrentTtiIndex());
            m_hasGrant = false;
        }
        return;
    }

    const uint32_t prbCount = static_cast<uint32_t>(__builtin_popcountll(m_lastGrant.prb_bitmap));
    const uint32_t estimatedBytes = prbCount * (static_cast<uint32_t>(m_lastGrant.mcs) + 1u) * 4u;
    NS_LOG_DEBUG("UE " << m_ueId << " consume grant tti=" << m_lastGrant.tti
                       << " prb=" << prbCount << " mcs=" << static_cast<uint32_t>(m_lastGrant.mcs)
                       << " estBytes=" << estimatedBytes);
    m_queueBytes = (estimatedBytes >= m_queueBytes) ? 0 : (m_queueBytes - estimatedBytes);

    if (m_rxStatusSink)
    {
        RxStatus status{};
        status.ue_id = m_ueId;
        status.tti = m_lastGrant.tti;
        status.ok = 1;

        const uint8_t memberCount =
            std::min<uint8_t>(m_clusterSizeActive, CfMimoConstants::MAX_CLUSTER_SIZE);
        uint8_t fanout = 0;
        for (uint8_t i = 0; i < memberCount; ++i)
        {
            const AP_ID apId = m_clusterMembersActive[i];
            if (apId == INVALID_AP_ID)
            {
                continue;
            }
            m_rxStatusSink(status, apId);
            ++fanout;
        }
        if (fanout == 0 && m_primaryApActive != INVALID_AP_ID)
        {
            m_rxStatusSink(status, m_primaryApActive);
        }
    }

    m_hasGrant = false;
}

void
CfUeMac::ReceiveAssocOffer(const AssocOffer& offer)
{
    NS_LOG_FUNCTION(this << offer.ue_id << offer.ap_id << offer.assoc_epoch << offer.score_q);
    if (m_mode != CfMode::DISTRIBUTED || offer.ue_id != m_ueId ||
        offer.assoc_epoch != GetCurrentEpoch() || m_state != UeMacState::UE_ASSOC_WAIT)
    {
        NS_LOG_DEBUG("UE " << m_ueId << " ignored offer from AP " << offer.ap_id
                           << " epoch=" << offer.assoc_epoch);
        return;
    }

    auto it = m_offerBuffer.find(offer.ap_id);
    if (it == m_offerBuffer.end() || offer.score_q > it->second.score_q)
    {
        m_offerBuffer[offer.ap_id] = offer;
        NS_LOG_DEBUG("UE " << m_ueId << " accepted offer AP " << offer.ap_id
                           << " scoreQ=" << offer.score_q);
    }
}

void
CfUeMac::ReceiveAssocAck(const AssocAck& ack)
{
    NS_LOG_FUNCTION(this << ack.ue_id << ack.ap_id << ack.assoc_epoch << ack.cluster_ver
                         << static_cast<uint32_t>(ack.ok));
    if (ack.ue_id != m_ueId || !m_assocAttempt.active)
    {
        return;
    }
    if (ack.assoc_epoch != m_assocAttempt.assocEpoch || ack.cluster_ver != m_assocAttempt.clusterVer)
    {
        return;
    }
    const auto it = m_assocAttempt.ackByAp.find(ack.ap_id);
    if (it == m_assocAttempt.ackByAp.end())
    {
        return;
    }
    if (ack.ok == 0)
    {
        RollbackAssocAttempt(ack.reason_code);
        return;
    }

    it->second = true;
    const bool allAcked = std::all_of(m_assocAttempt.ackByAp.begin(),
                                      m_assocAttempt.ackByAp.end(),
                                      [](const auto& item) { return item.second; });
    if (!allAcked)
    {
        return;
    }

    m_assocAckDeadlineEvent.Cancel();
    m_pendingCluster.valid = true;
    m_pendingCluster.validFromEpoch = m_assocAttempt.assocEpoch + 1;
    m_pendingCluster.clusterVer = m_assocAttempt.clusterVer;
    m_pendingCluster.primaryAp = m_assocAttempt.primaryAp;
    m_pendingCluster.clusterSize = m_assocAttempt.clusterSize;
    m_pendingCluster.members = m_assocAttempt.members;
    m_assocAttempt = AssocAttempt{};
    NS_LOG_DEBUG("UE " << m_ueId << " all association ACK received, pending applyEpoch="
                       << m_pendingCluster.validFromEpoch);
}

void
CfUeMac::ReceiveSchedulingGrant(const SchedulingGrant& grant)
{
    NS_LOG_FUNCTION(this << grant.ue_id << grant.tti);
    if (grant.ue_id != m_ueId)
    {
        NS_LOG_WARN("UE " << m_ueId << " received grant for UE " << grant.ue_id);
        return;
    }
    m_lastGrant = grant;
    m_hasGrant = true;
    NS_LOG_DEBUG("UE " << m_ueId << " grant received tti=" << grant.tti
                       << " mcs=" << static_cast<uint32_t>(grant.mcs));
}

void
CfUeMac::ReceiveCentralizedAssoc(const AssocConfirm& confirm)
{
    NS_LOG_FUNCTION(this << confirm.ue_id << confirm.assoc_epoch << confirm.cluster_ver);
    if (confirm.ue_id != m_ueId)
    {
        NS_LOG_WARN("UE " << m_ueId << " received centralized assoc for UE " << confirm.ue_id);
        return;
    }

    m_assocAckDeadlineEvent.Cancel();
    m_assocAttempt = AssocAttempt{};
    m_pendingCluster.valid = true;
    m_pendingCluster.validFromEpoch = confirm.assoc_epoch + 1;
    m_pendingCluster.clusterVer = confirm.cluster_ver;
    m_pendingCluster.primaryAp = confirm.primary_ap;
    m_pendingCluster.clusterSize = confirm.cluster_size;
    m_pendingCluster.members = confirm.cluster_members;
    NS_LOG_DEBUG("UE " << m_ueId << " centralized cluster pending ver=" << confirm.cluster_ver
                       << " primary=" << confirm.primary_ap
                       << " applyEpoch=" << m_pendingCluster.validFromEpoch);
}

UeMacState
CfUeMac::GetState() const
{
    return m_state;
}

AP_ID
CfUeMac::GetPrimaryApActive() const
{
    return m_primaryApActive;
}

uint32_t
CfUeMac::GetClusterVerActive() const
{
    return m_clusterVerActive;
}

std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE>
CfUeMac::GetClusterMembersActive() const
{
    return m_clusterMembersActive;
}

uint32_t
CfUeMac::GetCurrentEpoch() const
{
    return GetAssocEpoch(Simulator::Now(), m_tAssoc);
}

uint32_t
CfUeMac::GetCurrentTtiIndex() const
{
    return GetTtiIndex(Simulator::Now(), m_tti);
}

} // namespace ns3
