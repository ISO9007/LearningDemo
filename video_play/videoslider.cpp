#include "videoslider.h"
#include <QMouseEvent>
#include <QStyle>

VideoSlider::VideoSlider(QWidget *parent) : QSlider(parent)
{

}

void VideoSlider::mousePressEvent(QMouseEvent *ev) {
    // 根据x值设置对应的value
    // valueRange = max - min;
    // value = min + x / width * valueRange
//    double value = minimum() + (ev->pos().x() * 1.0 / width()) * (maximum() - minimum());
//    setValue(value);
//    qDebug() << "mousePressEvent" << value;

    // 第二种方法计算
    int value = QStyle::sliderValueFromPosition(minimum(),maximum(),
                                                ev->pos().x(), width());
    setValue(value);

    // 调用父类原本事件做其他事情
    QSlider::mousePressEvent(ev);

    // 点击事件
    emit clicked(this);
}
