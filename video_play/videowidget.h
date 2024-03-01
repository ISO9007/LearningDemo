#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include "videoplayer.h"

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

signals:
public slots:
    void frameDecoded(VideoPlayer *player, uint8_t *data, VideoPlayer::VideoSwsSpec &vSwsOutSpec);
    void onPlayerVideoStatc(VideoPlayer *player);
private:

    QImage *_frame = nullptr;
    QRect _rect;

    void paintEvent(QPaintEvent *event) override;
    void freeImage();
};

#endif // VIDEOWIDGET_H
