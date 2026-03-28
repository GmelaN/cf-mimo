// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/nr-dl-cluster-coordinator.h"
#include "ns3/nr-dl-cluster-context-manager.h"
#include "ns3/nr-dl-cluster-selection-mechanism.h"
#include "ns3/nr-gnb-cmac-sap.h"
#include "ns3/nr-gnb-mac.h"
#include "ns3/nr-gnb-phy.h"
#include "ns3/nr-mac-sap.h"
#include "ns3/nr-mac-csched-sap.h"
#include "ns3/packet-burst.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <memory>

namespace ns3
{

class TestDlClusterApAgent : public NrDlClusterApAgent
{
  public:
    static TypeId GetTypeId();

    TestDlClusterApAgent(uint16_t cellId, bool accepted, double priorityMetric)
        : m_cellId(cellId),
          m_accepted(accepted),
          m_priorityMetric(priorityMetric)
    {
    }

    uint16_t GetCellId() const override
    {
        return m_cellId;
    }

    NrDlClusterParticipationResponse EvaluateParticipation(
        const NrDlClusterParticipationRequest& request) override
    {
        ++m_numRequests;
        m_lastRequest = request;
        return NrDlClusterParticipationResponse{m_cellId, m_accepted, m_priorityMetric};
    }

    uint32_t GetNumRequests() const
    {
        return m_numRequests;
    }

    NrDlClusterParticipationRequest GetLastRequest() const
    {
        return m_lastRequest;
    }

    void ReceiveDlGrant(const NrDlClusterGrant& grant) override
    {
        ++m_numReceivedGrants;
        m_lastGrant = grant;
        m_lastGrantRxTime = Simulator::Now();
    }

    uint32_t GetNumReceivedGrants() const
    {
        return m_numReceivedGrants;
    }

    NrDlClusterGrant GetLastGrant() const
    {
        return m_lastGrant;
    }

    Time GetLastGrantRxTime() const
    {
        return m_lastGrantRxTime;
    }

  private:
    uint16_t m_cellId{0};
    bool m_accepted{false};
    double m_priorityMetric{0.0};
    uint32_t m_numRequests{0};
    uint32_t m_numReceivedGrants{0};
    NrDlClusterParticipationRequest m_lastRequest{};
    NrDlClusterGrant m_lastGrant{};
    Time m_lastGrantRxTime{Seconds(-1)};
};

NS_OBJECT_ENSURE_REGISTERED(TestDlClusterApAgent);

TypeId
TestDlClusterApAgent::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TestDlClusterApAgent").SetParent<NrDlClusterApAgent>().SetGroupName("Nr");
    return tid;
}

class TestNrPhySapProvider : public NrPhySapProvider
{
  public:
    explicit TestNrPhySapProvider(uint16_t cellId, uint16_t bwpId = 0, uint32_t rbNum = 50)
        : m_cellId(cellId),
          m_bwpId(bwpId),
          m_rbNum(rbNum)
    {
    }

    void SendMacPdu(const Ptr<Packet>& p, const SfnSf& sfn, uint8_t symStart, uint16_t rnti) override
    {
        m_txCount++;
        m_lastPacket = p;
        m_lastSfn = sfn;
        m_lastSymStart = symStart;
        m_lastRnti = rnti;
    }

    void SendControlMessage(Ptr<NrControlMessage> msg) override
    {
        m_lastCtrlMsg = msg;
    }

    void SendRachPreamble(uint8_t, uint8_t) override
    {
    }

    void SetSlotAllocInfo(const SlotAllocInfo& slotAllocInfo) override
    {
        m_lastSlotAllocInfo = slotAllocInfo;
        m_hasSlotAllocInfo = true;
    }

    void NotifyConnectionSuccessful() override
    {
    }

    BeamId GetBeamId(uint8_t) const override
    {
        return BeamId(0, 0);
    }

    Ptr<const SpectrumModel> GetSpectrumModel() override
    {
        return nullptr;
    }

    uint16_t GetBwpId() const override
    {
        return m_bwpId;
    }

    uint16_t GetCellId() const override
    {
        return m_cellId;
    }

    uint32_t GetSymbolsPerSlot() const override
    {
        return 14;
    }

    Time GetSlotPeriod() const override
    {
        return MilliSeconds(1);
    }

    uint32_t GetRbNum() const override
    {
        return m_rbNum;
    }

    void RegisterToGnb(uint16_t) override
    {
    }

    uint32_t GetArfcn() const override
    {
        return 0;
    }

    bool HasSlotAllocInfo() const
    {
        return m_hasSlotAllocInfo;
    }

