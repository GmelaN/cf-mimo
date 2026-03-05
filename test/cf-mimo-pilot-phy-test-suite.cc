/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/cf-ap-phy.h"
#include "ns3/cf-ue-phy.h"
#include "ns3/channel-condition-model.h"
#include "ns3/boolean.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/double.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/node.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/simple-net-device.h"
#include "ns3/spectrum-phy.h"
#include "ns3/test.h"
#include "ns3/three-gpp-propagation-loss-model.h"

#include <cstdint>

namespace ns3
{

namespace
{

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

Ptr<ConstantPositionMobilityModel>
CreateMobility(const Vector& pos)
{
    auto m = CreateObject<ConstantPositionMobilityModel>();
    m->SetPosition(pos);
    return m;
}

void
AttachNodeContext(Ptr<SpectrumPhy> phy, const Vector& pos)
{
    auto node = CreateObject<Node>();
    auto dev = CreateObject<SimpleNetDevice>();
    node->AddDevice(dev);
    dev->SetNode(node);
    auto mob = CreateMobility(pos);
    node->AggregateObject(mob);
    phy->SetDevice(dev);
    phy->SetMobility(mob);
}

} // namespace

class CfMimoPilotSingleApTestCase : public TestCase
{
  public:
    CfMimoPilotSingleApTestCase()
        : TestCase("Single-AP pilot PSD should create UE RSRP/SINR/CQI measurement")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(11);
        RngSeedManager::SetRun(1);

        const double freqHz = 3.5e9;
        const double prbBwHz = 180000.0;
        auto channel = Create3gppChannel(freqHz);

        auto apPhy = CreateObject<CfApPhy>();
        apPhy->SetApId(1);
        apPhy->SetPrbConfig(64, prbBwHz, freqHz);
        apPhy->SetPilotPowerW(1.0);
        apPhy->SetPilotPrbBitmap(UINT64_MAX);
        apPhy->SetChannel(channel);
        AttachNodeContext(apPhy, Vector(0.0, 0.0, 10.0));

        auto uePhy = CreateObject<CfUePhy>();
        uePhy->SetUeId(1);
        uePhy->SetPrbConfig(64, prbBwHz, freqHz);
        uePhy->SetChannel(channel);
        AttachNodeContext(uePhy, Vector(20.0, 0.0, 1.5));

        channel->AddRx(apPhy);
        channel->AddRx(uePhy);

        apPhy->TransmitPilot(0);

        Simulator::Stop(MilliSeconds(2));
        Simulator::Run();

        UeApMeasurement m{};
        const bool ok = uePhy->GetMeasurement(1, &m);
        NS_TEST_EXPECT_MSG_EQ(ok, true, "UE should have pilot-based measurement for AP1");
        NS_TEST_EXPECT_MSG_EQ(m.ap_id, static_cast<AP_ID>(1), "AP id mismatch");
        NS_TEST_EXPECT_MSG_LT_OR_EQ(m.cqi, static_cast<uint8_t>(15), "CQI out of range");
        NS_TEST_EXPECT_MSG_GT(m.rsrp_q, static_cast<int16_t>(-2000), "RSRP value too small");

        const auto top = uePhy->GetTopMeasurements(4);
        NS_TEST_EXPECT_MSG_EQ(top.size(), static_cast<size_t>(1), "Only one AP should be measured");

        Simulator::Destroy();
    }
};

class CfMimoPilotInterferenceTestCase : public TestCase
{
  public:
    CfMimoPilotInterferenceTestCase()
        : TestCase("Simultaneous multi-AP pilots should be reflected as interference in UE CQI")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(13);
        RngSeedManager::SetRun(1);

        const double freqHz = 3.5e9;
        const double prbBwHz = 180000.0;
        auto channel = Create3gppChannel(freqHz);

        auto apNear = CreateObject<CfApPhy>();
        apNear->SetApId(1);
        apNear->SetPrbConfig(64, prbBwHz, freqHz);
        apNear->SetPilotPowerW(1.0);
        apNear->SetPilotPrbBitmap(UINT64_MAX);
        apNear->SetChannel(channel);
        AttachNodeContext(apNear, Vector(0.0, 0.0, 10.0));

        auto apFar = CreateObject<CfApPhy>();
        apFar->SetApId(2);
        apFar->SetPrbConfig(64, prbBwHz, freqHz);
        apFar->SetPilotPowerW(1.0);
        apFar->SetPilotPrbBitmap(UINT64_MAX);
        apFar->SetChannel(channel);
        AttachNodeContext(apFar, Vector(120.0, 0.0, 10.0));

