/**
 * @file RLEnvironment.hpp
 * @brief 강화학습(RL) 환경 인터페이스 클래스
 *
 * OpenAI Gym 스타일의 step/reset/render 인터페이스를 C++로 구현합니다.
 *
 * 행동 공간 (Action Space):
 *   - 0: 전진 (forward)
 *   - 1: 좌회전 (turn left)
 *   - 2: 우회전 (turn right)
 *
 * 상태 공간 (State / Observation):
 *   - 에이전트 위치 (x, y) [0..1 정규화]
 *   - 에이전트 방향 (cos θ, sin θ)
 *   - 에이전트 현재 고도
 *   - 이번 스텝 새 탐험 격자 수 (정규화)
 *   - 누적 탐험 비율 (explored / total)
 *   - FOV 61개 광선의 정규화 거리 벡터
 *   ▶ 총 차원: 5 + 61 = 66
 *
 * 보상 함수 (Reward):
 *   R = w_explore  * 새로 밝혀진 격자 수
 *     + w_altitude * 현재 고도 (산봉우리 가중치)
 *     - w_idle     * 제자리 패널티 (이동 없으면)
 *     - w_revisit  * 과도한 회전 패널티
 */

#pragma once

#include "Terrain.hpp"
#include "Agent.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 보상 가중치 설정
// ─────────────────────────────────────────────────────────────────────────────

struct RewardConfig {
    float wExplore  = 1.0f;    ///< 신규 탐험 격자 수 보상 가중치
    float wAltitude = 0.5f;    ///< 고도 보상 가중치
    float wIdle     = 0.1f;    ///< 제자리 패널티 가중치
    float wRevisit  = 0.05f;   ///< 탐험된 구역 재방문 패널티 가중치
};

// ─────────────────────────────────────────────────────────────────────────────
// 스텝 결과 구조체
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct StepResult
 * @brief step() 함수 반환값
 */
struct StepResult {
    std::vector<float> observation;  ///< 상태 벡터 (66차원)
    float              reward;       ///< 이번 스텝 보상
    bool               done;         ///< 에피소드 종료 여부
    // 디버그 정보
    int   newExplored;   ///< 새로 밝혀진 격자 수
    float altitudeBonus; ///< 고도 보너스
    float idlePenalty;   ///< 제자리 패널티
    float revisitPenalty;///< 재방문 패널티
    int   step;          ///< 현재 스텝 번호
};

// ─────────────────────────────────────────────────────────────────────────────
// RLEnvironment 클래스
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class RLEnvironment
 * @brief RL 에이전트가 상호작용하는 환경 클래스
 *
 * 사용 예:
 * @code
 *   RLEnvironment env(42u);
 *   auto obs = env.reset();
 *   for (int t = 0; t < 1000; ++t) {
 *       int action = policy(obs);
 *       auto result = env.step(action);
 *       obs = result.observation;
 *       if (result.done) break;
 *   }
 * @endcode
 */
class RLEnvironment {
public:
    // ── 관측 공간 차원 ──────────────────────────────────────────────────────
    static inline constexpr int OBS_BASE_DIM  = 5;              ///< 기본 상태 차원 수
    static inline constexpr int OBS_RAY_DIM   = FOV_ANGLE_SAMPLES; ///< 광선 거리 차원
    static inline constexpr int OBS_TOTAL_DIM = OBS_BASE_DIM + OBS_RAY_DIM; ///< 총 66차원

    // ── 에피소드 설정 ────────────────────────────────────────────────────────
    static inline constexpr int MAX_STEPS     = 500;  ///< 에피소드 최대 스텝 수
    static inline constexpr float DONE_RATIO  = 0.9f; ///< 탐험 완료 비율 (90%)

    // ── 생성자 ───────────────────────────────────────────────────────────────

    /**
     * @brief 환경 초기화
     * @param terrainSeed  지형 생성 시드
     * @param rewardCfg    보상 함수 설정
     * @param maxSteps     에피소드 최대 스텝 수
     */
    explicit RLEnvironment(uint32_t       terrainSeed = 42u,
                           RewardConfig   rewardCfg   = {},
                           int            maxSteps    = MAX_STEPS);

