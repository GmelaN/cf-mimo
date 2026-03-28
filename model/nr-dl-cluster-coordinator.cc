// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#include "nr-dl-cluster-coordinator.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NrDlClusterCoordinator");

NS_OBJECT_ENSURE_REGISTERED(NrDlClusterApAgent);
NS_OBJECT_ENSURE_REGISTERED(NrDlClusterCoordinator);

TypeId
NrDlClusterApAgent::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NrDlClusterApAgent").SetParent<Object>().SetGroupName("Nr");
    return tid;
}

NrDlClusterApAgent::~NrDlClusterApAgent() = default;

TypeId
NrDlClusterCoordinator::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NrDlClusterCoordinator")
            .SetParent<Object>()
            .SetGroupName("Nr")
            .AddConstructor<NrDlClusterCoordinator>()
            .AddAttribute("MaxCollaboratingAps",
                          "Maximum number of APs that can be selected for one DL TDMA grant.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&NrDlClusterCoordinator::m_maxCollaboratingAps),
                          MakeUintegerChecker<uint32_t>(1));
    return tid;
}

NrDlClusterCoordinator::NrDlClusterCoordinator() = default;

NrDlClusterCoordinator::~NrDlClusterCoordinator() = default;

void
NrDlClusterCoordinator::RegisterApAgent(const Ptr<NrDlClusterApAgent>& agent)
{
    NS_ABORT_MSG_IF(agent == nullptr, "AP agent cannot be null");
    m_apAgents[agent->GetCellId()] = agent;
}

void
NrDlClusterCoordinator::AssociateUe(const NrDlClusterUeAssociation& association)
{
    NS_ABORT_MSG_IF(association.imsi == 0, "IMSI 0 is not valid for DL cluster coordination");
    NS_ABORT_MSG_IF(association.coordinatorCellId == 0,
                    "Coordinator cell id must be configured for DL cluster coordination");

    m_associations[association.imsi] = association;
}

std::vector<NrDlClusterParticipationResponse>
NrDlClusterCoordinator::EvaluateParticipationResponses(
    const NrDlClusterParticipationRequest& request,
    const std::vector<uint16_t>& candidateApCellIds) const
{
    std::vector<NrDlClusterParticipationResponse> acceptedResponses;
    for (auto candidateCellId : candidateApCellIds)
    {
        auto it = m_apAgents.find(candidateCellId);
        if (it == m_apAgents.end())
        {
            NS_LOG_DEBUG("Skipping candidate AP " << candidateCellId
                                                  << " because no agent is registered");
            continue;
        }

        auto response = it->second->EvaluateParticipation(request);
        response.apCellId = candidateCellId;
        if (response.accepted)
        {
            acceptedResponses.push_back(response);
        }
    }
    return acceptedResponses;
}

NrDlClusterUeAssociation
NrDlClusterCoordinator::ReselectCoordinator(uint64_t imsi,
                                            const std::vector<uint16_t>& candidateApCellIds,
                                            uint32_t slotIndex,
                                            uint32_t tbSize,
                                            uint8_t mcs)
{
    NS_ABORT_MSG_IF(!HasAssociation(imsi), "No DL cluster association for IMSI " << imsi);

    auto association = GetAssociation(imsi);
    std::vector<uint16_t> rankedCandidates = candidateApCellIds;
    if (std::find(rankedCandidates.begin(), rankedCandidates.end(), association.coordinatorCellId) ==
        rankedCandidates.end())
    {
        rankedCandidates.push_back(association.coordinatorCellId);
    }

    NrDlClusterParticipationRequest request{
        imsi,
        association.coordinatorCellId,
        slotIndex,
        tbSize,
        mcs,
    };
    auto acceptedResponses = EvaluateParticipationResponses(request, rankedCandidates);
    NS_ABORT_MSG_IF(acceptedResponses.empty(),
                    "Coordinator reselection left UE " << imsi << " without any accepted AP");

    std::stable_sort(acceptedResponses.begin(),
                     acceptedResponses.end(),
                     [](const NrDlClusterParticipationResponse& lhs,
                        const NrDlClusterParticipationResponse& rhs) {
                         if (lhs.priorityMetric == rhs.priorityMetric)
                         {
                             return lhs.apCellId < rhs.apCellId;
                         }
                         return lhs.priorityMetric > rhs.priorityMetric;
                     });

    association.coordinatorCellId = acceptedResponses.front().apCellId;
    association.candidateApCellIds.clear();
    for (const auto& response : acceptedResponses)
    {
        if (response.apCellId != association.coordinatorCellId)
        {
            association.candidateApCellIds.push_back(response.apCellId);
        }
    }
    m_associations[imsi] = association;
    return association;
}

