/**
 * @file VideoCapture.hpp
 * @brief OpenCV VideoWriter를 이용한 프레임 캡처 및 영상 저장 모듈
 *
 * captureFrame():
 *   SFML RenderWindow → sf::Texture → sf::Image → cv::Mat(BGRA→BGR 변환)
 *   → VideoWriter에 프레임 추가
 *
 * 의존성:
 *   - SFML (Graphics)
 *   - OpenCV (core, videoio, imgproc)
 *
 * 사용 예:
 * @code
 *   VideoCapture vc("output.avi", 60.0, 800, 800);
 *   // 메인 루프 내:
 *   vc.captureFrame(window);
 *   // 루프 종료 후:
 *   vc.finalize();
 * @endcode
 */

#pragma once

#include <SFML/Graphics.hpp>

// OpenCV가 설치된 경우에만 활성화
// 설치되지 않은 환경에서도 빌드 가능하도록 조건부 컴파일 사용
#ifdef USE_OPENCV
#  include <opencv2/core.hpp>
#  include <opencv2/videoio.hpp>
#  include <opencv2/imgproc.hpp>
#endif

#include <string>
#include <cstdint>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// VideoCapture 클래스
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class VideoCapture
 * @brief SFML 창 화면을 OpenCV VideoWriter로 저장하는 유틸리티 클래스
 *
 * OpenCV가 없는 환경에서는 빈 구현으로 컴파일되어 오류 없이 동작합니다.
 */
class VideoCapture {
public:
    // ── 생성자 / 소멸자 ──────────────────────────────────────────────────────

    /**
     * @brief VideoCapture 초기화
     * @param outputPath  저장할 영상 파일 경로 (예: "sim_output.avi")
     * @param fps         영상 프레임레이트 (기본 60)
     * @param width       프레임 가로 픽셀
     * @param height      프레임 세로 픽셀
     * @param enabled     false이면 캡처를 비활성화 (성능 우선 모드)
     */
    explicit VideoCapture(const std::string& outputPath = "sim_output.avi",
                          double             fps        = 60.0,
                          int                width      = 800,
                          int                height     = 800,
                          bool               enabled    = true);

    ~VideoCapture();

    // ── 핵심 인터페이스 ──────────────────────────────────────────────────────

    /**
     * @brief SFML 창의 현재 화면을 캡처하여 VideoWriter에 추가
     *
     * 내부 동작 (USE_OPENCV 정의된 경우):
     *   1. sf::Texture 로 현재 창 내용 캡처
     *   2. sf::Image 로 픽셀 데이터 추출 (RGBA)
     *   3. cv::Mat 생성 후 RGBA → BGR 변환 (OpenCV 기본 포맷)
     *   4. VideoWriter::write() 호출
     *
     * @param window  캡처할 SFML 렌더 창
     */
    void captureFrame(sf::RenderWindow& window);

    /**
     * @brief VideoWriter를 닫고 파일을 디스크에 저장 완료
     */
    void finalize();

    // ── 상태 접근자 ──────────────────────────────────────────────────────────

    /// 현재까지 기록된 프레임 수
    [[nodiscard]] uint64_t frameCount() const noexcept { return m_frameCount; }
    /// 캡처 활성화 여부
    [[nodiscard]] bool     isEnabled()  const noexcept { return m_enabled; }
    /// VideoWriter가 정상 열려 있는지
    [[nodiscard]] bool     isOpen()     const noexcept;

    // ── 프레임 변환 유틸리티 (static, 외부에서 직접 사용 가능) ──────────────

#ifdef USE_OPENCV
    /**
     * @brief SFML 창 → OpenCV cv::Mat 변환
     *
     * SFML 픽셀 포맷이 RGBA이므로 OpenCV BGR로 변환합니다.
     * 알파 채널은 제거됩니다.
     *
     * @param window  소스 SFML 창
     * @return BGR cv::Mat (CV_8UC3)
     */
    [[nodiscard]] static cv::Mat windowToMat(sf::RenderWindow& window);

    /**
     * @brief sf::Image → cv::Mat 변환
     * @param image SFML 이미지 (RGBA)
     * @return BGR cv::Mat (CV_8UC3)
     */
    [[nodiscard]] static cv::Mat imageToMat(const sf::Image& image);
#endif

private:
    std::string m_outputPath;  ///< 출력 파일 경로
    double      m_fps;         ///< 영상 FPS
    int         m_width;       ///< 프레임 가로
    int         m_height;      ///< 프레임 세로
    bool        m_enabled;     ///< 캡처 활성화 여부
    uint64_t    m_frameCount{0}; ///< 기록된 프레임 수

#ifdef USE_OPENCV
    cv::VideoWriter m_writer;  ///< OpenCV VideoWriter 인스턴스
#endif
};

} // namespace rl_fov
