/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cf-ue-traffic-adapter.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CfUeTrafficAdapter");
NS_OBJECT_ENSURE_REGISTERED(CfUeTrafficAdapter);

TypeId
CfUeTrafficAdapter::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CfUeTrafficAdapter")
                            .SetParent<Object>()
                            .SetGroupName("CfMimo")
                            .AddConstructor<CfUeTrafficAdapter>();
    return tid;
}

CfUeTrafficAdapter::CfUeTrafficAdapter() = default;

CfUeTrafficAdapter::~CfUeTrafficAdapter()
{
    NS_LOG_FUNCTION(this);
}

void
CfUeTrafficAdapter::DoDispose()
{
    Detach();
    m_ueMac = nullptr;
    Object::DoDispose();
}

void
CfUeTrafficAdapter::SetUeMac(Ptr<CfUeMac> ueMac)
{
    m_ueMac = ueMac;
}

Ptr<CfUeMac>
CfUeTrafficAdapter::GetUeMac() const
{
    return m_ueMac;
}

bool
CfUeTrafficAdapter::Attach(Ptr<NetDevice> device)
{
    Detach();

    if (!device)
    {
        return false;
    }

    const bool ok =
        device->TraceConnectWithoutContext("MacTx", MakeCallback(&CfUeTrafficAdapter::OnMacTx, this));
    if (!ok)
    {
        return false;
    }

    m_device = device;
    m_attached = true;
    return true;
}

void
CfUeTrafficAdapter::Detach()
{
    if (!m_attached || !m_device)
    {
        m_attached = false;
        m_device = nullptr;
        return;
    }

    m_device->TraceDisconnectWithoutContext("MacTx",
                                            MakeCallback(&CfUeTrafficAdapter::OnMacTx, this));
    m_attached = false;
    m_device = nullptr;
}

void
CfUeTrafficAdapter::OnMacTx(Ptr<const Packet> packet)
{
    if (!m_ueMac || !packet)
    {
        return;
    }

    const uint32_t injectedBytes = packet->GetSize();
    const uint32_t nextQueue = m_ueMac->GetQueueBytes() + injectedBytes;
    m_ueMac->SetQueueBytes(nextQueue);
    ++m_totalInjectedPackets;
    m_totalInjectedBytes += injectedBytes;
}

uint64_t
CfUeTrafficAdapter::GetTotalInjectedBytes() const
{
    return m_totalInjectedBytes;
}

uint64_t
CfUeTrafficAdapter::GetTotalInjectedPackets() const
{
    return m_totalInjectedPackets;
}

} // namespace ns3
