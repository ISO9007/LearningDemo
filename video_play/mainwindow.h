#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "videoplayer.h"
#include "videoslider.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void on_openFileBtn_clicked();

//    void on_currentSlider_valueChanged(int value);

    void on_volumeSlider_valueChanged(int value);

    void onPlayerVideoStatc(VideoPlayer *player);

    void onPlayerTimeChanged(VideoPlayer *player);

    void onPlayerVideoInitFinished(VideoPlayer *player);

    void onPlayerPlayFalied(VideoPlayer *player);

    void on_playBtn_clicked();

    void on_stopBtn_clicked();

    void on_muteBtn_clicked();

    void on_timeSlider_valueChanged(int value);

    void onPlayerTimeSliderClicked(VideoSlider *slider);

private:
    Ui::MainWindow *ui;
    VideoPlayer *_player = nullptr;
    QString getTimeText(int duration);
};
#endif // MAINWINDOW_H
