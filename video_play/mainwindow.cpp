#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // 注册信号参数类型
    qRegisterMetaType<VideoPlayer::VideoSwsSpec>("VideoSwsSpec&");

    _player = new VideoPlayer();
    connect(_player, &VideoPlayer::videoStatcChanged,
            this, &MainWindow::onPlayerVideoStatc);
    connect(_player,&VideoPlayer::videoInitFinished,
            this, &MainWindow::onPlayerVideoInitFinished);
    connect(_player,&VideoPlayer::videoPlayFalied,
            this, &MainWindow::onPlayerPlayFalied);
    connect(_player, &VideoPlayer::timeChanged,
            this, &MainWindow::onPlayerTimeChanged);

    connect(_player,&VideoPlayer::videoPlayFrameDecoded,
            ui->videoWidget, &VideoWidget::frameDecoded);
    connect(_player,&VideoPlayer::videoStatcChanged,
            ui->videoWidget, &VideoWidget::onPlayerVideoStatc);

    connect(ui->timeSlider, &VideoSlider::clicked,
            this, &MainWindow::onPlayerTimeSliderClicked);

    // 设置音量范围
    ui->volumeSlider->setRange(VideoPlayer::Volume::Min,
                               VideoPlayer::Volume::Max);
    ui->volumeSlider->setValue(VideoPlayer::Volume::Max >> 1);

    setWindowTitle("基于FFmpeg播放器");

}

MainWindow::~MainWindow()
{
    delete _player;
    delete ui;

}


void MainWindow::on_openFileBtn_clicked()
{
    QString filename = QFileDialog::getOpenFileName(nullptr,
                                                    "选择多媒体文件",
                                                    "/Users/iso9007/Desktop/ffmpeg/testMusic",
                                                    "多媒体文件 (*.mp4 *.avi *.mkv *.aac *.mp3)");
    qDebug() << filename;
    if (filename.isEmpty()) return;

    _player->setFilename(filename);
    _player->play();
    // 这是多选
    /*
    QStringList filenames = QFileDialog::getOpenFileNames(nullptr,
                                                          "选择多媒体文件",
                                                          "/Users/iso9007/Desktop/ffmpeg/testMusic",
                                                          "视频文件 (*.mp4 *.avi *.mkv);;音频文件 (*.aac *.mp3)");
    foreach(QString filename, filenames) {
        qDebug() << filename;
    }
    */
}
void MainWindow::onPlayerVideoStatc(VideoPlayer *player) {
    VideoPlayer::State statc = player->getStatc();
    if (statc == VideoPlayer::Playing) {
        ui->playBtn->setText("暂停");
    }else {
        ui->playBtn->setText("播放");
    }

    if (statc == VideoPlayer::Stopped) {
        ui->playBtn->setEnabled(false);
        ui->stopBtn->setEnabled(false);
        ui->timeSlider->setEnabled(false);
        ui->muteBtn->setEnabled(false);
        ui->volumeSlider->setEnabled(false);

        ui->timeSlider->setValue(0);
        ui->durationTime->setText(getTimeText(0));
        // 显示打开文件界面
        ui->playWidget->setCurrentWidget(ui->openFilePage);
    }else {
        ui->playBtn->setEnabled(true);
        ui->stopBtn->setEnabled(true);
        ui->timeSlider->setEnabled(true);
        ui->muteBtn->setEnabled(true);
        ui->volumeSlider->setEnabled(true);
        // 显示视频界面
        ui->playWidget->setCurrentWidget(ui->videoPage);
    }
}
void MainWindow::onPlayerTimeChanged(VideoPlayer *player){
    int second = player->getTime();
    ui->timeSlider->setValue(second);
}
void MainWindow::onPlayerPlayFalied(VideoPlayer *player) {
    QMessageBox::critical(nullptr, "提示","播放出错!!");
}
void MainWindow::onPlayerVideoInitFinished(VideoPlayer *player) {
    int64_t second = player->getDuration();
    // 设置视频拖动slider范围
    ui->timeSlider->setRange(0, second);
    // 设置持续时间
    ui->durationTime->setText(getTimeText(second));

}
void MainWindow::onPlayerTimeSliderClicked(VideoSlider *slider) {
    _player->setTime(slider->value());
}
void MainWindow::on_timeSlider_valueChanged(int value)
{
    ui->currentTime->setText(getTimeText(value));
}
void MainWindow::on_volumeSlider_valueChanged(int value)
{
    QString valueStr = QString("%1").arg(value);
    ui->volumeLabel->setText(valueStr);
    _player->setVolume(value);
}
void MainWindow::on_playBtn_clicked()
{
    VideoPlayer::State statc = _player->getStatc();
    if (statc == VideoPlayer::Playing) {
        _player->pause();
    }else {
        _player->play();
    }
}
void MainWindow::on_stopBtn_clicked()
{
    _player->stop();
}
QString MainWindow::getTimeText(int duration) {

    // 第一种方式
//    QString h = QString("0%1").arg(second / 3600).right(2);
//    QString m = QString("0%1").arg((second / 60) % 60).right(2);
//    QString s = QString("0%1").arg(second % 60).right(2);
//    return QString("%1:%2:%3").arg(h).arg(m).arg(s);

    // 第二种方式,占位符
    QLatin1Char full = QLatin1Char('0');
    return QString("%1:%2:%3")
            // 参数1: 值, 参数2: 多少位 参数3: 哪种进制 参数4: 缺省暂未符
            .arg(duration / 3600, 2, 10 ,full)
            .arg((duration / 60) % 60, 2, 10 ,full)
            .arg(duration % 60, 2, 10 ,full);
}

void MainWindow::on_muteBtn_clicked()
{
    if (_player->isMute()){
        ui->muteBtn->setText("静音");
        _player->setMute(false);
    }else {
        ui->muteBtn->setText("开音");
        _player->setMute(true);
    }
}


