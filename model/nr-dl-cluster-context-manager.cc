// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-dl-cluster-context-manager.h"

#include "ns3/abort.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrDlClusterContextManager");

NS_OBJECT_ENSURE_REGISTERED(NrDlClusterContextManager);

TypeId
NrDlClusterContextManager::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NrDlClusterContextManager")
                            .SetParent<Object>()
                            .SetGroupName("Nr")
                            .AddConstructor<NrDlClusterContextManager>();
    return tid;
}

NrDlClusterContextManager::NrDlClusterContextManager()
    : m_selectionMechanism(CreateObject<NrDlClusterSelectionMechanismDefault>())
{
}

NrDlClusterContextManager::~NrDlClusterContextManager() = default;

void
NrDlClusterContextManager::SetCoordinator(const Ptr<NrDlClusterCoordinator>& coordinator)
{
    m_coordinator = coordinator;
}

Ptr<NrDlClusterCoordinator>
NrDlClusterContextManager::GetCoordinator() const
{
    return m_coordinator;
}

void
NrDlClusterContextManager::SetSelectionMechanism(
    const Ptr<NrDlClusterSelectionMechanism>& selectionMechanism)
{
    NS_ABORT_MSG_IF(selectionMechanism == nullptr, "Cluster selection mechanism cannot be null");
    m_selectionMechanism = selectionMechanism;
}

Ptr<NrDlClusterSelectionMechanism>
NrDlClusterContextManager::GetSelectionMechanism() const
{
    return m_selectionMechanism;
}

void
NrDlClusterContextManager::RegisterApAgent(const Ptr<NrDlClusterApAgent>& agent)
{
    NS_ABORT_MSG_IF(m_coordinator == nullptr, "Cluster coordinator must be set before AP registration");
    m_coordinator->RegisterApAgent(agent);
}

void
NrDlClusterContextManager::AssociateUe(const NrDlClusterUeAssociation& association)
{
    NS_ABORT_MSG_IF(association.imsi == 0, "IMSI 0 is not valid for cluster context");
    m_associations[association.imsi] = association;
    if (m_coordinator != nullptr)
    {
        m_coordinator->AssociateUe(association);
    }
}

bool
NrDlClusterContextManager::HasAssociation(uint64_t imsi) const
{
    return m_associations.find(imsi) != m_associations.end();
}

NrDlClusterUeAssociation
NrDlClusterContextManager::GetAssociation(uint64_t imsi) const
{
    auto it = m_associations.find(imsi);
    NS_ABORT_MSG_IF(it == m_associations.end(), "No DL cluster context for IMSI " << imsi);
    return it->second;
}

uint16_t
NrDlClusterContextManager::GetCurrentCoordinatorCellId(uint64_t imsi) const
{
    return GetAssociation(imsi).coordinatorCellId;
}

void
NrDlClusterContextManager::SyncAssociationToCoordinator(uint64_t imsi)
{
    NS_ABORT_MSG_IF(m_coordinator == nullptr, "Cluster coordinator has not been configured");
    m_coordinator->AssociateUe(GetAssociation(imsi));
}

NrDlClusterUeAssociation
NrDlClusterContextManager::ReselectCoordinator(uint64_t imsi,
                                               const std::vector<uint16_t>& candidateApCellIds,
                                               uint32_t slotIndex,
                                               uint32_t tbSize,
                                               uint8_t mcs)
{
    NS_ABORT_MSG_IF(m_selectionMechanism == nullptr,
                    "Cluster selection mechanism has not been configured");
    SyncAssociationToCoordinator(imsi);
    const auto currentAssociation = GetAssociation(imsi);
    if (!m_selectionMechanism->ShouldTriggerClusterUpdate(currentAssociation, candidateApCellIds))
    {
        return currentAssociation;
    }

    const NrDlClusterParticipationRequest request{imsi,
                                                  currentAssociation.coordinatorCellId,
                                                  slotIndex,
                                                  tbSize,
                                                  mcs};
    auto acceptedResponses =
        m_coordinator->EvaluateParticipationResponses(request, candidateApCellIds);
    if (acceptedResponses.empty())
    {
        NS_LOG_WARN("Cluster update left UE " << imsi
                                              << " without any accepted AP; keeping previous"
                                              << " coordinator and AP set");
        return currentAssociation;
    }

    auto updated = m_selectionMechanism->SelectAssociation(currentAssociation, acceptedResponses);
    m_associations[imsi] = updated;
    m_coordinator->AssociateUe(updated);
    return updated;
}

NrDlClusterGrant
NrDlClusterContextManager::CreateDlGrant(uint64_t imsi,
                                         uint32_t slotIndex,
                                         uint32_t tbSize,
                                         uint8_t mcs)
{
    SyncAssociationToCoordinator(imsi);
    return m_coordinator->CreateDlGrant(imsi, slotIndex, tbSize, mcs);
}

void
NrDlClusterContextManager::DoDispose()
{
    m_associations.clear();
    m_selectionMechanism = nullptr;
    m_coordinator = nullptr;
    Object::DoDispose();
}

} // namespace ns3
