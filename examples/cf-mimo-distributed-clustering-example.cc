/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/applications-module.h"
#include "ns3/cf-ap-mac.h"
#include "ns3/cf-ap-phy.h"
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

static void
OnAppRxTrace(Ptr<CfUeMac> ueMac,
             uint64_t* rxPkts,
             uint64_t* rxBytes,
             Ptr<const Packet> p,
             const Address&)
{
    ++(*rxPkts);
    *rxBytes += p->GetSize();
    std::cout << "[RxTrace] t=" << Simulator::Now().GetMilliSeconds() << "ms"
              << " pkt=" << p->GetSize() << "B"
              << " primary=" << ueMac->GetPrimaryApActive()
              << " clusterVer=" << ueMac->GetClusterVerActive()
              << " cfQueue=" << ueMac->GetQueueBytes() << "\n";
}

int
main(int argc, char* argv[])
{
    LogComponentEnable("CfApMac", LOG_LEVEL_DEBUG);
    LogComponentEnable("CfUeMac", LOG_LEVEL_DEBUG);
    LogComponentEnable("CfCpuController", LOG_LEVEL_DEBUG);
    double simTimeMs = 10.0;
    std::string appRate = "10Mbps";
    uint32_t appPktSize = 900;
    double appStartMs = 1.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTimeMs", "Simulation time in milliseconds", simTimeMs);
    cmd.AddValue("appRate", "OnOff application data rate", appRate);
    cmd.AddValue("appPktSize", "OnOff packet size in bytes", appPktSize);
    cmd.AddValue("appStartMs", "Application start time in milliseconds", appStartMs);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(2); // 0: UE app node, 1: remote sink

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("200Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devs = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.10.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = ipv4.Assign(devs);

    const uint16_t port = 5100;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(MilliSeconds(simTimeMs));

    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(ifs.GetAddress(1), port));
    onoff.SetAttribute("DataRate", DataRateValue(DataRate(appRate)));
    onoff.SetAttribute("PacketSize", UintegerValue(appPktSize));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer txApps = onoff.Install(nodes.Get(0));
    txApps.Start(MilliSeconds(appStartMs));
    txApps.Stop(MilliSeconds(simTimeMs - 50.0));

    std::map<AP_ID, Ptr<CfApMac>> apById;
    std::map<AP_ID, Ptr<CfApPhy>> apPhyById;
    for (AP_ID apId = 1; apId <= 3; ++apId)
    {
        auto ap = CreateObject<CfApMac>();
        ap->SetApId(apId);
        ap->SetMode(CfMode::DISTRIBUTED);
        apById[apId] = ap;
    }

    const double freqHz = 3.5e9;
    const double prbBwHz = 180000.0;
    auto channel = Create3gppChannel(freqHz);

    auto ue = CreateObject<CfUeMac>();
    ue->SetUeId(1);
    ue->SetMode(CfMode::DISTRIBUTED);
    ue->SetQueueBytes(0);
    ue->SetTrafficClass(1);

    auto uePhy = CreateObject<CfUePhy>();
    uePhy->SetUeId(ue->GetUeId());
    uePhy->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
    uePhy->SetChannel(channel);
    AttachNodeContext(uePhy, Vector(25.0, 0.0, 1.5));
    channel->AddRx(uePhy);
    CfPhyMacHelper::BindUeMeasurementProvider(ue, uePhy);

    std::vector<Vector> apPositions = {Vector(0.0, 0.0, 10.0),
                                       Vector(65.0, 0.0, 10.0),
                                       Vector(140.0, 0.0, 10.0)};
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

    auto adapter = CreateObject<CfUeTrafficAdapter>();
    adapter->SetUeMac(ue);
    const bool adapterOk = adapter->Attach(devs.Get(0));

    ue->SetStateReportSink([&apById](const UEStateReport& report, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveUeStateReport(report);
        }
    });
    ue->SetAssocConfirmSink([&apById](const AssocConfirm& confirm, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveAssocConfirm(confirm);
        }
    });
    ue->SetSrBsrSink([&apById](const SR_BSR& srBsr, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveSrBsr(srBsr);
        }
    });
    ue->SetRxStatusSink([&apById](const RxStatus& status, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveRxStatus(status);
        }
    });

    for (const auto& [_, ap] : apById)
    {
        const AP_ID apId = ap->GetApId();
        ap->SetOfferSink([ue](const AssocOffer& offer, UE_ID ueId) {
            if (ueId == ue->GetUeId())
            {
                ue->ReceiveAssocOffer(offer);
            }
        });
        ap->SetAssocAckSink([ue](const AssocAck& ack, UE_ID ueId) {
            if (ueId == ue->GetUeId())
            {
                ue->ReceiveAssocAck(ack);
            }
        });
        CfPhyMacHelper::BindApGrantPath(ap, apPhyById[apId], ue, uePhy);
        ap->SetNeighborCoordSink([](const NeighborCoord&, AP_ID) {});
        ap->Start();
        apPhyById[apId]->Start();
    }

    uint64_t rxPkts = 0;
    uint64_t rxBytes = 0;
    auto sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    sink->TraceConnectWithoutContext("Rx",
                                     MakeBoundCallback(&OnAppRxTrace, ue, &rxPkts, &rxBytes));

    ue->Start();

    Simulator::Stop(MilliSeconds(simTimeMs));
    Simulator::Run();

    std::cout << "=== CF-MIMO Distributed Example ===\n";
    std::cout << "Adapter attached: " << (adapterOk ? "yes" : "no") << "\n";
    std::cout << "App RX packets/bytes: " << rxPkts << " / " << rxBytes << "\n";
    std::cout << "Primary AP: " << ue->GetPrimaryApActive() << "\n";
    std::cout << "Cluster Ver: " << ue->GetClusterVerActive() << "\n";
    std::cout << "Remaining UE CF queue bytes: " << ue->GetQueueBytes() << "\n";
    std::cout << "Injected to CF queue bytes: " << adapter->GetTotalInjectedBytes() << "\n";
    std::cout << "Sink total bytes: " << (sink ? sink->GetTotalRx() : 0) << "\n";

    Simulator::Destroy();
    return 0;
}
