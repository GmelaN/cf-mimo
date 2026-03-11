#!/usr/bin/env bash
set -euo pipefail

# Run distributed baselines for cf-mimo-app-traffic-example under UE RandomWalk mobility
# and summarize key metrics in CSV + stdout table.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

SIM_TIME_MS="${SIM_TIME_MS:-800}"
APP_RATE="${APP_RATE:-10Mbps}"
APP_PKT_SIZE="${APP_PKT_SIZE:-900}"

UE_RANDOM_WALK="${UE_RANDOM_WALK:-1}"
UE_WALK_SPEED_MPS="${UE_WALK_SPEED_MPS:-2.0}"
UE_WALK_STEP_DISTANCE_M="${UE_WALK_STEP_DISTANCE_M:-5.0}"
UE_WALK_MIN_X="${UE_WALK_MIN_X:--30}"
UE_WALK_MAX_X="${UE_WALK_MAX_X:-180}"
UE_WALK_MIN_Y="${UE_WALK_MIN_Y:--40}"
UE_WALK_MAX_Y="${UE_WALK_MAX_Y:-40}"

BASELINES=(matched dcc fhaware)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/build/cf-mimo-baseline-results}"
mkdir -p "$OUT_DIR"

BIN_PATH="$(ls "$ROOT_DIR"/build/contrib/cf-mimo/examples/ns3.*-cf-mimo-app-traffic-example-default 2>/dev/null | head -n 1 || true)"
if [[ -z "$BIN_PATH" ]]; then
  echo "Binary not found. Build first:"
  echo "  cmake --build $ROOT_DIR/cmake-cache --target cf-mimo-app-traffic-example -j"
  exit 1
fi

CSV_FILE="$OUT_DIR/randomwalk-baseline-comparison.csv"
echo "baseline,simTimeMs,appRate,appPktSize,ueRandomWalk,ueWalkSpeedMps,ueWalkStepDistanceM,uePrimaryAp,clusterVer,injectedBytes,injectedPkts,queueRemainBytes,remoteRxBytes" > "$CSV_FILE"

printf "%-10s %-10s %-10s %-14s %-13s %-13s\n" "baseline" "primaryAP" "clusterVer" "injectedBytes" "queueRemain" "remoteRx"

for baseline in "${BASELINES[@]}"; do
  LOG_FILE="$OUT_DIR/${baseline}.log"
  "$BIN_PATH" \
    --simTimeMs="$SIM_TIME_MS" \
    --appRate="$APP_RATE" \
    --appPktSize="$APP_PKT_SIZE" \
    --baseline="$baseline" \
    --ueRandomWalk="$UE_RANDOM_WALK" \
    --ueWalkSpeedMps="$UE_WALK_SPEED_MPS" \
    --ueWalkStepDistanceM="$UE_WALK_STEP_DISTANCE_M" \
    --ueWalkMinX="$UE_WALK_MIN_X" \
    --ueWalkMaxX="$UE_WALK_MAX_X" \
    --ueWalkMinY="$UE_WALK_MIN_Y" \
    --ueWalkMaxY="$UE_WALK_MAX_Y" | tee "$LOG_FILE" >/dev/null

  primary_ap="$(awk '/UE primary AP:/ {print $4}' "$LOG_FILE")"
  cluster_ver="$(awk '/UE primary AP:/ {print $6}' "$LOG_FILE")"
  injected_bytes="$(awk -F'[(): /]+' '/Injected to CF queue/ {print $7}' "$LOG_FILE")"
  injected_pkts="$(awk -F'[(): /]+' '/Injected to CF queue/ {print $8}' "$LOG_FILE")"
  queue_remain="$(awk '/UE remaining CF queue bytes:/ {print $6}' "$LOG_FILE")"
  remote_rx="$(awk '/Remote sink received bytes:/ {print $5}' "$LOG_FILE")"

  printf "%-10s %-10s %-10s %-14s %-13s %-13s\n" \
    "$baseline" "$primary_ap" "$cluster_ver" "$injected_bytes" "$queue_remain" "$remote_rx"

  echo "${baseline},${SIM_TIME_MS},${APP_RATE},${APP_PKT_SIZE},${UE_RANDOM_WALK},${UE_WALK_SPEED_MPS},${UE_WALK_STEP_DISTANCE_M},${primary_ap},${cluster_ver},${injected_bytes},${injected_pkts},${queue_remain},${remote_rx}" \
    >> "$CSV_FILE"
done

echo
echo "Saved:"
echo "  $CSV_FILE"
echo "  $OUT_DIR/{matched,dcc,fhaware}.log"
