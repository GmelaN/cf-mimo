// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_DL_CLUSTER_CONTEXT_MANAGER_H
#define NR_DL_CLUSTER_CONTEXT_MANAGER_H

#include "nr-dl-cluster-coordinator.h"
#include "nr-dl-cluster-selection-mechanism.h"

#include "ns3/object.h"

#include <cstdint>
#include <map>

namespace ns3
{

class NrDlClusterContextManager : public Object
{
  public:
    static TypeId GetTypeId();

    NrDlClusterContextManager();
    ~NrDlClusterContextManager() override;

    void SetCoordinator(const Ptr<NrDlClusterCoordinator>& coordinator);
    Ptr<NrDlClusterCoordinator> GetCoordinator() const;

    void SetSelectionMechanism(const Ptr<NrDlClusterSelectionMechanism>& selectionMechanism);
    Ptr<NrDlClusterSelectionMechanism> GetSelectionMechanism() const;

    void RegisterApAgent(const Ptr<NrDlClusterApAgent>& agent);

    void AssociateUe(const NrDlClusterUeAssociation& association);
    bool HasAssociation(uint64_t imsi) const;
    NrDlClusterUeAssociation GetAssociation(uint64_t imsi) const;
    uint16_t GetCurrentCoordinatorCellId(uint64_t imsi) const;

    NrDlClusterUeAssociation ReselectCoordinator(uint64_t imsi,
                                                 const std::vector<uint16_t>& candidateApCellIds,
                                                 uint32_t slotIndex,
                                                 uint32_t tbSize,
                                                 uint8_t mcs);

    NrDlClusterGrant CreateDlGrant(uint64_t imsi, uint32_t slotIndex, uint32_t tbSize, uint8_t mcs);

  protected:
    void DoDispose() override;

  private:
    void SyncAssociationToCoordinator(uint64_t imsi);

    Ptr<NrDlClusterCoordinator> m_coordinator;
    Ptr<NrDlClusterSelectionMechanism> m_selectionMechanism;
    std::map<uint64_t, NrDlClusterUeAssociation> m_associations;
};

} // namespace ns3

#endif /* NR_DL_CLUSTER_CONTEXT_MANAGER_H */
