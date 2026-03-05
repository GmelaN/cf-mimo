/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/applications-module.h"
#include "ns3/cf-ap-mac.h"
#include "ns3/cf-ap-phy.h"
#include "ns3/cf-cpu-controller.h"
#include "ns3/cf-phy-mac-helper.h"
#include "ns3/cf-ue-mac.h"
#include "ns3/cf-ue-phy.h"
#include "ns3/cf-ue-traffic-adapter.h"
#include "ns3/channel-condition-model.h"
#include "ns3/command-line.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/double.h"
#include "ns3/internet-module.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/simple-net-device.h"
#include "ns3/spectrum-phy.h"
#include "ns3/three-gpp-propagation-loss-model.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

using namespace ns3;

static Ptr<MultiModelSpectrumChannel>
Create3gppChannel(double frequencyHz)
{
    auto channel = CreateObject<MultiModelSpectrumChannel>();
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

    auto loss = CreateObject<ThreeGppUmiStreetCanyonPropagationLossModel>();
    loss->SetAttribute("Frequency", DoubleValue(frequencyHz));
    loss->SetAttribute("ShadowingEnabled", BooleanValue(false));

    auto cond = CreateObject<ThreeGppUmiStreetCanyonChannelConditionModel>();
    cond->SetAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
    loss->SetChannelConditionModel(cond);
    channel->AddPropagationLossModel(loss);
    return channel;
}

static void
AttachNodeContext(Ptr<SpectrumPhy> phy, const Vector& v)
{
    auto node = CreateObject<Node>();
    auto dev = CreateObject<SimpleNetDevice>();
    node->AddDevice(dev);
    dev->SetNode(node);

    auto m = CreateObject<ConstantPositionMobilityModel>();
    m->SetPosition(v);
    node->AggregateObject(m);

    phy->SetDevice(dev);
    phy->SetMobility(m);
}

struct EpochDecisions
{
    std::map<AP_ID, ClusterDecision> byAp;
};

static void
OnAppRxTrace(uint16_t ueId,
             Ptr<CfUeMac> ueMac,
             uint64_t* rxPkts,
             uint64_t* rxBytes,
             Ptr<const Packet> p,
             const Address&)
{
    ++(*rxPkts);
    *rxBytes += p->GetSize();
    std::cout << "[RxTrace UE" << ueId << "] t=" << Simulator::Now().GetMilliSeconds() << "ms"
              << " pkt=" << p->GetSize() << "B"
              << " primary=" << ueMac->GetPrimaryApActive()
              << " clusterVer=" << ueMac->GetClusterVerActive()
              << " cfQueue=" << ueMac->GetQueueBytes() << "\n";
}

