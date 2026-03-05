/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-cpu-controller.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <set>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CfCpuController");
NS_OBJECT_ENSURE_REGISTERED(CfCpuController);

TypeId
CfCpuController::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfCpuController")
                            .SetParent<Object>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfCpuController>();
    return tid;
}

CfCpuController::CfCpuController() = default;

CfCpuController::~CfCpuController()
{
    NS_LOG_FUNCTION(this);
}

void
CfCpuController::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Stop();
    Object::DoDispose();
}

void
CfCpuController::SetAssocInterval(Time tAssoc)
{
    NS_LOG_FUNCTION(this << tAssoc.GetNanoSeconds());
    m_tAssoc = tAssoc;
}

void
CfCpuController::SetControlDelay(Time ctrlDelay)
{
    NS_LOG_FUNCTION(this << ctrlDelay.GetNanoSeconds());
    m_ctrlDelay = ctrlDelay;
}

void
CfCpuController::SetLambda(double lambda)
{
    NS_LOG_FUNCTION(this << lambda);
    m_lambda = std::max(0.0, lambda);
}

void
CfCpuController::SetMaxClusterSize(uint8_t maxClusterSize)
{
    NS_LOG_FUNCTION(this << static_cast<uint32_t>(maxClusterSize));
    m_maxClusterSize =
        std::max<uint8_t>(1, std::min<uint8_t>(CfMimoConstants::MAX_CLUSTER_SIZE, maxClusterSize));
}

void
CfCpuController::SetDecisionSink(DecisionSink cb)
{
    NS_LOG_FUNCTION(this);
    m_decisionSink = std::move(cb);
}

void
CfCpuController::Start()
{
    NS_LOG_FUNCTION(this);
    if (m_running)
    {
        NS_LOG_DEBUG("CPU controller Start() ignored: already running");
        return;
    }
    m_running = true;
    NS_LOG_DEBUG("CPU controller start");
    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfCpuController::OnAssocTimer, this);
}

void
CfCpuController::Stop()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        NS_LOG_DEBUG("CPU controller Stop() ignored: not running");
        return;
    }
    m_running = false;
    NS_LOG_DEBUG("CPU controller stop");
    m_assocEvent.Cancel();
}

void
CfCpuController::ReceiveApSummary(const APSummary& summary)
{
    NS_LOG_FUNCTION(this << summary.ap_id << summary.assoc_epoch);
    m_apSummaries[summary.ap_id] = SummaryWithTs{summary, Simulator::Now()};
    NS_LOG_DEBUG("CPU received APSummary ap=" << summary.ap_id << " epoch=" << summary.assoc_epoch
                                              << " candCnt="
                                              << static_cast<uint32_t>(summary.cand_ue_cnt)
                                              << " loadQ=" << summary.load_q);
}

void
CfCpuController::OnAssocTimer()
{
    NS_LOG_FUNCTION(this);
    if (!m_running)
    {
        return;
    }

    const uint32_t epoch = GetCurrentEpoch();
    ComputeAndDispatch(epoch);
    m_assocEvent = Simulator::Schedule(m_tAssoc, &CfCpuController::OnAssocTimer, this);
}

