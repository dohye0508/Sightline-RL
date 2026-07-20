/**
 * @file Terrain.cpp
 * @brief Terrain 클래스 구현
 *
 * 지형 고도 데이터 생성 로직:
 *   Phase 1 – 다중 옥타브 sin/cos 파형 합산 (저주파 → 고주파)
 *   Phase 2 – 가우시안 범프 추가 (산봉우리 / 분지 표현)
 *   Phase 3 – 전역 [0, 1] 정규화 + 통계 계산
 */

#include "Terrain.hpp"
#include "MathUtils.hpp"
#include <limits>
#include <numeric>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 생성자
// ─────────────────────────────────────────────────────────────────────────────

Terrain::Terrain(uint32_t seed)
    : m_seed(seed)
{
    // 배열 0으로 초기화
    for (auto& row : m_height)
        row.fill(0.0f);

    generateWaveLayers(seed);    // Phase 1
    addGaussianBumps(seed);      // Phase 2
    normalizeAndComputeStats();  // Phase 3
}

// ─────────────────────────────────────────────────────────────────────────────
// 고도 접근자
// ─────────────────────────────────────────────────────────────────────────────

float Terrain::heightAt(int col, int row) const noexcept {
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H)
        return 0.0f;
    return m_height[static_cast<size_t>(row)][static_cast<size_t>(col)];
}

float Terrain::heightAtF(float x, float y) const noexcept {
    // 격자 기준 정수 좌표
    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);

    // 바이리니어 보간용 소수부
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    // 4개 코너 고도
    const float h00 = heightAt(ix,     iy    );
    const float h10 = heightAt(ix + 1, iy    );
    const float h01 = heightAt(ix,     iy + 1);
    const float h11 = heightAt(ix + 1, iy + 1);

    // 쌍선형 보간
    const float h0 = math::lerp(h00, h10, fx);
    const float h1 = math::lerp(h01, h11, fx);
    return math::lerp(h0, h1, fy);
}

TileType Terrain::tileAt(int col, int row) const noexcept {
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H)
        return TileType::Wall; // 맵 밖은 벽으로 취급
    return m_tileType[static_cast<size_t>(row)][static_cast<size_t>(col)];
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 생성: Phase 1 – 다중 옥타브 웨이브 레이어
// ─────────────────────────────────────────────────────────────────────────────

