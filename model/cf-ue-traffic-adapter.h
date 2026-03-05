/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_UE_TRAFFIC_ADAPTER_H
#define CF_UE_TRAFFIC_ADAPTER_H

#include "cf-ue-mac.h"

#include "ns3/net-device.h"
#include "ns3/object.h"
#include "ns3/packet.h"

namespace ns3
{

/**
 * Bridge app/device TX traffic into CfUeMac queue bytes.
 *
 * This adapter listens to NetDevice "MacTx" trace source and accumulates
 * transmitted packet bytes into CfUeMac::queue_bytes. This provides a minimal
 * app-traffic-to-CF-MAC integration path without implementing a custom NetDevice.
 */
class CfUeTrafficAdapter : public Object
{
  public:
    static TypeId GetTypeId();

    CfUeTrafficAdapter();
    ~CfUeTrafficAdapter() override;

    void SetUeMac(Ptr<CfUeMac> ueMac);
    Ptr<CfUeMac> GetUeMac() const;

    /**
     * Attach to a NetDevice that exports "MacTx" trace source.
     * @return true if trace connection succeeded.
     */
    bool Attach(Ptr<NetDevice> device);
    void Detach();

    uint64_t GetTotalInjectedBytes() const;
    uint64_t GetTotalInjectedPackets() const;

  protected:
    void DoDispose() override;

  private:
    void OnMacTx(Ptr<const Packet> packet);

    Ptr<CfUeMac> m_ueMac;
    Ptr<NetDevice> m_device;
    bool m_attached{false};
    uint64_t m_totalInjectedBytes{0};
    uint64_t m_totalInjectedPackets{0};
};

} // namespace ns3

#endif // CF_UE_TRAFFIC_ADAPTER_H
