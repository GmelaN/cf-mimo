/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/boolean.h"
#include "ns3/cf-ap-mac.h"
#include "ns3/cf-ap-phy.h"
#include "ns3/cf-control-plane-helper.h"
#include "ns3/cf-cpu-controller.h"
#include "ns3/cf-phy-mac-helper.h"
#include "ns3/cf-ue-mac.h"
#include "ns3/cf-ue-phy.h"
#include "ns3/channel-condition-model.h"
#include "ns3/command-line.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/double.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/node.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/simple-net-device.h"
#include "ns3/spectrum-phy.h"
#include "ns3/three-gpp-propagation-loss-model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

struct LoadState
{
    Ptr<CfUeMac> ue;
    double bytesPerTti{0.0};
    double residualBytes{0.0};
    uint64_t injectedBytes{0};
};

CfMode
ParseMode(const std::string& modeStr)
{
    if (modeStr == "centralized")
    {
        return CfMode::CENTRALIZED;
    }
    return CfMode::DISTRIBUTED;
}

double
NsToMs(uint64_t ns)
{
    return static_cast<double>(ns) / 1e6;
}

Ptr<MultiModelSpectrumChannel>
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

void
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

void
InjectLoadTick(std::vector<LoadState>* loads, Time tti, Time stopAt)
{
    if (Simulator::Now() >= stopAt)
    {
        return;
    }

    for (auto& st : *loads)
    {
        if (!st.ue)
        {
            continue;
        }
        const double exact = st.bytesPerTti + st.residualBytes;
        const uint32_t addBytes = static_cast<uint32_t>(std::floor(exact));
        st.residualBytes = exact - static_cast<double>(addBytes);
        if (addBytes == 0)
        {
            continue;
        }

        const uint32_t currentQ = st.ue->GetQueueBytes();
        const uint32_t nextQ =
            (currentQ > (std::numeric_limits<uint32_t>::max() - addBytes))
                ? std::numeric_limits<uint32_t>::max()
                : currentQ + addBytes;
        st.ue->SetQueueBytes(nextQ);
        st.injectedBytes += static_cast<uint64_t>(nextQ - currentQ);
    }

    Simulator::Schedule(tti, &InjectLoadTick, loads, tti, stopAt);
}

