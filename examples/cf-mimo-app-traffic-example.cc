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
#include "ns3/random-walk-2d-mobility-model.h"
#include "ns3/rectangle.h"
#include "ns3/simple-net-device.h"
#include "ns3/spectrum-phy.h"
#include "ns3/three-gpp-propagation-loss-model.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

namespace
{

enum class BaselineType : uint8_t
{
    MATCHED = 0,
    DCC = 1,
    FHAWARE = 2
};

BaselineType
ParseBaseline(const std::string& s)
{
    if (s == "dcc")
    {
        return BaselineType::DCC;
    }
    if (s == "fhaware")
    {
        return BaselineType::FHAWARE;
    }
    return BaselineType::MATCHED;
}

std::string
BaselineToString(BaselineType b)
{
    switch (b)
    {
    case BaselineType::MATCHED:
        return "matched";
    case BaselineType::DCC:
        return "dcc";
    case BaselineType::FHAWARE:
        return "fhaware";
    }
    return "matched";
}

CfUeMac::DistributedClusterPolicy
MakeDistributedPolicy(BaselineType baseline, std::map<AP_ID, uint16_t> fhCapByAp)
{
    return [baseline, fhCapByAp = std::move(fhCapByAp)](const std::vector<AssocOffer>& offers,
                                                         const std::vector<UeApMeasurement>& meas,
                                                         AP_ID& primary,
                                                         uint8_t maxClusterSize,
                                                         double lambda,
                                                         uint16_t loadRejectThresholdQ) {
        std::vector<AP_ID> members;
        members.reserve(maxClusterSize);
        primary = INVALID_AP_ID;
        if (offers.empty())
        {
            for (const auto& m : meas)
            {
                if (members.size() >= maxClusterSize || m.ap_id == INVALID_AP_ID)
                {
                    break;
                }
                members.push_back(m.ap_id);
            }
            if (!members.empty())
            {
                primary = members.front();
            }
            return members;
        }

        if (baseline == BaselineType::MATCHED)
        {
            // Distributed approximation of two-stage AP selection:
            // stage 1 primary by score-load, stage 2 strongest secondary by offer score.
            std::vector<std::pair<double, AP_ID>> primaryRank;
            primaryRank.reserve(offers.size());
            for (const auto& o : offers)
            {
                primaryRank.emplace_back(static_cast<double>(o.score_q) - lambda * o.load_q, o.ap_id);
            }
            std::sort(primaryRank.begin(), primaryRank.end(), std::greater<>());
            primary = primaryRank.front().second;
            members.push_back(primary);

            std::vector<AssocOffer> secondary = offers;
            std::sort(secondary.begin(), secondary.end(), [](const AssocOffer& a, const AssocOffer& b) {
                return a.score_q > b.score_q;
            });
            for (const auto& o : secondary)
            {
                if (members.size() >= maxClusterSize)
                {
                    break;
                }
                if (o.ap_id == primary || o.load_q > loadRejectThresholdQ)
                {
                    continue;
                }
                if (std::find(members.begin(), members.end(), o.ap_id) == members.end())
                {
                    members.push_back(o.ap_id);
                }
            }
            return members;
        }

        if (baseline == BaselineType::DCC)
        {
            // DCC-style thresholding over large-scale measurements (RSRP/SINR proxy).
            std::vector<std::tuple<int16_t, int16_t, AP_ID>> rank;
            rank.reserve(meas.size());
            for (const auto& m : meas)
            {
                if (m.ap_id != INVALID_AP_ID)
                {
                    rank.emplace_back(m.rsrp_q, m.sinr_q, m.ap_id);
                }
            }
            if (rank.empty())
            {
                return members;
            }
            std::sort(rank.begin(), rank.end(), std::greater<>());
            const int16_t bestRsrp = std::get<0>(rank.front());
            const int16_t dccThreshold = static_cast<int16_t>(bestRsrp - 120); // 12 dB in q0.1-ish scale
            primary = std::get<2>(rank.front());
            for (const auto& [rsrpQ, _, apId] : rank)
            {
                if (members.size() >= maxClusterSize)
                {
                    break;
                }
                if (rsrpQ < dccThreshold)
                {
                    continue;
                }
                if (std::find(members.begin(), members.end(), apId) == members.end())
                {
                    members.push_back(apId);
                }
            }
            if (members.empty())
            {
                members.push_back(primary);
            }
            return members;
        }

        // Fronthaul-aware distributed approximation:
        // combine offer score, AP load, and static AP fronthaul headroom.
        std::vector<std::pair<double, AP_ID>> rank;
        rank.reserve(offers.size());
        for (const auto& o : offers)
        {
            const double fhNorm = static_cast<double>(fhCapByAp.count(o.ap_id) ? fhCapByAp.at(o.ap_id) : 0) /
                                  65535.0;
            const double metric = static_cast<double>(o.score_q) - lambda * o.load_q + 50.0 * fhNorm;
            rank.emplace_back(metric, o.ap_id);
        }
        std::sort(rank.begin(), rank.end(), std::greater<>());
        primary = rank.front().second;
        for (const auto& [_, apId] : rank)
        {
            if (members.size() >= maxClusterSize)
            {
                break;
            }
            if (std::find(members.begin(), members.end(), apId) == members.end())
            {
                members.push_back(apId);
            }
        }
        return members;
    };
}

} // namespace

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
AttachUeNodeContext(Ptr<SpectrumPhy> phy,
                    const Vector& startPos,
                    bool enableRandomWalk,
                    double speedMps,
                    double stepDistanceM,
                    double minX,
                    double maxX,
                    double minY,
                    double maxY)
{
    auto node = CreateObject<Node>();
    auto dev = CreateObject<SimpleNetDevice>();
    node->AddDevice(dev);
    dev->SetNode(node);

    Ptr<MobilityModel> m;
    if (enableRandomWalk)
    {
        auto rw = CreateObject<RandomWalk2dMobilityModel>();
        rw->SetAttribute("Mode", EnumValue(RandomWalk2dMobilityModel::MODE_DISTANCE));
        rw->SetAttribute("Distance", DoubleValue(stepDistanceM));
        rw->SetAttribute("Speed",
                         StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(speedMps) +
                                     "]"));
        rw->SetAttribute("Bounds", RectangleValue(Rectangle(minX, maxX, minY, maxY)));
        rw->SetPosition(startPos);
        m = rw;
    }
    else
    {
        auto c = CreateObject<ConstantPositionMobilityModel>();
        c->SetPosition(startPos);
        m = c;
    }
    node->AggregateObject(m);

    phy->SetDevice(dev);
    phy->SetMobility(m);
}

