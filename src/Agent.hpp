/**
 * @file Agent.hpp
 * @brief 에이전트 속성 및 부채꼴 시야(FOV) 레이캐스팅 정의
 *
 * 에이전트는 맵 위를 이동하며 부채꼴 시야 내 격자를 탐색합니다.
 *
 * FOV 사양:
 *   - 반각도(half-angle): 30° (전체 60°)
 *   - 최대 시야 거리: 30칸
 *   - 광선 간격: 1°
 *   - 차폐 조건: 격자 고도 > 에이전트 고도 + 0.2
 *
 * visibleMap:
 *   - 한 번 밝혀진 격자는 영구적으로 true 유지 (탐험 기록)
 *   - 이번 step에서 새로 밝혀진 격자 수 → 보상 계산에 사용
 */

#pragma once

#include "Terrain.hpp"
#include "MathUtils.hpp"
#include <array>
#include <vector>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// FOV 설정 상수
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr float FOV_HALF_ANGLE_DEG  = 30.0f;   ///< 시야 반각도 (도)
inline constexpr float FOV_HALF_ANGLE_RAD  = FOV_HALF_ANGLE_DEG * math::DEG2RAD;
inline constexpr float FOV_MAX_DIST        = 30.0f;   ///< 최대 시야 거리 (격자 단위)
inline constexpr float FOV_RAY_STEP        = 0.5f;    ///< 광선 스텝 크기 (격자)
inline constexpr float FOV_OCCLUSION_DELTA = 0.2f;    ///< 차폐 판단 고도 차이 임계값
inline constexpr int   FOV_ANGLE_SAMPLES   = 61;      ///< 광선 개수 (−30°~+30°, 1° 간격)

// ─────────────────────────────────────────────────────────────────────────────
// 에이전트 이동 설정
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr float AGENT_MOVE_STEP  = 1.0f;               ///< 전진 거리 (격자 단위)
inline constexpr float AGENT_TURN_DEG   = 5.0f;               ///< 회전 각도 (도)
inline constexpr float AGENT_TURN_RAD   = AGENT_TURN_DEG * math::DEG2RAD;

// ─────────────────────────────────────────────────────────────────────────────
// 광선 히트 정보 구조체
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct RayHit
 * @brief 단일 광선(Ray)의 결과 정보
 */
struct RayHit {
    float  angle;      ///< 광선 발사 각도 (라디안, 월드 기준)
    float  distance;   ///< 도달 거리 (격자 단위)
    int    hitCol;     ///< 최종 도달 격자 열(x)
    int    hitRow;     ///< 최종 도달 격자 행(y)
    bool   blocked;    ///< 차폐 여부 (true: 지형에 막힘)
    float  endX;       ///< 광선 종점 x 좌표 (float)
    float  endY;       ///< 광선 종점 y 좌표 (float)
};

// ─────────────────────────────────────────────────────────────────────────────
// Agent 클래스
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Agent
 * @brief 2D 격자 맵 위를 이동하는 RL 에이전트
 *
 * 책임:
 *  - 위치(x, y) 및 방향(theta) 관리
 *  - 부채꼴 FOV 레이캐스팅 수행
 *  - visibleMap 누적 갱신 및 신규 탐험 격자 수 반환
 */
class Agent {
public:
    // ── 가시성 맵 타입 ───────────────────────────────────────────────────────
    using VisRow   = std::array<bool, MAP_W>;
    using VisArray = std::array<VisRow, MAP_H>;

    // ── 생성자 ───────────────────────────────────────────────────────────────

    /**
     * @brief 에이전트 초기화
     * @param terrain  지형 참조 (고도 쿼리용)
     * @param startX   초기 x 좌표
     * @param startY   초기 y 좌표
     * @param startTheta 초기 방향 (라디안)
     */
    explicit Agent(const Terrain& terrain,
                   float startX     = MAP_W / 2.0f,
                   float startY     = MAP_H / 2.0f,
                   float startTheta = 0.0f);

    ~Agent() = default;

