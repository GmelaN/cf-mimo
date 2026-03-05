/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_CPU_CONTROLLER_H
#define CF_CPU_CONTROLLER_H

#include "cf-mimo-common.h"
#include "cf-mimo-messages.h"

#include "ns3/event-id.h"
#include "ns3/object.h"

#include <functional>
#include <map>

namespace ns3
{

class CfCpuController : public Object
{
  public:
    static TypeId GetTypeId();

    CfCpuController();
    ~CfCpuController() override;

    using DecisionSink = std::function<void(const ClusterDecision&, AP_ID)>;

    void SetAssocInterval(Time tAssoc);
    void SetControlDelay(Time ctrlDelay);
    void SetLambda(double lambda);
    void SetMaxClusterSize(uint8_t maxClusterSize);

    void SetDecisionSink(DecisionSink cb);

    void Start();
    void Stop();

    void ReceiveApSummary(const APSummary& summary);

    CpuControllerState GetState() const;
    uint32_t GetClusterVerGlobal() const;

  protected:
    void DoDispose() override;

  private:
    struct SummaryWithTs
    {
        APSummary summary;
        Time ts;
    };

    void OnAssocTimer();
    void ComputeAndDispatch(uint32_t assocEpoch);
    uint32_t GetCurrentEpoch() const;

    Time m_tAssoc{MilliSeconds(100)};
    Time m_ctrlDelay{NanoSeconds(0)};
    double m_lambda{0.1};
    uint8_t m_maxClusterSize{CfMimoConstants::MAX_CLUSTER_SIZE};

    bool m_running{false};
    CpuControllerState m_state{CpuControllerState::CPU_AGGREGATE};
    uint32_t m_clusterVerGlobal{0};

    std::map<AP_ID, SummaryWithTs> m_apSummaries;
    DecisionSink m_decisionSink;

    EventId m_assocEvent;
};

} // namespace ns3

#endif // CF_CPU_CONTROLLER_H
