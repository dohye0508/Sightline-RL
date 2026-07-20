/**
 * @file main.cpp
 * @brief 강화학습용 펄린 노이즈 지형 내 시야각(FOV) 제약 탐색 시뮬레이터 메인 엔트리
 *
 * 이 메인 루프는 다음 두 가지 모드를 지원합니다:
 * 1. 수동 모드 (Manual Mode): 키보드 방향키(Up: 전진, Left/Right: 회전)로 직접 에이전트를 조작하며 보상과 상태 변화를 관찰합니다.
 * 2. 자동/학습 예시 모드 (Auto Explorer Mode): 에이전트가 매 스텝마다 휴리스틱 또는 무작위 탐색 정책에 따라 스스로 지형을 탐사합니다.
 *
 * 또한 OpenCV 빌드가 활성화된 경우(USE_OPENCV), 시뮬레이션 화면이 실시간으로 "simulation_output.avi" 영상 파일로 캡처 및 저장됩니다.
 */

#include "RLEnvironment.hpp"
#include "Renderer.hpp"
#include "VideoCapture.hpp"
#include "MathUtils.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace rl_fov;

// ── 탐색 알고리즘 설정 ───────────────────────────────────────────────────────
enum class RunMode {
    Manual,  ///< 키보드 입력 조작
    Auto     ///< 휴리스틱 기반 자동 탐사
};

/**
 * @brief 휴리스틱 기반의 간단한 자동 탐사 규칙 (Rule-based Exploration Policy)
 *
 * 상태 벡터(obs)를 입력받아 다음 행동을 결정합니다:
 * - 61개 광선 데이터 중 가장 멀리 뻗어 나간(장애물이 가장 늦게 나타난) 방향을 찾습니다.
 * - 지형의 고도 정보와 광선 거리를 조합하여, 고도가 높고 탁 트인 쪽으로 선회 및 전진합니다.
 */
