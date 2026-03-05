/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_UE_MAC_H
#define CF_UE_MAC_H

#include "cf-mimo-common.h"
#include "cf-mimo-messages.h"

#include "ns3/event-id.h"
#include "ns3/object.h"

#include <array>
#include <functional>
#include <map>
#include <vector>

namespace ns3
{

class CfUeMac : public Object
{
  public:
    static TypeId GetTypeId();

    CfUeMac();
    ~CfUeMac() override;

    using StateReportSink = std::function<void(const UEStateReport&, AP_ID)>;
    using AssocConfirmSink = std::function<void(const AssocConfirm&, AP_ID)>;
    using SrBsrSink = std::function<void(const SR_BSR&, AP_ID)>;
    using RxStatusSink = std::function<void(const RxStatus&, AP_ID)>;
    using LinkMeasurementProvider = std::function<std::vector<UeApMeasurement>()>;

    void SetUeId(UE_ID ueId);
    UE_ID GetUeId() const;

    void SetMode(CfMode mode);
    CfMode GetMode() const;

    void SetIntervals(Time tti, Time tState, Time tAssoc);
    void SetOfferWindow(Time offerWindow);

    void SetTopNAp(uint8_t topNAp);
    void SetMaxClusterSize(uint8_t maxClusterSize);
    void SetLambda(double lambda);
    void SetLoadRejectThreshold(uint16_t loadThresholdQ);

    void SetQueueBytes(uint32_t queueBytes);
    uint32_t GetQueueBytes() const;
    void SetTrafficClass(uint8_t trafficClass);

    void SetStateReportSink(StateReportSink cb);
    void SetAssocConfirmSink(AssocConfirmSink cb);
    void SetSrBsrSink(SrBsrSink cb);
    void SetRxStatusSink(RxStatusSink cb);
    void SetLinkMeasurementProvider(LinkMeasurementProvider cb);

    void Start();
    void Stop();

    void ReceiveAssocOffer(const AssocOffer& offer);
    void ReceiveSchedulingGrant(const SchedulingGrant& grant);
    void ReceiveCentralizedAssoc(const AssocConfirm& confirm);

    UeMacState GetState() const;
    AP_ID GetPrimaryApActive() const;
    uint32_t GetClusterVerActive() const;
    std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE> GetClusterMembersActive() const;

  protected:
    void DoDispose() override;

  private:
    struct PendingCluster
    {
        bool valid{false};
        uint32_t validFromEpoch{0};
        uint32_t clusterVer{0};
        AP_ID primaryAp{INVALID_AP_ID};
        uint8_t clusterSize{0};
        std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE> members{MakeInvalidClusterMembers()};
    };

    void SchedulePeriodicEvents();
    void CancelEvents();
    void OnStateReportTimer();
    void OnAssocTimer();
    void OnAssocDecisionTimer(uint32_t assocEpoch);
    void OnTtiTimer();

    void BuildAndSendStateReport();
    void DecideDistributedCluster(uint32_t assocEpoch);
    void ApplyPendingClusterIfDue();

    void SendSrBsr();
    void ConsumeGrant();

    uint32_t GetCurrentEpoch() const;
    uint32_t GetCurrentTtiIndex() const;
    std::vector<UeApMeasurement> GetSortedMeasurements() const;

    UE_ID m_ueId{0};
    CfMode m_mode{CfMode::DISTRIBUTED};
    UeMacState m_state{UeMacState::UE_TRACK};

    Time m_tti{MicroSeconds(500)};
    Time m_tState{MilliSeconds(10)};
    Time m_tAssoc{MilliSeconds(100)};
    Time m_offerWindow{MilliSeconds(5)};

    uint8_t m_topNAp{CfMimoConstants::TOP_N_AP};
    uint8_t m_maxClusterSize{CfMimoConstants::MAX_CLUSTER_SIZE};
    uint16_t m_loadRejectThresholdQ{60000};
    double m_lambda{0.1};

    bool m_running{false};
    uint32_t m_queueBytes{0};
    uint8_t m_trafficClass{0};

    uint32_t m_offerEpoch{0};
    uint32_t m_nextClusterVer{1};

    std::vector<UeApMeasurement> m_measurementCache;
    std::map<AP_ID, AssocOffer> m_offerBuffer;

    PendingCluster m_pendingCluster;
    AP_ID m_primaryApActive{INVALID_AP_ID};
    uint32_t m_clusterVerActive{0};
    uint8_t m_clusterSizeActive{0};
    std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE> m_clusterMembersActive{MakeInvalidClusterMembers()};

    SchedulingGrant m_lastGrant{};
    bool m_hasGrant{false};

    StateReportSink m_stateReportSink;
    AssocConfirmSink m_assocConfirmSink;
    SrBsrSink m_srBsrSink;
    RxStatusSink m_rxStatusSink;
    LinkMeasurementProvider m_linkMeasurementProvider;

    EventId m_stateReportEvent;
    EventId m_assocEvent;
    EventId m_assocDecisionEvent;
    EventId m_ttiEvent;
};

} // namespace ns3

#endif // CF_UE_MAC_H
