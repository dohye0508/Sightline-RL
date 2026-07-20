/**
 * @file MathUtils.hpp
 * @brief 공통 수학 유틸리티 함수 모음
 *
 * 각도 정규화, 선형 보간, 클램프, 거리 계산 등
 * 프로젝트 전반에서 사용되는 헤더-온리 수학 함수를 정의합니다.
 */

#pragma once

#include <cmath>
#include <algorithm>
#include <type_traits>

namespace rl_fov {
namespace math {

// ─────────────────────────────────────────────────────────────────────────────
// 상수
// ─────────────────────────────────────────────────────────────────────────────

/// 원주율
inline constexpr float PI  = 3.14159265358979323846f;
/// 2π (한 바퀴)
inline constexpr float TAU = 2.0f * PI;
/// π/2 (직각)
inline constexpr float HALF_PI = PI / 2.0f;
/// 도(degree) → 라디안 변환 계수
inline constexpr float DEG2RAD = PI / 180.0f;
/// 라디안 → 도(degree) 변환 계수
inline constexpr float RAD2DEG = 180.0f / PI;

// ─────────────────────────────────────────────────────────────────────────────
// 제네릭 유틸리티
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 값을 [lo, hi] 범위로 클램프
 */
template <typename T>
[[nodiscard]] inline constexpr T clamp(T v, T lo, T hi) noexcept {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/**
 * @brief 선형 보간 (Lerp)
 * @param a 시작값
 * @param b 끝값
 * @param t 보간 인수 [0, 1]
 */
template <typename T>
[[nodiscard]] inline constexpr T lerp(T a, T b, float t) noexcept {
    return a + static_cast<T>((b - a) * t);
}

/**
 * @brief Smoothstep (3차 에르미트 보간) — 부드러운 0→1 전이
 */
[[nodiscard]] inline constexpr float smoothstep(float edge0, float edge1, float x) noexcept {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ─────────────────────────────────────────────────────────────────────────────
// 각도 유틸리티
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 각도를 [-π, π] 구간으로 정규화
 */
[[nodiscard]] inline float wrapAngle(float rad) noexcept {
    while (rad >  PI) rad -= TAU;
    while (rad < -PI) rad += TAU;
    return rad;
}

/**
 * @brief 각도를 [0, 2π) 구간으로 정규화
 */
[[nodiscard]] inline float wrapAngle2Pi(float rad) noexcept {
    rad = std::fmod(rad, TAU);
    if (rad < 0.0f) rad += TAU;
    return rad;
}

/**
 * @brief 도(degree) → 라디안 변환
 */
[[nodiscard]] inline constexpr float toRad(float deg) noexcept {
    return deg * DEG2RAD;
}

/**
 * @brief 라디안 → 도(degree) 변환
 */
[[nodiscard]] inline constexpr float toDeg(float rad) noexcept {
    return rad * RAD2DEG;
}

// ─────────────────────────────────────────────────────────────────────────────
// 거리 / 벡터
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 2D 유클리드 거리
 */
[[nodiscard]] inline float dist2D(float x1, float y1, float x2, float y2) noexcept {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * @brief 2D 유클리드 거리의 제곱 (sqrt 없이 비교용)
 */
[[nodiscard]] inline constexpr float dist2DSq(float x1, float y1, float x2, float y2) noexcept {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy;
}

// ─────────────────────────────────────────────────────────────────────────────
// 해시 / 난수 보조 (LCG 기반 결정론적 난수)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 정수 시드로부터 [0.0, 1.0] 난수 생성 (LCG)
 * 재현 가능한 결정론적 지형 생성에 사용
 */
[[nodiscard]] inline float pseudoRand(uint32_t seed) noexcept {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

} // namespace math
} // namespace rl_fov
