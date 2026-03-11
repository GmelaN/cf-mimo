/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_CONTROL_PLANE_HELPER_H
#define CF_CONTROL_PLANE_HELPER_H

#include "cf-ap-mac.h"
#include "cf-cpu-controller.h"
#include "cf-mimo-messages.h"
#include "cf-ue-mac.h"

#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

struct CfCtrlMsgCounter
{
    uint64_t tx{0};
    uint64_t rx{0};
    uint64_t drop{0};
};

struct CfControlPlaneStats
{
    CfCtrlMsgCounter ueStateReport;
    CfCtrlMsgCounter assocOffer;
    CfCtrlMsgCounter assocConfirm;
    CfCtrlMsgCounter assocAck;
    CfCtrlMsgCounter srBsr;
    CfCtrlMsgCounter rxStatus;
    CfCtrlMsgCounter apSummary;
    CfCtrlMsgCounter clusterDecision;
    CfCtrlMsgCounter clusterDecisionAck;
    CfCtrlMsgCounter schedulingGrant;
    CfCtrlMsgCounter centralizedAssocToUe;

    uint64_t grantEstimatedBytes{0};
    uint64_t distributedRejectCount{0};
    uint64_t centralizedRejectCount{0};
};

struct CfEpochControlStats
{
    uint64_t ueStateReportBytes{0};
    uint64_t assocOfferBytes{0};
    uint64_t assocConfirmBytes{0};
    uint64_t assocAckBytes{0};
    uint64_t apSummaryBytes{0};
    uint64_t clusterDecisionBytes{0};
    uint64_t clusterDecisionAckBytes{0};
    uint64_t centralizedAssocToUeBytes{0};
    uint64_t bytesOfferedTotal{0};
    uint64_t distributedConfirmFanout{0};
    uint64_t distributedAckFanout{0};
    uint64_t centralizedDecisionFanout{0};
    bool budgetExceed{false};
};

struct CfControlPipeStats
{
    uint64_t enqueued{0};
    uint64_t delivered{0};
    uint64_t sumDelayNs{0};
    uint64_t maxDelayNs{0};
};

class CfControlPlaneHelper
{
  public:
    void SetMode(CfMode mode);
    void SetAssocInterval(Time tAssoc);
    void SetControlBudgetPerEpoch(uint64_t budgetBytes);
    void SetProcessingTimes(Time apRxProc, Time ueRxProc, Time cpuRxProc);
    void SetNetwork(const std::map<AP_ID, Ptr<CfApMac>>& apById,
                    const std::map<UE_ID, Ptr<CfUeMac>>& ueById,
                    Ptr<CfCpuController> cpu);
    void Install();

    const CfControlPlaneStats& GetStats() const;
    const std::map<uint32_t, CfEpochControlStats>& GetEpochStats() const;
    CfControlPipeStats GetAggregatedApPipeStats() const;
    CfControlPipeStats GetAggregatedUePipeStats() const;
    CfControlPipeStats GetCpuPipeStats() const;

  private:
    struct SerialPipe
    {
        Time busyUntil{Seconds(0)};
        CfControlPipeStats stats;

        void Enqueue(Time serviceTime, std::function<void()> fn);
    };

    static uint64_t EstimateGrantBytes(const SchedulingGrant& grant);
    static CfControlPipeStats AggregatePipeStats(const std::map<uint16_t, SerialPipe>& pipes);

    enum class CtrlMsgType : uint8_t
    {
        UE_STATE_REPORT = 0,
        ASSOC_OFFER = 1,
        ASSOC_CONFIRM = 2,
        ASSOC_ACK = 3,
        AP_SUMMARY = 4,
        CLUSTER_DECISION = 5,
        CLUSTER_DECISION_ACK = 6,
        CENTRALIZED_ASSOC_TO_UE = 7
    };

    template <typename T>
    static constexpr uint64_t MsgBytes()
    {
        return sizeof(T);
    }
    void AccountOffered(uint32_t epoch, CtrlMsgType type, uint64_t bytes);
    uint32_t EpochNow() const;

    CfMode m_mode{CfMode::DISTRIBUTED};
    Time m_tAssoc{MilliSeconds(100)};
    uint64_t m_ctrlBudgetBytesPerEpoch{0};
    Time m_apRxProc{MicroSeconds(0)};
    Time m_ueRxProc{MicroSeconds(0)};
    Time m_cpuRxProc{MicroSeconds(0)};

    std::map<AP_ID, Ptr<CfApMac>> m_apById;
    std::map<UE_ID, Ptr<CfUeMac>> m_ueById;
    Ptr<CfCpuController> m_cpu;

    std::map<AP_ID, SerialPipe> m_apRxPipes;
    std::map<UE_ID, SerialPipe> m_ueRxPipes;
    SerialPipe m_cpuRxPipe;
    std::map<uint32_t, std::map<AP_ID, ClusterDecision>> m_decisionsByEpoch;
    std::map<uint32_t, std::map<AP_ID, ClusterDecisionAck>> m_decisionAcksByEpoch;

    CfControlPlaneStats m_stats;
    std::map<uint32_t, CfEpochControlStats> m_epochStats;
};

} // namespace ns3

#endif // CF_CONTROL_PLANE_HELPER_H