    uint32_t GetTxCount() const
    {
        return m_txCount;
    }

  private:
    uint16_t m_cellId;
    uint16_t m_bwpId;
    uint32_t m_rbNum;
    uint32_t m_txCount{0};
    bool m_hasSlotAllocInfo{false};
    Ptr<Packet> m_lastPacket;
    Ptr<NrControlMessage> m_lastCtrlMsg;
    SfnSf m_lastSfn{0, 0, 0, 0};
    SlotAllocInfo m_lastSlotAllocInfo{SfnSf(0, 0, 0, 0)};
    uint8_t m_lastSymStart{0};
    uint16_t m_lastRnti{0};
};

class TestClusterPeerPhy : public NrGnbPhy
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::TestClusterPeerPhy").SetParent<NrGnbPhy>().SetGroupName("Nr");
        return tid;
    }

    void ConfigureForTest(uint16_t cellId, uint16_t numerology = 0)
    {
        DoSetCellId(cellId);
        SetNumerology(numerology);
    }

    bool HasAllocation(const SfnSf& sfn)
    {
        return SlotAllocInfoExists(sfn);
    }

    uint32_t GetAllocationCount(const SfnSf& sfn)
    {
        return static_cast<uint32_t>(PeekSlotAllocInfo(sfn).m_varTtiAllocInfo.size());
    }

    Ptr<PacketBurst> TakeBurst(const SfnSf& sfn, uint8_t symStart, uint16_t rnti)
    {
        return GetPacketBurst(sfn, symStart, rnti);
    }
};

NS_OBJECT_ENSURE_REGISTERED(TestClusterPeerPhy);

class TestMacRlc : public NrMacSapUser
{
  public:
    explicit TestMacRlc(NrMacSapProvider* macSapProvider)
        : m_macSapProvider(macSapProvider)
    {
    }

    void NotifyTxOpportunity(TxOpportunityParameters params) override
    {
        auto packet = Create<Packet>(params.bytes);
        NrMacSapProvider::TransmitPduParameters txParams;
        txParams.pdu = packet;
        txParams.rnti = params.rnti;
        txParams.lcid = params.lcid;
        txParams.layer = params.layer;
        txParams.harqProcessId = params.harqId;
        txParams.componentCarrierId = params.componentCarrierId;
        m_macSapProvider->TransmitPdu(txParams);
    }

    void NotifyHarqDeliveryFailure() override
    {
    }

    void ReceivePdu(ReceivePduParameters) override
    {
    }

  private:
    NrMacSapProvider* m_macSapProvider;
};

class TestMacCschedSapProvider : public NrMacCschedSapProvider
{
  public:
    void CschedCellConfigReq(const CschedCellConfigReqParameters&) override
    {
    }

    void CschedUeConfigReq(const CschedUeConfigReqParameters&) override
    {
    }

    void CschedLcConfigReq(const CschedLcConfigReqParameters&) override
    {
    }

    void CschedLcReleaseReq(const CschedLcReleaseReqParameters&) override
    {
    }

    void CschedUeReleaseReq(const CschedUeReleaseReqParameters&) override
    {
    }
};

class NrDlClusterCoordinatorBasicTestCase : public TestCase
{
  public:
    NrDlClusterCoordinatorBasicTestCase()
        : TestCase("DL cluster coordinator selects accepted APs and forwards the request")
    {
    }

  private:
    void DoRun() override
    {
        Simulator::Destroy();

        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(2));

        auto ap10 = CreateObject<TestDlClusterApAgent>(10, true, 0.30);
        auto ap20 = CreateObject<TestDlClusterApAgent>(20, false, 0.95);
        auto ap30 = CreateObject<TestDlClusterApAgent>(30, true, 0.80);

        coordinator->RegisterApAgent(ap10);
        coordinator->RegisterApAgent(ap20);
        coordinator->RegisterApAgent(ap30);

        coordinator->AssociateUe(NrDlClusterUeAssociation{1, 10, {20, 30}});
        auto grant = coordinator->CreateDlGrant(1, 7, 2048, 14);

