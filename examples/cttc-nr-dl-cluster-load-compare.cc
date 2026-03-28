// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CttcNrDlClusterLoadCompare");

namespace
{

class MultiUeDistanceClusterAgent : public NrDlClusterApAgent
{
  public:
    static TypeId GetTypeId();

    MultiUeDistanceClusterAgent();

    void Configure(uint16_t cellId, const Ptr<MobilityModel>& apMobility, double acceptRadiusMeters);

    void RegisterUe(uint64_t ueId, const Ptr<MobilityModel>& ueMobility);

    uint16_t GetCellId() const override;

    NrDlClusterParticipationResponse EvaluateParticipation(
        const NrDlClusterParticipationRequest& request) override;

    void ReceiveDlGrant(const NrDlClusterGrant&) override;

  private:
    uint16_t m_cellId{0};
    Ptr<MobilityModel> m_apMobility;
    double m_acceptRadiusMeters{0.0};
    std::map<uint64_t, Ptr<MobilityModel>> m_ueMobilities;
};

NS_OBJECT_ENSURE_REGISTERED(MultiUeDistanceClusterAgent);

TypeId
MultiUeDistanceClusterAgent::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MultiUeDistanceClusterAgent")
                            .SetParent<NrDlClusterApAgent>()
                            .SetGroupName("Nr")
                            .AddConstructor<MultiUeDistanceClusterAgent>();
    return tid;
}

MultiUeDistanceClusterAgent::MultiUeDistanceClusterAgent() = default;

void
MultiUeDistanceClusterAgent::Configure(uint16_t cellId,
                                       const Ptr<MobilityModel>& apMobility,
                                       double acceptRadiusMeters)
{
    m_cellId = cellId;
    m_apMobility = apMobility;
    m_acceptRadiusMeters = acceptRadiusMeters;
}

void
MultiUeDistanceClusterAgent::RegisterUe(uint64_t ueId, const Ptr<MobilityModel>& ueMobility)
{
    NS_ABORT_MSG_IF(ueId == 0, "UE identifier 0 is invalid for cluster agent registration");
    m_ueMobilities[ueId] = ueMobility;
}

uint16_t
MultiUeDistanceClusterAgent::GetCellId() const
{
    return m_cellId;
}

NrDlClusterParticipationResponse
MultiUeDistanceClusterAgent::EvaluateParticipation(const NrDlClusterParticipationRequest& request)
{
    NS_ABORT_MSG_IF(m_apMobility == nullptr, "AP mobility has not been configured");

    auto ueIt = m_ueMobilities.find(request.imsi);
    NS_ABORT_MSG_IF(ueIt == m_ueMobilities.end(),
                    "No UE mobility was registered for cluster request id " << request.imsi);

    const double distance = m_apMobility->GetDistanceFrom(ueIt->second);
    const bool accepted =
        (m_cellId == request.coordinatorCellId) || (distance <= m_acceptRadiusMeters);
    const double metric = accepted ? (1.0 / std::max(distance, 1.0)) : 0.0;
    return NrDlClusterParticipationResponse{m_cellId, accepted, metric};
}

void
MultiUeDistanceClusterAgent::ReceiveDlGrant(const NrDlClusterGrant&)
{
}

struct FlowSummary
{
    uint32_t numFlows{0};
    double meanThroughputMbps{0.0};
    double meanDelayMs{0.0};
    double meanLossRatio{0.0};
};

std::vector<uint16_t>
SelectCandidateCells(const Ptr<MobilityModel>& ueMobility,
                     const std::vector<std::pair<uint16_t, Ptr<MobilityModel>>>& aps,
                     uint32_t maxCandidates)
{
    std::vector<std::pair<double, uint16_t>> rankedCells;
    rankedCells.reserve(aps.size());
    for (const auto& ap : aps)
    {
        rankedCells.emplace_back(ap.second->GetDistanceFrom(ueMobility), ap.first);
    }

    std::sort(rankedCells.begin(), rankedCells.end());

    std::vector<uint16_t> candidateCells;
    for (const auto& rankedCell : rankedCells)
    {
        candidateCells.push_back(rankedCell.second);
        if (candidateCells.size() >= maxCandidates)
        {
            break;
        }
    }
    return candidateCells;
}

