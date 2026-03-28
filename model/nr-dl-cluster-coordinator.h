// Copyright (c) 2026
//
// SPDX-License-Identifier: GPL-2.0-only

#ifndef NR_DL_CLUSTER_COORDINATOR_H
#define NR_DL_CLUSTER_COORDINATOR_H

#include "ns3/nstime.h"
#include "ns3/object.h"

#include <cstdint>
#include <map>
#include <vector>

namespace ns3
{

struct NrDlClusterUeAssociation
{
    uint64_t imsi{0};
    uint16_t coordinatorCellId{0};
    std::vector<uint16_t> candidateApCellIds;
};

struct NrDlClusterParticipationRequest
{
    uint64_t imsi{0};
    uint16_t coordinatorCellId{0};
    uint32_t slotIndex{0};
    uint32_t tbSize{0};
    uint8_t mcs{0};
};

struct NrDlClusterParticipationResponse
{
    uint16_t apCellId{0};
    bool accepted{false};
    double priorityMetric{0.0};
};

struct NrDlClusterGrant
{
    uint64_t imsi{0};
    uint16_t coordinatorCellId{0};
    uint32_t slotIndex{0};
    uint32_t tbSize{0};
    uint8_t mcs{0};
    bool granted{false};
    std::vector<uint16_t> selectedApCellIds;
};

class NrDlClusterApAgent : public Object
{
  public:
    static TypeId GetTypeId();

    ~NrDlClusterApAgent() override;

    virtual uint16_t GetCellId() const = 0;

    virtual NrDlClusterParticipationResponse EvaluateParticipation(
        const NrDlClusterParticipationRequest& request) = 0;

    virtual void ReceiveDlGrant(const NrDlClusterGrant& grant) = 0;
};

class NrDlClusterCoordinator : public Object
{
  public:
    static TypeId GetTypeId();

    NrDlClusterCoordinator();
    ~NrDlClusterCoordinator() override;

    void RegisterApAgent(const Ptr<NrDlClusterApAgent>& agent);
    void AssociateUe(const NrDlClusterUeAssociation& association);
    std::vector<NrDlClusterParticipationResponse> EvaluateParticipationResponses(
        const NrDlClusterParticipationRequest& request,
        const std::vector<uint16_t>& candidateApCellIds) const;
    NrDlClusterUeAssociation ReselectCoordinator(uint64_t imsi,
                                                 const std::vector<uint16_t>& candidateApCellIds,
                                                 uint32_t slotIndex,
                                                 uint32_t tbSize,
                                                 uint8_t mcs);

    NrDlClusterGrant CreateDlGrant(uint64_t imsi, uint32_t slotIndex, uint32_t tbSize, uint8_t mcs);
    NrDlClusterGrant CreateAndDispatchDlGrant(uint64_t imsi,
                                              uint32_t slotIndex,
                                              uint32_t tbSize,
                                              uint8_t mcs,
                                              Time dispatchDelay = Seconds(0));
    void DispatchDlGrant(const NrDlClusterGrant& grant, Time dispatchDelay = Seconds(0));

    bool HasAssociation(uint64_t imsi) const;
    NrDlClusterUeAssociation GetAssociation(uint64_t imsi) const;
    uint16_t GetCurrentCoordinatorCellId(uint64_t imsi) const;

  protected:
    void DoDispose() override;

  private:
    void DoDispatchDlGrant(NrDlClusterGrant grant);

    std::map<uint16_t, Ptr<NrDlClusterApAgent>> m_apAgents;
    std::map<uint64_t, NrDlClusterUeAssociation> m_associations;
    uint32_t m_maxCollaboratingAps{1};
};

} // namespace ns3

#endif /* NR_DL_CLUSTER_COORDINATOR_H */