        NS_TEST_ASSERT_MSG_EQ(grant.granted, true, "The coordinator should create a DL grant");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.size(),
                              2U,
                              "Two APs should be selected for the cluster grant");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.at(0),
                              10,
                              "The coordinator AP should lead the selected AP set");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.at(1),
                              30,
                              "The second accepted AP should be selected");

        coordinator->DispatchDlGrant(grant);
        Simulator::Run();
        Simulator::Destroy();

        for (auto ap : {ap10, ap20, ap30})
        {
            NS_TEST_ASSERT_MSG_EQ(ap->GetNumRequests(), 1U, "Each AP should evaluate the request");
            auto lastRequest = ap->GetLastRequest();
            NS_TEST_ASSERT_MSG_EQ(lastRequest.imsi, 1U, "Wrong IMSI in AP request");
            NS_TEST_ASSERT_MSG_EQ(lastRequest.coordinatorCellId,
                                  10,
                                  "Wrong coordinator cell id in AP request");
            NS_TEST_ASSERT_MSG_EQ(lastRequest.slotIndex, 7U, "Wrong slot index in AP request");
            NS_TEST_ASSERT_MSG_EQ(lastRequest.tbSize, 2048U, "Wrong TB size in AP request");
            NS_TEST_ASSERT_MSG_EQ(lastRequest.mcs, 14U, "Wrong MCS in AP request");
        }

        NS_TEST_ASSERT_MSG_EQ(ap10->GetNumReceivedGrants(),
                              1U,
                              "Coordinator AP should receive the DL grant");
        NS_TEST_ASSERT_MSG_EQ(ap30->GetNumReceivedGrants(),
                              1U,
                              "Selected secondary AP should receive the DL grant");
        NS_TEST_ASSERT_MSG_EQ(ap20->GetNumReceivedGrants(),
                              0U,
                              "Rejected AP should not receive the DL grant");
        NS_TEST_ASSERT_MSG_EQ(ap10->GetLastGrant().imsi, 1U, "Wrong IMSI in dispatched grant");
        NS_TEST_ASSERT_MSG_EQ(ap30->GetLastGrant().imsi, 1U, "Wrong IMSI in dispatched grant");
        NS_TEST_ASSERT_MSG_EQ(ap10->GetLastGrantRxTime(),
                              ap30->GetLastGrantRxTime(),
                              "Selected APs should receive the grant at the same simulation time");
    }
};

class NrDlClusterCoordinatorCapTestCase : public TestCase
{
  public:
    NrDlClusterCoordinatorCapTestCase()
        : TestCase("DL cluster coordinator enforces the configured AP cap")
    {
    }

  private:
    void DoRun() override
    {
        Simulator::Destroy();

        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(1));

        coordinator->RegisterApAgent(CreateObject<TestDlClusterApAgent>(10, true, 0.40));
        coordinator->RegisterApAgent(CreateObject<TestDlClusterApAgent>(20, true, 0.90));
        coordinator->RegisterApAgent(CreateObject<TestDlClusterApAgent>(30, true, 0.80));

        coordinator->AssociateUe(NrDlClusterUeAssociation{2, 10, {20, 30}});
        auto grant = coordinator->CreateDlGrant(2, 3, 1024, 7);

        NS_TEST_ASSERT_MSG_EQ(grant.granted, true, "The DL grant should still be created");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.size(),
                              1U,
                              "Only one AP should be kept due to the cap");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.front(),
                              10,
                              "The coordinator AP should be retained when the cap is one");
        Simulator::Destroy();
    }
};

class NrDlClusterCoordinatorRejectTestCase : public TestCase
{
  public:
    NrDlClusterCoordinatorRejectTestCase()
        : TestCase("DL cluster coordinator returns an empty grant when all APs reject")
    {
    }

  private:
    void DoRun() override
    {
        Simulator::Destroy();

        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(3));

        coordinator->RegisterApAgent(CreateObject<TestDlClusterApAgent>(10, false, 0.20));
        coordinator->RegisterApAgent(CreateObject<TestDlClusterApAgent>(20, false, 0.40));

        coordinator->AssociateUe(NrDlClusterUeAssociation{3, 10, {20}});
        auto grant = coordinator->CreateDlGrant(3, 11, 512, 4);

        NS_TEST_ASSERT_MSG_EQ(grant.granted, false, "Grant should be rejected when all APs reject");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.empty(),
                              true,
                              "No AP should be selected when all APs reject");
        Simulator::Destroy();
    }
};

class NrDlClusterCoordinatorDelayedDispatchTestCase : public TestCase
{
  public:
    NrDlClusterCoordinatorDelayedDispatchTestCase()
        : TestCase("DL cluster coordinator dispatches selected AP grants on the requested delay")
    {
    }

  private:
    void DoRun() override
    {
        Simulator::Destroy();

        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(2));