void
UpdateUeClusterSelection(
    const Ptr<NrDlClusterContextManager>& contextManager,
    const Ptr<NrUeNetDevice>& ueDevice,
    const std::vector<std::pair<uint16_t, Ptr<MobilityModel>>>& aps,
    const std::vector<Ptr<MultiUeDistanceClusterAgent>>& agents,
    uint32_t packetSize,
    uint8_t fixedMcs,
    uint32_t maxCandidates,
    Time interval,
    Time stopTime)
{
    if (Simulator::Now() >= stopTime)
    {
        return;
    }

    const uint16_t rnti = ueDevice->GetRrc()->GetRnti();
    if (rnti == 0)
    {
        Simulator::Schedule(interval,
                            &UpdateUeClusterSelection,
                            contextManager,
                            ueDevice,
                            aps,
                            agents,
                            packetSize,
                            fixedMcs,
                            maxCandidates,
                            interval,
                            stopTime);
        return;
    }

    const auto ueMobility = ueDevice->GetNode()->GetObject<MobilityModel>();
    for (const auto& agent : agents)
    {
        agent->RegisterUe(rnti, ueMobility);
    }

    const auto candidateCells = SelectCandidateCells(ueMobility, aps, maxCandidates);
    if (!contextManager->HasAssociation(rnti))
    {
        NS_ABORT_MSG_IF(candidateCells.empty(), "No candidate APs are available for initial cluster");
        contextManager->AssociateUe(
            NrDlClusterUeAssociation{rnti, candidateCells.front(), candidateCells});
    }

    contextManager->ReselectCoordinator(rnti,
                                        candidateCells,
                                        static_cast<uint32_t>(Simulator::Now().GetMilliSeconds()),
                                        packetSize,
                                        fixedMcs);

    Simulator::Schedule(interval,
                        &UpdateUeClusterSelection,
                        contextManager,
                        ueDevice,
                        aps,
                        agents,
                        packetSize,
                        fixedMcs,
                        maxCandidates,
                        interval,
                        stopTime);
}

