/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CF_MIMO_MESSAGES_H
#define CF_MIMO_MESSAGES_H

#include "cf-mimo-common.h"

#include <array>
#include <vector>

namespace ns3
{

struct UeApMeasurement
{
    AP_ID ap_id{INVALID_AP_ID};
    uint8_t cqi{0};
    int16_t sinr_q{0};
    int16_t rsrp_q{0};
    uint8_t pad{0};
};

struct UEStateReport
{
    UE_ID ue_id{0};
    uint32_t report_time_tti{0};
    uint32_t queue_bytes{0};
    uint8_t traffic_class{0};
    uint8_t reserved0{0};
    uint8_t meas_cnt{0};
    uint8_t reserved1{0};
    std::array<UeApMeasurement, CfMimoConstants::TOP_N_AP> meas{};
};

struct AssocOffer
{
    AP_ID ap_id{INVALID_AP_ID};
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t assoc_epoch{0};
    uint32_t cluster_ver{0};
    int16_t score_q{0};
    uint16_t load_q{0};
    uint8_t max_prb_offer{0};
    uint8_t reserved{0};
};

struct AssocConfirm
{
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t assoc_epoch{0};
    uint32_t cluster_ver{0};
    AP_ID primary_ap{INVALID_AP_ID};
    uint8_t cluster_size{0};
    uint8_t reserved{0};
    std::array<AP_ID, CfMimoConstants::MAX_CLUSTER_SIZE> cluster_members{MakeInvalidClusterMembers()};
};

struct NeighborCoord
{
    AP_ID src_ap{INVALID_AP_ID};
    uint32_t coord_time_tti{0};
    uint16_t load_q{0};
    uint16_t interf_q{0};
    uint8_t served_ue_cnt{0};
    uint8_t reserved{0};
    uint32_t top_ue_hash{0};
};

struct APSummary
{
    AP_ID ap_id{INVALID_AP_ID};
    uint32_t assoc_epoch{0};
    uint16_t load_q{0};
    uint16_t interf_q{0};
    uint8_t cand_ue_cnt{0};
    uint8_t reserved{0};
    std::array<UE_ID, CfMimoConstants::MAX_UE_PER_AP_SCHED> cand_ue_list{MakeInvalidAssignedUes()};
    std::array<int16_t, CfMimoConstants::MAX_UE_PER_AP_SCHED> cand_metric_q{};
};

struct ClusterDecision
{
    uint32_t assoc_epoch{0};
    uint32_t cluster_ver{0};
    AP_ID ap_id{INVALID_AP_ID};
    uint8_t assigned_ue_cnt{0};
    uint8_t reserved{0};
    std::array<UE_ID, CfMimoConstants::MAX_UE_PER_AP_SCHED> assigned_ue_list{MakeInvalidAssignedUes()};
    std::array<uint8_t, CfMimoConstants::MAX_UE_PER_AP_SCHED> role_flags{};
};

struct SchedulingGrant
{
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t tti{0};
    uint8_t mcs{0};
    uint8_t tx_power_idx{0};
    uint64_t prb_bitmap{0};
    uint16_t reserved{0};
};

struct SR_BSR
{
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t tti{0};
    uint8_t sr_flag{0};
    uint8_t traffic_class{0};
    uint32_t queue_bytes{0};
};

struct RxStatus
{
    UE_ID ue_id{INVALID_UE_ID};
    uint32_t tti{0};
    uint8_t ok{0};
    uint8_t reserved{0};
};

uint32_t CountClusterMembers(const AssocConfirm& confirm);
bool HasClusterMember(const AssocConfirm& confirm, AP_ID apId);
std::vector<AP_ID> ToClusterMembers(const AssocConfirm& confirm);
uint64_t BuildPrbBitmap(uint8_t startPrb, uint8_t prbCount);

} // namespace ns3

#endif // CF_MIMO_MESSAGES_H