int getHeuristicAction(const std::vector<float>& obs, const Agent& agent, const Terrain& terrain) {
    // obs 구조:
    // [0]: x_norm, [1]: y_norm, [2]: cos(theta), [3]: sin(theta), [4]: height_norm
    // [5..65]: 61개 광선 거리 (0.0 ~ 1.0)
    
    constexpr int rayStartIdx = RLEnvironment::OBS_BASE_DIM;
    constexpr int numRays = FOV_ANGLE_SAMPLES;
    
    // 1. 전방 장애물이 너무 가까이 있는지 검사 (가운데 부근 광선들의 거리)
    float minCenterDist = 1.0f;
    for (int i = numRays / 2 - 5; i <= numRays / 2 + 5; ++i) {
        if (obs[rayStartIdx + i] < minCenterDist) {
            minCenterDist = obs[rayStartIdx + i];
        }
    }
    
    // 전방에 벽(고도 차이, 타일 등)이 가까이 있으면 (약 3칸 이내) 회전 시도
    // 전체 맵 대각선이 141.42칸이므로 3칸은 약 0.021 비율입니다.
    if (minCenterDist < 0.021f) {
        // 왼쪽 시야와 오른쪽 시야의 거리 합을 비교하여 더 열린 곳으로 회전
        float leftSum = 0.0f;
        float rightSum = 0.0f;
        for (int i = 0; i < numRays / 2; ++i) {
            leftSum += obs[rayStartIdx + i];
        }
        for (int i = numRays / 2 + 1; i < numRays; ++i) {
            rightSum += obs[rayStartIdx + i];
        }
        
        // 왼쪽이 더 열려있으면 좌회전(1), 오른쪽이 더 열려있으면 우회전(2)
        return (leftSum > rightSum) ? 1 : 2;
    }
    
    // 2. 평상시에는 80% 확률로 전진하고, 20% 확률로 고도가 더 높아지는 방향을 찾아 회전
    // 고도가 높을수록 더 큰 시야 영역을 가집니다.
    static std::mt19937 randGen(1337u);
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    if (dis(randGen) < 0.8f) {
        return 0; // 전진
    }
    
    // 에이전트의 현재 방향 기준 좌/우 방향의 지형 고도 샘플링
    const float currentTheta = agent.theta();
    const float leftCheckTheta = currentTheta - math::toRad(20.0f);
    const float rightCheckTheta = currentTheta + math::toRad(20.0f);
    
    const float lx = agent.x() + 3.0f * std::cos(leftCheckTheta);
    const float ly = agent.y() + 3.0f * std::sin(leftCheckTheta);
    const float rx = agent.x() + 3.0f * std::cos(rightCheckTheta);
    const float ry = agent.y() + 3.0f * std::sin(rightCheckTheta);
    
    const float leftHeight = terrain.heightAtF(lx, ly);
    const float rightHeight = terrain.heightAtF(rx, ry);
    
    if (leftHeight > rightHeight && leftHeight > agent.currentHeight()) {
        return 1; // 좌회전 (높은 지형 방향)
    } else if (rightHeight > leftHeight && rightHeight > agent.currentHeight()) {
        return 2; // 우회전 (높은 지형 방향)
    }
    
    // 기본적으로 전진
    return 0;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "=========================================================\n"
              << " RL Constrained FOV Terrain Exploration Simulator\n"
              << "=========================================================\n"
              << " 외부 라이브러리 최소화 기반 2D 시야각 제약 극복 환경 데모\n\n";

    // ── 실행 모드 설정 선택 ──────────────────────────────────────────────────
    RunMode runMode = RunMode::Auto;
    std::cout << "실행 모드를 선택하세요:\n"
              << "  1. 자동 탐색 모드 (Heuristic Auto Explorer) - [기본값]\n"
              << "  2. 수동 조작 모드 (Manual Control via Keyboard)\n"
              << "선택 (1 또는 2): ";
    
    std::string choice;
    if (std::getline(std::cin, choice)) {
        if (choice == "2") {
            runMode = RunMode::Manual;
            std::cout << "-> 수동 조작 모드로 실행합니다. (방향키 Up/Left/Right 사용)\n";
        } else {
            std::cout << "-> 자동 탐색 모드로 실행합니다.\n";
        }
    }

    float fovAngle = 60.0f;
    std::cout << "에이전트의 FOV 시야각을 입력하세요 (예: 30, 기본값 60): ";
    std::string fovStr;
    if (std::getline(std::cin, fovStr) && !fovStr.empty()) {
        try { fovAngle = std::stof(fovStr); } catch (...) {}
    }
    std::cout << "-> FOV가 " << fovAngle << "도로 설정되었습니다.\n";

    // ── 환경 및 시각화 인스턴스 생성 ─────────────────────────────────────────
    std::random_device rd;
    const uint32_t seed = rd(); // 매번 랜덤한 지형 시드
    
    RewardConfig rCfg;
    rCfg.wExplore = 1.2f;    // 신규 격자 탐험 보상 가중치 증가
    rCfg.wAltitude = 0.8f;   // 고도 가중치 증가 (산 꼭대기로 가도록 유도)
    rCfg.wIdle = 0.15f;      // 제자리 제약 페널티 강화
    rCfg.wRevisit = 0.05f;   // 재방문 패널티

    auto env = std::make_unique<RLEnvironment>(seed, rCfg, 600, fovAngle); // 최대 600 스텝, FOV 전달
    auto obs = env->reset();

    // 800x800 크기로 SFML 렌더 창 생성
    Renderer renderer(*env, 800, 800, "RL Constrained FOV Terrain Explorer");

    // ── 비디오 레코더 초기화 ─────────────────────────────────────────────────
    // OpenCV로 저장하고 싶을 시 USE_OPENCV를 켜고 컴파일합니다.
    // simulation_output.avi 파일에 60FPS로 저장
    VideoCapture videoCapture("simulation_output.avi", 60.0, 800, 800, true);

    StepResult stepResult{};
    stepResult.step = 0;
    stepResult.reward = 0.0f;
    stepResult.done = false;

    // ── 메인 프레임 루프 ─────────────────────────────────────────────────────
    std::cout << "\n시뮬레이터 시작! SFML 렌더 창을 확인하세요.\n"
              << " - ESC 또는 창 닫기 단추로 즉시 종료할 수 있습니다.\n"
              << " - 수동 모드 조작법: 키보드 [↑]: 전진, [←]: 좌회전, [→]: 우회전\n\n";

    sf::Clock stepTimer;
    constexpr float stepDelaySec = 0.05f; // 시뮬레이션 한 스텝당 최소 지연 시간 (초)

    while (renderer.isOpen()) {
        // SFML 창 이벤트 처리 (종점 체크 및 수동 키 바인딩 폴링)
        bool windowOpen = renderer.handleEvents();
        if (!windowOpen) {
            break;
        }

        int action = -1;

        if (runMode == RunMode::Manual) {
            // 수동 조작 모드일 때는 마지막으로 눌린 키 이벤트를 가져옴
            action = renderer.pollAction();
        } else {
            // 자동 탐사 모드에서는 프레임 속도 조절
            if (stepTimer.getElapsedTime().asSeconds() >= stepDelaySec) {
                // heuristic 기반 행동 결정
                action = getHeuristicAction(obs, env->agent(), env->terrain());
                stepTimer.restart();
            }
        }

        // 유효한 행동이 발생했을 시(혹은 자동 모드에서 스텝 타이밍이 되었을 때) 환경 업데이트
        if (action != -1 && !stepResult.done) {
            stepResult = env->step(action);
            obs = stepResult.observation;

            // 디버그 콘솔 출력
            std::cout << "[Step " << stepResult.step << "] Action: " << action
                      << " | Reward: " << stepResult.reward
                      << " | Total R: " << env->totalReward()
                      << " | Explored: " << (static_cast<float>(env->agent().totalExplored()) / (MAP_W * MAP_H) * 100.0f) << "%\n";

            if (stepResult.done) {
                std::cout << "\n===== 에피소드 종료! =====\n"
                          << "최종 탐험 비율: " << (static_cast<float>(env->agent().totalExplored()) / (MAP_W * MAP_H) * 100.0f) << "%\n"
                          << "누적 획득 보상: " << env->totalReward() << "\n"
                          << "Reset을 위해 R 키를 누르거나 수동 리셋 모드입니다.\n\n";
            }
        }

        // 특정 상태에서의 자동 재시작 로직 (자동 탐사 시 계속 이어지도록)
        if (runMode == RunMode::Auto && stepResult.done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            std::cout << "\n--- 환경 리셋 및 새 에피소드 시작 ---\n";
            obs = env->reset();
            stepResult.step = 0;
            stepResult.reward = 0.0f;
            stepResult.done = false;
            stepTimer.restart();
        }

        // 수동 리셋 단축키 (R 키 입력 시 에피소드 리셋)
        static bool rKeyPressedLastFrame = false;
        bool rKeyPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::R);
        if (rKeyPressed && !rKeyPressedLastFrame) {
            std::cout << "\n--- 수동 리셋 ---\n";
            obs = env->reset();
            stepResult.step = 0;
            stepResult.reward = 0.0f;
            stepResult.done = false;
        }
        rKeyPressedLastFrame = rKeyPressed;

        // 렌더링 프레임 갱신
        renderer.render(stepResult);

        // 프레임 캡처 및 영상 파일에 추가
        if (videoCapture.isEnabled()) {
            videoCapture.captureFrame(renderer.window());
        }
    }

    // 영상 캡처 세션 정지 및 자원 정리
    videoCapture.finalize();
    std::cout << "시뮬레이션 프로그램이 정상적으로 종료되었습니다.\n";

    return 0;
}
