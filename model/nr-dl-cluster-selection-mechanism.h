// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_DL_CLUSTER_SELECTION_MECHANISM_H
#define NR_DL_CLUSTER_SELECTION_MECHANISM_H

#include "nr-dl-cluster-coordinator.h"

#include "ns3/object.h"

namespace ns3
{

class NrDlClusterSelectionMechanism : public Object
{
  public:
    static TypeId GetTypeId();

    ~NrDlClusterSelectionMechanism() override;

    virtual bool ShouldTriggerClusterUpdate(const NrDlClusterUeAssociation& currentAssociation,
                                            const std::vector<uint16_t>& candidateApCellIds) const = 0;

    virtual NrDlClusterUeAssociation SelectAssociation(
        const NrDlClusterUeAssociation& currentAssociation,
        const std::vector<NrDlClusterParticipationResponse>& acceptedResponses) const = 0;
};

class NrDlClusterSelectionMechanismDefault : public NrDlClusterSelectionMechanism
{
  public:
    static TypeId GetTypeId();

    NrDlClusterSelectionMechanismDefault();
    ~NrDlClusterSelectionMechanismDefault() override;

    bool ShouldTriggerClusterUpdate(const NrDlClusterUeAssociation& currentAssociation,
                                    const std::vector<uint16_t>& candidateApCellIds) const override;

    NrDlClusterUeAssociation SelectAssociation(
        const NrDlClusterUeAssociation& currentAssociation,
        const std::vector<NrDlClusterParticipationResponse>& acceptedResponses) const override;
};

} // namespace ns3

#endif /* NR_DL_CLUSTER_SELECTION_MECHANISM_H */
