/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-control-plane-helper.h"

#include "ns3/simulator.h"

#include <algorithm>
#include <utility>

namespace ns3
{

void
CfControlPlaneHelper::SerialPipe::Enqueue(Time serviceTime, std::function<void()> fn)
{
    const Time now = Simulator::Now();
    const Time start = (busyUntil > now) ? busyUntil : now;
    const Time deliverAt = start + serviceTime;
    const Time delay = deliverAt - now;
    busyUntil = deliverAt;

    ++stats.enqueued;
    const uint64_t delayNs = static_cast<uint64_t>(std::max<int64_t>(0, delay.GetNanoSeconds()));
    stats.sumDelayNs += delayNs;
    stats.maxDelayNs = std::max(stats.maxDelayNs, delayNs);

    Simulator::Schedule(delay, [this, fn = std::move(fn)]() mutable {
        ++stats.delivered;
        fn();
    });
}

void
CfControlPlaneHelper::SetMode(CfMode mode)
{
    m_mode = mode;
}

void
CfControlPlaneHelper::SetAssocInterval(Time tAssoc)
{
    m_tAssoc = tAssoc;
}

void
CfControlPlaneHelper::SetControlBudgetPerEpoch(uint64_t budgetBytes)
{
    m_ctrlBudgetBytesPerEpoch = budgetBytes;
}

void
CfControlPlaneHelper::SetProcessingTimes(Time apRxProc, Time ueRxProc, Time cpuRxProc)
{
    m_apRxProc = apRxProc;
    m_ueRxProc = ueRxProc;
    m_cpuRxProc = cpuRxProc;
}

void
CfControlPlaneHelper::SetNetwork(const std::map<AP_ID, Ptr<CfApMac>>& apById,
                                 const std::map<UE_ID, Ptr<CfUeMac>>& ueById,
                                 Ptr<CfCpuController> cpu)
{
    m_apById = apById;
    m_ueById = ueById;
    m_cpu = cpu;

    m_apRxPipes.clear();
    for (const auto& [apId, _] : m_apById)
    {
        m_apRxPipes.emplace(apId, SerialPipe{});
    }
    m_ueRxPipes.clear();
    for (const auto& [ueId, _] : m_ueById)
    {
        m_ueRxPipes.emplace(ueId, SerialPipe{});
    }
    m_cpuRxPipe = SerialPipe{};
    m_decisionsByEpoch.clear();
    m_decisionAcksByEpoch.clear();
    m_stats = CfControlPlaneStats{};
    m_epochStats.clear();
}

uint64_t
CfControlPlaneHelper::EstimateGrantBytes(const SchedulingGrant& grant)
{
    const uint32_t prbCount = static_cast<uint32_t>(__builtin_popcountll(grant.prb_bitmap));
    return static_cast<uint64_t>(prbCount) * static_cast<uint64_t>(grant.mcs + 1u) * 4u;
}

uint32_t
CfControlPlaneHelper::EpochNow() const
{
    return GetAssocEpoch(Simulator::Now(), m_tAssoc);
}

void
CfControlPlaneHelper::AccountOffered(uint32_t epoch, CtrlMsgType type, uint64_t bytes)
{
    auto& ep = m_epochStats[epoch];
    switch (type)
    {
    case CtrlMsgType::UE_STATE_REPORT:
        ep.ueStateReportBytes += bytes;
        break;
    case CtrlMsgType::ASSOC_OFFER:
        ep.assocOfferBytes += bytes;
        break;
    case CtrlMsgType::ASSOC_CONFIRM:
        ep.assocConfirmBytes += bytes;
        ++ep.distributedConfirmFanout;
        break;
    case CtrlMsgType::ASSOC_ACK:
        ep.assocAckBytes += bytes;
        ++ep.distributedAckFanout;
        break;
    case CtrlMsgType::AP_SUMMARY:
        ep.apSummaryBytes += bytes;
        break;
    case CtrlMsgType::CLUSTER_DECISION:
        ep.clusterDecisionBytes += bytes;
        ++ep.centralizedDecisionFanout;
        break;
    case CtrlMsgType::CLUSTER_DECISION_ACK:
        ep.clusterDecisionAckBytes += bytes;
        break;
    case CtrlMsgType::CENTRALIZED_ASSOC_TO_UE:
        ep.centralizedAssocToUeBytes += bytes;
        break;
    }
    ep.bytesOfferedTotal += bytes;
    ep.budgetExceed =
        (m_ctrlBudgetBytesPerEpoch > 0) && (ep.bytesOfferedTotal > m_ctrlBudgetBytesPerEpoch);
}

void
CfControlPlaneHelper::Install()
{
    for (const auto& [ueId, ue] : m_ueById)
    {
        ue->SetStateReportSink([this](const UEStateReport& report, AP_ID apId) {
            ++m_stats.ueStateReport.tx;
            AccountOffered(EpochNow(), CtrlMsgType::UE_STATE_REPORT, MsgBytes<UEStateReport>());
            const auto apIt = m_apById.find(apId);
            const auto pipeIt = m_apRxPipes.find(apId);
            if (apIt == m_apById.end() || pipeIt == m_apRxPipes.end())
            {
                return;
            }
            pipeIt->second.Enqueue(m_apRxProc, [this, report, ap = apIt->second]() {
                ++m_stats.ueStateReport.rx;
                ap->ReceiveUeStateReport(report);
            });
        });

        ue->SetAssocConfirmSink([this](const AssocConfirm& confirm, AP_ID apId) {
            ++m_stats.assocConfirm.tx;
            AccountOffered(confirm.assoc_epoch, CtrlMsgType::ASSOC_CONFIRM, MsgBytes<AssocConfirm>());
            const auto apIt = m_apById.find(apId);
            const auto pipeIt = m_apRxPipes.find(apId);
            if (apIt == m_apById.end() || pipeIt == m_apRxPipes.end())
            {
                return;
            }
            pipeIt->second.Enqueue(m_apRxProc, [this, confirm, ap = apIt->second]() {
                ++m_stats.assocConfirm.rx;
                ap->ReceiveAssocConfirm(confirm);
            });
        });

        ue->SetSrBsrSink([this](const SR_BSR& srBsr, AP_ID apId) {
            ++m_stats.srBsr.tx;
            const auto apIt = m_apById.find(apId);
            const auto pipeIt = m_apRxPipes.find(apId);
            if (apIt == m_apById.end() || pipeIt == m_apRxPipes.end())
            {
                return;
            }
            pipeIt->second.Enqueue(m_apRxProc, [this, srBsr, ap = apIt->second]() {
                ++m_stats.srBsr.rx;
                ap->ReceiveSrBsr(srBsr);
            });
        });
        ue->SetRxStatusSink([this](const RxStatus& status, AP_ID apId) {
            ++m_stats.rxStatus.tx;
            const auto apIt = m_apById.find(apId);
            const auto pipeIt = m_apRxPipes.find(apId);
            if (apIt == m_apById.end() || pipeIt == m_apRxPipes.end())
            {
                return;
            }
            pipeIt->second.Enqueue(m_apRxProc, [this, status, ap = apIt->second]() {
                ++m_stats.rxStatus.rx;
                ap->ReceiveRxStatus(status);
            });
        });
        (void)ueId;
    }

    if (m_mode == CfMode::CENTRALIZED && m_cpu)
    {
        m_cpu->SetDecisionSink([this](const ClusterDecision& decision, AP_ID apId) {
            ++m_stats.clusterDecision.tx;
            AccountOffered(decision.assoc_epoch,
                           CtrlMsgType::CLUSTER_DECISION,
                           MsgBytes<ClusterDecision>());
            const auto apIt = m_apById.find(apId);
            const auto pipeIt = m_apRxPipes.find(apId);
            if (apIt == m_apById.end() || pipeIt == m_apRxPipes.end())
            {
                return;
            }

            pipeIt->second.Enqueue(m_apRxProc, [this, decision, ap = apIt->second]() {
                ++m_stats.clusterDecision.rx;
                ap->ReceiveClusterDecision(decision);
            });

            auto& epochMap = m_decisionsByEpoch[decision.assoc_epoch];
            epochMap[apId] = decision;
        });
    }

    for (const auto& [apId, ap] : m_apById)
    {
        if (m_mode == CfMode::DISTRIBUTED)
        {
            ap->SetOfferSink([this](const AssocOffer& offer, UE_ID ueId) {
                ++m_stats.assocOffer.tx;
                AccountOffered(offer.assoc_epoch, CtrlMsgType::ASSOC_OFFER, MsgBytes<AssocOffer>());
                const auto ueIt = m_ueById.find(ueId);
                const auto pipeIt = m_ueRxPipes.find(ueId);
                if (ueIt == m_ueById.end() || pipeIt == m_ueRxPipes.end())
                {
                    return;
                }
                pipeIt->second.Enqueue(m_ueRxProc, [this, offer, ue = ueIt->second]() {
                    ++m_stats.assocOffer.rx;
                    ue->ReceiveAssocOffer(offer);
                });
            });
            ap->SetAssocAckSink([this](const AssocAck& ack, UE_ID ueId) {
                ++m_stats.assocAck.tx;
                if (ack.ok == 0)
                {
                    ++m_stats.distributedRejectCount;
                }
                AccountOffered(ack.assoc_epoch, CtrlMsgType::ASSOC_ACK, MsgBytes<AssocAck>());
                const auto ueIt = m_ueById.find(ueId);
                const auto pipeIt = m_ueRxPipes.find(ueId);
                if (ueIt == m_ueById.end() || pipeIt == m_ueRxPipes.end())
                {
                    return;
                }
                pipeIt->second.Enqueue(m_ueRxProc, [this, ack, ue = ueIt->second]() {
                    ++m_stats.assocAck.rx;
                    ue->ReceiveAssocAck(ack);
                });
            });
        }
        else
        {
            ap->SetOfferSink([](const AssocOffer&, UE_ID) {});
            ap->SetAssocAckSink([](const AssocAck&, UE_ID) {});
        }

        ap->SetGrantSink([this](const SchedulingGrant& grant, UE_ID ueId) {
            ++m_stats.schedulingGrant.tx;
            m_stats.grantEstimatedBytes += EstimateGrantBytes(grant);
            const auto ueIt = m_ueById.find(ueId);
            const auto pipeIt = m_ueRxPipes.find(ueId);
            if (ueIt == m_ueById.end() || pipeIt == m_ueRxPipes.end())
            {
                return;
            }
            pipeIt->second.Enqueue(m_ueRxProc, [this, grant, ue = ueIt->second]() {
                ++m_stats.schedulingGrant.rx;
                ue->ReceiveSchedulingGrant(grant);
            });
        });
        ap->SetNeighborCoordSink([](const NeighborCoord&, AP_ID) {});

        if (m_mode == CfMode::CENTRALIZED && m_cpu)
        {
            ap->SetApSummarySink([this](const APSummary& summary) {
                ++m_stats.apSummary.tx;
                AccountOffered(summary.assoc_epoch, CtrlMsgType::AP_SUMMARY, MsgBytes<APSummary>());
                m_cpuRxPipe.Enqueue(m_cpuRxProc, [this, summary]() {
                    ++m_stats.apSummary.rx;
                    APSummary aligned = summary;
                    // One-epoch delayed summary delivery under queued processing.
                    aligned.assoc_epoch = summary.assoc_epoch + 1u;
                    m_cpu->ReceiveApSummary(aligned);
                });
            });
            ap->SetClusterDecisionAckSink([this](const ClusterDecisionAck& ack, AP_ID apId) {
                ++m_stats.clusterDecisionAck.tx;
                if (ack.ok == 0)
                {
                    ++m_stats.centralizedRejectCount;
                }
                AccountOffered(ack.assoc_epoch,
                               CtrlMsgType::CLUSTER_DECISION_ACK,
                               MsgBytes<ClusterDecisionAck>());

                m_cpuRxPipe.Enqueue(m_cpuRxProc, [this, ack]() {
                    ++m_stats.clusterDecisionAck.rx;
                    m_cpu->ReceiveClusterDecisionAck(ack);
                });

                auto& ackMap = m_decisionAcksByEpoch[ack.assoc_epoch];
                ackMap[apId] = ack;
                if (ackMap.size() < m_apById.size())
                {
                    return;
                }

                const auto decisionIt = m_decisionsByEpoch.find(ack.assoc_epoch);
                if (decisionIt == m_decisionsByEpoch.end())
                {
                    m_decisionAcksByEpoch.erase(ack.assoc_epoch);
                    return;
                }

                struct UeClusterDraft
                {
                    AP_ID primary{INVALID_AP_ID};
                    std::vector<AP_ID> members;
                    uint32_t clusterVer{0};
                };
                std::map<UE_ID, UeClusterDraft> byUe;
                for (const auto& [dApId, d] : decisionIt->second)
                {
                    const auto ackIt = ackMap.find(dApId);
                    if (ackIt == ackMap.end() || ackIt->second.ok == 0)
                    {
                        continue;
                    }
                    for (uint8_t i = 0; i < d.assigned_ue_cnt; ++i)
                    {
                        const UE_ID ueId = d.assigned_ue_list[i];
                        if (ueId == INVALID_UE_ID)
                        {
                            continue;
                        }
                        auto& draft = byUe[ueId];
                        draft.clusterVer = d.cluster_ver;
                        if (std::find(draft.members.begin(), draft.members.end(), dApId) ==
                            draft.members.end())
                        {
                            draft.members.push_back(dApId);
                        }
                        if (d.role_flags[i] == static_cast<uint8_t>(CfRoleFlag::PRIMARY))
                        {
                            draft.primary = dApId;
                        }
                    }
                }

                for (const auto& [ueId, draft] : byUe)
                {
                    const auto ueIt = m_ueById.find(ueId);
                    const auto uePipeIt = m_ueRxPipes.find(ueId);
                    if (ueIt == m_ueById.end() || uePipeIt == m_ueRxPipes.end() ||
                        draft.members.empty())
                    {
                        continue;
                    }

                    AssocConfirm confirm{};
                    confirm.ue_id = ueId;
                    confirm.assoc_epoch = ack.assoc_epoch;
                    confirm.cluster_ver = draft.clusterVer;
                    confirm.primary_ap =
                        (draft.primary == INVALID_AP_ID) ? draft.members.front() : draft.primary;
                    confirm.cluster_size = static_cast<uint8_t>(
                        std::min<size_t>(draft.members.size(), CfMimoConstants::MAX_CLUSTER_SIZE));
                    for (uint8_t i = 0; i < confirm.cluster_size; ++i)
                    {
                        confirm.cluster_members[i] = draft.members[i];
                    }

                    ++m_stats.centralizedAssocToUe.tx;
                    AccountOffered(confirm.assoc_epoch,
                                   CtrlMsgType::CENTRALIZED_ASSOC_TO_UE,
                                   MsgBytes<AssocConfirm>());
                    uePipeIt->second.Enqueue(m_ueRxProc, [this, confirm, ue = ueIt->second]() {
                        ++m_stats.centralizedAssocToUe.rx;
                        ue->ReceiveCentralizedAssoc(confirm);
                    });
                }

                m_decisionsByEpoch.erase(ack.assoc_epoch);
                m_decisionAcksByEpoch.erase(ack.assoc_epoch);
            });
        }
        else
        {
            ap->SetApSummarySink([](const APSummary&) {});
            ap->SetClusterDecisionAckSink([](const ClusterDecisionAck&, AP_ID) {});
        }

        (void)apId;
    }
}

const CfControlPlaneStats&
CfControlPlaneHelper::GetStats() const
{
    return m_stats;
}

const std::map<uint32_t, CfEpochControlStats>&
CfControlPlaneHelper::GetEpochStats() const
{
    return m_epochStats;
}

CfControlPipeStats
CfControlPlaneHelper::AggregatePipeStats(const std::map<uint16_t, SerialPipe>& pipes)
{
    CfControlPipeStats out;
    for (const auto& [_, p] : pipes)
    {
        out.enqueued += p.stats.enqueued;
        out.delivered += p.stats.delivered;
        out.sumDelayNs += p.stats.sumDelayNs;
        out.maxDelayNs = std::max(out.maxDelayNs, p.stats.maxDelayNs);
    }
    return out;
}

CfControlPipeStats
CfControlPlaneHelper::GetAggregatedApPipeStats() const
{
    std::map<uint16_t, SerialPipe> casted;
    for (const auto& [apId, pipe] : m_apRxPipes)
    {
        casted.emplace(apId, pipe);
    }
    return AggregatePipeStats(casted);
}

CfControlPipeStats
CfControlPlaneHelper::GetAggregatedUePipeStats() const
{
    std::map<uint16_t, SerialPipe> casted;
    for (const auto& [ueId, pipe] : m_ueRxPipes)
    {
        casted.emplace(ueId, pipe);
    }
    return AggregatePipeStats(casted);
}

CfControlPipeStats
CfControlPlaneHelper::GetCpuPipeStats() const
{
    return m_cpuRxPipe.stats;
}

} // namespace ns3
