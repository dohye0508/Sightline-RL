/**
 * @file Renderer.hpp
 * @brief SFML 기반 2D 시각화 렌더러
 *
 * 시각화 레이어:
 *   Layer 0 – 지형 고도맵 (어두운 녹색 ~ 흰색 산악)
 *   Layer 1 – 안개 전쟁(Fog of War): visibleMap=false 격자를 반투명 검은색으로 덮음
 *   Layer 2 – 부채꼴 FOV 시야 영역 (반투명 노란색)
 *   Layer 3 – 에이전트 (빨간색 점 + 방향 표시 선)
 *   Layer 4 – HUD 텍스트 (스텝, 보상, 탐험률)
 *
 * 최적화 전략:
 *   - 지형 레이어는 sf::VertexArray (Quads)로 단일 드로우 콜 처리
 *   - 안개 레이어는 별도 sf::VertexArray로 가시 격자만 갱신
 *   - FOV는 sf::VertexArray (TriangleFan)으로 부채꼴 렌더링
 */

#pragma once

#include "Terrain.hpp"
#include "Agent.hpp"
#include "RLEnvironment.hpp"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 색상 테마 설정
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 고도 값(0~1)을 지형 색상으로 매핑하는 함수
 * @param h 정규화 고도 [0, 1]
 * @return sf::Color
 *
 * 매핑:
 *   0.0 ~ 0.25: 저지대 (짙은 녹색 ~ 중간 녹색)
 *   0.25 ~ 0.6: 구릉 (황토색)
 *   0.6  ~ 0.8: 고원 (갈색/바위)
 *   0.8  ~ 1.0: 산봉우리 (흰색/눈 덮인 봉우리)
 */
[[nodiscard]] sf::Color heightToColor(float h) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Renderer 클래스
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Renderer
 * @brief RLEnvironment 상태를 SFML 창에 렌더링하는 클래스
 */
class Renderer {
public:
    // ── 생성자 / 소멸자 ──────────────────────────────────────────────────────

    /**
     * @brief 렌더러 초기화 (SFML 창 생성 포함)
     * @param env        연결할 RL 환경 참조
     * @param winWidth   창 가로 픽셀 (기본 800)
     * @param winHeight  창 세로 픽셀 (기본 800)
     * @param title      창 제목
     */
    explicit Renderer(const RLEnvironment& env,
                      unsigned             winWidth  = 800u,
                      unsigned             winHeight = 800u,
                      const std::string&   title     = "RL FOV Simulator");

    ~Renderer() = default;

    // ── 메인 렌더 루프 인터페이스 ────────────────────────────────────────────

    /**
     * @brief 이벤트 처리 및 창 상태 반환
     * @return true: 창이 열려 있음, false: 창이 닫혔음
     */
    bool handleEvents();

    /**
     * @brief 현재 환경 상태로 프레임 렌더링
     * @param lastResult  마지막 step() 결과 (HUD 표시용)
     */
    void render(const StepResult& lastResult);

    /**
     * @brief 창 표시 여부
     */
    [[nodiscard]] bool isOpen() const { return m_window.isOpen(); }

    /**
     * @brief SFML 창 참조 (captureFrame() 등 외부 접근용)
     */
    [[nodiscard]] sf::RenderWindow& window() { return m_window; }

    // ── 마지막으로 누른 키 반환 (수동 조작 모드) ────────────────────────────

    /**
     * @brief 이번 프레임에서 감지된 행동 반환
     * @return -1: 행동 없음, 0~2: 해당 행동 인덱스
     */
    [[nodiscard]] int pollAction();

private:
    // ── 초기화 ───────────────────────────────────────────────────────────────

    /// 지형 VertexArray 초기 생성 (한 번만 수행)
    void buildTerrainMesh();

    // ── 렌더 레이어 ──────────────────────────────────────────────────────────

    /// Layer 0: 지형 메시 그리기
    void drawTerrain();

    /// Layer 1: 안개 전쟁(Fog of War) 그리기
    void drawFogOfWar();

    /// Layer 2: FOV 부채꼴 시야 그리기
    void drawFOV();

    /// Layer 3: 에이전트 그리기
    void drawAgent();

    /// Layer 4: HUD 텍스트 그리기
    void drawHUD(const StepResult& result);

    // ── 좌표 변환 ────────────────────────────────────────────────────────────

    /**
     * @brief 격자 좌표(col, row) → 픽셀 좌표 변환
     */
    [[nodiscard]] sf::Vector2f gridToPixel(float col, float row) const noexcept;

    // ── 멤버 변수 ────────────────────────────────────────────────────────────
    const RLEnvironment& m_env;       ///< 환경 참조 (비소유)

    sf::RenderWindow m_window;        ///< SFML 렌더 창

    // 격자 → 픽셀 변환 스케일
    float m_cellW;   ///< 격자 하나의 픽셀 너비
    float m_cellH;   ///< 격자 하나의 픽셀 높이

    // 렌더용 VertexArray (성능 최적화)
    sf::VertexArray m_terrainMesh;  ///< 지형 쿼드 메시 (정적, 한 번 생성)
    sf::VertexArray m_fogMesh;      ///< 안개 쿼드 메시 (동적, 매 프레임 갱신)
    sf::VertexArray m_fovMesh;      ///< FOV 삼각형 팬 (동적)

    // HUD 폰트 / 텍스트
    sf::Font         m_font;         ///< 시스템 폰트
    bool             m_fontLoaded;   ///< 폰트 로드 성공 여부

    // 키보드 행동 버퍼
    int m_pendingAction{-1};
};

} // namespace rl_fov