void
PrintPeriodicSnapshot(const std::map<UE_ID, Ptr<CfUeMac>>* ueById,
                      const CfControlPlaneHelper* cpHelper,
                      Time period,
                      Time stopAt)
{
    if (Simulator::Now() >= stopAt)
    {
        return;
    }

    uint64_t queueSum = 0;
    uint32_t associatedUe = 0;
    for (const auto& [_, ue] : *ueById)
    {
        queueSum += ue->GetQueueBytes();
        if (ue->GetPrimaryApActive() != INVALID_AP_ID)
        {
            ++associatedUe;
        }
    }

    const auto& stats = cpHelper->GetStats();
    std::cout << "[Snapshot] t=" << Simulator::Now().GetMilliSeconds() << "ms"
              << " associatedUE=" << associatedUe << "/" << ueById->size()
              << " queueSum=" << queueSum
              << " ueStateTx=" << stats.ueStateReport.tx
              << " offerTx=" << stats.assocOffer.tx
              << " confirmTx=" << stats.assocConfirm.tx
              << " grantTx=" << stats.schedulingGrant.tx << "\n";

    Simulator::Schedule(period, &PrintPeriodicSnapshot, ueById, cpHelper, period, stopAt);
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string modeStr = "distributed";
    uint32_t numUe = 100;
    uint32_t numAp = 20;
    double simTimeMs = 3000.0;
    double trafficStartMs = 250.0;
    double offeredLoadMbpsPerUe = 3.0;
    double apProcUs = 40.0;
    double ueProcUs = 25.0;
    double cpuProcUs = 60.0;
    double assocMs = 100.0;
    double stateMs = 10.0;
    double monitorMs = 200.0;
    uint32_t seed = 7;
    bool enableSnapshot = true;
    double apSpacingM = 80.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "distributed|centralized", modeStr);
    cmd.AddValue("numUe", "Number of UEs", numUe);
    cmd.AddValue("numAp", "Number of APs", numAp);
    cmd.AddValue("simTimeMs", "Simulation time [ms]", simTimeMs);
    cmd.AddValue("trafficStartMs", "Offered-load start time [ms]", trafficStartMs);
    cmd.AddValue("offeredLoadMbpsPerUe", "Per-UE offered load [Mbps]", offeredLoadMbpsPerUe);
    cmd.AddValue("apProcUs", "AP control processing time per msg [us]", apProcUs);
    cmd.AddValue("ueProcUs", "UE control processing time per msg [us]", ueProcUs);
    cmd.AddValue("cpuProcUs", "CPU control processing time per msg [us]", cpuProcUs);
    cmd.AddValue("assocMs", "Association epoch [ms]", assocMs);
    cmd.AddValue("stateMs", "UE state report period [ms]", stateMs);
    cmd.AddValue("monitorMs", "Periodic snapshot period [ms]", monitorMs);
    cmd.AddValue("seed", "Random seed", seed);
    cmd.AddValue("enableSnapshot", "Print periodic snapshots", enableSnapshot);
    cmd.AddValue("apSpacingM", "AP spacing on line topology [m]", apSpacingM);
    cmd.Parse(argc, argv);

    if (numUe == 0 || numAp == 0)
    {
        std::cerr << "numUe and numAp must be >= 1\n";
        return 1;
    }
    if (numUe > std::numeric_limits<UE_ID>::max() - 1 || numAp > std::numeric_limits<AP_ID>::max() - 1)
    {
        std::cerr << "numUe/numAp exceeds uint16 id range\n";
        return 1;
    }

    const CfMode mode = ParseMode(modeStr);
    const Time tti = MicroSeconds(500);
    const Time tAssoc = MilliSeconds(assocMs);
    const Time tState = MilliSeconds(stateMs);
    const Time simStop = MilliSeconds(simTimeMs);
    const Time trafficStart = MilliSeconds(trafficStartMs);

    const double freqHz = 3.5e9;
    const double prbBwHz = 180000.0;

    std::vector<double> apX(numAp);
    for (uint32_t i = 0; i < numAp; ++i)
    {
        apX[i] = static_cast<double>(i) * apSpacingM;
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uePosDist(-0.5 * apSpacingM,
                                                     (static_cast<double>(numAp) - 0.5) * apSpacingM);

    std::map<AP_ID, Ptr<CfApMac>> apById;
    std::map<AP_ID, Ptr<CfApPhy>> apPhyById;
    std::map<UE_ID, Ptr<CfUeMac>> ueById;
    std::map<UE_ID, Ptr<CfUePhy>> uePhyById;
    std::vector<LoadState> loadStates;
    loadStates.reserve(numUe);

    auto channel = Create3gppChannel(freqHz);

    for (uint32_t i = 0; i < numAp; ++i)
    {
        const AP_ID apId = static_cast<AP_ID>(i + 1);

        auto ap = CreateObject<CfApMac>();
        ap->SetApId(apId);
        ap->SetMode(mode);
        ap->SetIntervals(tti, MilliSeconds(10), tState, MilliSeconds(10), tAssoc);
        apById.emplace(apId, ap);

        auto apPhy = CreateObject<CfApPhy>();
        apPhy->SetApId(apId);
        apPhy->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
        apPhy->SetPilotPowerW(1.0);
        apPhy->SetPilotPrbBitmap(UINT64_MAX);
        apPhy->SetChannel(channel);
        AttachNodeContext(apPhy, Vector(apX[i], 0.0, 10.0));
        channel->AddRx(apPhy);
        apPhyById.emplace(apId, apPhy);
    }

    for (uint32_t i = 0; i < numUe; ++i)
    {
        const UE_ID ueId = static_cast<UE_ID>(i + 1);

        auto ue = CreateObject<CfUeMac>();
        ue->SetUeId(ueId);
        ue->SetMode(mode);
        ue->SetIntervals(tti, tState, tAssoc);
        ue->SetQueueBytes(0);
        ue->SetTrafficClass(static_cast<uint8_t>(ueId % 4));
        ueById.emplace(ueId, ue);

        const double ueX = uePosDist(rng);
        auto uePhy = CreateObject<CfUePhy>();
        uePhy->SetUeId(ueId);
        uePhy->SetPrbConfig(CfMimoConstants::MAX_PRB, prbBwHz, freqHz);
        uePhy->SetChannel(channel);
        AttachNodeContext(uePhy, Vector(ueX, 0.0, 1.5));
        channel->AddRx(uePhy);
        uePhyById.emplace(ueId, uePhy);

        CfPhyMacHelper::BindUeMeasurementProvider(ue, uePhy);

        LoadState ls;
        ls.ue = ue;
        ls.bytesPerTti = offeredLoadMbpsPerUe * 1e6 * (tti.GetSeconds() / 8.0);
        loadStates.push_back(ls);
    }

    Ptr<CfCpuController> cpu;
    if (mode == CfMode::CENTRALIZED)
    {
        cpu = CreateObject<CfCpuController>();
        cpu->SetAssocInterval(tAssoc);
    }

    CfControlPlaneHelper cpHelper;
    cpHelper.SetMode(mode);
    cpHelper.SetProcessingTimes(MicroSeconds(apProcUs), MicroSeconds(ueProcUs), MicroSeconds(cpuProcUs));
    cpHelper.SetNetwork(apById, ueById, cpu);
    cpHelper.Install();

    for (auto& [_, ap] : apById)
    {
        ap->Start();
    }
    for (auto& [_, apPhy] : apPhyById)
    {
        apPhy->Start();
    }
    for (auto& [_, ue] : ueById)
    {
        ue->Start();
    }
    if (cpu)
    {
        cpu->Start();
    }

    Simulator::Schedule(trafficStart, &InjectLoadTick, &loadStates, tti, simStop);
    if (enableSnapshot && monitorMs > 0.0)
    {
        Simulator::Schedule(MilliSeconds(monitorMs),
                            &PrintPeriodicSnapshot,
                            &ueById,
                            &cpHelper,
                            MilliSeconds(monitorMs),
                            simStop);
    }

    const auto wallStart = std::chrono::steady_clock::now();
    Simulator::Stop(simStop);
    Simulator::Run();
    const auto wallEnd = std::chrono::steady_clock::now();
    Simulator::Destroy();

    uint64_t queueRemain = 0;
    uint32_t associatedUe = 0;
    for (const auto& [_, ue] : ueById)
    {
        queueRemain += ue->GetQueueBytes();
        if (ue->GetPrimaryApActive() != INVALID_AP_ID)
        {
            ++associatedUe;
        }
    }

    uint64_t injectedBytes = 0;
    for (const auto& s : loadStates)
    {
        injectedBytes += s.injectedBytes;
    }

    const auto& stats = cpHelper.GetStats();
    const auto apPipe = cpHelper.GetAggregatedApPipeStats();
    const auto uePipe = cpHelper.GetAggregatedUePipeStats();
    const auto cpuPipe = cpHelper.GetCpuPipeStats();

    const double simSec = simTimeMs / 1000.0;
    const double wallSec =
        std::chrono::duration_cast<std::chrono::duration<double>>(wallEnd - wallStart).count();
    const double realtimeFactor = (wallSec > 0.0) ? (simSec / wallSec) : 0.0;

    const uint64_t totalCtrlTx = stats.ueStateReport.tx + stats.assocOffer.tx + stats.assocConfirm.tx +
                                 stats.srBsr.tx + stats.apSummary.tx + stats.clusterDecision.tx +
                                 stats.centralizedAssocToUe.tx + stats.schedulingGrant.tx;

    const double ctrlMsgRate = (simSec > 0.0) ? (static_cast<double>(totalCtrlTx) / simSec) : 0.0;
    const double offeredMbps = (simSec > 0.0) ? (static_cast<double>(injectedBytes) * 8.0 / simSec / 1e6) : 0.0;
    const double scheduledMbps =
        (simSec > 0.0) ? (static_cast<double>(stats.grantEstimatedBytes) * 8.0 / simSec / 1e6) : 0.0;

    const double avgApDelayMs =
        (apPipe.enqueued > 0) ? NsToMs(apPipe.sumDelayNs) / static_cast<double>(apPipe.enqueued) : 0.0;
    const double avgUeDelayMs =
        (uePipe.enqueued > 0) ? NsToMs(uePipe.sumDelayNs) / static_cast<double>(uePipe.enqueued) : 0.0;
    const double avgCpuDelayMs =
        (cpuPipe.enqueued > 0) ? NsToMs(cpuPipe.sumDelayNs) / static_cast<double>(cpuPipe.enqueued) : 0.0;

    std::cout << "\n=== CF-MIMO Control Storm Scenario ===\n";
    std::cout << "mode=" << ((mode == CfMode::CENTRALIZED) ? "centralized" : "distributed")
              << " UE=" << numUe << " AP=" << numAp << " simTimeMs=" << simTimeMs << "\n";
    std::cout << "ctrlProcUs(ap/ue/cpu)=" << apProcUs << "/" << ueProcUs << "/" << cpuProcUs << "\n";
    std::cout << "offeredLoadMbpsPerUe=" << offeredLoadMbpsPerUe << " totalOfferedMbps=" << offeredMbps << "\n";
    std::cout << "totalCtrlTx=" << totalCtrlTx << " ctrlMsgPerSec=" << ctrlMsgRate << "\n";
    std::cout << "simWallSec=" << wallSec << " realtimeFactor=" << realtimeFactor << "\n";
    std::cout << "associatedUE=" << associatedUe << "/" << numUe << " queueRemainBytes=" << queueRemain << "\n";
    std::cout << "scheduledEstimatedMbps=" << scheduledMbps << " injectedBytes=" << injectedBytes
              << " grantBytes=" << stats.grantEstimatedBytes << "\n";

    std::cout << "\n[Message Counters tx/rx]\n";
    std::cout << "UEStateReport: " << stats.ueStateReport.tx << " / " << stats.ueStateReport.rx << "\n";
    std::cout << "AssocOffer: " << stats.assocOffer.tx << " / " << stats.assocOffer.rx << "\n";
    std::cout << "AssocConfirm: " << stats.assocConfirm.tx << " / " << stats.assocConfirm.rx << "\n";
    std::cout << "SR_BSR: " << stats.srBsr.tx << " / " << stats.srBsr.rx << "\n";
    std::cout << "APSummary: " << stats.apSummary.tx << " / " << stats.apSummary.rx << "\n";
    std::cout << "ClusterDecision: " << stats.clusterDecision.tx << " / " << stats.clusterDecision.rx << "\n";
    std::cout << "CentralizedAssocToUe: " << stats.centralizedAssocToUe.tx << " / "
              << stats.centralizedAssocToUe.rx << "\n";
    std::cout << "SchedulingGrant: " << stats.schedulingGrant.tx << " / " << stats.schedulingGrant.rx << "\n";

    std::cout << "\n[Control Queue Delay]\n";
    std::cout << "AP-RX: enq=" << apPipe.enqueued << " deliv=" << apPipe.delivered
              << " avgDelayMs=" << avgApDelayMs
              << " maxDelayMs=" << NsToMs(apPipe.maxDelayNs) << "\n";
    std::cout << "UE-RX: enq=" << uePipe.enqueued << " deliv=" << uePipe.delivered
              << " avgDelayMs=" << avgUeDelayMs
              << " maxDelayMs=" << NsToMs(uePipe.maxDelayNs) << "\n";
    std::cout << "CPU-RX: enq=" << cpuPipe.enqueued << " deliv=" << cpuPipe.delivered
              << " avgDelayMs=" << avgCpuDelayMs
              << " maxDelayMs=" << NsToMs(cpuPipe.maxDelayNs) << "\n";

    return 0;
}