    // ── 이동 인터페이스 ──────────────────────────────────────────────────────

    /**
     * @brief 현재 방향으로 AGENT_MOVE_STEP 전진
     * @return true: 이동 성공, false: 맵 경계 밖 이동 시도(위치 유지)
     */
    bool moveForward();

    /**
     * @brief 좌회전 (−AGENT_TURN_RAD)
     */
    void turnLeft();

    /**
     * @brief 우회전 (+AGENT_TURN_RAD)
     */
    void turnRight();

    // ── FOV 레이캐스팅 ───────────────────────────────────────────────────────

    /**
     * @brief 현재 위치에서 부채꼴 FOV 레이캐스팅 수행
     *
     * 수행 후:
     *  - m_rayHits 에 각 광선 결과 저장
     *  - m_visibleMap 에 새로 보인 격자를 true로 기록
     *
     * @return 이번 캐스팅에서 새로 밝혀진 격자 개수
     */
    int castFOV();

    // ── 접근자 ───────────────────────────────────────────────────────────────

    [[nodiscard]] float x()     const noexcept { return m_x; }
    [[nodiscard]] float y()     const noexcept { return m_y; }
    [[nodiscard]] float theta() const noexcept { return m_theta; }

    /// 에이전트가 서 있는 격자의 고도
    [[nodiscard]] float currentHeight() const noexcept;

    /// 누적 가시성 맵 (한 번 true면 영구 유지)
    [[nodiscard]] const VisArray& visibleMap() const noexcept { return m_visibleMap; }

    /// 이번 스텝의 광선 히트 정보 배열
    [[nodiscard]] const std::vector<RayHit>& rayHits() const noexcept { return m_rayHits; }

    /// 지금까지 탐험한 총 격자 수
    [[nodiscard]] int totalExplored() const noexcept { return m_totalExplored; }

    /// 에이전트가 서 있는 격자의 열(col)
    [[nodiscard]] int col() const noexcept { return static_cast<int>(m_x); }
    /// 에이전트가 서 있는 격자의 행(row)
    [[nodiscard]] int row() const noexcept { return static_cast<int>(m_y); }

    /// 이전 위치 x
    [[nodiscard]] float prevX() const noexcept { return m_prevX; }
    /// 이전 위치 y
    [[nodiscard]] float prevY() const noexcept { return m_prevY; }

    // ── 상태 초기화 ──────────────────────────────────────────────────────────

    /**
     * @brief 에이전트 위치와 방향, 가시성 맵 초기화
     */
    void reset(float startX, float startY, float startTheta);

    /**
     * @brief visibleMap 초기화 (에피소드 리셋 시 사용)
     */
    void resetVisibility();

private:
    // ── 내부 헬퍼 ────────────────────────────────────────────────────────────

    /**
     * @brief 단일 광선(ray) 캐스팅
     * @param angle 광선 발사 각도 (라디안, 월드 기준)
     * @param[out] hit 결과 저장 구조체
     * @param[out] newCells 이번 광선이 새로 밝힌 격자 목록
     */
    void castSingleRay(float angle, RayHit& hit, int& newCount);

    /// 좌표가 맵 안에 있는지 확인
    [[nodiscard]] static bool inBounds(int col, int row) noexcept {
        return col >= 0 && col < MAP_W && row >= 0 && row < MAP_H;
    }

    // ── 멤버 변수 ────────────────────────────────────────────────────────────
    const Terrain& m_terrain;   ///< 지형 참조 (비소유)

    float m_x;        ///< 현재 x 좌표 (float)
    float m_y;        ///< 현재 y 좌표 (float)
    float m_theta;    ///< 현재 방향 (라디안)

    float m_prevX;    ///< 이전 스텝 x 좌표
    float m_prevY;    ///< 이전 스텝 y 좌표

    VisArray m_visibleMap{};    ///< 누적 가시성 맵 [row][col]
    int      m_totalExplored{}; ///< 총 탐험 격자 수

    std::vector<RayHit> m_rayHits; ///< 이번 스텝 광선 결과
};

} // namespace rl_fov