int
main(int argc, char* argv[])
{
    double simTimeMs = 600.0;
    std::string appRate = "10Mbps";
    uint32_t appPktSize = 900;
    std::string baselineStr = "matched";
    bool ueRandomWalk = false;
    double ueWalkSpeedMps = 1.0;
    double ueWalkStepDistanceM = 5.0;
    double ueWalkMinX = -30.0;
    double ueWalkMaxX = 180.0;
    double ueWalkMinY = -40.0;
    double ueWalkMaxY = 40.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTimeMs", "Simulation time in milliseconds", simTimeMs);
    cmd.AddValue("appRate", "OnOff application data rate", appRate);
    cmd.AddValue("appPktSize", "OnOff packet size in bytes", appPktSize);
    cmd.AddValue("baseline",
                 "Distributed baseline policy: matched|dcc|fhaware",
                 baselineStr);
    cmd.AddValue("ueRandomWalk", "Enable RandomWalk2d mobility for UE", ueRandomWalk);
    cmd.AddValue("ueWalkSpeedMps", "UE RandomWalk speed [m/s]", ueWalkSpeedMps);
    cmd.AddValue("ueWalkStepDistanceM", "UE RandomWalk step distance [m]", ueWalkStepDistanceM);
    cmd.AddValue("ueWalkMinX", "UE RandomWalk bound min X [m]", ueWalkMinX);
    cmd.AddValue("ueWalkMaxX", "UE RandomWalk bound max X [m]", ueWalkMaxX);
    cmd.AddValue("ueWalkMinY", "UE RandomWalk bound min Y [m]", ueWalkMinY);
    cmd.AddValue("ueWalkMaxY", "UE RandomWalk bound max Y [m]", ueWalkMaxY);
    cmd.Parse(argc, argv);
    const BaselineType baseline = ParseBaseline(baselineStr);

    NodeContainer nodes;
    nodes.Create(2); // 0: UE app node, 1: remote sink

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("200Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devs = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = ipv4.Assign(devs);

    const uint16_t port = 5000;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(MilliSeconds(simTimeMs));

    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(ifs.GetAddress(1), port));
    onoff.SetAttribute("DataRate", DataRateValue(DataRate(appRate)));
    onoff.SetAttribute("PacketSize", UintegerValue(appPktSize));
    onoff.SetAttribute("OnTime",
                       StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime",
                       StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer txApps = onoff.Install(nodes.Get(0));
    txApps.Start(MilliSeconds(50));
    txApps.Stop(MilliSeconds(simTimeMs - 50));

    std::map<AP_ID, Ptr<CfApMac>> apById;
    std::map<AP_ID, Ptr<CfApPhy>> apPhyById;
    std::map<AP_ID, uint16_t> fhCapByAp;
    for (AP_ID apId = 1; apId <= 3; ++apId)
    {
        auto ap = CreateObject<CfApMac>();
        ap->SetApId(apId);
        ap->SetMode(CfMode::DISTRIBUTED);
        // Static per-AP fronthaul headroom used by the fhaware baseline.
        const uint16_t fhCap = static_cast<uint16_t>(45000 + (apId - 1) * 8000);
        ap->SetFronthaulCap(fhCap);
        fhCapByAp[apId] = fhCap;
        apById[apId] = ap;
    }

    const double freqHz = 3.5e9;
    const double prbBwHz = 180000.0;
    auto channel = Create3gppChannel(freqHz);

    auto ueMac = CreateObject<CfUeMac>();
    ueMac->SetUeId(1);
    ueMac->SetMode(CfMode::DISTRIBUTED);
    ueMac->SetQueueBytes(0);
    ueMac->SetTrafficClass(1);
    ueMac->SetDistributedClusterPolicy(MakeDistributedPolicy(baseline, fhCapByAp));

    auto uePhy = CreateObject<CfUePhy>();
    uePhy->SetUeId(ueMac->GetUeId());
    uePhy->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
    uePhy->SetChannel(channel);
    AttachUeNodeContext(uePhy,
                        Vector(25.0, 0.0, 1.5),
                        ueRandomWalk,
                        ueWalkSpeedMps,
                        ueWalkStepDistanceM,
                        ueWalkMinX,
                        ueWalkMaxX,
                        ueWalkMinY,
                        ueWalkMaxY);
    channel->AddRx(uePhy);
    CfPhyMacHelper::BindUeMeasurementProvider(ueMac, uePhy);

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

    auto trafficAdapter = CreateObject<CfUeTrafficAdapter>();
    trafficAdapter->SetUeMac(ueMac);
    const bool attached = trafficAdapter->Attach(devs.Get(0));

    ueMac->SetStateReportSink([&apById](const UEStateReport& report, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveUeStateReport(report);
        }
    });
    ueMac->SetAssocConfirmSink([&apById](const AssocConfirm& confirm, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveAssocConfirm(confirm);
        }
    });
    ueMac->SetSrBsrSink([&apById](const SR_BSR& srBsr, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveSrBsr(srBsr);
        }
    });
    ueMac->SetRxStatusSink([&apById](const RxStatus& status, AP_ID apId) {
        const auto it = apById.find(apId);
        if (it != apById.end())
        {
            it->second->ReceiveRxStatus(status);
        }
    });

    for (const auto& [_, ap] : apById)
    {
        const AP_ID apId = ap->GetApId();
        ap->SetOfferSink([ueMac](const AssocOffer& offer, UE_ID ueId) {
            if (ueId == ueMac->GetUeId())
            {
                ueMac->ReceiveAssocOffer(offer);
            }
        });
        ap->SetAssocAckSink([ueMac](const AssocAck& ack, UE_ID ueId) {
            if (ueId == ueMac->GetUeId())
            {
                ueMac->ReceiveAssocAck(ack);
            }
        });
        CfPhyMacHelper::BindApGrantPath(ap, apPhyById[apId], ueMac, uePhy);
        ap->SetNeighborCoordSink([](const NeighborCoord&, AP_ID) {});
        ap->Start();
        apPhyById[apId]->Start();
    }

    ueMac->Start();

    Simulator::Stop(MilliSeconds(simTimeMs));
    Simulator::Run();

    auto sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    std::cout << "=== CF-MIMO App Traffic Example ===\n";
    std::cout << "Baseline: " << BaselineToString(baseline) << "\n";
    std::cout << "UE randomWalk: " << (ueRandomWalk ? "on" : "off") << "\n";
    std::cout << "Adapter attached to UE NetDevice(MacTx): " << (attached ? "yes" : "no") << "\n";
    std::cout << "Injected to CF queue (bytes/packets): " << trafficAdapter->GetTotalInjectedBytes()
              << " / " << trafficAdapter->GetTotalInjectedPackets() << "\n";
    std::cout << "UE remaining CF queue bytes: " << ueMac->GetQueueBytes() << "\n";
    std::cout << "UE primary AP: " << ueMac->GetPrimaryApActive()
              << " clusterVer: " << ueMac->GetClusterVerActive() << "\n";
    std::cout << "Remote sink received bytes: " << (sink ? sink->GetTotalRx() : 0) << "\n";

    Simulator::Destroy();
    return 0;
}