    ~RLEnvironment() = default;

    // ── 핵심 RL 인터페이스 ───────────────────────────────────────────────────

    /**
     * @brief 에피소드 초기화 — 에이전트를 맵 중앙에 배치하고 관측 반환
     * @return 초기 관측 벡터 (66차원)
     */
    std::vector<float> reset();

    /**
     * @brief 한 스텝 실행
     * @param action 행동 인덱스 (0:전진, 1:좌회전, 2:우회전)
     * @return StepResult {observation, reward, done, ...}
     */
    StepResult step(int action);

    // ── 접근자 ───────────────────────────────────────────────────────────────

    /// 지형 const 참조
    [[nodiscard]] const Terrain& terrain() const noexcept { return m_terrain; }
    /// 에이전트 const 참조
    [[nodiscard]] const Agent&   agent()   const noexcept { return m_agent; }

    /// 현재 스텝 번호
    [[nodiscard]] int currentStep() const noexcept { return m_step; }
    /// 최대 스텝 수
    [[nodiscard]] int maxSteps()    const noexcept { return m_maxSteps; }
    /// 에피소드 누적 보상
    [[nodiscard]] float totalReward() const noexcept { return m_totalReward; }

    /// 보상 설정 접근자
    [[nodiscard]] const RewardConfig& rewardConfig() const noexcept { return m_rewardCfg; }
    [[nodiscard]]       RewardConfig& rewardConfig()       noexcept { return m_rewardCfg; }

    // ── 상태 직렬화 (학습 로그용) ────────────────────────────────────────────

    /**
     * @brief 현재 환경 상태를 CSV 한 줄로 직렬화
     * @return "step,x,y,theta,explored_ratio,total_reward"
     */
    [[nodiscard]] std::string serializeStateCSV() const;

private:
    // ── 관측 벡터 생성 ───────────────────────────────────────────────────────

    /**
     * @brief 현재 에이전트 상태에서 66차원 관측 벡터 생성
     * @param newExplored 이번 스텝 신규 탐험 격자 수 (정규화에 사용)
     */
    [[nodiscard]] std::vector<float> buildObservation(int newExplored) const;

    // ── 보상 계산 ────────────────────────────────────────────────────────────

    /**
     * @brief 보상값 계산
     * @param newExplored  신규 탐험 격자 수
     * @param moved        에이전트가 실제로 이동했는지 여부
     * @param[out] result  각 보상 컴포넌트 기록
     * @return 최종 보상값
     */
    float computeReward(int newExplored, bool moved, StepResult& result) const;

    // ── 스텝 이력 관리 (제자리/재방문 감지) ─────────────────────────────────

    /// 최근 위치 이력 (제자리 감지용)
    struct PosRecord {
        float x, y;
    };
    std::vector<PosRecord> m_posHistory;
    static inline constexpr int   HISTORY_LEN = 10; ///< 이력 길이

    /**
     * @brief 이력을 분석하여 제자리 여부 반환
     * @return true: 최근 HISTORY_LEN 스텝 동안 이동 거리 합이 임계값 이하
     */
    [[nodiscard]] bool isIdling() const noexcept;

    // ── 멤버 변수 ────────────────────────────────────────────────────────────
    Terrain      m_terrain;     ///< 지형 인스턴스 (값으로 소유)
    Agent        m_agent;       ///< 에이전트 인스턴스 (값으로 소유)
    RewardConfig m_rewardCfg;   ///< 보상 설정
    int          m_maxSteps;    ///< 에피소드 최대 스텝 수
    int          m_step{0};     ///< 현재 스텝 번호
    float        m_totalReward{0.0f}; ///< 에피소드 누적 보상
    uint32_t     m_terrainSeed; ///< 저장된 지형 시드 (reset 시 재사용)
};

} // namespace rl_fov