        auto uePhy = CreateObject<CfUePhy>();
        uePhy->SetUeId(1);
        uePhy->SetPrbConfig(64, prbBwHz, freqHz);
        uePhy->SetChannel(channel);
        AttachNodeContext(uePhy, Vector(15.0, 0.0, 1.5));

        channel->AddRx(apNear);
        channel->AddRx(apFar);
        channel->AddRx(uePhy);

        apNear->TransmitPilot(0);
        apFar->TransmitPilot(0);

        Simulator::Stop(MilliSeconds(2));
        Simulator::Run();

        UeApMeasurement nearMeas{};
        UeApMeasurement farMeas{};
        NS_TEST_EXPECT_MSG_EQ(uePhy->GetMeasurement(1, &nearMeas),
                              true,
                              "Near AP measurement missing");
        NS_TEST_EXPECT_MSG_EQ(uePhy->GetMeasurement(2, &farMeas),
                              true,
                              "Far AP measurement missing");
        NS_TEST_EXPECT_MSG_GT(nearMeas.rsrp_q, farMeas.rsrp_q, "Near AP should have larger RSRP");
        NS_TEST_EXPECT_MSG_GT_OR_EQ(nearMeas.cqi,
                                    farMeas.cqi,
                                    "Near AP should not have lower CQI");

        const auto top = uePhy->GetTopMeasurements(2);
        NS_TEST_EXPECT_MSG_EQ(top.size(), static_cast<size_t>(2), "Expected two AP measurements");
        NS_TEST_EXPECT_MSG_EQ(top.front().ap_id,
                              static_cast<AP_ID>(1),
                              "Near AP should rank first by RSRP");

        Simulator::Destroy();
    }
};

class CfMimoUlDataPathTestCase : public TestCase
{
  public:
    CfMimoUlDataPathTestCase()
        : TestCase("UE UL DATA PSD should be received and decoded at AP PHY")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(17);
        RngSeedManager::SetRun(1);

        const double freqHz = 3.5e9;
        const double prbBwHz = 180000.0;
        auto channel = Create3gppChannel(freqHz);

        auto apPhy = CreateObject<CfApPhy>();
        apPhy->SetApId(1);
        apPhy->SetPrbConfig(64, prbBwHz, freqHz);
        apPhy->SetNoiseModel(-174.0, 7.0);
        apPhy->SetChannel(channel);
        AttachNodeContext(apPhy, Vector(0.0, 0.0, 10.0));

        auto uePhy = CreateObject<CfUePhy>();
        uePhy->SetUeId(1);
        uePhy->SetPrbConfig(64, prbBwHz, freqHz);
        uePhy->SetDataPowerDbmRange(10.0, 26.0);
        uePhy->SetChannel(channel);
        AttachNodeContext(uePhy, Vector(8.0, 0.0, 1.5));

        channel->AddRx(apPhy);
        channel->AddRx(uePhy);

        SchedulingGrant grant{};
        grant.ue_id = 1;
        grant.tti = 0;
        grant.mcs = 12;
        grant.tx_power_idx = 255;
        grant.prb_bitmap = UINT64_MAX;
        uePhy->QueueUlGrant(grant, 1);

        Simulator::Stop(MilliSeconds(2));
        Simulator::Run();

        CfApPhy::UlRxResult result{};
        const bool ok = apPhy->GetLastUlRx(1, &result);
        NS_TEST_EXPECT_MSG_EQ(ok, true, "AP should have UL reception result for UE1");
        NS_TEST_EXPECT_MSG_EQ(result.apId, static_cast<AP_ID>(1), "AP id mismatch");
        NS_TEST_EXPECT_MSG_EQ(result.ueId, static_cast<UE_ID>(1), "UE id mismatch");
        NS_TEST_EXPECT_MSG_EQ(result.tti, 0u, "UL TTI mismatch");
        NS_TEST_EXPECT_MSG_GT(result.rxBytes, 0u, "UL decoded bytes should be positive");
        NS_TEST_EXPECT_MSG_EQ(result.ok, true, "UL decode should succeed in high-SINR setup");

        Simulator::Destroy();
    }
};

class CfMimoPilotPhyTestSuite : public TestSuite
{
  public:
    CfMimoPilotPhyTestSuite()
        : TestSuite("cf-mimo-pilot-phy", Type::UNIT)
    {
        AddTestCase(new CfMimoPilotSingleApTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CfMimoPilotInterferenceTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CfMimoUlDataPathTestCase(), TestCase::Duration::QUICK);
    }
};

static CfMimoPilotPhyTestSuite g_cfMimoPilotPhyTestSuite;

} // namespace ns3
