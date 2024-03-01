#include "videowidget.h"
#include <QDebug>
#include <QPainter>

/**
 * 负责显示(渲染)数据
*/
VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent)
{
    // 背景黑色
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("background: back");
    qDebug() << "VideoWidget";
}
VideoWidget::~VideoWidget() {
    freeImage();
}
// 渲染
void VideoWidget::paintEvent(QPaintEvent *event) {
    if (!_frame) return;
    QPainter(this).drawImage(_rect, *_frame);
}
void VideoWidget::onPlayerVideoStatc(VideoPlayer *player) {
    if (player->getStatc() != VideoPlayer::Stopped) return;
    freeImage();
    update();
    qDebug() << "VideoWidget::onPlayerVideoStatc";
}
// 接受视频frame
void VideoWidget::frameDecoded(VideoPlayer *player,
                               uint8_t *data,
                               VideoPlayer::VideoSwsSpec &spec) {

    if (player->getStatc() == VideoPlayer::Stopped) return;
    // 释放上一张图片
    freeImage();
    // 创建新图片
    if (data != nullptr) {

        _frame = new QImage(data,
                            spec.width ,spec.height,
                            QImage::Format_RGB888);

        // 视频适应播放器宽高比
        int w = width();
        int h = height();

        int dstX = 0;
        int dstY = 0;
        int dstW = spec.width;
        int dstH = spec.height;

//        qDebug() << dstX << dstY << dstW << dstH;
        // 如果视频宽或者高超过播放器
        if (dstW > w || dstH > h) {
            /*
             * 视频宽高比大于播放器宽高比, 这里可以说视频的宽比播放器宽大,因为数学上比数意义等于每份高的宽多少, 比数大在也可以说谁的宽度大
             * dstW / dstH > w / h 方便计算小数位, 进行换算等价 dstw * h > w * dstH
             */
            if (dstW * h > w * dstH) {
                dstH = w * dstH / dstW;
                dstW = w;
            }else { // 视频宽高比小于播放器宽高比, 也就是说视频高比播放器长
                dstW = h * dstW / dstH;
                dstH = h;
            }

        }
        // 设置屏幕居中
        dstX = (w - dstW) >> 1;
        dstY = (h - dstH) >> 1;
        _rect = QRect(dstX, dstY, dstW, dstH);


    }

    // 重绘
    update();

}

void VideoWidget::freeImage() {
    if (_frame) {
        av_free(_frame->bits());
        delete _frame;
        _frame = nullptr;
    }
}
