/**
 * @file Renderer.cpp
 * @brief SFML 렌더러 구현
 *
 * 렌더링 순서:
 *   1. window.clear()
 *   2. drawTerrain()   — 지형 색상 메시
 *   3. drawFogOfWar()  — 안개 전쟁
 *   4. drawFOV()       — 반투명 노란색 부채꼴
 *   5. drawAgent()     — 에이전트 점 + 방향선
 *   6. drawHUD()       — 스텝/보상/탐험률 텍스트
 *   7. window.display()
 */

#include "Renderer.hpp"
#include "MathUtils.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 고도 → 색상 매핑
// ─────────────────────────────────────────────────────────────────────────────

sf::Color heightToColor(float h) noexcept {
    h = math::clamp(h, 0.0f, 1.0f);

    // 각 지형 구간 색상 (선형 보간으로 부드럽게 전환)
    // [0.00, 0.20]: 깊은 수면/저지대 — 짙은 청록
    // [0.20, 0.40]: 저지대 평원      — 어두운 녹색
    // [0.40, 0.60]: 구릉지           — 황토색
    // [0.60, 0.80]: 고원/바위        — 갈색
    // [0.80, 1.00]: 산봉우리/눈       — 흰색

    struct ColorStop { float t; sf::Uint8 r, g, b; };
    constexpr ColorStop stops[] = {
        { 0.00f,  20,  50, 80  }, // 저지대 수변 (짙은 파랑)
        { 0.20f,  34,  85, 34  }, // 저지대 초원 (어두운 녹색)
        { 0.40f,  80, 120, 40  }, // 구릉지 (황록)
        { 0.55f, 140, 110, 60  }, // 황토 구릉
        { 0.70f, 100,  75, 50  }, // 고원 (갈색)
        { 0.85f, 160, 150, 140 }, // 바위 (회갈색)
        { 1.00f, 240, 240, 248 }, // 눈 덮인 봉우리 (흰색)
    };
    constexpr int N = sizeof(stops) / sizeof(stops[0]);

    // 이분 탐색으로 구간 찾기
    int lo = 0, hi = N - 2;
    for (int i = 0; i < N - 1; ++i) {
        if (h <= stops[i + 1].t) { lo = i; hi = i + 1; break; }
    }
    const float span = stops[hi].t - stops[lo].t;
    const float t    = (span > 1e-6f) ? (h - stops[lo].t) / span : 0.0f;

    const auto lerp8 = [](sf::Uint8 a, sf::Uint8 b, float t) -> sf::Uint8 {
        return static_cast<sf::Uint8>(a + static_cast<int>((static_cast<int>(b) - a) * t));
    };

    return sf::Color{
        lerp8(stops[lo].r, stops[hi].r, t),
        lerp8(stops[lo].g, stops[hi].g, t),
        lerp8(stops[lo].b, stops[hi].b, t),
        255u
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// 생성자
// ─────────────────────────────────────────────────────────────────────────────

Renderer::Renderer(const RLEnvironment& env,
                   unsigned winWidth,
                   unsigned winHeight,
                   const std::string& title)
    : m_env(env)
    , m_window(sf::VideoMode(winWidth, winHeight), title,
               sf::Style::Titlebar | sf::Style::Close)
    , m_cellW(static_cast<float>(winWidth)  / static_cast<float>(MAP_W))
    , m_cellH(static_cast<float>(winHeight) / static_cast<float>(MAP_H))
    , m_fontLoaded(false)
{
    m_window.setFramerateLimit(60u);

    // ── 지형 메시 초기 생성 (한 번만) ────────────────────────────────────────
    buildTerrainMesh();

    // ── 안개/FOV VertexArray 예약 ─────────────────────────────────────────────
    m_fogMesh.setPrimitiveType(sf::Quads);
    m_fogMesh.resize(static_cast<size_t>(MAP_W * MAP_H * 4)); // 4정점/격자

    m_fovMesh.setPrimitiveType(sf::TriangleFan);
    // 에이전트 위치(1) + 광선 종점들(FOV_ANGLE_SAMPLES) + 닫는 정점(1)
    m_fovMesh.resize(static_cast<size_t>(FOV_ANGLE_SAMPLES + 2));

    // ── 폰트 로드 시도 ────────────────────────────────────────────────────────
    // Windows 기본 폰트 경로 목록
    const std::vector<std::string> fontPaths = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "assets/fonts/Roboto-Regular.ttf"
    };
    for (const auto& fp : fontPaths) {
        if (m_font.loadFromFile(fp)) {
            m_fontLoaded = true;
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// handleEvents()
// ─────────────────────────────────────────────────────────────────────────────

bool Renderer::handleEvents() {
    m_pendingAction = -1; // 이번 프레임 행동 초기화

    sf::Event event{};
    while (m_window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            m_window.close();
            return false;
        }
        // 키보드 입력 → 행동 매핑 (수동 조작 모드)
        if (event.type == sf::Event::KeyPressed) {
            switch (event.key.code) {
                case sf::Keyboard::Up:    m_pendingAction = 0; break; // 전진
                case sf::Keyboard::Left:  m_pendingAction = 1; break; // 좌회전
                case sf::Keyboard::Right: m_pendingAction = 2; break; // 우회전
                case sf::Keyboard::Escape:
                    m_window.close();
                    return false;
                default: break;
            }
        }
    }
    return m_window.isOpen();
}

int Renderer::pollAction() {
    const int a = m_pendingAction;
    m_pendingAction = -1;
    return a;
}

// ─────────────────────────────────────────────────────────────────────────────
// render()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::render(const StepResult& lastResult) {
    m_window.clear(sf::Color(15, 15, 20)); // 거의 검은 배경

    drawTerrain();
    drawFogOfWar();
    drawFOV();
    drawAgent();
    drawHUD(lastResult);

    m_window.display();
}

// ─────────────────────────────────────────────────────────────────────────────
// buildTerrainMesh() — 정적 지형 메시 생성
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildTerrainMesh() {
    /*
     * 각 격자를 sf::Quad(4정점) 하나로 표현합니다.
     * Quad 정점 순서: 좌상 → 우상 → 우하 → 좌하 (CCW)
     * 총 정점 수: 100 × 100 × 4 = 40,000
     * → 단 하나의 draw 콜로 전체 지형 렌더링
     */
    m_terrainMesh.setPrimitiveType(sf::Quads);
    m_terrainMesh.resize(static_cast<size_t>(MAP_W * MAP_H * 4));

    const Terrain& terrain = m_env.terrain();

    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            const size_t idx = static_cast<size_t>((row * MAP_W + col) * 4);

            const float px = static_cast<float>(col)   * m_cellW;
            const float py = static_cast<float>(row)   * m_cellH;
            const float qx = static_cast<float>(col+1) * m_cellW;
            const float qy = static_cast<float>(row+1) * m_cellH;

            const float h    = terrain.heightAt(col, row);
            const sf::Color c = heightToColor(h);

            m_terrainMesh[idx + 0].position = {px, py};
            m_terrainMesh[idx + 1].position = {qx, py};
            m_terrainMesh[idx + 2].position = {qx, qy};
            m_terrainMesh[idx + 3].position = {px, qy};

            m_terrainMesh[idx + 0].color = c;
            m_terrainMesh[idx + 1].color = c;
            m_terrainMesh[idx + 2].color = c;
            m_terrainMesh[idx + 3].color = c;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawTerrain()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawTerrain() {
    m_window.draw(m_terrainMesh);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFogOfWar()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawFogOfWar() {
    /*
     * 가시성 맵(visibleMap)이 false인 격자 위에
     * 반투명 검은색 쿼드를 그려 "안개" 효과를 냅니다.
     *
     * 최적화: 보이지 않는 격자만 실제 색상(alpha=210)으로 설정하고
     * 보이는 격자는 완전 투명(alpha=0)으로 설정 → 단일 draw 콜 유지
     */
    const auto& visMap = m_env.agent().visibleMap();
    constexpr sf::Uint8 FOG_ALPHA    = 210u; // 안개 불투명도
    const sf::Color FOG_COLOR   = {0u, 0u, 0u, FOG_ALPHA};
    const sf::Color CLEAR_COLOR = {0u, 0u, 0u, 0u};

    for (int row = 0; row < MAP_H; ++row) {
        for (int col = 0; col < MAP_W; ++col) {
            const size_t idx = static_cast<size_t>((row * MAP_W + col) * 4);

            const float px = static_cast<float>(col)   * m_cellW;
            const float py = static_cast<float>(row)   * m_cellH;
            const float qx = static_cast<float>(col+1) * m_cellW;
            const float qy = static_cast<float>(row+1) * m_cellH;

            const bool visible = visMap[static_cast<size_t>(row)][static_cast<size_t>(col)];
            const sf::Color& fc = visible ? CLEAR_COLOR : FOG_COLOR;

            m_fogMesh[idx + 0] = {{px, py}, fc};
            m_fogMesh[idx + 1] = {{qx, py}, fc};
            m_fogMesh[idx + 2] = {{qx, qy}, fc};
            m_fogMesh[idx + 3] = {{px, qy}, fc};
        }
    }

    m_window.draw(m_fogMesh);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFOV()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawFOV() {
    /*
     * TriangleFan을 사용하여 부채꼴 FOV 시야 영역을 렌더링합니다.
     *
     * 정점 배열 구조:
     *   [0]         = 에이전트 위치 (팬의 중심)
     *   [1..N]      = 각 광선의 종점
     *   [N+1]       = [1]과 동일 (팬 닫기용, 필요시)
     *
     * TriangleFan: [0, 1, 2], [0, 2, 3], ... 자동으로 삼각형 생성
     */

    const auto& hits = m_env.agent().rayHits();
    if (hits.empty()) return;

    const sf::Vector2f center = gridToPixel(m_env.agent().x(), m_env.agent().y());
    const sf::Color FOV_COLOR{255u, 220u, 50u, 45u}; // 반투명 황금색

    const size_t totalVerts = hits.size() + 1u;
    m_fovMesh.resize(totalVerts);

    // 중심점
    m_fovMesh[0].position = center;
    m_fovMesh[0].color    = {255u, 220u, 50u, 80u}; // 조금 더 불투명한 중심

    // 광선 종점들
    for (size_t i = 0; i < hits.size(); ++i) {
        m_fovMesh[i + 1].position = gridToPixel(hits[i].endX, hits[i].endY);
        m_fovMesh[i + 1].color    = FOV_COLOR;
    }

    m_window.draw(m_fovMesh);

    // ── 광선 윤곽선 (가장자리 광선만 별도 선으로 그려 선명하게) ──────────────
    if (hits.size() >= 2) {
        const sf::Color LINE_COLOR{255u, 240u, 100u, 160u};

        // 첫 번째 광선
        sf::Vertex line1[2] = {
            {center,                                           LINE_COLOR},
            {gridToPixel(hits.front().endX, hits.front().endY), LINE_COLOR}
        };
        m_window.draw(line1, 2, sf::Lines);

        // 마지막 광선
        sf::Vertex line2[2] = {
            {center,                                          LINE_COLOR},
            {gridToPixel(hits.back().endX, hits.back().endY), LINE_COLOR}
        };
        m_window.draw(line2, 2, sf::Lines);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawAgent()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawAgent() {
    const sf::Vector2f center = gridToPixel(m_env.agent().x(), m_env.agent().y());
    const float theta         = m_env.agent().theta();

    // ── 에이전트 본체 (빨간색 원) ────────────────────────────────────────────
    constexpr float AGENT_RADIUS = 5.0f;
    sf::CircleShape agentCircle(AGENT_RADIUS);
    agentCircle.setFillColor(sf::Color{230u, 40u, 40u, 255u});
    agentCircle.setOutlineColor(sf::Color{255u, 150u, 150u, 255u});
    agentCircle.setOutlineThickness(1.5f);
    agentCircle.setOrigin(AGENT_RADIUS, AGENT_RADIUS);
    agentCircle.setPosition(center);
    m_window.draw(agentCircle);

    // ── 방향 표시 선 ─────────────────────────────────────────────────────────
    constexpr float DIR_LENGTH = 12.0f;
    const sf::Vector2f dirEnd = {
        center.x + std::cos(theta) * DIR_LENGTH,
        center.y + std::sin(theta) * DIR_LENGTH
    };
    sf::Vertex dirLine[2] = {
        {center, sf::Color{255u, 255u, 100u, 255u}},
        {dirEnd,  sf::Color{255u, 255u, 100u, 180u}}
    };
    m_window.draw(dirLine, 2, sf::Lines);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawHUD()
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawHUD(const StepResult& result) {
    if (!m_fontLoaded) return;

    const float exploredRatio = static_cast<float>(m_env.agent().totalExplored())
                              / static_cast<float>(MAP_W * MAP_H);

    // ── 문자열 조합 ──────────────────────────────────────────────────────────
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "[Step " << result.step << " / " << m_env.maxSteps() << "]\n"
        << "Pos:  (" << m_env.agent().x()   << ", " << m_env.agent().y()   << ")\n"
        << "Dir:  " << math::toDeg(m_env.agent().theta()) << " deg\n"
        << "Alt:  " << m_env.agent().currentHeight()    << "\n"
        << "Explored: " << (exploredRatio * 100.0f) << "%\n"
        << "Reward:   " << result.reward               << "\n"
        << "Total R:  " << m_env.totalReward()         << "\n"
        << "New cells:" << result.newExplored          << "\n"
        << "\n"
        << "[Keys]\n"
        << "Up: Forward\n"
        << "Left/Right: Rotate\n"
        << "Esc: Quit";

    // ── 배경 패널 ────────────────────────────────────────────────────────────
    sf::RectangleShape panel({190.0f, 200.0f});
    panel.setFillColor({0u, 0u, 0u, 160u});
    panel.setOutlineColor({80u, 80u, 80u, 200u});
    panel.setOutlineThickness(1.0f);
    panel.setPosition(6.0f, 6.0f);
    m_window.draw(panel);

    // ── 텍스트 ───────────────────────────────────────────────────────────────
    sf::Text text;
    text.setFont(m_font);
    text.setString(oss.str());
    text.setCharacterSize(12u);
    text.setFillColor(sf::Color{220u, 220u, 220u, 255u});
    text.setPosition(10.0f, 10.0f);
    m_window.draw(text);

    // ── 탐험률 진행 바 ────────────────────────────────────────────────────────
    constexpr float BAR_Y   = 214.0f;
    constexpr float BAR_X   = 6.0f;
    constexpr float BAR_W   = 190.0f;
    constexpr float BAR_H   = 8.0f;

    // 배경 바
    sf::RectangleShape barBg({BAR_W, BAR_H});
    barBg.setFillColor({40u, 40u, 40u, 200u});
    barBg.setPosition(BAR_X, BAR_Y);
    m_window.draw(barBg);

    // 진행 바
    sf::RectangleShape barFill({BAR_W * exploredRatio, BAR_H});
    barFill.setFillColor({80u, 200u, 120u, 230u});
    barFill.setPosition(BAR_X, BAR_Y);
    m_window.draw(barFill);
}

// ─────────────────────────────────────────────────────────────────────────────
// gridToPixel() — 격자 → 픽셀 좌표 변환
// ─────────────────────────────────────────────────────────────────────────────

sf::Vector2f Renderer::gridToPixel(float col, float row) const noexcept {
    return {
        (col + 0.5f) * m_cellW,
        (row + 0.5f) * m_cellH
    };
}

} // namespace rl_fov
