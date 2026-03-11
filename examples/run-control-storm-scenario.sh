#!/usr/bin/env bash
set -euo pipefail

# Run cf-mimo-control-storm-example with a reproducible "UE=20, AP=50" scenario.
# UE positions are seed-based random in the example implementation.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MODE="${MODE:-distributed}"
NUM_UE="${NUM_UE:-100}"
NUM_AP="${NUM_AP:-5 0}"
SIM_TIME_MS="${SIM_TIME_MS:-600}"
TRAFFIC_START_MS="${TRAFFIC_START_MS:-250}"
OFFERED_LOAD_MBPS_PER_UE="${OFFERED_LOAD_MBPS_PER_UE:-3.0}"
AP_PROC_US="${AP_PROC_US:-20}"
UE_PROC_US="${UE_PROC_US:-25}"
CPU_PROC_US="${CPU_PROC_US:-60}"
ASSOC_MS="${ASSOC_MS:-100}"
STATE_MS="${STATE_MS:-10}"
CTRL_BUDGET_BYTES_PER_EPOCH="${CTRL_BUDGET_BYTES_PER_EPOCH:-0}"
SEED="${SEED:-7}"
AP_SPACING_M="${AP_SPACING_M:-80}"

OUT_DIR="${OUT_DIR:-$ROOT_DIR/build/cf-mimo-control-storm-results}"
mkdir -p "$OUT_DIR"

BIN_PATH="$(ls "$ROOT_DIR"/build/contrib/cf-mimo/examples/ns3.*-cf-mimo-control-storm-example-default 2>/dev/null | head -n 1 || true)"
if [[ -z "$BIN_PATH" ]]; then
  echo "Binary not found. Build first:"
  echo "  cmake --build $ROOT_DIR/cmake-cache --target cf-mimo-control-storm-example -j"
  exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
LOG_FILE="$OUT_DIR/control-storm-${MODE}-ue${NUM_UE}-ap${NUM_AP}-${STAMP}.log"
CSV_FILE="$OUT_DIR/control-storm-summary.csv"

if [[ ! -f "$CSV_FILE" ]]; then
  echo "timestamp,mode,numUe,numAp,simTimeMs,offeredLoadMbpsPerUe,assocMs,stateMs,ctrlBudgetBytesPerEpoch,seed,associatedUe,totalCtrlTx,ctrlMsgPerSec,queueRemainBytes,scheduledEstimatedMbps,budgetExceedEpochs,distributedAckMissingRate,distributedRejectRate,centralizedRejectRate,apRxAvgDelayMs,apRxMaxDelayMs" > "$CSV_FILE"
fi

"$BIN_PATH" \
  --mode="$MODE" \
  --numUe="$NUM_UE" \
  --numAp="$NUM_AP" \
  --simTimeMs="$SIM_TIME_MS" \
  --trafficStartMs="$TRAFFIC_START_MS" \
  --offeredLoadMbpsPerUe="$OFFERED_LOAD_MBPS_PER_UE" \
  --apProcUs="$AP_PROC_US" \
  --ueProcUs="$UE_PROC_US" \
  --cpuProcUs="$CPU_PROC_US" \
  --assocMs="$ASSOC_MS" \
  --stateMs="$STATE_MS" \
  --ctrlBudgetBytesPerEpoch="$CTRL_BUDGET_BYTES_PER_EPOCH" \
  --seed="$SEED" \
  --apSpacingM="$AP_SPACING_M" \
  --enableSnapshot=0 | tee "$LOG_FILE"

summary_line="$(grep -E 'totalCtrlTx=.*ctrlMsgPerSec=' "$LOG_FILE" | tail -n 1 || true)"
assoc_line="$(grep -E 'associatedUE=.*queueRemainBytes=' "$LOG_FILE" | tail -n 1 || true)"
sched_line="$(grep -E 'scheduledEstimatedMbps=' "$LOG_FILE" | tail -n 1 || true)"
epoch_line="$(grep -E 'budgetExceedEpochs=' "$LOG_FILE" | tail -n 1 || true)"
consistency_line="$(grep -E 'distributedAckMissingRate=' "$LOG_FILE" | tail -n 1 || true)"
ap_rx_line="$(grep -E '^AP-RX:' "$LOG_FILE" | tail -n 1 || true)"

associated_ue="$(echo "$assoc_line" | sed -n 's/.*associatedUE=\([0-9]\+\)\/.*/\1/p')"
total_ctrl_tx="$(echo "$summary_line" | sed -n 's/.*totalCtrlTx=\([0-9]\+\).*/\1/p')"
ctrl_msg_per_sec="$(echo "$summary_line" | sed -n 's/.*ctrlMsgPerSec=\([^ ]*\).*/\1/p')"
queue_remain="$(echo "$assoc_line" | sed -n 's/.*queueRemainBytes=\([0-9]\+\).*/\1/p')"
scheduled_mbps="$(echo "$sched_line" | sed -n 's/.*scheduledEstimatedMbps=\([^ ]*\).*/\1/p')"
budget_exceed_epochs="$(echo "$epoch_line" | sed -n 's/.*budgetExceedEpochs=\([0-9]\+\).*/\1/p')"
ack_missing_rate="$(echo "$consistency_line" | sed -n 's/.*distributedAckMissingRate=\([^ ]*\).*/\1/p')"
distributed_reject_rate="$(echo "$consistency_line" | sed -n 's/.*distributedRejectRate=\([^ ]*\).*/\1/p')"
centralized_reject_rate="$(echo "$consistency_line" | sed -n 's/.*centralizedRejectRate=\([^ ]*\).*/\1/p')"
ap_rx_avg_delay="$(echo "$ap_rx_line" | sed -n 's/.*avgDelayMs=\([^ ]*\).*/\1/p')"
ap_rx_max_delay="$(echo "$ap_rx_line" | sed -n 's/.*maxDelayMs=\([^ ]*\).*/\1/p')"

echo "$STAMP,$MODE,$NUM_UE,$NUM_AP,$SIM_TIME_MS,$OFFERED_LOAD_MBPS_PER_UE,$ASSOC_MS,$STATE_MS,$CTRL_BUDGET_BYTES_PER_EPOCH,$SEED,$associated_ue,$total_ctrl_tx,$ctrl_msg_per_sec,$queue_remain,$scheduled_mbps,$budget_exceed_epochs,$ack_missing_rate,$distributed_reject_rate,$centralized_reject_rate,$ap_rx_avg_delay,$ap_rx_max_delay" >> "$CSV_FILE"

echo
echo "Saved log: $LOG_FILE"
echo "Updated summary: $CSV_FILE"