void Terrain::generateWaveLayers(uint32_t seed) {
    /*
     * 각 옥타브(octave)는 주파수(freq)와 진폭(amp)을 가집니다.
     * - 낮은 주파수 레이어: 전반적인 기복(산맥, 평야) 형성
     * - 높은 주파수 레이어: 세부 질감(바위, 언덕) 형성
     * - 각 레이어마다 sin/cos 파형의 방향과 위상을 달리하여
     *   비등방성(anisotropic) 지형 구조를 만듭니다.
     */

    struct OctaveParam {
        float freqX;   ///< x 방향 공간 주파수
        float freqY;   ///< y 방향 공간 주파수
        float phaseX;  ///< x 위상 (라디안)
        float phaseY;  ///< y 위상 (라디안)
        float amp;     ///< 진폭 가중치
    };

    // 시드 기반으로 옥타브 파라미터 결정론적 생성
    constexpr int NUM_OCTAVES = 8;
    OctaveParam octaves[NUM_OCTAVES];

    float totalAmp = 0.0f;
    for (int i = 0; i < NUM_OCTAVES; ++i) {
        // 시드 파생 난수 (각 옥타브 고유 시드)
        const uint32_t s0 = seed ^ (static_cast<uint32_t>(i) * 2654435761u);
        const uint32_t s1 = s0  ^ 0xDEADBEEFu;
        const uint32_t s2 = s1  + 0xB5297A4Du;
        const uint32_t s3 = s2  ^ (s2 >> 13u);

        // 주파수: 옥타브가 올라갈수록 2배씩 증가 (1/32 ~ 1/4 칸 주기)
        const float baseFreq = 1.0f / (static_cast<float>(MAP_W >> (NUM_OCTAVES - 1 - i)) + 1.0f);

        octaves[i].freqX  = baseFreq * (0.8f + 0.4f * math::pseudoRand(s0));
        octaves[i].freqY  = baseFreq * (0.8f + 0.4f * math::pseudoRand(s1));
        octaves[i].phaseX = math::TAU * math::pseudoRand(s2);
        octaves[i].phaseY = math::TAU * math::pseudoRand(s3);
        // 진폭: 옥타브가 올라갈수록 절반으로 감소 (1/f 노이즈 특성)
        octaves[i].amp    = 1.0f / static_cast<float>(1 << i);
        totalAmp += octaves[i].amp;
    }

    // 모든 격자에 웨이브 레이어 누적
    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            float h = 0.0f;
            for (const auto& oct : octaves) {
                // 두 직교 방향 sin 파형을 곱하여 2D 패턴 형성
                const float wx = std::sin(oct.freqX * math::TAU * static_cast<float>(col) + oct.phaseX);
                const float wy = std::cos(oct.freqY * math::TAU * static_cast<float>(row) + oct.phaseY);
                // 대각선 파형 추가 (지형에 방향성 다양성 부여)
                const float wd = std::sin((oct.freqX + oct.freqY) * math::PI *
                                          static_cast<float>(col + row) + oct.phaseX * 0.5f);
                h += oct.amp * (wx * wy + 0.3f * wd);
            }
            // [-totalAmp, totalAmp] → [0, 1] 임시 정규화 (최종은 Phase 3에서)
            m_height[static_cast<size_t>(row)][static_cast<size_t>(col)] =
                (h / totalAmp + 1.0f) * 0.5f;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 생성: Phase 2 – 가우시안 범프 추가
// ─────────────────────────────────────────────────────────────────────────────

void Terrain::addGaussianBumps(uint32_t seed) {
    /*
     * 지형에 가우시안 형태의 산봉우리/분지를 추가합니다.
     * 양의 진폭(amp > 0): 산봉우리
     * 음의 진폭(amp < 0): 분지/호수 저지대
     */

    constexpr int NUM_PEAKS   = 6;  ///< 산봉우리 수
    constexpr int NUM_VALLEYS = 3;  ///< 분지 수

    std::mt19937 rng(seed + 1000u);
    std::uniform_real_distribution<float> posX(10.0f, MAP_W - 10.0f);
    std::uniform_real_distribution<float> posY(10.0f, MAP_H - 10.0f);
    std::uniform_real_distribution<float> sigD(8.0f,  20.0f);
    std::uniform_real_distribution<float> ampD(0.3f,  0.7f);

    // 산봉우리
    for (int i = 0; i < NUM_PEAKS; ++i) {
        addBump(posX(rng), posY(rng), sigD(rng), ampD(rng));
    }

    // 분지 (음의 진폭)
    std::uniform_real_distribution<float> ampNeg(-0.3f, -0.1f);
    for (int i = 0; i < NUM_VALLEYS; ++i) {
        addBump(posX(rng), posY(rng), sigD(rng) * 0.6f, ampNeg(rng));
    }
}

void Terrain::addBump(float cx, float cy, float sigma, float amp) {
    // 가우시안: f(x,y) = amp * exp(-(dx²+dy²)/(2σ²))
    const float inv2sig2 = 1.0f / (2.0f * sigma * sigma);

    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            const float dx = static_cast<float>(col) - cx;
            const float dy = static_cast<float>(row) - cy;
            const float g  = amp * std::exp(-(dx * dx + dy * dy) * inv2sig2);
            m_height[static_cast<size_t>(row)][static_cast<size_t>(col)] += g;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 생성: Phase 3 – 정규화 및 통계 계산
// ─────────────────────────────────────────────────────────────────────────────

void Terrain::normalizeAndComputeStats() {
    // 최솟값·최댓값 탐색
    float vMin = std::numeric_limits<float>::max();
    float vMax = std::numeric_limits<float>::lowest();

    for (const auto& row : m_height) {
        for (float v : row) {
            if (v < vMin) vMin = v;
            if (v > vMax) vMax = v;
        }
    }

    // [vMin, vMax] → [0, 1] 선형 정규화
    const float range = (vMax - vMin > 1e-6f) ? (vMax - vMin) : 1.0f;
    float sum = 0.0f;

    for (auto& row : m_height) {
        for (float& v : row) {
            v = (v - vMin) / range;
            sum += v;
        }
    }

    m_min  = 0.0f;
    m_max  = 1.0f;
    m_mean = sum / static_cast<float>(MAP_W * MAP_H);

    // 타일 타입 결정 로직: 주변 5x5 고도의 평균을 사용하여 물과 벽이 자연스럽게 뭉치도록 유도
    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            float sumHeight = 0.0f;
            int count = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int ny = row + dy;
                    int nx = col + dx;
                    if (ny >= 0 && ny < MAP_H && nx >= 0 && nx < MAP_W) {
                        sumHeight += m_height[ny][nx];
                        count++;
                    }
                }
            }
            float avgHeight = sumHeight / static_cast<float>(count);

            if (avgHeight <= 0.20f) { // 뭉치게 하기 위해 임계값을 약간 조정
                m_tileType[row][col] = TileType::Water;
            } else if (avgHeight >= 0.65f) { // 벽도 뭉치도록 임계값 조정
                m_tileType[row][col] = TileType::Wall;
            } else {
                m_tileType[row][col] = TileType::Normal;
            }
        }
    }
}

} // namespace rl_fov
