#include "videoplayer.h"
#include <thread>
#include <QThread>
#include <QDebug>

#define AUDIO_MAX_PKT_SIZE 1000
#define VIDEO_MAX_PKT_SIZE 500

/**
 * 负责预处理视频数据(解封装\编解码流数据)
*/
#pragma mark - 构造 析构
VideoPlayer::VideoPlayer(QObject *parent) : QObject(parent)
{
    // 初始化SDL
    if (SDL_Init(SDL_INIT_AUDIO)) {
        qDebug() << "sdl init error: " << SDL_GetError();
        return;
    }

    // 创建音视频包列表
    _aPktList = new std::list<AVPacket>();
    _vPktList = new std::list<AVPacket>();
    // 创建音视频包列表锁
    _aMutex = new CondMutex();
    _vMutex = new CondMutex();

}
VideoPlayer::~VideoPlayer()
{
    // 断开信号, 防止ui界面已经释放了,player发送信息
    disconnect();
    stop();

    delete  _aPktList;
    delete  _vPktList;
    delete  _aMutex;
    delete  _vMutex;
    SDL_Quit();
}
#pragma mark - 公有方法
void VideoPlayer::play() {
    if (_state == Playing) return;

    if (_state == Stopped) {
        std::thread([this]() {
            readFile();
        }).detach();
    }else {
        setState(Playing);
    }

}

void VideoPlayer::pause() {
    if (_state != Playing) return;
    setState(Paused);
}

void VideoPlayer::stop() {
    if (_state == Stopped) return;

    setState(Stopped);
    // 释放资源,
    free();
    // 多线程下, 一种方法: 延迟等待其他线程走完一圈流程在释放,
//    std::thread([this](){
//        SDL_Delay(100);
//        free();
//    }).detach();
}

bool VideoPlayer::isPlaying() {
    return _state == Playing;
}

VideoPlayer::State VideoPlayer::getStatc() {
    return _state;
}
void VideoPlayer::setFilename(QString filename) {
    char *name = filename.toUtf8().data();
    // strlen只获取全部字符数量, 加1是因为还有\0结束符
    memcpy(_filename, name, strlen(name) + 1);
}
int64_t VideoPlayer::getDuration() {
    // 从ffmpeg的时间戳转化为显示时间秒
    // 时间戳 * time_base(时间戳单位)
    int duration = _fmtCxt->duration * av_q2d(AV_TIME_BASE_Q);
    return _fmtCxt ? duration : 0;
}
void VideoPlayer::setVolume(int volume) {
    _volume = volume;
}
int VideoPlayer::getVolume() {
    return _volume;
}
void VideoPlayer::setMute(bool mute) {
    _mute = mute;
}
bool VideoPlayer::isMute() {
    return _mute;
}
int64_t VideoPlayer::getTime() {
    return round(_aTime);
}
int64_t VideoPlayer::setTime(int time) {
    _seekTime = time;
    qDebug() << "setTime" << time;
}
#pragma mark - 私有方法
void VideoPlayer::setState(State state) {
    if (_state == state) return;
    _state = state;

    // 播放状态改变发送信号
    emit videoStatcChanged(this);
}
void VideoPlayer::readFile() {

    // 返回结果
    int ret = 0;
    // 创建解封装上下文
    ret  = avformat_open_input(&_fmtCxt, _filename, nullptr, nullptr);
    END(avformat_open_input);

    // 检索音频,视频流信息, 比如音频 采样率 声道 采样格式 比特率, 视频 宽高 存储格式等等
    ret = avformat_find_stream_info(_fmtCxt, nullptr);
    END(avformat_find_stream_info);

    // 打印流信息到控制台(这是用于调试的)
    // 流信息都dump到stderr
    av_dump_format(_fmtCxt, 0, _filename, 0);
    // 刷新打印缓冲区
    fflush(stderr);

    // 初始化音视频信息
    _hasAudio = initAudioInfo() >= 0;
    _hasVideo = initVideoInfo() >= 0;
    // 都不是音频和视频文件返回
    if (!_hasAudio && !_hasVideo) {
        fataError();
        return;
    }

    // 音视频初始化完毕改变状态play
    setState(Playing);

    // 音视频初始化完毕
    emit videoInitFinished(this);

    // 音频子线程开始播放 0是取消暂停, 1是暂停播放.
    SDL_PauseAudio(0);

    // 开启新线程, 视频像素格式开始解码
    std::thread([this](){
        decodervideo();
    }).detach();

    // 从输入文件流数据中读取数据
    AVPacket pkt;
    while (_state != Stopped) {

        if (_seekTime >= 0) {
            int streamId;
            if (_hasAudio) { // 优先考虑音频流
                streamId = _aStream->index;
            }else {
                streamId = _vStream->index;
            }
            // 现实时间转为时间戳
            int64_t seekTimestamp = _seekTime  / av_q2d(_fmtCxt->streams[streamId]->time_base);
            // seek操作
            // 可以认为seek其中一条流,
            ret = av_seek_frame(_fmtCxt,
                                streamId,
                                seekTimestamp,
                                AVSEEK_FLAG_BACKWARD);
            if (ret < 0) {
                qDebug() << "seek失败" << seekTimestamp;
                _seekTime = -1;
                CONTINUE(av_seek_frame);
            }else {
                qDebug() << "seek成功" << seekTimestamp << _seekTime;
                _vSeekTime = _seekTime;
                _aSeekTime = _seekTime;
                _seekTime = -1;
                // 清除之前的pkt列表
                // 这里要先处理之前pkt资源列表再恢复时钟, 不然往回seek时候会出现视频解码线程抢到一部分旧pkt包,
                // 而之后时钟已经重置过了, vTime用回旧pkt包时钟, _aTime用新pkt包时钟, 造成为了同步音视频, 视频不断在等待音频.
                clearAudioList();
                clearVideoList();
                // 恢复pkt时钟, 防止视频解码pkt时, 还用seek前的时钟判断是否音视频同步, 出现不断等待循环
                _aTime = 0;
                _vTime = 0;
            }
        }


        // 因为av_read_frame读取资源很快放入资源列表, 防止list资源列表太多暂用内存太多
        if (_aPktList->size() >= AUDIO_MAX_PKT_SIZE
                || _vPktList->size() >= VIDEO_MAX_PKT_SIZE) {
//            SDL_Delay(10);
            continue;
        }

        ret = av_read_frame(_fmtCxt, &pkt);
        if (ret == 0) {
            if (pkt.stream_index == _aStream->index) {// 音频数据
                addAudioPkt(pkt);
            }else if (pkt.stream_index == _vStream->index) {// 视频数据
                addVideoPkt(pkt);
            }else {// 非音频 视频不处理, 释放pkt
                av_packet_unref(&pkt);
            }

        }else if(ret == AVERROR_EOF) {// 读到文件尾部
            // 需要注意,因为读取和解码不同线程,读到文件尾部不代表音视频播放完毕
//            qDebug() << "读到文件尾部..";
            // 有seek功能这里不能break出循环了
//            break;

            // 播放完成停止
            if (_vPktList->size() == 0 && _aPktList->size() == 0) {
                // 说明正常播放完毕
                _fmtCxtCanFree = true;
                break;
            }
        } else {
            // 如果播放期间某个包出现错误, 继续处理先.这不影响整体播放就好
            ERROR_BUF(ret);
            qDebug() << "av_read_frame error:" << errBuff;
            continue;
        }
    }
    // 标记fmtCxt可以释放
    if (_fmtCxtCanFree) {
        stop();
    }else {
        _fmtCxtCanFree = true;
    }

}