NrDlClusterGrant
NrDlClusterCoordinator::CreateDlGrant(uint64_t imsi,
                                      uint32_t slotIndex,
                                      uint32_t tbSize,
                                      uint8_t mcs)
{
    NS_ABORT_MSG_IF(!HasAssociation(imsi), "No DL cluster association for IMSI " << imsi);

    const auto association = GetAssociation(imsi);
    NrDlClusterParticipationRequest request{imsi, association.coordinatorCellId, slotIndex, tbSize, mcs};
    NrDlClusterGrant grant{imsi, association.coordinatorCellId, slotIndex, tbSize, mcs, false, {}};

    std::vector<uint16_t> candidateApCellIds = association.candidateApCellIds;
    if (std::find(candidateApCellIds.begin(),
                  candidateApCellIds.end(),
                  association.coordinatorCellId) == candidateApCellIds.end())
    {
        candidateApCellIds.insert(candidateApCellIds.begin(), association.coordinatorCellId);
    }

    auto acceptedResponses = EvaluateParticipationResponses(request, candidateApCellIds);

    std::stable_sort(acceptedResponses.begin(),
                     acceptedResponses.end(),
                     [coordinatorCellId = association.coordinatorCellId](
                         const NrDlClusterParticipationResponse& lhs,
                         const NrDlClusterParticipationResponse& rhs) {
                         if (lhs.apCellId == coordinatorCellId && rhs.apCellId != coordinatorCellId)
                         {
                             return true;
                         }
                         if (lhs.apCellId != coordinatorCellId && rhs.apCellId == coordinatorCellId)
                         {
                             return false;
                         }
                         if (lhs.priorityMetric == rhs.priorityMetric)
                         {
                             return lhs.apCellId < rhs.apCellId;
                         }
                         return lhs.priorityMetric > rhs.priorityMetric;
                     });

    const auto selectedCount = std::min<uint32_t>(m_maxCollaboratingAps, acceptedResponses.size());
    for (uint32_t i = 0; i < selectedCount; ++i)
    {
        grant.selectedApCellIds.push_back(acceptedResponses[i].apCellId);
    }

    grant.granted = !grant.selectedApCellIds.empty();
    return grant;
}

NrDlClusterGrant
NrDlClusterCoordinator::CreateAndDispatchDlGrant(uint64_t imsi,
                                                 uint32_t slotIndex,
                                                 uint32_t tbSize,
                                                 uint8_t mcs,
                                                 Time dispatchDelay)
{
    auto grant = CreateDlGrant(imsi, slotIndex, tbSize, mcs);
    DispatchDlGrant(grant, dispatchDelay);
    return grant;
}

void
NrDlClusterCoordinator::DispatchDlGrant(const NrDlClusterGrant& grant, Time dispatchDelay)
{
    NS_ABORT_MSG_IF(!grant.granted, "Cannot dispatch an invalid DL cluster grant");
    Simulator::Schedule(dispatchDelay, &NrDlClusterCoordinator::DoDispatchDlGrant, this, grant);
}

bool
NrDlClusterCoordinator::HasAssociation(uint64_t imsi) const
{
    return m_associations.find(imsi) != m_associations.end();
}

NrDlClusterUeAssociation
NrDlClusterCoordinator::GetAssociation(uint64_t imsi) const
{
    auto it = m_associations.find(imsi);
    NS_ABORT_MSG_IF(it == m_associations.end(), "No DL cluster association for IMSI " << imsi);
    return it->second;
}

uint16_t
NrDlClusterCoordinator::GetCurrentCoordinatorCellId(uint64_t imsi) const
{
    return GetAssociation(imsi).coordinatorCellId;
}

void
NrDlClusterCoordinator::DoDispatchDlGrant(NrDlClusterGrant grant)
{
    NS_LOG_FUNCTION(this << grant.imsi << grant.slotIndex);

    for (auto apCellId : grant.selectedApCellIds)
    {
        auto it = m_apAgents.find(apCellId);
        NS_ABORT_MSG_IF(it == m_apAgents.end(),
                        "No AP agent registered for selected cell " << apCellId);
        it->second->ReceiveDlGrant(grant);
    }
}

void
NrDlClusterCoordinator::DoDispose()
{
    m_apAgents.clear();
    m_associations.clear();
    Object::DoDispose();
}

} // namespace ns3
