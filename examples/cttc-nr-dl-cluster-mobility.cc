// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/antenna-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CttcNrDlClusterMobility");

namespace
{

class DistanceBasedClusterAgent : public NrDlClusterApAgent
{
  public:
    static TypeId GetTypeId();

    DistanceBasedClusterAgent();

    void Configure(uint16_t cellId,
                   const Ptr<MobilityModel>& apMobility,
                   const Ptr<MobilityModel>& ueMobility,
                   double acceptRadiusMeters,
                   bool alwaysAccept);

    uint16_t GetCellId() const override;

    NrDlClusterParticipationResponse EvaluateParticipation(
        const NrDlClusterParticipationRequest& request) override;

    void ReceiveDlGrant(const NrDlClusterGrant& grant) override;

  private:
    uint16_t m_cellId{0};
    Ptr<MobilityModel> m_apMobility;
    Ptr<MobilityModel> m_ueMobility;
    double m_acceptRadiusMeters{0.0};
    bool m_alwaysAccept{false};
};

NS_OBJECT_ENSURE_REGISTERED(DistanceBasedClusterAgent);

TypeId
DistanceBasedClusterAgent::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DistanceBasedClusterAgent")
                            .SetParent<NrDlClusterApAgent>()
                            .SetGroupName("Nr")
                            .AddConstructor<DistanceBasedClusterAgent>();
    return tid;
}

DistanceBasedClusterAgent::DistanceBasedClusterAgent() = default;

void
DistanceBasedClusterAgent::Configure(uint16_t cellId,
                                     const Ptr<MobilityModel>& apMobility,
                                     const Ptr<MobilityModel>& ueMobility,
                                     double acceptRadiusMeters,
                                     bool alwaysAccept)
{
    m_cellId = cellId;
    m_apMobility = apMobility;
    m_ueMobility = ueMobility;
    m_acceptRadiusMeters = acceptRadiusMeters;
    m_alwaysAccept = alwaysAccept;
}

uint16_t
DistanceBasedClusterAgent::GetCellId() const
{
    return m_cellId;
}

NrDlClusterParticipationResponse
DistanceBasedClusterAgent::EvaluateParticipation(const NrDlClusterParticipationRequest&) 
{
    NS_ABORT_MSG_IF(m_apMobility == nullptr || m_ueMobility == nullptr,
                    "Cluster agent mobility was not configured");

    const double distance = m_apMobility->GetDistanceFrom(m_ueMobility);
    const bool accepted = m_alwaysAccept || distance <= m_acceptRadiusMeters;
    const double metric = accepted ? (1.0 / std::max(distance, 1.0)) : 0.0;
    return NrDlClusterParticipationResponse{m_cellId, accepted, metric};
}

void
DistanceBasedClusterAgent::ReceiveDlGrant(const NrDlClusterGrant& grant)
{
    NS_LOG_INFO("AP " << m_cellId << " received clustered DL grant for RNTI " << grant.imsi
                      << " at " << Simulator::Now().As(Time::MS));
}

std::string
FormatCellList(const std::vector<uint16_t>& cells)
{
    std::ostringstream oss;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (i != 0)
        {
            oss << ",";
        }
        oss << cells[i];
    }
    return oss.str();
}

void
SendDlPacket(const Ptr<NrDlClusterContextManager>& contextManager,
             const Ptr<NetDevice>& servingGnbDevice,
             const Address& ueAddress,
             const Ptr<NrUeNetDevice>& ueDevice,
             uint32_t packetSize,
             Time interval,
             Time stopTime)
{
    if (Simulator::Now() >= stopTime)
    {
        return;
    }

    uint16_t rnti = ueDevice->GetRrc()->GetRnti();
    if (rnti == 0 || !contextManager->HasAssociation(rnti))
    {
        Simulator::Schedule(interval,
                            &SendDlPacket,
                            contextManager,
                            servingGnbDevice,
                            ueAddress,
                            ueDevice,
                            packetSize,
                            interval,
                            stopTime);
        return;
    }

    Ptr<Packet> pkt = Create<Packet>(packetSize);
    Ipv4Header ipv4Header;
    ipv4Header.SetProtocol(UdpL4Protocol::PROT_NUMBER);
    pkt->AddHeader(ipv4Header);

    if (rnti != 0)
    {
        NrQosFlowTag tag(rnti, 1);
        pkt->AddPacketTag(tag);
    }

    servingGnbDevice->Send(pkt, ueAddress, Ipv4L3Protocol::PROT_NUMBER);
    Simulator::Schedule(interval,
                        &SendDlPacket,
                        contextManager,
                        servingGnbDevice,
                        ueAddress,
                        ueDevice,
                        packetSize,
                        interval,
                        stopTime);
}

