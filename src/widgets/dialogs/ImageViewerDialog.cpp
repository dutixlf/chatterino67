// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/ImageViewerDialog.hpp"

#include "messages/Image.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace chatterino {

ImageViewerDialog::ImageViewerDialog(ImagePtr image, QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint)
    , image_(std::move(image))
    , scrollArea_(new QScrollArea(this))
    , label_(new QLabel)
{
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(this->scrollArea_);

    this->scrollArea_->setWidgetResizable(false);
    this->scrollArea_->setAlignment(Qt::AlignCenter);
    this->scrollArea_->setStyleSheet(QLatin1String(
        "QScrollArea { background-color: black; border: none; }"
        "QScrollBar { background-color: #222; }"
        "QScrollBar::handle { background-color: #888; border-radius: 4px; }"));
    this->scrollArea_->setWidget(this->label_);

    // Catch events on the scroll-area and every child widget so that wheel and
    // mouse events are not swallowed by the scroll area / scroll bars.
    this->installEventFilterRecursively(this->scrollArea_);
    // Also filter our own events so grabMouse() during a drag still reaches us.
    // ponytail: one eventFilter, two targets; keeps the panning logic in one place.
    this->installEventFilter(this);

    this->label_->setAlignment(Qt::AlignCenter);
    this->label_->setStyleSheet(QLatin1String("background-color: black;"));

    // Size the window to a fraction of the screen, then fit the image inside.
    auto *screen = this->screen();
    if (screen == nullptr)
    {
        screen = QApplication::primaryScreen();
    }
    if (screen != nullptr)
    {
        const auto screenGeo = screen->availableGeometry();
        const QSize windowSize = screenGeo.size() * MAX_WINDOW_SCREEN_FRACTION;
        this->resize(windowSize);
        this->move(screenGeo.center() -
                   QPoint(windowSize.width() / 2, windowSize.height() / 2));
    }

    this->updatePixmap();
    this->show();
}

void ImageViewerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        this->close();
    }
    QDialog::keyPressEvent(event);
}

bool ImageViewerDialog::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    switch (event->type())
    {
        case QEvent::MouseButtonPress: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                this->startDrag(mouseEvent->globalPosition());
                this->grabMouse();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const auto delta =
                mouseEvent->globalPosition() - this->dragStartGlobal_;
            if (!this->panning_ && delta.manhattanLength() > PAN_DEADZONE_PX)
            {
                this->panning_ = true;
            }
            if (this->panning_)
            {
                this->scrollArea_->horizontalScrollBar()->setValue(
                    this->dragStartScrollX_ - static_cast<int>(delta.x()));
                this->scrollArea_->verticalScrollBar()->setValue(
                    this->dragStartScrollY_ - static_cast<int>(delta.y()));
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                this->releaseMouse();
                this->panning_ = false;
                return true;
            }
            if (mouseEvent->button() == Qt::RightButton)
            {
                this->close();
                return true;
            }
            break;
        }
        case QEvent::Wheel: {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            if (wheelEvent->modifiers() & Qt::ControlModifier)
            {
                this->handleZoomWheel(wheelEvent);
                return true;
            }
            break;
        }
        default:
            break;
    }

    return QDialog::eventFilter(watched, event);
}

void ImageViewerDialog::startDrag(const QPointF &globalPos)
{
    this->panning_ = false;
    this->dragStartGlobal_ = globalPos;
    this->dragStartScrollX_ = this->scrollArea_->horizontalScrollBar()->value();
    this->dragStartScrollY_ = this->scrollArea_->verticalScrollBar()->value();
}

void ImageViewerDialog::handleZoomWheel(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        return;
    }

    // Convert the event position to viewport coordinates so zoom is centered
    // on the cursor regardless of which child widget received the wheel event.
    const auto mousePos = QPointF(this->scrollArea_->viewport()->mapFromGlobal(
        event->globalPosition().toPoint()));
    const double factor = delta > 0 ? ZOOM_STEP : 1.0 / ZOOM_STEP;
    this->applyZoom(factor, mousePos);
}

void ImageViewerDialog::updatePixmap()
{
    if (this->image_ == nullptr)
    {
        return;
    }

    auto pixmap = this->image_->pixmapOrLoad();
    if (!pixmap)
    {
        return;
    }

    const auto imageSize = pixmap->size();
    if (imageSize.isEmpty())
    {
        return;
    }

    // On first show, fit the image to the window while keeping aspect ratio.
    if (this->zoomFactor_ < 0.0)
    {
        const auto windowSize = this->size();
        const double zoomX =
            static_cast<double>(windowSize.width()) / imageSize.width();
        const double zoomY =
            static_cast<double>(windowSize.height()) / imageSize.height();
        this->zoomFactor_ = std::min(zoomX, zoomY);
    }

    const QSize scaledSize = imageSize * this->zoomFactor_;
    this->label_->setPixmap(pixmap->scaled(scaledSize, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    this->label_->setFixedSize(scaledSize);
}

void ImageViewerDialog::applyZoom(double factor, const QPointF &mousePos)
{
    const double newZoom =
        std::clamp(this->zoomFactor_ * factor, MIN_ZOOM, MAX_ZOOM);
    if (newZoom == this->zoomFactor_)
    {
        return;
    }

    const auto oldImageSize = this->label_->size();

    const int oldScrollX = this->scrollArea_->horizontalScrollBar()->value();
    const int oldScrollY = this->scrollArea_->verticalScrollBar()->value();

    this->zoomFactor_ = newZoom;
    this->updatePixmap();

    const auto newImageSize = this->label_->size();
    const int newScrollX = static_cast<int>(
        (oldScrollX + mousePos.x()) *
            (newImageSize.width() / static_cast<double>(oldImageSize.width())) -
        mousePos.x());
    const int newScrollY =
        static_cast<int>((oldScrollY + mousePos.y()) *
                             (newImageSize.height() /
                              static_cast<double>(oldImageSize.height())) -
                         mousePos.y());

    this->scrollArea_->horizontalScrollBar()->setValue(newScrollX);
    this->scrollArea_->verticalScrollBar()->setValue(newScrollY);
}

void ImageViewerDialog::installEventFilterRecursively(QWidget *widget)
{
    widget->installEventFilter(this);
    for (auto *child : widget->findChildren<QWidget *>())
    {
        child->installEventFilter(this);
    }
}

}  // namespace chatterino
