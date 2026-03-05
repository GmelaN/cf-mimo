/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/boolean.h"
#include "ns3/cf-ap-phy.h"
#include "ns3/cf-ue-phy.h"
#include "ns3/channel-condition-model.h"
#include "ns3/command-line.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/double.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/node.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/simulator.h"
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

int
main(int argc, char* argv[])
{
    double simTimeMs = 25.0;
    uint32_t topN = 3;
    CommandLine cmd(__FILE__);
    cmd.AddValue("simTimeMs", "Simulation time in milliseconds", simTimeMs);
    cmd.AddValue("topN", "Number of AP measurements to print", topN);
    cmd.Parse(argc, argv);

    const double freqHz = 3.5e9;
    const double prbBwHz = 180000.0;
    auto channel = Create3gppChannel(freqHz);

    auto uePhy = CreateObject<CfUePhy>();
    uePhy->SetUeId(1);
    uePhy->SetPrbConfig(64, prbBwHz, freqHz);
    uePhy->SetChannel(channel);
    AttachNodeContext(uePhy, Vector(25.0, 0.0, 1.5));
    channel->AddRx(uePhy);

    std::map<AP_ID, Ptr<CfApPhy>> apById;
    std::vector<Vector> apPositions = {Vector(0.0, 0.0, 10.0),
                                       Vector(60.0, 0.0, 10.0),
                                       Vector(140.0, 0.0, 10.0)};
    for (AP_ID apId = 1; apId <= apPositions.size(); ++apId)
    {
        auto apPhy = CreateObject<CfApPhy>();
        apPhy->SetApId(apId);
        apPhy->SetPrbConfig(64, prbBwHz, freqHz);
        apPhy->SetPilotPowerW(1.0);
        apPhy->SetPilotPrbBitmap(UINT64_MAX);
        apPhy->SetChannel(channel);
        AttachNodeContext(apPhy, apPositions[apId - 1]);
        channel->AddRx(apPhy);
        apPhy->Start();
        apById[apId] = apPhy;
    }

    Simulator::Stop(MilliSeconds(simTimeMs));
    Simulator::Run();

    std::cout << "=== CF-MIMO Pilot PHY Example ===\n";
    const auto top = uePhy->GetTopMeasurements(static_cast<uint8_t>(topN));
    for (const auto& m : top)
    {
        std::cout << "AP " << m.ap_id << " CQI=" << static_cast<uint32_t>(m.cqi)
                  << " SINR[dB*10]=" << m.sinr_q << " RSRP[dBm*10]=" << m.rsrp_q << "\n";
    }

    Simulator::Destroy();
    return 0;
}
