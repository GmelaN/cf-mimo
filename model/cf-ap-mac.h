/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_AP_MAC_H
#define CF_AP_MAC_H

#include "cf-mimo-common.h"
#include "cf-mimo-messages.h"

#include "ns3/event-id.h"
#include "ns3/object.h"

#include <functional>
#include <map>
#include <vector>

namespace ns3
{

class CfApMac : public Object
{
  public:
    static TypeId GetTypeId();

    CfApMac();
    ~CfApMac() override;

    using OfferSink = std::function<void(const AssocOffer&, UE_ID)>;
    using AssocAckSink = std::function<void(const AssocAck&, UE_ID)>;
    using GrantSink = std::function<void(const SchedulingGrant&, UE_ID)>;
    using NeighborCoordSink = std::function<void(const NeighborCoord&, AP_ID)>;
    using ApSummarySink = std::function<void(const APSummary&)>;
    using ClusterDecisionAckSink = std::function<void(const ClusterDecisionAck&, AP_ID)>;

    void SetApId(AP_ID apId);
    AP_ID GetApId() const;

    void SetMode(CfMode mode);
    CfMode GetMode() const;

    void SetIntervals(Time tti, Time tCoh, Time tState, Time tCoord, Time tAssoc);
    void SetControlDelay(Time ctrlDelay);

    void SetScoringWeights(double w1, double w2, double w3);
    void SetScoreThreshold(int16_t scoreThresholdQ);
    void SetLoadLimit(uint16_t loadMaxQ);
    void SetFronthaulCap(uint16_t fhCapQ);
    void SetMaxUePerSched(uint8_t maxUePerSched);

    void SetOfferSink(OfferSink cb);
    void SetAssocAckSink(AssocAckSink cb);
    void SetGrantSink(GrantSink cb);
    void SetNeighborCoordSink(NeighborCoordSink cb);
    void SetApSummarySink(ApSummarySink cb);
    void SetClusterDecisionAckSink(ClusterDecisionAckSink cb);

    void Start();
    void Stop();

    void ReceiveUeStateReport(const UEStateReport& report);
    void ReceiveAssocConfirm(const AssocConfirm& confirm);
    void ReceiveClusterDecision(const ClusterDecision& decision);
    void ReceiveSrBsr(const SR_BSR& srBsr);
    void ReceiveRxStatus(const RxStatus& status);
    void ReceiveNeighborCoord(const NeighborCoord& coord);

    uint16_t GetLoadQ() const;
    uint16_t GetInterfQ() const;
    ApMacState GetState() const;
    uint32_t GetClusterVerActive() const;

  protected:
    void DoDispose() override;

  private:
    struct ReportWithTs
    {
        UEStateReport report;
        Time ts;
    };

    struct Assignment
    {
        bool served{false};
        CfRoleFlag role{CfRoleFlag::NONE};
        uint32_t clusterVer{0};
        uint32_t validFromEpoch{0};
    };

    void SchedulePeriodicEvents();
    void CancelEvents();

    void OnCohTimer();
    void OnCoordTimer();
    void OnAssocTimer();
    void OnTtiTimer();

    void ApplyPendingAssignmentsIfDue();
    void UpdateLoadAndInterference();

    void GenerateDistributedOffers(uint32_t assocEpoch);
    void BuildAndSendApSummary(uint32_t assocEpoch);
    void ScheduleTtiGrants(uint32_t tti);

    int16_t ComputeOfferScoreQ(const UEStateReport& report) const;
    uint8_t FindCqiForSelf(const UEStateReport& report) const;
    bool IsReportFresh(const ReportWithTs& reportEntry) const;

    uint32_t GetCurrentEpoch() const;
    uint32_t GetCurrentTtiIndex() const;

    AP_ID m_apId{0};
    CfMode m_mode{CfMode::DISTRIBUTED};
    ApMacState m_state{ApMacState::AP_COLLECT};

    Time m_tti{MicroSeconds(500)};
    Time m_tCoh{MilliSeconds(10)};
    Time m_tState{MilliSeconds(10)};
    Time m_tCoord{MilliSeconds(10)};
    Time m_tAssoc{MilliSeconds(100)};
    Time m_ctrlDelay{NanoSeconds(0)};

    double m_w1{10.0};
    double m_w2{1.0};
    double m_w3{1.0};
    int16_t m_scoreThresholdQ{0};
    uint16_t m_loadMaxQ{60000};
    uint8_t m_maxUePerSched{CfMimoConstants::MAX_UE_PER_AP_SCHED};

    bool m_running{false};
    uint16_t m_loadQ{0};
    uint16_t m_fhCapQ{65535};
    uint16_t m_interfQ{0};
    uint32_t m_clusterVerActive{0};

    std::map<UE_ID, ReportWithTs> m_ueReports;
    std::map<UE_ID, uint32_t> m_ueQueueBytes;
    std::map<UE_ID, RxStatus> m_lastRxStatus;
    std::map<AP_ID, NeighborCoord> m_neighborTable;

    std::map<UE_ID, Assignment> m_clusterMapActive;
    std::map<UE_ID, Assignment> m_clusterMapPending;

    OfferSink m_offerSink;
    AssocAckSink m_assocAckSink;
    GrantSink m_grantSink;
    NeighborCoordSink m_neighborCoordSink;
    ApSummarySink m_apSummarySink;
    ClusterDecisionAckSink m_clusterDecisionAckSink;

    EventId m_cohEvent;
    EventId m_coordEvent;
    EventId m_assocEvent;
    EventId m_ttiEvent;
};

} // namespace ns3

#endif // CF_AP_MAC_H