        auto ap10 = CreateObject<TestDlClusterApAgent>(10, true, 0.60);
        auto ap30 = CreateObject<TestDlClusterApAgent>(30, true, 0.55);
        coordinator->RegisterApAgent(ap10);
        coordinator->RegisterApAgent(ap30);
        coordinator->AssociateUe(NrDlClusterUeAssociation{4, 10, {30}});

        auto grant = coordinator->CreateAndDispatchDlGrant(4, 12, 4096, 18, MilliSeconds(2));
        NS_TEST_ASSERT_MSG_EQ(grant.granted, true, "Delayed dispatch should still create a grant");

        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_EQ(ap10->GetNumReceivedGrants(),
                              1U,
                              "Coordinator AP should receive the delayed grant");
        NS_TEST_ASSERT_MSG_EQ(ap30->GetNumReceivedGrants(),
                              1U,
                              "Secondary AP should receive the delayed grant");
        NS_TEST_ASSERT_MSG_EQ(ap10->GetLastGrantRxTime(),
                              MilliSeconds(2),
                              "Coordinator AP received the grant at an unexpected time");
        NS_TEST_ASSERT_MSG_EQ(ap30->GetLastGrantRxTime(),
                              MilliSeconds(2),
                              "Secondary AP received the grant at an unexpected time");
    }
};

class NrDlClusterCoordinatorReselectionTestCase : public TestCase
{
  public:
    NrDlClusterCoordinatorReselectionTestCase()
        : TestCase("Coordinator reselection picks the highest-metric AP and updates peer set")
    {
    }

  private:
    void DoRun() override
    {
        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(2));
        auto contextManager = CreateObject<NrDlClusterContextManager>();
        auto selectionMechanism = CreateObject<NrDlClusterSelectionMechanismDefault>();
        contextManager->SetCoordinator(coordinator);
        contextManager->SetSelectionMechanism(selectionMechanism);
        contextManager->RegisterApAgent(CreateObject<TestDlClusterApAgent>(10, true, 0.30));
        contextManager->RegisterApAgent(CreateObject<TestDlClusterApAgent>(20, true, 0.95));
        contextManager->RegisterApAgent(CreateObject<TestDlClusterApAgent>(30, true, 0.60));
        contextManager->AssociateUe(NrDlClusterUeAssociation{5, 10, {20, 30}});

        auto updated = contextManager->ReselectCoordinator(5, {10, 20, 30}, 8, 2048, 12);

        NS_TEST_ASSERT_MSG_EQ(updated.coordinatorCellId,
                              20,
                              "Coordinator reselection should switch to the highest metric AP");
        NS_TEST_ASSERT_MSG_EQ(updated.candidateApCellIds.size(),
                              2u,
                              "The remaining accepted APs should stay as candidates");
        NS_TEST_ASSERT_MSG_EQ(contextManager->GetCurrentCoordinatorCellId(5),
                              20,
                              "Stored coordinator cell id was not updated");

        auto grant = contextManager->CreateDlGrant(5, 9, 2048, 12);
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.size(),
                              2u,
                              "Grant should include the new coordinator and one peer AP");
        NS_TEST_ASSERT_MSG_EQ(grant.selectedApCellIds.at(0),
                              20,
                              "The new coordinator must be the first selected AP");
    }
};

class NrDlClusterSelectionMechanismTriggerTestCase : public TestCase
{
  public:
    NrDlClusterSelectionMechanismTriggerTestCase()
        : TestCase("Selection mechanism triggers updates only when the candidate cluster changes")
    {
    }

  private:
    void DoRun() override
    {
        auto selectionMechanism = CreateObject<NrDlClusterSelectionMechanismDefault>();
        NrDlClusterUeAssociation association{9, 20, {30}};

        NS_TEST_ASSERT_MSG_EQ(selectionMechanism->ShouldTriggerClusterUpdate(association, {30}),
                              false,
                              "Unchanged candidate APs should not trigger a cluster update");
        NS_TEST_ASSERT_MSG_EQ(selectionMechanism->ShouldTriggerClusterUpdate(association, {10, 30}),
                              true,
                              "Changed candidate APs should trigger a cluster update");
        NS_TEST_ASSERT_MSG_EQ(selectionMechanism->ShouldTriggerClusterUpdate(association, {10}),
                              true,
                              "Shrinking the candidate AP set should trigger a cluster update");
    }
};

class NrDlClusterMacMirrorTestCase : public TestCase
{
  public:
    NrDlClusterMacMirrorTestCase()
        : TestCase("Primary MAC mirrors DL allocation and packet burst to selected peer PHYs")
    {
    }