FlowSummary
SummarizeFlows(const Ptr<FlowMonitor>& monitor,
               const Ptr<Ipv4FlowClassifier>& classifier,
               const std::vector<uint16_t>& uePorts,
               Time appStart,
               Time appStop)
{
    monitor->CheckForLostPackets();
    const auto stats = monitor->GetFlowStats();

    FlowSummary summary;
    const double flowDurationSeconds = (appStop - appStart).GetSeconds();

    for (const auto& [flowId, stat] : stats)
    {
        const auto flow = classifier->FindFlow(flowId);
        if (std::find(uePorts.begin(), uePorts.end(), flow.destinationPort) == uePorts.end())
        {
            continue;
        }

        summary.numFlows++;
        summary.meanThroughputMbps += (stat.rxBytes * 8.0) / flowDurationSeconds / 1e6;
        if (stat.rxPackets > 0)
        {
            summary.meanDelayMs += 1000.0 * stat.delaySum.GetSeconds() / stat.rxPackets;
        }
        if (stat.txPackets > 0)
        {
            summary.meanLossRatio += static_cast<double>(stat.lostPackets) / stat.txPackets;
        }
    }

    if (summary.numFlows > 0)
    {
        summary.meanThroughputMbps /= summary.numFlows;
        summary.meanDelayMs /= summary.numFlows;
        summary.meanLossRatio /= summary.numFlows;
    }

    return summary;
}

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t numAps = 10;
    uint32_t numUes = 3;
    bool enableCluster = true;
    double simTimeSeconds = 3.0;
    double appStartSeconds = 0.4;
    double clusterUpdateMs = 100.0;
    double offeredLoadMbpsPerUe = 80.0;
    uint32_t udpPacketSize = 1200;
    double interSiteDistanceMeters = 20.0;
    double acceptRadiusMeters = 35.0;
    uint32_t maxCollaboratingAps = 3;
    uint32_t maxCandidateAps = 4;
    uint8_t numerology = 0;
    uint8_t fixedMcsDl = 20;
    double centralFrequency = 28e9;
    double bandwidth = 100e6;
    double ueSpeedScale = 1.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numAps", "Number of AP/gNB nodes", numAps);
    cmd.AddValue("numUes", "Number of moving UEs", numUes);
    cmd.AddValue("enableCluster", "Enable dynamic cluster updates and peer AP fan-out", enableCluster);
    cmd.AddValue("simTime", "Simulation time in seconds", simTimeSeconds);
    cmd.AddValue("appStart", "Application start time in seconds", appStartSeconds);
    cmd.AddValue("clusterUpdateMs", "Cluster update period in milliseconds", clusterUpdateMs);
    cmd.AddValue("offeredLoadMbpsPerUe", "UDP downlink offered load per UE in Mbps", offeredLoadMbpsPerUe);
    cmd.AddValue("udpPacketSize", "UDP payload size in bytes", udpPacketSize);
    cmd.AddValue("interSiteDistance", "Distance between adjacent APs in meters", interSiteDistanceMeters);
    cmd.AddValue("acceptRadiusMeters", "AP admission radius for cluster participation", acceptRadiusMeters);
    cmd.AddValue("maxCollaboratingAps", "Maximum number of selected APs per UE", maxCollaboratingAps);
    cmd.AddValue("maxCandidateAps", "Maximum number of candidate APs evaluated per update", maxCandidateAps);
    cmd.AddValue("numerology", "Numerology used by the AP BWP", numerology);
    cmd.AddValue("fixedMcsDl", "Fixed DL MCS used by the scheduler", fixedMcsDl);
    cmd.AddValue("ueSpeedScale", "Scale factor applied to UE traversal speed", ueSpeedScale);
    cmd.Parse(argc, argv);

    const Time simTime = Seconds(simTimeSeconds);
    const Time appStart = Seconds(appStartSeconds);
    const Time clusterUpdatePeriod = MilliSeconds(clusterUpdateMs);
    const Time appStop = simTime - MilliSeconds(10);
    const double packetsPerSecond = (offeredLoadMbpsPerUe * 1e6) / (udpPacketSize * 8.0);
    const Time udpInterval = Seconds(1.0 / packetsPerSecond);

    Config::SetDefault("ns3::NrRlcUm::MaxTxBufferSize", UintegerValue(999999999));
    Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(0)));

    NodeContainer gnbNodes;
    gnbNodes.Create(numAps);
    NodeContainer ueNodes;
    ueNodes.Create(numUes);

    MobilityHelper gnbMobility;
    Ptr<ListPositionAllocator> gnbPositions = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < numAps; ++i)
    {
        gnbPositions->Add(Vector(i * interSiteDistanceMeters, 0.0, 10.0));
    }
    gnbMobility.SetPositionAllocator(gnbPositions);
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.Install(gnbNodes);

    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel");
    ueMobility.Install(ueNodes);
    for (uint32_t i = 0; i < numUes; ++i)
    {
        auto waypoint = ueNodes.Get(i)->GetObject<WaypointMobilityModel>();
        const double yOffset = -15.0 + 15.0 * static_cast<double>(i);
        const double startX = 5.0 * static_cast<double>(i);
        const double endX =
            std::max(startX + 5.0, (numAps - 1) * interSiteDistanceMeters - 10.0 * (numUes - i - 1));
        const double traversalSeconds =
            std::max(0.2, simTimeSeconds / std::max(ueSpeedScale, 1e-3));
        const Time traversalTime = Seconds(std::min(traversalSeconds, simTimeSeconds));
        waypoint->AddWaypoint(Waypoint(Seconds(0.0), Vector(startX, yOffset, 1.5)));
        waypoint->AddWaypoint(
            Waypoint(traversalTime / 2.0, Vector((startX + endX) / 2.0, yOffset, 1.5)));
        waypoint->AddWaypoint(Waypoint(traversalTime, Vector(endX, yOffset, 1.5)));
        if (traversalTime < simTime)
        {
            waypoint->AddWaypoint(Waypoint(simTime, Vector(endX, yOffset, 1.5)));
        }
    }

    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();

    nrHelper->SetBeamformingHelper(beamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);
    nrHelper->SetSchedulerAttribute("FixedMcsDl", BooleanValue(true));
    nrHelper->SetSchedulerAttribute("StartingMcsDl", UintegerValue(fixedMcsDl));
    nrHelper->SetSchedulerAttribute("SrsSymbols", UintegerValue(1));
    nrHelper->SetSchedulerAttribute("EnableSrsInUlSlots", BooleanValue(false));
    nrHelper->SetSchedulerAttribute("EnableSrsInFSlots", BooleanValue(false));
    beamformingHelper->SetAttribute("BeamformingMethod",
                                    TypeIdValue(DirectPathBeamforming::GetTypeId()));
    channelHelper->ConfigureFactories("UMi", "Default", "ThreeGpp");
    channelHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
    channelHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(false));

    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(4));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency, bandwidth, 1);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    NetDeviceContainer gnbNetDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueNetDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
    {
        NrHelper::GetGnbPhy(gnbNetDevs.Get(i), 0)
            ->SetAttribute("Numerology", UintegerValue(numerology));
    }

    InternetStackHelper internet;
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueNetDevs));
    nrHelper->AttachToClosestGnb(ueNetDevs, gnbNetDevs);

    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
    p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    Ptr<NrDlClusterContextManager> contextManager;
    std::vector<Ptr<MultiUeDistanceClusterAgent>> agents;
    std::vector<std::pair<uint16_t, Ptr<MobilityModel>>> apMobilities;

    if (enableCluster)
    {
        auto coordinator = CreateObject<NrDlClusterCoordinator>();
        coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(maxCollaboratingAps));
        contextManager = CreateObject<NrDlClusterContextManager>();
        contextManager->SetCoordinator(coordinator);
        contextManager->SetSelectionMechanism(CreateObject<NrDlClusterSelectionMechanismDefault>());

        for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
        {
            auto gnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(i));
            auto agent = CreateObject<MultiUeDistanceClusterAgent>();
            agent->Configure(gnb->GetCellId(),
                             gnbNodes.Get(i)->GetObject<MobilityModel>(),
                             acceptRadiusMeters);
            contextManager->RegisterApAgent(agent);
            agents.push_back(agent);
            apMobilities.emplace_back(gnb->GetCellId(), gnbNodes.Get(i)->GetObject<MobilityModel>());
        }

        for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
        {
            auto gnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(i));
            auto mac = gnb->GetMac(0);
            mac->SetDlClusterContextManager(contextManager);
            for (uint32_t j = 0; j < gnbNetDevs.GetN(); ++j)
            {
                if (i == j)
                {
                    continue;
                }
                auto peerGnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(j));
                mac->RegisterDlClusterPeerPhy(peerGnb->GetCellId(), peerGnb->GetPhy(0));
            }
        }
    }

    ApplicationContainer serverApps;
    ApplicationContainer clientApps;
    std::vector<uint16_t> uePorts;
    for (uint32_t i = 0; i < numUes; ++i)
    {
        const uint16_t port = 4000 + i;
        uePorts.push_back(port);

        UdpServerHelper server(port);
        serverApps.Add(server.Install(ueNodes.Get(i)));

        UdpClientHelper client(ueIpIfaces.GetAddress(i), port);
        client.SetAttribute("Interval", TimeValue(udpInterval));
        client.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        client.SetAttribute("PacketSize", UintegerValue(udpPacketSize));
        clientApps.Add(client.Install(remoteHost));
    }

    serverApps.Start(appStart);
    clientApps.Start(appStart);
    serverApps.Stop(appStop);
    clientApps.Stop(appStop);

    if (enableCluster)
    {
        for (uint32_t i = 0; i < numUes; ++i)
        {
            auto ueDevice = DynamicCast<NrUeNetDevice>(ueNetDevs.Get(i));
            Simulator::Schedule(MilliSeconds(200 + 20 * i),
                                &UpdateUeClusterSelection,
                                contextManager,
                                ueDevice,
                                apMobilities,
                                agents,
                                udpPacketSize,
                                fixedMcsDl,
                                maxCandidateAps,
                                clusterUpdatePeriod,
                                simTime);
        }
    }

    FlowMonitorHelper flowmonHelper;
    NodeContainer endpointNodes;
    endpointNodes.Add(remoteHost);
    endpointNodes.Add(ueNodes);
    Ptr<FlowMonitor> monitor = flowmonHelper.Install(endpointNodes);
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001));
    monitor->SetAttribute("JitterBinWidth", DoubleValue(0.001));
    monitor->SetAttribute("PacketSizeBinWidth", DoubleValue(1));

    Simulator::Stop(simTime);
    Simulator::Run();

    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    const auto summary = SummarizeFlows(monitor, classifier, uePorts, appStart, appStop);

    std::cout << "Scenario=" << (enableCluster ? "cluster" : "baseline") << std::endl;
    std::cout << "APs=" << numAps << " UEs=" << numUes
              << " OfferedLoadPerUeMbps=" << offeredLoadMbpsPerUe << std::endl;
    std::cout << "MeanFlowThroughputMbps=" << summary.meanThroughputMbps << std::endl;
    std::cout << "MeanFlowDelayMs=" << summary.meanDelayMs << std::endl;
    std::cout << "MeanFlowLossRatio=" << summary.meanLossRatio << std::endl;

    const auto stats = monitor->GetFlowStats();
    for (const auto& [flowId, stat] : stats)
    {
        const auto flow = classifier->FindFlow(flowId);
        if (std::find(uePorts.begin(), uePorts.end(), flow.destinationPort) == uePorts.end())
        {
            continue;
        }

        const double throughputMbps = (stat.rxBytes * 8.0) / (appStop - appStart).GetSeconds() / 1e6;
        const double meanDelayMs =
            stat.rxPackets > 0 ? 1000.0 * stat.delaySum.GetSeconds() / stat.rxPackets : 0.0;
        const double lossRatio =
            stat.txPackets > 0 ? static_cast<double>(stat.lostPackets) / stat.txPackets : 0.0;

        std::cout << "Flow " << flowId << " Dst=" << flow.destinationAddress << ":" << flow.destinationPort
                  << " RxPackets=" << stat.rxPackets << " ThroughputMbps=" << throughputMbps
                  << " MeanDelayMs=" << meanDelayMs << " LossRatio=" << lossRatio << std::endl;
    }

    Simulator::Destroy();
    return 0;
}
