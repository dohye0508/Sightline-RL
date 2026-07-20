/**
 * @file RLEnvironment.cpp
 * @brief RLEnvironment 클래스 구현
 *
 * step() 흐름:
 *   1. 행동(action) 실행 → 에이전트 이동/회전
 *   2. FOV 레이캐스팅 → visibleMap 갱신 + 신규 탐험 격자 수 획득
 *   3. 보상 계산 (탐험 + 고도 + 패널티)
 *   4. 관측 벡터 생성 (66차원)
 *   5. 에피소드 종료 조건 체크
 */

#include "RLEnvironment.hpp"
#include "MathUtils.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 생성자
// ─────────────────────────────────────────────────────────────────────────────

RLEnvironment::RLEnvironment(uint32_t terrainSeed, RewardConfig rewardCfg, int maxSteps)
    : m_terrain(terrainSeed)
    , m_agent(m_terrain,
              static_cast<float>(MAP_W) / 2.0f,
              static_cast<float>(MAP_H) / 2.0f,
              0.0f)
    , m_rewardCfg(rewardCfg)
    , m_maxSteps(maxSteps)
    , m_terrainSeed(terrainSeed)
{
    m_posHistory.reserve(HISTORY_LEN + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// reset()
// ─────────────────────────────────────────────────────────────────────────────

std::vector<float> RLEnvironment::reset() {
    // 에이전트를 맵 중앙으로 재배치
    m_agent.reset(static_cast<float>(MAP_W) / 2.0f,
                  static_cast<float>(MAP_H) / 2.0f,
                  0.0f);

    m_step        = 0;
    m_totalReward = 0.0f;
    m_posHistory.clear();

    // 초기 FOV 계산 (Agent 생성자에서 이미 수행되지만 명시적으로 재수행)
    const int newExplored = m_agent.castFOV();

    return buildObservation(newExplored);
}

// ─────────────────────────────────────────────────────────────────────────────
// step()
// ─────────────────────────────────────────────────────────────────────────────

StepResult RLEnvironment::step(int action) {
    StepResult result{};
    result.step = ++m_step;

    // ── 1. 행동 실행 ────────────────────────────────────────────────────────
    bool moved = false;
    switch (action) {
        case 0: // 전진
            moved = m_agent.moveForward();
            break;
        case 1: // 좌회전
            m_agent.turnLeft();
            // 회전만 해도 위치는 유지 (moved = false)
            break;
        case 2: // 우회전
            m_agent.turnRight();
            break;
        default:
            // 알 수 없는 행동 → 아무것도 하지 않음
            break;
    }

    // 위치 이력 업데이트
    m_posHistory.push_back({m_agent.x(), m_agent.y()});
    if (static_cast<int>(m_posHistory.size()) > HISTORY_LEN)
        m_posHistory.erase(m_posHistory.begin());

    // ── 2. FOV 레이캐스팅 → 신규 탐험 격자 수 ──────────────────────────────
    const int newExplored = m_agent.castFOV();
    result.newExplored = newExplored;

    // ── 3. 보상 계산 ────────────────────────────────────────────────────────
    result.reward = computeReward(newExplored, moved, result);
    m_totalReward += result.reward;

    // ── 4. 관측 벡터 생성 ───────────────────────────────────────────────────
    result.observation = buildObservation(newExplored);

    // ── 5. 에피소드 종료 조건 ───────────────────────────────────────────────
    const float exploredRatio = static_cast<float>(m_agent.totalExplored())
                              / static_cast<float>(MAP_W * MAP_H);
    result.done = (m_step >= m_maxSteps) || (exploredRatio >= DONE_RATIO);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildObservation() — 66차원 관측 벡터 생성
// ─────────────────────────────────────────────────────────────────────────────

std::vector<float> RLEnvironment::buildObservation(int newExplored) const {
    std::vector<float> obs;
    obs.reserve(OBS_TOTAL_DIM);

    // ── 기본 상태 (5차원) ───────────────────────────────────────────────────
    // [0] 정규화된 x 좌표 [0, 1]
    obs.push_back(m_agent.x() / static_cast<float>(MAP_W - 1));
    // [1] 정규화된 y 좌표 [0, 1]
    obs.push_back(m_agent.y() / static_cast<float>(MAP_H - 1));
    // [2] cos(θ) — 방향 인코딩 ([-1, 1])
    obs.push_back(std::cos(m_agent.theta()));
    // [3] sin(θ) — 방향 인코딩 ([-1, 1])
    obs.push_back(std::sin(m_agent.theta()));
    // [4] 현재 고도 [0, 1]
    obs.push_back(m_agent.currentHeight());

    // ── FOV 광선 거리 (61차원) ──────────────────────────────────────────────
    // 각 광선의 도달 거리를 [0, 1]로 정규화
    const auto& hits = m_agent.rayHits();
    for (const auto& hit : hits) {
        obs.push_back(hit.distance / FOV_MAX_DIST);
    }

    // 광선이 부족할 경우 패딩 (정상 상황에서는 발생 안 함)
    while (static_cast<int>(obs.size()) < OBS_TOTAL_DIM) {
        obs.push_back(0.0f);
    }

    return obs;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeReward() — 보상 계산
// ─────────────────────────────────────────────────────────────────────────────

float RLEnvironment::computeReward(int newExplored, bool moved, StepResult& result) const {
    float reward = 0.0f;

    // ── (1) 탐험 보상: 새로 밝혀진 격자 수 비례 ────────────────────────────
    // 정규화: 최대 격자 수(10000) 대비 비율 × 100 스케일
    const float exploreBonus = m_rewardCfg.wExplore
                             * static_cast<float>(newExplored);
    reward += exploreBonus;

    // ── (2) 고도 보상: 높은 곳에 있을수록 시야가 트임 ──────────────────────
    // 에이전트 고도 [0, 1] 그대로 보너스 (산꼭대기 인센티브)
    const float altHeight     = m_agent.currentHeight();
    const float altBonus      = m_rewardCfg.wAltitude * altHeight;
    result.altitudeBonus      = altBonus;
    reward                   += altBonus;

    // ── (3) 제자리 패널티: 충분한 이동 없이 같은 위치 반복 ─────────────────
    float idlePen = 0.0f;
    if (isIdling()) {
        idlePen = m_rewardCfg.wIdle;
        reward -= idlePen;
    }
    result.idlePenalty = idlePen;

    // ── (4) 재방문 패널티: 새로 밝힌 격자가 없는데 전진한 경우 ──────────────
    // (이미 탐험된 곳을 왔다 갔다하는 비효율적 행동 억제)
    float revisitPen = 0.0f;
    if (moved && newExplored == 0) {
        // 이동했는데 새로 밝힌 게 없음 → 작은 패널티
        revisitPen = m_rewardCfg.wRevisit;
        reward -= revisitPen;
    }
    result.revisitPenalty = revisitPen;

    return reward;
}

// ─────────────────────────────────────────────────────────────────────────────
// isIdling() — 최근 이력으로 제자리 감지
// ─────────────────────────────────────────────────────────────────────────────

bool RLEnvironment::isIdling() const noexcept {
    if (static_cast<int>(m_posHistory.size()) < HISTORY_LEN)
        return false;

    // 최근 HISTORY_LEN개 위치의 총 이동 거리 합산
    float totalDist = 0.0f;
    for (int i = 1; i < HISTORY_LEN; ++i) {
        const float dx = m_posHistory[static_cast<size_t>(i)].x
                       - m_posHistory[static_cast<size_t>(i - 1)].x;
        const float dy = m_posHistory[static_cast<size_t>(i)].y
                       - m_posHistory[static_cast<size_t>(i - 1)].y;
        totalDist += std::sqrt(dx * dx + dy * dy);
    }

    // 총 이동거리가 1칸 미만 → 제자리로 간주
    constexpr float IDLE_THRESHOLD = 1.0f;
    return totalDist < IDLE_THRESHOLD;
}

// ─────────────────────────────────────────────────────────────────────────────
// serializeStateCSV()
// ─────────────────────────────────────────────────────────────────────────────

std::string RLEnvironment::serializeStateCSV() const {
    const float ratio = static_cast<float>(m_agent.totalExplored())
                      / static_cast<float>(MAP_W * MAP_H);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4)
        << m_step       << ","
        << m_agent.x()  << ","
        << m_agent.y()  << ","
        << m_agent.theta() << ","
        << ratio        << ","
        << m_totalReward;
    return oss.str();
}

} // namespace rl_fov