  private:
    void DoRun() override
    {
        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(2));
        auto contextManager = CreateObject<NrDlClusterContextManager>();
        auto selectionMechanism = CreateObject<NrDlClusterSelectionMechanismDefault>();
        contextManager->SetCoordinator(coordinator);
        contextManager->SetSelectionMechanism(selectionMechanism);
        contextManager->RegisterApAgent(CreateObject<TestDlClusterApAgent>(10, true, 1.0));
        contextManager->RegisterApAgent(CreateObject<TestDlClusterApAgent>(30, true, 0.8));
        contextManager->AssociateUe(NrDlClusterUeAssociation{7, 10, {30}});

        auto mac = CreateObject<NrGnbMac>();
        auto primaryPhySap = std::make_unique<TestNrPhySapProvider>(10);
        auto cschedSap = std::make_unique<TestMacCschedSapProvider>();
        mac->SetPhySapProvider(primaryPhySap.get());
        mac->SetNrMacCschedSapProvider(cschedSap.get());
        mac->SetDlClusterContextManager(contextManager);

        auto peerPhy = CreateObject<TestClusterPeerPhy>();
        peerPhy->ConfigureForTest(30);
        mac->RegisterDlClusterPeerPhy(30, peerPhy);

        mac->GetGnbCmacSapProvider()->AddUe(7);
        auto rlc = std::make_unique<TestMacRlc>(mac->GetMacSapProvider());
        NrGnbCmacSapProvider::LcInfo lcInfo{};
        lcInfo.rnti = 7;
        lcInfo.lcId = 4;
        lcInfo.lcGroup = 0;
        lcInfo.fiveQi = 9;
        mac->GetGnbCmacSapProvider()->AddLc(lcInfo, rlc.get());

        auto dci = std::make_shared<DciInfoElementTdma>(7,
                                                        DciInfoElementTdma::DL,
                                                        2,
                                                        4,
                                                        10,
                                                        1,
                                                        nullptr,
                                                        100,
                                                        1,
                                                        0,
                                                        DciInfoElementTdma::DATA,
                                                        0,
                                                        0);
        dci->m_harqProcess = 1;
        dci->m_rbgBitmask = {true, true};

        SfnSf sfn(0, 0, 0, 0);
        mac->SetCurrentSfn(sfn);
        NrMacSchedSapUser::SchedConfigIndParameters ind(sfn);
        VarTtiAllocInfo slotInfo(dci);
        slotInfo.m_rlcPduInfo.emplace_back(lcInfo.lcId, dci->m_tbSize - 3);
        ind.m_slotAllocInfo.m_varTtiAllocInfo.emplace_back(slotInfo);
        ind.m_slotAllocInfo.m_numSymAlloc += dci->m_numSym;

        mac->GetNrMacSchedSapUser()->SchedConfigInd(ind);

        NS_TEST_ASSERT_MSG_EQ(primaryPhySap->HasSlotAllocInfo(),
                              true,
                              "Primary PHY SAP did not receive slot allocation");
        NS_TEST_ASSERT_MSG_EQ(primaryPhySap->GetTxCount(),
                              1u,
                              "Primary PHY SAP did not receive the MAC PDU");
        NS_TEST_ASSERT_MSG_EQ(peerPhy->HasAllocation(sfn),
                              true,
                              "Peer PHY did not receive mirrored allocation");
        NS_TEST_ASSERT_MSG_EQ(peerPhy->GetAllocationCount(sfn),
                              1u,
                              "Peer PHY should contain exactly one mirrored allocation");

        auto burst = peerPhy->TakeBurst(sfn, dci->m_symStart, dci->m_rnti);
        NS_TEST_ASSERT_MSG_NE(burst, nullptr, "Peer PHY did not receive mirrored packet burst");
        NS_TEST_ASSERT_MSG_EQ(burst->GetNPackets(),
                              1u,
                              "Peer PHY mirrored packet burst should contain one packet");
    }
};

class NrDlClusterCoordinatorSuite : public TestSuite
{
  public:
    NrDlClusterCoordinatorSuite()
        : TestSuite("nr-test-dl-cluster-coordinator", Type::UNIT)
    {
        AddTestCase(new NrDlClusterCoordinatorBasicTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterCoordinatorCapTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterCoordinatorRejectTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterCoordinatorDelayedDispatchTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterCoordinatorReselectionTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterSelectionMechanismTriggerTestCase(), Duration::QUICK);
        AddTestCase(new NrDlClusterMacMirrorTestCase(), Duration::QUICK);
    }
};

static NrDlClusterCoordinatorSuite g_nrDlClusterCoordinatorSuite;

} // namespace ns3
