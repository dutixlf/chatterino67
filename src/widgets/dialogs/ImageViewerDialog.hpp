// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDialog>

class QLabel;
class QScrollArea;
class QWheelEvent;

namespace chatterino {

class Image;
using ImagePtr = std::shared_ptr<Image>;

/// @brief A floating image viewer for inline eblo.id images.
///
/// The window is frameless. The image is fitted to the window while keeping the
/// aspect ratio. Holding Ctrl and scrolling zooms in/out; scrollbars appear
/// when the zoomed image is larger than the window. When zoomed in, the image
/// can be panned by holding the left mouse button and dragging. Right-click or
/// Esc closes the viewer.
class ImageViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageViewerDialog(ImagePtr image, QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updatePixmap();
    void applyZoom(double factor, const QPointF &mousePos);
    void handleZoomWheel(QWheelEvent *event);
    void startDrag(const QPointF &globalPos);
    void installEventFilterRecursively(QWidget *widget);

    ImagePtr image_;
    QScrollArea *scrollArea_;
    QLabel *label_;

    double zoomFactor_ = -1.0;
    bool panning_ = false;
    QPointF dragStartGlobal_;
    int dragStartScrollX_ = 0;
    int dragStartScrollY_ = 0;

    static constexpr double ZOOM_STEP = 1.25;
    static constexpr double MIN_ZOOM = 0.1;
    static constexpr double MAX_ZOOM = 10.0;
    static constexpr double MAX_WINDOW_SCREEN_FRACTION = 0.8;
    static constexpr int PAN_DEADZONE_PX = 4;
};

}  // namespace chatterino
