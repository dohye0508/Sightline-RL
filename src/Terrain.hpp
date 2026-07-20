/**
 * @file Terrain.hpp
 * @brief 100×100 격자 고도맵(Heightmap) 생성 및 관리
 *
 * 외부 라이브러리 없이 순수 수학 함수(sin/cos/gauss)를 조합하여
 * 펄린 노이즈와 유사한 부드러운 가상 지형을 생성합니다.
 *
 * 설계 원칙:
 *  - 모든 고도값은 [0.0, 1.0] 정규화 float
 *  - 데이터는 행-우선(row-major) 2D 배열로 저장
 *  - 생성 함수는 완전히 결정론적(seed 기반)
 */

#pragma once

#include "MathUtils.hpp"
#include <array>
#include <cmath>
#include <random>
#include <string>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 상수
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr int MAP_W = 100; ///< 맵 가로 격자 수
inline constexpr int MAP_H = 100; ///< 맵 세로 격자 수

/**
 * @enum TileType
 * @brief 지형 타일 속성
 */
enum class TileType {
    Normal, ///< 일반 평지 (이동 가능, 시야 통과)
    Water,  ///< 물 (이동 불가, 시야 통과)
    Wall    ///< 절벽/장애물 (이동 불가, 시야 차단)
};

// ─────────────────────────────────────────────────────────────────────────────
// Terrain 클래스
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Terrain
 * @brief 지형 고도 맵을 생성하고 쿼리하는 클래스
 *
 * 생성 단계:
 *   1. 여러 주파수의 sin/cos 파형을 중첩 (프랙탈 유사 구조)
 *   2. 가우시안 범프를 여러 개 추가하여 산봉우리 표현
 *   3. 전체 값을 [0, 1]로 정규화
 */
class Terrain {
public:
    // ── 타입 별칭 ────────────────────────────────────────────────────────────
    using HeightRow   = std::array<float, MAP_W>;
    using HeightArray = std::array<HeightRow, MAP_H>;
    using TileRow     = std::array<TileType, MAP_W>;
    using TileArray   = std::array<TileRow, MAP_H>;

    // ── 생성자 / 소멸자 ──────────────────────────────────────────────────────

    /**
     * @brief 지형 생성
     * @param seed 난수 시드 (동일 seed → 동일 지형)
     * @param enableObstacles 장애물(물, 벽) 타일 생성 여부
     */
    explicit Terrain(uint32_t seed = 42u, bool enableObstacles = true);

    ~Terrain() = default;

    // ── 데이터 접근 ──────────────────────────────────────────────────────────

    /**
     * @brief 특정 격자 고도 반환 (범위 밖이면 0.0 반환)
     * @param col x 좌표(열)
     * @param row y 좌표(행)
     */
    [[nodiscard]] float heightAt(int col, int row) const noexcept;

    /**
     * @brief float 좌표로 고도 반환 (바이리니어 보간)
     * @param x 실수 x 좌표
     * @param y 실수 y 좌표
     */
    [[nodiscard]] float heightAtF(float x, float y) const noexcept;

    /**
     * @brief 특정 격자 타일 속성 반환 (범위 밖이면 Wall 반환)
     * @param col x 좌표(열)
     * @param row y 좌표(행)
     */
    [[nodiscard]] TileType tileAt(int col, int row) const noexcept;

    /// 고도 배열 const 참조 반환
    [[nodiscard]] const HeightArray& data() const noexcept { return m_height; }

    /// 맵 가로 크기
    [[nodiscard]] static constexpr int width()  noexcept { return MAP_W; }
    /// 맵 세로 크기
    [[nodiscard]] static constexpr int height() noexcept { return MAP_H; }

    /// 사용된 시드 반환
    [[nodiscard]] uint32_t seed() const noexcept { return m_seed; }

    // ── 통계 ─────────────────────────────────────────────────────────────────

    /// 전체 맵의 평균 고도 (캐시됨)
    [[nodiscard]] float meanHeight()  const noexcept { return m_mean; }
    /// 전체 맵의 최대 고도
    [[nodiscard]] float maxHeight()   const noexcept { return m_max;  }
    /// 전체 맵의 최소 고도
    [[nodiscard]] float minHeight()   const noexcept { return m_min;  }

private:
    // ── 내부 생성 함수 ───────────────────────────────────────────────────────

    /**
     * @brief 다중 주파수 sin/cos 레이어를 누적하여 원시 고도 생성
     * @param seed 난수 시드
     */
    void generateWaveLayers(uint32_t seed);

    /**
     * @brief 가우시안 범프(산봉우리)를 지형에 추가
     * @param seed 난수 시드
     */
    void addGaussianBumps(uint32_t seed);

    /**
     * @brief 전체 고도 배열을 [0, 1] 구간으로 정규화하고 통계 계산
     * @param enableObstacles 장애물(물, 벽) 타일 생성 여부
     */
    void normalizeAndComputeStats(bool enableObstacles);

    /**
     * @brief 단일 가우시안 범프 추가
     * @param cx    중심 x
     * @param cy    중심 y
     * @param sigma 범프 너비 (표준편차)
     * @param amp   진폭
     */
    void addBump(float cx, float cy, float sigma, float amp);

    // ── 멤버 변수 ────────────────────────────────────────────────────────────
    HeightArray m_height{};  ///< 고도 데이터 [row][col]
    TileArray   m_tileType{};///< 타일 속성 데이터 [row][col]
    uint32_t    m_seed;      ///< 생성 시드
    float       m_mean{};    ///< 평균 고도
    float       m_min{};     ///< 최소 고도
    float       m_max{};     ///< 최대 고도
};

} // namespace rl_fov
