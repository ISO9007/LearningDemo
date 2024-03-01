#ifndef VIDEOSLIDER_H
#define VIDEOSLIDER_H

#include <QSlider>


class VideoSlider : public QSlider
{
    Q_OBJECT
public:
    explicit VideoSlider(QWidget *parent = nullptr);
    void mousePressEvent(QMouseEvent *ev);
signals:
    void clicked(VideoSlider *slider);

};

#endif // VIDEOSLIDER_H
