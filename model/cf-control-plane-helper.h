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
#include <vector>

namespace ns3
{

struct CfCtrlMsgCounter
{
    uint64_t tx{0};
    uint64_t rx{0};
};

struct CfControlPlaneStats
{
    CfCtrlMsgCounter ueStateReport;
    CfCtrlMsgCounter assocOffer;
    CfCtrlMsgCounter assocConfirm;
    CfCtrlMsgCounter srBsr;
    CfCtrlMsgCounter rxStatus;
    CfCtrlMsgCounter apSummary;
    CfCtrlMsgCounter clusterDecision;
    CfCtrlMsgCounter schedulingGrant;
    CfCtrlMsgCounter centralizedAssocToUe;

    uint64_t grantEstimatedBytes{0};
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
    void SetProcessingTimes(Time apRxProc, Time ueRxProc, Time cpuRxProc);
    void SetNetwork(const std::map<AP_ID, Ptr<CfApMac>>& apById,
                    const std::map<UE_ID, Ptr<CfUeMac>>& ueById,
                    Ptr<CfCpuController> cpu);
    void Install();

    const CfControlPlaneStats& GetStats() const;
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

    CfMode m_mode{CfMode::DISTRIBUTED};
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

    CfControlPlaneStats m_stats;
};

} // namespace ns3

#endif // CF_CONTROL_PLANE_HELPER_H