int
main(int argc, char* argv[])
{
    double simTimeMs = 800.0;
    std::string appRate = "8Mbps";
    uint32_t appPktSize = 900;
    double appStartMs = 220.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTimeMs", "Simulation time in milliseconds", simTimeMs);
    cmd.AddValue("appRate", "OnOff application data rate", appRate);
    cmd.AddValue("appPktSize", "OnOff packet size in bytes", appPktSize);
    cmd.AddValue("appStartMs", "Application start time in milliseconds", appStartMs);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(4); // 0 UE1, 1 Sink1, 2 UE2, 3 Sink2

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("200Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devsUe1 = p2p.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer devsUe2 = p2p.Install(nodes.Get(2), nodes.Get(3));

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.20.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs1 = ipv4.Assign(devsUe1);
    ipv4.SetBase("10.20.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs2 = ipv4.Assign(devsUe2);

    PacketSinkHelper sink1Helper("ns3::UdpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), 5201));
    PacketSinkHelper sink2Helper("ns3::UdpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), 5202));
    ApplicationContainer sink1Apps = sink1Helper.Install(nodes.Get(1));
    ApplicationContainer sink2Apps = sink2Helper.Install(nodes.Get(3));
    sink1Apps.Start(Seconds(0.0));
    sink2Apps.Start(Seconds(0.0));
    sink1Apps.Stop(MilliSeconds(simTimeMs));
    sink2Apps.Stop(MilliSeconds(simTimeMs));

    OnOffHelper onoff1("ns3::UdpSocketFactory", InetSocketAddress(ifs1.GetAddress(1), 5201));
    onoff1.SetAttribute("DataRate", DataRateValue(DataRate(appRate)));
    onoff1.SetAttribute("PacketSize", UintegerValue(appPktSize));
    onoff1.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff1.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer ue1Tx = onoff1.Install(nodes.Get(0));

    OnOffHelper onoff2("ns3::UdpSocketFactory", InetSocketAddress(ifs2.GetAddress(1), 5202));
    onoff2.SetAttribute("DataRate", DataRateValue(DataRate(appRate)));
    onoff2.SetAttribute("PacketSize", UintegerValue(appPktSize));
    onoff2.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff2.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer ue2Tx = onoff2.Install(nodes.Get(2));

    ue1Tx.Start(MilliSeconds(appStartMs));
    ue2Tx.Start(MilliSeconds(appStartMs));
    ue1Tx.Stop(MilliSeconds(simTimeMs - 50.0));
    ue2Tx.Stop(MilliSeconds(simTimeMs - 50.0));

    std::map<AP_ID, Ptr<CfApMac>> apById;
    std::map<AP_ID, Ptr<CfApPhy>> apPhyById;
    for (AP_ID apId = 1; apId <= 3; ++apId)
    {
        auto ap = CreateObject<CfApMac>();
        ap->SetApId(apId);
        ap->SetMode(CfMode::CENTRALIZED);
        apById[apId] = ap;
    }

    std::map<UE_ID, Ptr<CfUeMac>> ueById;
    for (UE_ID ueId = 1; ueId <= 2; ++ueId)
    {
        auto ue = CreateObject<CfUeMac>();
        ue->SetUeId(ueId);
        ue->SetMode(CfMode::CENTRALIZED);
        ue->SetQueueBytes(0);
        ue->SetTrafficClass(ueId);
        ueById[ueId] = ue;
    }

    const double freqHz = 3.5e9;
    const double prbBwHz = 180000.0;
    auto channel = Create3gppChannel(freqHz);

    std::map<UE_ID, Ptr<CfUePhy>> uePhyById;
    uePhyById[1] = CreateObject<CfUePhy>();
    uePhyById[1]->SetUeId(1);
    uePhyById[1]->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
    uePhyById[1]->SetChannel(channel);
    AttachNodeContext(uePhyById[1], Vector(25.0, 0.0, 1.5));
    channel->AddRx(uePhyById[1]);
    CfPhyMacHelper::BindUeMeasurementProvider(ueById.at(1), uePhyById.at(1));

    uePhyById[2] = CreateObject<CfUePhy>();
    uePhyById[2]->SetUeId(2);
    uePhyById[2]->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
    uePhyById[2]->SetChannel(channel);
    AttachNodeContext(uePhyById[2], Vector(125.0, 0.0, 1.5));
    channel->AddRx(uePhyById[2]);
    CfPhyMacHelper::BindUeMeasurementProvider(ueById.at(2), uePhyById.at(2));

    std::vector<Vector> apPositions = {Vector(0.0, 0.0, 10.0),
                                       Vector(65.0, 0.0, 10.0),
                                       Vector(150.0, 0.0, 10.0)};
    for (AP_ID apId = 1; apId <= apPositions.size(); ++apId)
    {
        auto apPhy = CreateObject<CfApPhy>();
        apPhy->SetApId(apId);
        apPhy->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
        apPhy->SetPilotPowerW(1.0);
        apPhy->SetPilotPrbBitmap(UINT64_MAX);
        apPhy->SetChannel(channel);
        AttachNodeContext(apPhy, apPositions[apId - 1]);
        channel->AddRx(apPhy);
        apPhyById[apId] = apPhy;
    }

    auto adapter1 = CreateObject<CfUeTrafficAdapter>();
    adapter1->SetUeMac(ueById.at(1));
    const bool adapter1Ok = adapter1->Attach(devsUe1.Get(0));

    auto adapter2 = CreateObject<CfUeTrafficAdapter>();
    adapter2->SetUeMac(ueById.at(2));
    const bool adapter2Ok = adapter2->Attach(devsUe2.Get(0));

    for (const auto& [ueId, ue] : ueById)
    {
        ue->SetStateReportSink([&apById](const UEStateReport& report, AP_ID apId) {
            const auto it = apById.find(apId);
            if (it != apById.end())
            {
                it->second->ReceiveUeStateReport(report);
            }
        });
        ue->SetSrBsrSink([&apById](const SR_BSR& srBsr, AP_ID apId) {
            const auto it = apById.find(apId);
            if (it != apById.end())
            {
                it->second->ReceiveSrBsr(srBsr);
            }
        });
        ue->SetAssocConfirmSink([](const AssocConfirm&, AP_ID) {});
        ue->SetRxStatusSink([&apById](const RxStatus& status, AP_ID apId) {
            const auto it = apById.find(apId);
            if (it != apById.end())
            {
                it->second->ReceiveRxStatus(status);
            }
        });
    }

    auto cpu = CreateObject<CfCpuController>();
    std::map<uint32_t, EpochDecisions> decisionsByEpoch;

    cpu->SetDecisionSink([&](const ClusterDecision& decision, AP_ID apId) {
        const auto apIt = apById.find(apId);
        if (apIt != apById.end())
        {
            apIt->second->ReceiveClusterDecision(decision);
        }

        auto& epochEntry = decisionsByEpoch[decision.assoc_epoch];
        epochEntry.byAp[apId] = decision;
        if (epochEntry.byAp.size() < apById.size())
        {
            return;
        }

        struct UeClusterDraft
        {
            AP_ID primary{INVALID_AP_ID};
            std::vector<AP_ID> members;
            uint32_t clusterVer{0};
        };
        std::map<UE_ID, UeClusterDraft> byUe;

        for (const auto& [dApId, d] : epochEntry.byAp)
        {
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
            const auto ueIt = ueById.find(ueId);
            if (ueIt == ueById.end() || draft.members.empty())
            {
                continue;
            }

            AssocConfirm confirm{};
            confirm.ue_id = ueId;
            confirm.assoc_epoch = decision.assoc_epoch;
            confirm.cluster_ver = draft.clusterVer;
            confirm.primary_ap = (draft.primary == INVALID_AP_ID) ? draft.members.front() : draft.primary;
            confirm.cluster_size = static_cast<uint8_t>(
                std::min<size_t>(draft.members.size(), CfMimoConstants::MAX_CLUSTER_SIZE));
            for (uint8_t i = 0; i < confirm.cluster_size; ++i)
            {
                confirm.cluster_members[i] = draft.members[i];
            }
            ueIt->second->ReceiveCentralizedAssoc(confirm);
        }

        decisionsByEpoch.erase(decision.assoc_epoch);
    });

    for (const auto& [_, ap] : apById)
    {
        const AP_ID apId = ap->GetApId();
        ap->SetApSummarySink([cpu](const APSummary& summary) { cpu->ReceiveApSummary(summary); });
        CfPhyMacHelper::BindApGrantPath(ap, apPhyById[apId], ueById, uePhyById);
        ap->SetOfferSink([](const AssocOffer&, UE_ID) {});
        ap->SetNeighborCoordSink([](const NeighborCoord&, AP_ID) {});
        ap->Start();
        apPhyById[apId]->Start();
    }

    uint64_t rxPkts1 = 0, rxBytes1 = 0;
    uint64_t rxPkts2 = 0, rxBytes2 = 0;
    auto sink1 = DynamicCast<PacketSink>(sink1Apps.Get(0));
    auto sink2 = DynamicCast<PacketSink>(sink2Apps.Get(0));
    sink1->TraceConnectWithoutContext("Rx",
                                      MakeBoundCallback(&OnAppRxTrace,
                                                        static_cast<uint16_t>(1),
                                                        ueById.at(1),
                                                        &rxPkts1,
                                                        &rxBytes1));
    sink2->TraceConnectWithoutContext("Rx",
                                      MakeBoundCallback(&OnAppRxTrace,
                                                        static_cast<uint16_t>(2),
                                                        ueById.at(2),
                                                        &rxPkts2,
                                                        &rxBytes2));

    for (const auto& [_, ue] : ueById)
    {
        ue->Start();
    }
    cpu->Start();

    Simulator::Stop(MilliSeconds(simTimeMs));
    Simulator::Run();

    std::cout << "=== CF-MIMO Centralized Example ===\n";
    std::cout << "Adapter UE1/UE2 attached: " << (adapter1Ok ? "yes" : "no") << " / "
              << (adapter2Ok ? "yes" : "no") << "\n";
    std::cout << "App RX UE1 packets/bytes: " << rxPkts1 << " / " << rxBytes1 << "\n";
    std::cout << "App RX UE2 packets/bytes: " << rxPkts2 << " / " << rxBytes2 << "\n";

    for (const auto& [ueId, ue] : ueById)
    {
        std::cout << "UE " << ueId << " primary AP: " << ue->GetPrimaryApActive()
                  << " clusterVer: " << ue->GetClusterVerActive()
                  << " cfQueue=" << ue->GetQueueBytes()
                  << " injectedBytes="
                  << ((ueId == 1) ? adapter1->GetTotalInjectedBytes() : adapter2->GetTotalInjectedBytes())
                  << "\n";
    }

    std::cout << "Sink total bytes UE1/UE2: " << (sink1 ? sink1->GetTotalRx() : 0) << " / "
              << (sink2 ? sink2->GetTotalRx() : 0) << "\n";

    Simulator::Destroy();
    return 0;
}