void
UpdateClusterSelection(const Ptr<NrDlClusterContextManager>& contextManager,
                       const Ptr<NrUeNetDevice>& ueDevice,
                       const std::vector<std::pair<uint16_t, Ptr<MobilityModel>>>& peers,
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

    uint16_t rnti = ueDevice->GetRrc()->GetRnti();
    if (rnti == 0)
    {
        Simulator::Schedule(interval,
                            &UpdateClusterSelection,
                            contextManager,
                            ueDevice,
                            peers,
                            packetSize,
                            fixedMcs,
                            maxCandidates,
                            interval,
                            stopTime);
        return;
    }

    auto ueMobility = ueDevice->GetNode()->GetObject<MobilityModel>();
    std::vector<std::pair<double, uint16_t>> rankedPeers;
    rankedPeers.reserve(peers.size());
    for (const auto& peer : peers)
    {
        rankedPeers.emplace_back(peer.second->GetDistanceFrom(ueMobility), peer.first);
    }
    std::sort(rankedPeers.begin(), rankedPeers.end());

    std::vector<uint16_t> candidateCells;
    for (const auto& peer : rankedPeers)
    {
        candidateCells.push_back(peer.second);
        if (candidateCells.size() >= maxCandidates)
        {
            break;
        }
    }

    if (!contextManager->HasAssociation(rnti))
    {
        uint16_t initialCoordinator = candidateCells.empty() ? 0 : candidateCells.front();
        NS_ABORT_MSG_IF(initialCoordinator == 0, "No initial coordinator candidate is available");
        contextManager->AssociateUe(NrDlClusterUeAssociation{rnti, initialCoordinator, candidateCells});
    }

    auto updatedAssociation = contextManager->ReselectCoordinator(rnti,
                                                                  candidateCells,
                                                                  static_cast<uint32_t>(
                                                                      Simulator::Now().GetMilliSeconds()),
                                                                  packetSize,
                                                                  fixedMcs);
    auto previewGrant = contextManager->CreateDlGrant(
        rnti,
        static_cast<uint32_t>(Simulator::Now().GetMilliSeconds()),
        packetSize,
        fixedMcs);

    NS_LOG_INFO("UE requested cluster update at " << Simulator::Now().As(Time::MS) << " ms, RNTI "
                                                  << rnti << ", coordinator="
                                                  << updatedAssociation.coordinatorCellId
                                                  << ", candidates="
                                                  << FormatCellList(updatedAssociation.candidateApCellIds)
                                                  << ", probed="
                                                  << FormatCellList(candidateCells)
                                                  << ", selected="
                                                  << FormatCellList(previewGrant.selectedApCellIds));

    Simulator::Schedule(interval,
                        &UpdateClusterSelection,
                        contextManager,
                        ueDevice,
                        peers,
                        packetSize,
                        fixedMcs,
                        maxCandidates,
                        interval,
                        stopTime);
}

} // namespace

