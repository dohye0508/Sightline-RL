/**
 * @file VideoCapture.cpp
 * @brief VideoCapture 클래스 구현
 *
 * 조건부 컴파일:
 *   - USE_OPENCV 정의됨: 실제 OpenCV VideoWriter로 영상 저장
 *   - USE_OPENCV 미정의: 스텁(stub) 구현으로 경고 없이 컴파일
 */

#include "VideoCapture.hpp"
#include <iostream>
#include <stdexcept>

namespace rl_fov {

// ─────────────────────────────────────────────────────────────────────────────
// 생성자
// ─────────────────────────────────────────────────────────────────────────────

VideoCapture::VideoCapture(const std::string& outputPath,
                           double             fps,
                           int                width,
                           int                height,
                           bool               enabled)
    : m_outputPath(outputPath)
    , m_fps(fps)
    , m_width(width)
    , m_height(height)
    , m_enabled(enabled)
{
#ifdef USE_OPENCV
    if (!m_enabled) return;

    // OpenCV VideoWriter 초기화
    // FOURCC 코덱: XVID (AVI 컨테이너, 범용 호환)
    const int fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');

    m_writer.open(m_outputPath, fourcc, m_fps,
                  cv::Size(m_width, m_height), true /* isColor */);

    if (!m_writer.isOpened()) {
        std::cerr << "[VideoCapture] Warning: Failed to open VideoWriter for '"
                  << m_outputPath << "'\n"
                  << "  VideoWriter가 열리지 않았습니다. 코덱을 확인하세요.\n"
                  << "  캡처가 비활성화됩니다.\n";
        m_enabled = false;
    } else {
        std::cout << "[VideoCapture] Recording to: " << m_outputPath
                  << " (" << m_fps << " fps, "
                  << m_width << "x" << m_height << ")\n";
    }
#else
    if (m_enabled) {
        std::cout << "[VideoCapture] OpenCV not compiled in. "
                  << "영상 저장이 비활성화됩니다.\n"
                  << "  빌드 시 -DUSE_OPENCV 플래그를 추가하세요.\n";
        m_enabled = false;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// 소멸자
// ─────────────────────────────────────────────────────────────────────────────

VideoCapture::~VideoCapture() {
    finalize();
}

// ─────────────────────────────────────────────────────────────────────────────
// captureFrame()
// ─────────────────────────────────────────────────────────────────────────────

void VideoCapture::captureFrame(sf::RenderWindow& window) {
    if (!m_enabled) return;

#ifdef USE_OPENCV
    // SFML 창 → cv::Mat 변환
    cv::Mat frame = windowToMat(window);

    if (frame.empty()) {
        std::cerr << "[VideoCapture] Warning: 빈 프레임 — 건너뜀\n";
        return;
    }

    // VideoWriter에 프레임 추가
    m_writer.write(frame);
    ++m_frameCount;

    // 100 프레임마다 진행 상황 로그
    if (m_frameCount % 100 == 0) {
        std::cout << "[VideoCapture] " << m_frameCount << " frames written\n";
    }
#else
    (void)window; // 미사용 경고 방지
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// finalize()
// ─────────────────────────────────────────────────────────────────────────────

void VideoCapture::finalize() {
#ifdef USE_OPENCV
    if (m_writer.isOpened()) {
        m_writer.release();
        std::cout << "[VideoCapture] Saved " << m_frameCount
                  << " frames to: " << m_outputPath << "\n";
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// isOpen()
// ─────────────────────────────────────────────────────────────────────────────

bool VideoCapture::isOpen() const noexcept {
#ifdef USE_OPENCV
    return m_writer.isOpened();
#else
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// windowToMat() (USE_OPENCV 전용)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef USE_OPENCV
cv::Mat VideoCapture::windowToMat(sf::RenderWindow& window) {
    // ── Step 1: SFML 창 → sf::Texture ───────────────────────────────────────
    sf::Texture texture;
    texture.create(window.getSize().x, window.getSize().y);
    texture.update(window);

    // ── Step 2: sf::Texture → sf::Image (픽셀 버퍼, RGBA) ───────────────────
    const sf::Image sfImage = texture.copyToImage();

    // ── Step 3: sf::Image → cv::Mat (RGBA) ───────────────────────────────────
    return imageToMat(sfImage);
}

cv::Mat VideoCapture::imageToMat(const sf::Image& image) {
    const unsigned w = image.getSize().x;
    const unsigned h = image.getSize().y;

    // SFML 픽셀 포맷: RGBA (8bit × 4ch)
    // OpenCV VideoWriter 기본 포맷: BGR (8bit × 3ch)

    // RGBA 원시 포인터를 감싸는 cv::Mat (복사 없이 참조)
    const cv::Mat rgbaMat(
        static_cast<int>(h),
        static_cast<int>(w),
        CV_8UC4,
        // const_cast: OpenCV는 const 픽셀 포인터를 지원하지 않으므로 필요
        const_cast<sf::Uint8*>(image.getPixelsPtr())
    );

    // RGBA → BGR 변환 (OpenCV imwrite/VideoWriter 호환)
    cv::Mat bgrMat;
    cv::cvtColor(rgbaMat, bgrMat, cv::COLOR_RGBA2BGR);

    return bgrMat;
}
#endif

} // namespace rl_fov
