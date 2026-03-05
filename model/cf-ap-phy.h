/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_AP_PHY_H
#define CF_AP_PHY_H

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

#include <functional>
#include <limits>
#include <map>

namespace ns3
{

class CfApPhy : public SpectrumPhy
{
  public:
    static TypeId GetTypeId();

    CfApPhy();
    ~CfApPhy() override;

    using EpochProvider = std::function<uint32_t()>;
    struct UlRxResult
    {
        UE_ID ueId{INVALID_UE_ID};
        AP_ID apId{INVALID_AP_ID};
        uint32_t tti{0};
        uint8_t mcs{0};
        uint64_t prbBitmap{0};
        double sinrDb{-std::numeric_limits<double>::infinity()};
        uint32_t rxBytes{0};
        bool ok{false};
    };
    using UlRxSink = std::function<void(const UlRxResult&)>;

    void SetApId(AP_ID apId);
    AP_ID GetApId() const;

    void SetTti(Time tti);
    void SetCoherenceTime(Time tCoh);
    void SetPrbConfig(uint8_t maxPrb, double prbBandwidthHz, double centerFrequencyHz);

    void SetPilotPowerW(double pilotPowerW);
    void SetPilotPrbBitmap(uint64_t pilotPrbBitmap);
    void SetDataPowerDbmRange(double minDbm, double maxDbm);
    void SetNoiseModel(double thermalNoiseDbmPerHz, double noiseFigureDb);
    void SetEpochProvider(EpochProvider cb);
    void SetUlRxSink(UlRxSink cb);

    void QueueSchedulingGrant(const SchedulingGrant& grant);
    void Start();
    void Stop();

    void TransmitPilot(uint32_t ttiIndex);
    void TransmitData(const SchedulingGrant& grant);
    bool GetLastUlRx(UE_ID ueId, UlRxResult* out) const;

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
    struct DataWindow
    {
        std::map<UE_ID, double> rxPowerWByUe;
        std::map<UE_ID, uint64_t> prbBitmapByUe;
        std::map<UE_ID, uint8_t> mcsByUe;
        std::map<UE_ID, bool> decodeCandidateByUe;
        EventId finalizeEvent;
    };

    void OnTtiTick();
    void EnsureSpectrumModel();
    Ptr<SpectrumValue> BuildPsdFromBitmap(uint64_t bitmap, double totalPowerW) const;
    bool IsPilotTti(uint32_t ttiIndex) const;
    void FinalizeDataTti(uint32_t ttiIndex);
    uint32_t EstimateTbBytes(uint64_t prbBitmap, uint8_t mcs) const;
    double GetMcsThresholdDb(uint8_t mcs) const;

    AP_ID m_apId{INVALID_AP_ID};
    Time m_tti{MicroSeconds(500)};
    Time m_tCoh{MilliSeconds(10)};
    uint8_t m_maxPrb{CfMimoConstants::MAX_PRB};
    double m_prbBandwidthHz{180000.0};
    double m_centerFrequencyHz{3.5e9};

    double m_pilotPowerW{1.0};
    uint64_t m_pilotPrbBitmap{UINT64_MAX};
    double m_dataPowerMinDbm{0.0};
    double m_dataPowerMaxDbm{30.0};
    double m_thermalNoiseDbmPerHz{-174.0};
    double m_noiseFigureDb{9.0};

    EpochProvider m_epochProvider;
    UlRxSink m_ulRxSink;

    bool m_running{false};
    EventId m_ttiEvent;
    std::map<uint32_t, SchedulingGrant> m_pendingGrantByTti;
    std::map<uint32_t, DataWindow> m_dataWindows;
    std::map<UE_ID, UlRxResult> m_lastUlRxByUe;

    Ptr<SpectrumChannel> m_channel;
    Ptr<MobilityModel> m_mobility;
    Ptr<NetDevice> m_device;
    Ptr<AntennaModel> m_antenna;
    Ptr<const SpectrumModel> m_spectrumModel;
};

} // namespace ns3

#endif // CF_AP_PHY_H