int
main(int argc, char* argv[])
{
    double simTime = 2.0;
    uint32_t packetSize = 600;
    double packetIntervalMs = 10.0;
    double clusterUpdateMs = 100.0;
    double acceptRadiusMeters = 35.0;
    uint32_t maxCollaboratingAps = 2;
    uint32_t maxCandidateAps = 2;
    uint8_t numerology = 0;
    uint8_t fixedMcsDl = 20;
    double centralFrequency = 28e9;
    double bandwidth = 100e6;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("packetSize", "Downlink packet size in bytes", packetSize);
    cmd.AddValue("packetIntervalMs", "Interval between downlink packets in ms", packetIntervalMs);
    cmd.AddValue("clusterUpdateMs", "Cluster update request interval in ms", clusterUpdateMs);
    cmd.AddValue("acceptRadiusMeters",
                 "Peer AP acceptance radius around the moving UE",
                 acceptRadiusMeters);
    cmd.AddValue("maxCollaboratingAps",
                 "Maximum APs selected by the coordinator, including the anchor AP",
                 maxCollaboratingAps);
    cmd.AddValue("maxCandidateAps",
                 "Maximum number of peer APs included in each UE update request",
                 maxCandidateAps);
    cmd.AddValue("numerology", "Numerology used by the anchor BWP", numerology);
    cmd.AddValue("fixedMcsDl", "Fixed DL MCS used by the scheduler", fixedMcsDl);
    cmd.Parse(argc, argv);

    NodeContainer gnbNodes;
    gnbNodes.Create(3);
    NodeContainer ueNodes;
    ueNodes.Create(1);

    MobilityHelper gnbMobility;
    Ptr<ListPositionAllocator> gnbPositions = CreateObject<ListPositionAllocator>();
    gnbPositions->Add(Vector(0.0, 0.0, 10.0));
    gnbPositions->Add(Vector(30.0, 0.0, 10.0));
    gnbPositions->Add(Vector(60.0, 0.0, 10.0));
    gnbMobility.SetPositionAllocator(gnbPositions);
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.Install(gnbNodes);

    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel");
    ueMobility.Install(ueNodes);
    Ptr<WaypointMobilityModel> waypointMobility =
        ueNodes.Get(0)->GetObject<WaypointMobilityModel>();
    waypointMobility->AddWaypoint(Waypoint(Seconds(0.0), Vector(5.0, 0.0, 1.5)));
    waypointMobility->AddWaypoint(Waypoint(Seconds(simTime / 2.0), Vector(30.0, 0.0, 1.5)));
    waypointMobility->AddWaypoint(Waypoint(Seconds(simTime), Vector(55.0, 0.0, 1.5)));

    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();

    nrHelper->SetBeamformingHelper(beamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);
    channelHelper->ConfigureFactories("UMi", "Default", "ThreeGpp");
    Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(0)));
    channelHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
    channelHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(false));

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency, bandwidth, 1);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    nrHelper->SetSchedulerAttribute("FixedMcsDl", BooleanValue(true));
    nrHelper->SetSchedulerAttribute("StartingMcsDl", UintegerValue(fixedMcsDl));
    beamformingHelper->SetAttribute("BeamformingMethod",
                                    TypeIdValue(DirectPathBeamforming::GetTypeId()));

    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(4));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

    NetDeviceContainer gnbNetDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueNetDevs = nrHelper->InstallUeDevice(ueNodes, allBwps);

    InternetStackHelper internet;
    internet.Install(ueNodes);
    epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueNetDevs));

    for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
    {
        NrHelper::GetGnbPhy(gnbNetDevs.Get(i), 0)
            ->SetAttribute("Numerology", UintegerValue(numerology));
    }

    nrHelper->AttachToGnb(ueNetDevs.Get(0), gnbNetDevs.Get(0));

    Ptr<NrUeNetDevice> ueDevice = DynamicCast<NrUeNetDevice>(ueNetDevs.Get(0));
    Ptr<NrDlClusterCoordinator> coordinator = CreateObject<NrDlClusterCoordinator>();
    coordinator->SetAttribute("MaxCollaboratingAps", UintegerValue(maxCollaboratingAps));
    Ptr<NrDlClusterContextManager> contextManager = CreateObject<NrDlClusterContextManager>();
    Ptr<NrDlClusterSelectionMechanismDefault> selectionMechanism =
        CreateObject<NrDlClusterSelectionMechanismDefault>();
    contextManager->SetCoordinator(coordinator);
    contextManager->SetSelectionMechanism(selectionMechanism);

    std::vector<std::pair<uint16_t, Ptr<MobilityModel>>> clusterPeers;
    for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
    {
        Ptr<NrGnbNetDevice> gnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(i));
        Ptr<DistanceBasedClusterAgent> agent = CreateObject<DistanceBasedClusterAgent>();
        agent->Configure(gnb->GetCellId(),
                         gnbNodes.Get(i)->GetObject<MobilityModel>(),
                         ueNodes.Get(0)->GetObject<MobilityModel>(),
                         acceptRadiusMeters,
                         i == 0);
        contextManager->RegisterApAgent(agent);
        clusterPeers.emplace_back(gnb->GetCellId(), gnbNodes.Get(i)->GetObject<MobilityModel>());
    }

    for (uint32_t i = 0; i < gnbNetDevs.GetN(); ++i)
    {
        Ptr<NrGnbNetDevice> gnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(i));
        Ptr<NrGnbMac> mac = gnb->GetMac(0);
        mac->SetDlClusterContextManager(contextManager);
        for (uint32_t j = 0; j < gnbNetDevs.GetN(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            Ptr<NrGnbNetDevice> peerGnb = DynamicCast<NrGnbNetDevice>(gnbNetDevs.Get(j));
            mac->RegisterDlClusterPeerPhy(peerGnb->GetCellId(), peerGnb->GetPhy(0));
        }
    }

    Simulator::Schedule(MilliSeconds(200),
                        &UpdateClusterSelection,
                        contextManager,
                        ueDevice,
                        clusterPeers,
                        packetSize,
                        fixedMcsDl,
                        maxCandidateAps,
                        MilliSeconds(clusterUpdateMs),
                        Seconds(simTime));

    Simulator::Schedule(MilliSeconds(250),
                        &SendDlPacket,
                        contextManager,
                        gnbNetDevs.Get(0),
                        ueNetDevs.Get(0)->GetAddress(),
                        ueDevice,
                        packetSize,
                        MilliSeconds(packetIntervalMs),
                        Seconds(simTime));

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
