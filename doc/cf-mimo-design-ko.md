# CF-MIMO MAC/PHY 설계 요약 (한글)

이 문서는 `src/cf-mimo` 모듈의 현재 구현 기준으로,
- MAC/PHY 주요 기능
- 신호 처리 수식
- 가정(assumptions)
- 범위(scope)와 한계(limitations)
를 정리한다.

## 1) MAC 주요 Feature Set

### 1.1 동작 모드
- `DISTRIBUTED`
- `CENTRALIZED`

### 1.2 엔티티
- `CfUeMac`: UE 상태 보고, 분산 모드 클러스터 선택, SR/BSR 전송, grant 처리
- `CfApMac`: UE 보고 수집, 분산 offer 생성 또는 중앙 summary 보고, TTI 스케줄링
- `CfCpuController`: 중앙 모드에서 AP summary 집계, 클러스터 결정/배포

### 1.3 타이밍(기본값)
- `TTI = 0.5 ms`
- `T_state = 10 ms`
- `T_assoc = 100 ms`
- `T_coh = 10 ms` (PHY 파일럿과 연동)
- `T_coord = 10 ms`

### 1.4 메시지(구조체)
- `UEStateReport`
- `AssocOffer` / `AssocConfirm`
- `APSummary` / `ClusterDecision`
- `SchedulingGrant`
- `SR_BSR` / `RxStatus`
- 공통 상수:
  - `TOP_N_AP=4`
  - `MAX_CLUSTER_SIZE=8`
  - `MAX_PRB=64`
  - `MAX_UE_PER_AP_SCHED=16`

### 1.5 핵심 MAC 로직
- 분산 모드:
  - AP가 fresh report 기반으로 offer 생성
  - UE가 `score - lambda*load` 기준 primary 선택
  - secondary를 점수 순으로 확장
  - `AssocConfirm`는 `epoch E`에 결정 후 `E+1`부터 활성
- 중앙 모드:
  - AP가 `APSummary` 보고
  - CPU가 UE별 primary/secondary 선택
  - AP는 `ClusterDecision`을 pending 저장 후 `E+1` 활성
- 스케줄링:
  - AP는 active cluster UE를 queue 비례로 PRB 분배
  - `mcs`, `tx_power_idx`, `prb_bitmap` 포함 grant 생성

## 2) PHY 주요 Feature Set

### 2.1 PHY 컴포넌트
- `CfApPhy` (`SpectrumPhy` 상속)
- `CfUePhy` (`SpectrumPhy` 상속)
- `CfMimoSpectrumSignalParameters` (`SpectrumSignalParameters` 확장)
  - `signal_type = {DATA, PILOT}`
  - `tx_ap_id`, `ue_id`, `tti_index`, `assoc_epoch`, `prb_bitmap`
  - `mcs`, `tx_power_idx`, `tx_power_total_w`

### 2.2 파일럿 송신 방식
- DL pilot (AP → UE)
- coherence block마다 pilot-only TTI 사용
- 기본 pilot bitmap: 전체 64 PRB (`all-ones`)
- AP별 pilot 동시 송신 가능 (간섭 포함 측정)

### 2.3 UE 측정 방식
- pilot TTI 동안 AP별 수신 전력 누적
- pilot TTI 종료 시 AP별 `RSRP`, `SINR_pilot`, `CQI` 계산
- 측정치 저장 후 MAC의 `UEStateReport` 입력으로 사용 가능

### 2.4 채널/손실 모델 사용
- PHY 자체는 `SpectrumChannel` 인터페이스를 사용
- 예제/테스트에서는 `MultiModelSpectrumChannel + ThreeGppUmiStreetCanyonPropagationLossModel`

## 3) Signal 처리 수식

### 3.1 Pilot PSD 생성
- 활성 PRB 수:
  - `N = popcount(pilot_prb_bitmap)`
- PRB당 전력:
  - `P_pilot_prb = P_pilot_total_W / N`
- PRB당 PSD:
  - `PSD_prb = P_pilot_prb / B_prb_Hz`

### 3.2 Data 전력 인덱스 매핑 (dB 스케일)
- `r = tx_power_idx / 255`
- `P_tx_dBm = P_min_dBm + r * (P_max_dBm - P_min_dBm)`
- `P_tx_W = 10^((P_tx_dBm - 30)/10)`
- 활성 PRB에 균등 전력 분배 후 PSD 생성

### 3.3 UE RSRP 계산
- AP `a`에 대해:
  - `RSRP_a(W) = sum_{b in pilotPRB} PSD_a(b) * B_prb_Hz`
- dBm 변환:
  - `RSRP_a(dBm) = 10*log10(RSRP_a / 1mW)`

### 3.4 UE Pilot SINR 계산
- `S_a = RSRP_a`
- `I_a = sum_{k != a} RSRP_k`
- `N = N0_W_per_Hz * (N_PRB * B_prb_Hz)`
- `SINR_a = S_a / (I_a + N)`
- 구현상 `N0_W_per_Hz`는
  - `thermalNoise(dBm/Hz)` + `NoiseFigure(dB)`
  - 기본값: `-174 dBm/Hz`, `NF=9 dB`

### 3.5 CQI 양자화
- threshold table `T[0..15]`:
  - `{-10,-7,-5,-3,-1,1,3,5,7,9,11,13,15,17,19,21}` dB
- `CQI = max i such that SINR_dB >= T[i]`

## 4) Assumptions

- Waveform-level 채널 추정은 수행하지 않음 (pilot PSD 기반 추상 측정)
- Pilot은 decoding/BLER 경로를 타지 않음
- HARQ 재전송은 모델링하지 않음 (`RxStatus`는 통계 용도)
- 클러스터 변경은 epoch 경계(`E+1`)에서만 활성화
- UE 보고는 `TOP_N_AP`로 제한하여 signaling 오버헤드 축소
- 초기 구현에서 measurement filtering(EWMA)은 미적용(raw 측정)

## 5) Scope & Limitations

### 5.1 현재 범위
- 분산/중앙 모드 control-plane 절차
- pilot PSD 기반 multi-AP 관측
- RSRP/SINR/CQI 기반 측정치 생성
- 기본 queue 기반 스케줄링 grant 생성

### 5.2 현재 한계
- UE data 수신 PHY(BLER/MI/TB bytes 계산)는 아직 본격 구현 전
- AP PHY의 RX 경로는 최소화되어 있음 (현 단계는 DL pilot 중심)
- 동시 다중 AP data 합성/결합 수신(JT/CoMP 상세) 미구현
- NR 표준 CQI/MCS 테이블 1:1 매핑 대신 단순 threshold 기반 CQI 사용
- fast fading/beamforming을 포함한 full 3GPP spectrum loss 체인은 현재 예제 기본 경로에 미포함

## 6) 참고 실행 예제

- 분산 클러스터링:
  - `./build/src/cf-mimo/examples/ns3.46-cf-mimo-distributed-clustering-example-debug`
- 중앙집중 클러스터링:
  - `./build/src/cf-mimo/examples/ns3.46-cf-mimo-centralized-clustering-example-debug`
- 파일럿 PHY 측정:
  - `./build/src/cf-mimo/examples/ns3.46-cf-mimo-pilot-phy-example-debug`
