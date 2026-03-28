// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-dl-cluster-selection-mechanism.h"

#include "ns3/abort.h"

#include <algorithm>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(NrDlClusterSelectionMechanism);
NS_OBJECT_ENSURE_REGISTERED(NrDlClusterSelectionMechanismDefault);

TypeId
NrDlClusterSelectionMechanism::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrDlClusterSelectionMechanism").SetParent<Object>().SetGroupName("Nr");
    return tid;
}

NrDlClusterSelectionMechanism::~NrDlClusterSelectionMechanism() = default;

TypeId
NrDlClusterSelectionMechanismDefault::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NrDlClusterSelectionMechanismDefault")
                            .SetParent<NrDlClusterSelectionMechanism>()
                            .SetGroupName("Nr")
                            .AddConstructor<NrDlClusterSelectionMechanismDefault>();
    return tid;
}

NrDlClusterSelectionMechanismDefault::NrDlClusterSelectionMechanismDefault() = default;

NrDlClusterSelectionMechanismDefault::~NrDlClusterSelectionMechanismDefault() = default;

bool
NrDlClusterSelectionMechanismDefault::ShouldTriggerClusterUpdate(
    const NrDlClusterUeAssociation& currentAssociation,
    const std::vector<uint16_t>& candidateApCellIds) const
{
    return !candidateApCellIds.empty() &&
           currentAssociation.candidateApCellIds != candidateApCellIds;
}

NrDlClusterUeAssociation
NrDlClusterSelectionMechanismDefault::SelectAssociation(
    const NrDlClusterUeAssociation& currentAssociation,
    const std::vector<NrDlClusterParticipationResponse>& acceptedResponses) const
{
    NS_ABORT_MSG_IF(acceptedResponses.empty(),
                    "Cannot select a DL cluster association without accepted AP responses");

    auto rankedResponses = acceptedResponses;
    std::stable_sort(rankedResponses.begin(),
                     rankedResponses.end(),
                     [](const NrDlClusterParticipationResponse& lhs,
                        const NrDlClusterParticipationResponse& rhs) {
                         if (lhs.priorityMetric == rhs.priorityMetric)
                         {
                             return lhs.apCellId < rhs.apCellId;
                         }
                         return lhs.priorityMetric > rhs.priorityMetric;
                     });

    NrDlClusterUeAssociation updated = currentAssociation;
    updated.coordinatorCellId = rankedResponses.front().apCellId;
    updated.candidateApCellIds.clear();
    for (const auto& response : rankedResponses)
    {
        if (response.apCellId != updated.coordinatorCellId)
        {
            updated.candidateApCellIds.push_back(response.apCellId);
        }
    }
    return updated;
}

} // namespace ns3
