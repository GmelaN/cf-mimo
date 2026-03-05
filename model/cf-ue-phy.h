/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_UE_PHY_H
#define CF_UE_PHY_H

#include "cf-mimo-common.h"
#include "cf-mimo-messages.h"

#include "ns3/antenna-model.h"
#include "ns3/event-id.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/mobility-model.h"
#include "ns3/net-device.h"
#include "ns3/object.h"
#include "ns3/spectrum-channel.h"
#include "ns3/spectrum-model.h"
#include "ns3/spectrum-phy.h"
#include "ns3/spectrum-value.h"

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <vector>

namespace ns3
{

class CfUePhy : public SpectrumPhy
{
  public:
    static TypeId GetTypeId();

    CfUePhy();
    ~CfUePhy() override;

    void SetUeId(UE_ID ueId);
    UE_ID GetUeId() const;

    void SetTti(Time tti);
    void SetPrbConfig(uint8_t maxPrb, double prbBandwidthHz, double centerFrequencyHz);
    void SetNoiseModel(double thermalNoiseDbmPerHz, double noiseFigureDb);
    void SetDataPowerDbmRange(double minDbm, double maxDbm);
    void SetCqiThresholdsDb(const std::array<double, 16>& thresholdsDb);
    void QueueUlGrant(const SchedulingGrant& grant, AP_ID targetApId);

    std::vector<UeApMeasurement> GetTopMeasurements(uint8_t topN) const;
    bool GetMeasurement(AP_ID apId, UeApMeasurement* out) const;

    // SpectrumPhy interface
    void SetChannel(Ptr<SpectrumChannel> c) override;
    void SetMobility(Ptr<MobilityModel> m) override;
    void SetDevice(Ptr<NetDevice> d) override;
    Ptr<MobilityModel> GetMobility() const override;
    Ptr<NetDevice> GetDevice() const override;
    Ptr<const SpectrumModel> GetRxSpectrumModel() const override;
    Ptr<Object> GetAntenna() const override;
    void StartRx(Ptr<SpectrumSignalParameters> params) override;

  protected:
    void DoDispose() override;

  private:
    struct PilotWindow
    {
        std::map<AP_ID, double> rsrpWByAp;
        uint64_t prbBitmap{0};
        EventId finalizeEvent;
    };

    struct Measurement
    {
        UeApMeasurement meas;
        double sinrLin{0.0};
        double sinrDb{-std::numeric_limits<double>::infinity()};
        Time lastMeasTime{Seconds(0)};
    };

    struct PendingUlTx
    {
        SchedulingGrant grant;
        AP_ID targetApId{INVALID_AP_ID};
        EventId txEvent;
    };

    void EnsureSpectrumModel();
    void FinalizePilotTti(uint32_t ttiIndex);
    uint8_t QuantizeCqi(double sinrDb) const;
    Ptr<SpectrumValue> BuildPsdFromBitmap(uint64_t bitmap, double totalPowerW) const;
    void OnUlTxTime(uint32_t ttiIndex);
    void TransmitUlData(const SchedulingGrant& grant, AP_ID targetApId);

    UE_ID m_ueId{INVALID_UE_ID};
    Time m_tti{MicroSeconds(500)};
    uint8_t m_maxPrb{CfMimoConstants::MAX_PRB};
    double m_prbBandwidthHz{180000.0};
    double m_centerFrequencyHz{3.5e9};

    double m_thermalNoiseDbmPerHz{-174.0};
    double m_noiseFigureDb{9.0};
    double m_dataPowerMinDbm{-5.0};
    double m_dataPowerMaxDbm{23.0};
    std::array<double, 16> m_cqiThresholdsDb{
        -10.0, -7.0, -5.0, -3.0, -1.0, 1.0, 3.0, 5.0,
        7.0,   9.0,  11.0, 13.0, 15.0, 17.0, 19.0, 21.0};

    std::map<uint32_t, PilotWindow> m_pilotWindows;
    std::map<uint32_t, PendingUlTx> m_pendingUlTxByTti;
    std::map<AP_ID, Measurement> m_measurements;

    Ptr<SpectrumChannel> m_channel;
    Ptr<MobilityModel> m_mobility;
    Ptr<NetDevice> m_device;
    Ptr<AntennaModel> m_antenna;
    Ptr<const SpectrumModel> m_spectrumModel;
};

} // namespace ns3

#endif // CF_UE_PHY_H
