#include "native_dialogue_frame.h"
#include "formats.h"
#include "text/text_window.h"
#include <algorithm>
namespace Dialogue {
namespace {
struct Canvas {
    QByteArray pixels = QByteArray(320 * 200, char(0));
    const QByteArray &font;
    static void pixel(void *host, int x, int y, uint8_t color) {
        auto &s = *static_cast<Canvas *>(host);
        if (x >= 0 && y >= 0 && x < 320 && y < 200)
            s.pixels[y * 320 + x] = char(color);
    }
};
QByteArray crop(const QByteArray &pixels) {
    QByteArray result;
    result.reserve(IMPERA_TALK_FRAME_WIDTH * IMPERA_TALK_FRAME_HEIGHT);
    for (int y = 0; y < IMPERA_TALK_FRAME_HEIGHT; ++y)
        result.append(pixels.constData() + (IMPERA_TALK_FRAME_Y + y) * 320 + IMPERA_TALK_FRAME_X,
                      IMPERA_TALK_FRAME_WIDTH);
    return result;
}
} // namespace
QByteArray nativeGameFramePixels(const QByteArray &normalFont, unsigned frameColor) {
    U5::require(normalFont.size() == 1024, "Playtest requires IBM.CH");
    Canvas canvas{QByteArray(320 * 200, char(0)), normalFont};
    ImperaGameFrame frame{};
    frame.host = &canvas;
    frame.blue = frameColor & 15;
    frame.white = 15;
    frame.fill = [](void *host, uint8_t color, int x1, int y1, int x2, int y2) {
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x)
                Canvas::pixel(host, x, y, color);
    };
    frame.line = [](void *host, uint8_t color, int x1, int y1, int x2, int y2) {
        U5::require(x1 == x2 || y1 == y2, "Invalid native frame line");
        for (int y = std::min(y1, y2); y <= std::max(y1, y2); ++y)
            for (int x = std::min(x1, x2); x <= std::max(x1, x2); ++x)
                Canvas::pixel(host, x, y, color);
    };
    frame.corner = [](void *host, uint8_t color, int x, int y, uint8_t ch) {
        auto &s = *static_cast<Canvas *>(host);
        IMPERA_TextGlyph(reinterpret_cast<const uint8_t *>(s.font.constData()), ch * 8, color, 0,
                         x * 8, y * 8, host, Canvas::pixel);
    };
    IMPERA_DrawGameFrame(&frame);
    return canvas.pixels;
}
QImage nativeConversationFrame(const QByteArray &normalFont) {
    auto pixels = crop(nativeGameFramePixels(normalFont));
    QImage result(IMPERA_TALK_FRAME_WIDTH, IMPERA_TALK_FRAME_HEIGHT, QImage::Format_RGB32);
    for (int y = 0; y < result.height(); ++y)
        for (int x = 0; x < result.width(); ++x)
            result.setPixel(x, y,
                            0xff000000u |
                                IMPERA_TextPalette[uint8_t(pixels[y * result.width() + x]) & 15]);
    return result;
}
QByteArray nativeFramedPixels(const QByteArray &normalFont, const QByteArray &textPixels) {
    U5::require(textPixels.size() == 128 * 104, "Invalid native text framebuffer");
    auto pixels = crop(nativeGameFramePixels(normalFont));
    for (int y = 0; y < 104; ++y)
        pixels.replace((y + IMPERA_TALK_FRAME_TEXT_Y) * IMPERA_TALK_FRAME_WIDTH +
                           IMPERA_TALK_FRAME_TEXT_X,
                       128, textPixels.constData() + y * 128, 128);
    return pixels;
}
} // namespace Dialogue