void VideoPlayer::free() {
    while(_hasAudio && !_aCanFree);
    while(_hasVideo && !_vCanFree);
    while(!_fmtCxtCanFree);
    avformat_close_input(&_fmtCxt);
    _fmtCxtCanFree = false;
    _seekTime = -1;

    freeAudio();
    freeVideo();
}

void VideoPlayer::fataError() {
    setState(Stopped);
    emit VideoPlayer::videoPlayFalied(this);
    free();
}

// 初始化解码器
int VideoPlayer::initDecoder(AVCodecContext **decodeCxt ,
                             AVMediaType type,
                             AVStream **stream) {

    int ret = 0;
    // 寻找合适的流信息, 返回对应的流索引
    ret = av_find_best_stream(_fmtCxt, type,
                            // 后面参数先参考官方怎么传
                            -1, -1, nullptr, 0);
    RET(av_find_best_stream);

    int streamIdx = ret;
    // 根据返回的流索引, 查看是否存在对应的流
    *stream = _fmtCxt->streams[streamIdx];
    if (!*stream) {
        qDebug() << "stream is empty";
        return -1;
    }

    // 为流查找适合的解码器
    AVCodec *decoder = (AVCodec *)avcodec_find_decoder((*stream)->codecpar->codec_id);
    if (!decoder) {
        qDebug() << "decoder not find";
        return -1;
    }

    // 初始化解码上下文, 打开解码器
    *decodeCxt = avcodec_alloc_context3(decoder);
    if (!*decodeCxt) {
        qDebug() << "avcodec_alloc_context3 error";
        return -1;
    }

    // 从流中拷贝信息到解码上下文
    // 换句话说就是像之前aac或yuv解码一样,要设置解码上下文参数.
    ret = avcodec_parameters_to_context(*decodeCxt, (*stream)->codecpar);
    RET(avcodec_parameters_to_context);

    // 打开解码器
    ret = avcodec_open2(*decodeCxt, decoder, nullptr);
    RET(avcodec_open2);

    return 0;
}