void
CfCpuController::ComputeAndDispatch(uint32_t assocEpoch)
{
    NS_LOG_FUNCTION(this << assocEpoch);
    if (!m_decisionSink)
    {
        NS_LOG_WARN("CPU decision sink is not set, skip epoch " << assocEpoch);
        return;
    }

    m_state = CpuControllerState::CPU_DECIDE;

    struct Candidate
    {
        AP_ID apId;
        int16_t metricQ;
        uint16_t loadQ;
    };

    std::map<UE_ID, std::vector<Candidate>> candidatesByUe;
    std::set<AP_ID> knownAps;
    for (const auto& [apId, summaryWithTs] : m_apSummaries)
    {
        const auto& summary = summaryWithTs.summary;
        if (summary.assoc_epoch != assocEpoch)
        {
            continue;
        }
        knownAps.insert(apId);
        for (uint8_t i = 0; i < summary.cand_ue_cnt; ++i)
        {
            const UE_ID ueId = summary.cand_ue_list[i];
            if (ueId == INVALID_UE_ID)
            {
                continue;
            }
            candidatesByUe[ueId].push_back(Candidate{apId, summary.cand_metric_q[i], summary.load_q});
        }
    }
    if (knownAps.empty())
    {
        NS_LOG_WARN("CPU has no fresh APSummary for epoch " << assocEpoch);
    }
    NS_LOG_DEBUG("CPU decide epoch=" << assocEpoch << " knownAps=" << knownAps.size()
                                     << " candidateUes=" << candidatesByUe.size());

    std::map<AP_ID, std::vector<std::pair<UE_ID, CfRoleFlag>>> assignmentsPerAp;
    for (auto& [ueId, candidates] : candidatesByUe)
    {
        if (candidates.empty())
        {
            continue;
        }

        std::sort(candidates.begin(), candidates.end(), [this](const Candidate& lhs, const Candidate& rhs) {
            const double lhsPrimary = static_cast<double>(lhs.metricQ) - m_lambda * lhs.loadQ;
            const double rhsPrimary = static_cast<double>(rhs.metricQ) - m_lambda * rhs.loadQ;
            return lhsPrimary > rhsPrimary;
        });

        const AP_ID primary = candidates.front().apId;
        std::vector<AP_ID> cluster{primary};

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.metricQ > rhs.metricQ;
        });
        for (const auto& cand : candidates)
        {
            if (cluster.size() >= m_maxClusterSize)
            {
                break;
            }
            if (std::find(cluster.begin(), cluster.end(), cand.apId) != cluster.end())
            {
                continue;
            }
            cluster.push_back(cand.apId);
        }

        for (const auto apId : cluster)
        {
            assignmentsPerAp[apId].push_back(
                {ueId, apId == primary ? CfRoleFlag::PRIMARY : CfRoleFlag::SECONDARY});
        }
        NS_LOG_DEBUG("CPU UE " << ueId << " primary=" << primary
                               << " clusterSize=" << cluster.size());
    }

    m_state = CpuControllerState::CPU_DISPATCH;
    ++m_clusterVerGlobal;

    for (const auto apId : knownAps)
    {
        ClusterDecision decision{};
        decision.assoc_epoch = assocEpoch;
        decision.cluster_ver = m_clusterVerGlobal;
        decision.ap_id = apId;

        const auto it = assignmentsPerAp.find(apId);
        if (it != assignmentsPerAp.end())
        {
            const auto& assigned = it->second;
            const uint8_t count = std::min<uint8_t>(CfMimoConstants::MAX_UE_PER_AP_SCHED,
                                                    static_cast<uint8_t>(assigned.size()));
            decision.assigned_ue_cnt = count;
            for (uint8_t i = 0; i < count; ++i)
            {
                decision.assigned_ue_list[i] = assigned[i].first;
                decision.role_flags[i] = static_cast<uint8_t>(assigned[i].second);
            }
        }
        NS_LOG_DEBUG("CPU dispatch ap=" << apId << " epoch=" << assocEpoch
                                        << " assigned="
                                        << static_cast<uint32_t>(decision.assigned_ue_cnt)
                                        << " clusterVer=" << decision.cluster_ver);

        if (m_ctrlDelay.IsStrictlyPositive())
        {
            Simulator::Schedule(m_ctrlDelay,
                                [cb = m_decisionSink, decision, apId]() { cb(decision, apId); });
        }
        else
        {
            m_decisionSink(decision, apId);
        }
    }

    m_state = CpuControllerState::CPU_AGGREGATE;
}

CpuControllerState
CfCpuController::GetState() const
{
    return m_state;
}

uint32_t
CfCpuController::GetClusterVerGlobal() const
{
    return m_clusterVerGlobal;
}

uint32_t
CfCpuController::GetCurrentEpoch() const
{
    return GetAssocEpoch(Simulator::Now(), m_tAssoc);
}

} // namespace ns3
