/**
 * @file Agent.cpp
 * @brief Agent 클래스 구현 — 이동, FOV 레이캐스팅, 가시성 맵 갱신
 */

#include "Agent.hpp"
#include "MathUtils.hpp"
#include <cmath>
#include <algorithm>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 생성자
// ─────────────────────────────────────────────────────────────────────────────

Agent::Agent(const Terrain& terrain, float startX, float startY, float startTheta)
    : m_terrain(terrain)
    , m_x(math::clamp(startX, 0.0f, static_cast<float>(MAP_W - 1)))
    , m_y(math::clamp(startY, 0.0f, static_cast<float>(MAP_H - 1)))
    , m_theta(math::wrapAngle(startTheta))
    , m_prevX(m_x)
    , m_prevY(m_y)
{
    // 가시성 맵 초기화
    for (auto& row : m_visibleMap) row.fill(false);
    m_totalExplored = 0;

    // 광선 결과 배열 예약 (메모리 재할당 방지)
    m_rayHits.reserve(FOV_ANGLE_SAMPLES);

    // 초기 위치에서 FOV 즉시 계산
    castFOV();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이동
// ─────────────────────────────────────────────────────────────────────────────

bool Agent::moveForward() {
    // 이전 위치 기록
    m_prevX = m_x;
    m_prevY = m_y;

    // 현재 방향으로 AGENT_MOVE_STEP 전진 벡터 계산
    const float nx = m_x + AGENT_MOVE_STEP * std::cos(m_theta);
    const float ny = m_y + AGENT_MOVE_STEP * std::sin(m_theta);

    // 맵 경계 클램프
    const float clampedX = math::clamp(nx, 0.0f, static_cast<float>(MAP_W - 1));
    const float clampedY = math::clamp(ny, 0.0f, static_cast<float>(MAP_H - 1));

    // 실제 이동 여부 (경계에 막혔으면 false 반환)
    const bool moved = (clampedX != m_x || clampedY != m_y);
    m_x = clampedX;
    m_y = clampedY;

    return moved;
}

void Agent::turnLeft() {
    m_prevX = m_x;
    m_prevY = m_y;
    m_theta = math::wrapAngle(m_theta - AGENT_TURN_RAD);
}

void Agent::turnRight() {
    m_prevX = m_x;
    m_prevY = m_y;
    m_theta = math::wrapAngle(m_theta + AGENT_TURN_RAD);
}

// ─────────────────────────────────────────────────────────────────────────────
// 현재 고도
// ─────────────────────────────────────────────────────────────────────────────

float Agent::currentHeight() const noexcept {
    return m_terrain.heightAtF(m_x, m_y);
}

// ─────────────────────────────────────────────────────────────────────────────
// FOV 레이캐스팅 (메인 함수)
// ─────────────────────────────────────────────────────────────────────────────

int Agent::castFOV() {
    m_rayHits.clear();

    int totalNew = 0;

    // 에이전트 현재 고도 (차폐 판단 기준)
    const float agentHeight = currentHeight();
    // 차폐 임계 고도
    const float occlusionThreshold = agentHeight + FOV_OCCLUSION_DELTA;

    // −30° ~ +30° 범위를 1° 간격으로 순회 (총 61개 광선)
    // FOV_ANGLE_SAMPLES = 61
    for (int i = 0; i < FOV_ANGLE_SAMPLES; ++i) {
        // 현재 광선의 월드 각도 계산
        // i=0 → −30°, i=30 → 0° (정면), i=60 → +30°
        const float offset = (static_cast<float>(i) / static_cast<float>(FOV_ANGLE_SAMPLES - 1)
                              - 0.5f) * 2.0f * FOV_HALF_ANGLE_RAD;
        const float rayAngle = math::wrapAngle(m_theta + offset);

        // 광선 방향 단위 벡터 사전 계산
        const float dirX = std::cos(rayAngle);
        const float dirY = std::sin(rayAngle);

        // ── 단일 광선 캐스팅 ────────────────────────────────────────────────
        RayHit hit;
        hit.angle   = rayAngle;
        hit.blocked = false;
        hit.distance = 0.0f;

        int newCount = 0;

        float dist = 0.0f;
        while (dist < FOV_MAX_DIST) {
            dist += FOV_RAY_STEP;

            // 광선 끝점 계산
            const float rx = m_x + dirX * dist;
            const float ry = m_y + dirY * dist;

            // 격자 인덱스 변환
            const int gc = static_cast<int>(rx);
            const int gr = static_cast<int>(ry);

            // 맵 경계 밖이면 종료
            if (!inBounds(gc, gr)) {
                dist -= FOV_RAY_STEP; // 이전 위치가 최종점
                break;
            }

            // 차폐 체크: 격자 고도 > 에이전트 고도 + 0.2
            const float tileHeight = m_terrain.heightAt(gc, gr);
            if (tileHeight > occlusionThreshold) {
                // 시야 차단 — 이 격자는 밝히지 않음
                hit.blocked = true;
                dist -= FOV_RAY_STEP;
                break;
            }

            // ── 격자 가시성 업데이트 ─────────────────────────────────────
            auto& vis = m_visibleMap[static_cast<size_t>(gr)][static_cast<size_t>(gc)];
            if (!vis) {
                // 새로 밝혀진 격자
                vis = true;
                ++newCount;
                ++m_totalExplored;
            }
        }

        // 광선 종점 기록
        hit.distance = dist;
        hit.endX = m_x + dirX * dist;
        hit.endY = m_y + dirY * dist;
        hit.hitCol = static_cast<int>(hit.endX);
        hit.hitRow = static_cast<int>(hit.endY);

        // 경계 클램프
        hit.hitCol = math::clamp(hit.hitCol, 0, MAP_W - 1);
        hit.hitRow = math::clamp(hit.hitRow, 0, MAP_H - 1);

        totalNew += newCount;
        m_rayHits.push_back(hit);
    }

    return totalNew;
}

// ─────────────────────────────────────────────────────────────────────────────
// 가시성 맵 초기화 (에피소드 리셋)
// ─────────────────────────────────────────────────────────────────────────────

void Agent::reset(float startX, float startY, float startTheta) {
    m_x = math::clamp(startX, 0.0f, static_cast<float>(MAP_W - 1));
    m_y = math::clamp(startY, 0.0f, static_cast<float>(MAP_H - 1));
    m_theta = math::wrapAngle(startTheta);
    m_prevX = m_x;
    m_prevY = m_y;
    resetVisibility();
}

void Agent::resetVisibility() {
    for (auto& row : m_visibleMap) row.fill(false);
    m_totalExplored = 0;
}

} // namespace rl_fov
