/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/cf-ap-mac.h"
#include "ns3/cf-ue-mac.h"
#include "ns3/simulator.h"
#include "ns3/test.h"

namespace ns3
{

class CfMimoDistributedProtocolTestCase : public TestCase
{
  public:
    CfMimoDistributedProtocolTestCase()
        : TestCase("Distributed CF-mMIMO control-plane handshake and scheduling")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<CfUeMac> ue = CreateObject<CfUeMac>();
        Ptr<CfApMac> ap = CreateObject<CfApMac>();

        ue->SetUeId(1);
        ap->SetApId(1);
        ue->SetMode(CfMode::DISTRIBUTED);
        ap->SetMode(CfMode::DISTRIBUTED);
        ue->SetQueueBytes(60000);
        ue->SetTrafficClass(1);

        uint32_t reports = 0;
        uint32_t offers = 0;
        uint32_t confirms = 0;
        uint32_t grants = 0;
        uint32_t bsr = 0;

        ue->SetLinkMeasurementProvider([]() {
            std::vector<UeApMeasurement> m;
            m.push_back(UeApMeasurement{1, 12, 180, -780, 0});
            m.push_back(UeApMeasurement{2, 7, 90, -900, 0});
            return m;
        });

        ue->SetStateReportSink([&](const UEStateReport& report, AP_ID apId) {
            ++reports;
            if (apId == 1)
            {
                ap->ReceiveUeStateReport(report);
            }
        });

        ap->SetOfferSink([&](const AssocOffer& offer, UE_ID ueId) {
            if (ueId == 1)
            {
                ++offers;
                ue->ReceiveAssocOffer(offer);
            }
        });

        ue->SetAssocConfirmSink([&](const AssocConfirm& confirm, AP_ID apId) {
            if (apId == 1)
            {
                ++confirms;
                ap->ReceiveAssocConfirm(confirm);
            }
        });

        ue->SetSrBsrSink([&](const SR_BSR& msg, AP_ID apId) {
            ++bsr;
            if (apId == 1)
            {
                ap->ReceiveSrBsr(msg);
            }
        });

        ap->SetGrantSink([&](const SchedulingGrant& grant, UE_ID ueId) {
            if (ueId == 1)
            {
                ++grants;
                ue->ReceiveSchedulingGrant(grant);
            }
        });

        ue->Start();
        ap->Start();

        Simulator::Stop(MilliSeconds(280));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_GT(reports, 0u, "UE should periodically report state");
        NS_TEST_EXPECT_MSG_GT(offers, 0u, "AP should send association offers");
        NS_TEST_EXPECT_MSG_GT(confirms, 0u, "UE should send association confirm");
        NS_TEST_EXPECT_MSG_GT(bsr, 0u, "UE should send SR_BSR");
        NS_TEST_EXPECT_MSG_GT(grants, 0u, "AP should produce scheduling grants");
        NS_TEST_EXPECT_MSG_EQ(ue->GetPrimaryApActive(), static_cast<AP_ID>(1), "Primary AP mismatch");
        NS_TEST_EXPECT_MSG_LT(ue->GetQueueBytes(),
                              static_cast<uint32_t>(60000),
                              "Queue bytes should decrease after grants");

        ue->Stop();
        ap->Stop();
        Simulator::Destroy();
    }
};

class CfMimoProtocolTestSuite : public TestSuite
{
  public:
    CfMimoProtocolTestSuite()
        : TestSuite("cf-mimo-protocol", Type::UNIT)
    {
        AddTestCase(new CfMimoDistributedProtocolTestCase(), TestCase::Duration::QUICK);
    }
};

static CfMimoProtocolTestSuite g_cfMimoProtocolTestSuite;

} // namespace ns3
