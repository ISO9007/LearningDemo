#include "videoplayer.h"
#include <QDebug>

int VideoPlayer::initAudioInfo() {
    // 初始化解码器
    int ret = initDecoder(&_aDecodeCxt, AVMEDIA_TYPE_AUDIO, &_aStream);
    RET(initDecoder);

    // 初始化重采样
    ret = initSwr();
    RET(initSwr);

    // 初始化SDL
    ret = initSDL();
    RET(initSDL);

    return 0;
}

int VideoPlayer::initSDL() {
    // 设置参数
    SDL_AudioSpec audioSpec;
    // 采样率
    audioSpec.freq = _audioOutSpec.sampleRate;
    // 声道
    audioSpec.channels = _audioOutSpec.chs;
    // 采样大小 位深度
    audioSpec.format = AUDIO_S16LSB;
    // 音频缓存区有多少音频样本,这个数据决定缓冲区的大小, 必须是2的幂. 一般取值1024, 512
    audioSpec.samples = 512;
    // callBack sdl要数据时就回调他
    audioSpec.callback = VideoPlayer::audioSDLCallbackFunc;
    audioSpec.userdata = this;

    // 打开设备
    int ret = SDL_OpenAudio(&audioSpec, nullptr);
    if (ret) {
        qDebug() << "open audio devices error: " << SDL_GetError() << ret;
        return ret;
    }

    return 0;
}

int VideoPlayer::initSwr() {
    // 设置采样输入格式
    _audioInSpec.sampleRate = _aDecodeCxt->sample_rate;
    _audioInSpec.fmt = _aDecodeCxt->sample_fmt;
    _audioInSpec.chsLayout = _aDecodeCxt->channel_layout;
    _audioInSpec.chs = _aDecodeCxt->channels;
    // 设置采样输出格式
    _audioOutSpec.sampleRate = 44100;
    _audioOutSpec.fmt = AV_SAMPLE_FMT_S16;
    _audioOutSpec.chsLayout = AV_CH_LAYOUT_STEREO;
    _audioOutSpec.chs = av_get_channel_layout_nb_channels(_audioOutSpec.chsLayout);
    // 输出样本帧大小
    _audioOutSpec.bytesPerSampleFrame = _audioOutSpec.chs * av_get_bytes_per_sample(_audioOutSpec.fmt);

    // 创建重采样上下文
    _aSwrCxt = swr_alloc_set_opts(nullptr,
                                  _audioOutSpec.chsLayout, _audioOutSpec.fmt, _audioOutSpec.sampleRate,
                                  _audioInSpec.chsLayout, _audioInSpec.fmt, _audioInSpec.sampleRate,
                                  0, nullptr);
    if (!_aSwrCxt) {
        qDebug() << "swr_alloc_set_opts error";
        return -1;
    }

    // 初始化重采样上下文
    int ret = swr_init(_aSwrCxt);
    RET(swr_init);

    // 初始化Frame
    _aSwrInFrame = av_frame_alloc();
    if (!_aSwrInFrame) {
        qDebug() << "av_frame_alloc error";
        return -1;
    }

    _aSwrOutFrame = av_frame_alloc();
    if (!_aSwrOutFrame) {
        qDebug() << "av_frame_alloc error";
        return -1;
    }

    // 由于av_frame_alloc只是创建_aSwrOutFrame->data这个数组,而这个数组指向的缓存区需要自己创建
    // 初始化输出缓冲data[0]的空间.
    // 指定4096是因为每个frame大小不一样, 重采样后输出的数据大小不定.所以输出缓冲区空间搞个大空间囊括.
    ret = av_samples_alloc((uint8_t **)_aSwrOutFrame->data,
                           _aSwrOutFrame->linesize,
                           _audioOutSpec.chs,
                           4096,
                           _audioOutSpec.fmt,
                           1);
    RET(av_samples_alloc);
    return 0;
}

void VideoPlayer::audioSDLCallbackFunc(void *userdata, Uint8 *stream, int len) {
    VideoPlayer *player = (VideoPlayer *)userdata;
    player->audioSDLCallback(stream, len);
}

void VideoPlayer::audioSDLCallback(Uint8 *stream, int len) {

    SDL_memset(stream, 0, len);
    // 因为pkt包的大小不一定一次能填充够sdl索要的缓冲区(len: 缓冲区长度)
    // 反过来思考: 将赋予len作为剩余要填充缓冲区的长度,作为循环条件判断
    while (len > 0) {
        if (_state == Paused) break;

        if (_state == Stopped) {
            // 标记音频资源可以释放
            _aCanFree = true;
            break;
        }
        // 说明当面PCM数据已经全部拷贝到SDL缓冲区, 需要重新获取数据
        if (_aSwrOutFrameIdx >= _aSwrOutFrameSize) {
            // 需要解码下一个pkt包, 获取新的解码数据
            _aSwrOutFrameSize = decoderAudio();
//            qDebug() << _aSwrOutFrameSize;
            // 将记录输出PCM填充了多少数据的索引归零
            _aSwrOutFrameIdx = 0;
            // 处理重采样数据不成功的
            if (_aSwrOutFrameSize <= 0) {
                // 重采样数据不成功的话, 做假静音数据处理
                // 不能将整SDL缓冲区填充静音数据, 因为缓冲区全放静音数据, 可能会导致播放出来有一段没声音.
                // 而一个frame重采不成功, 我们用一小段(1024)静音数据当作重采样数据令SDL继续工作, 这样就算有静音耳朵也感觉不出来
                _aSwrOutFrameSize = 1024;
                memset(_aSwrOutFrame->data[0], 0, _aSwrOutFrameSize);
            }
        }
        // 得出剩余
        int fillLen = _aSwrOutFrameSize - _aSwrOutFrameIdx;
        fillLen = std::min(fillLen, len);

        // 音量
        int volume = _mute ? 0 : (_volume * 1.0 / Max) * SDL_MIX_MAXVOLUME;
        SDL_MixAudio(stream,
                     _aSwrOutFrame->data[0] + _aSwrOutFrameIdx,
                     fillLen,
                     volume);
        // 减去已填充的长度
        len-=fillLen;
        // 同时缓冲区移动指针
        stream+=fillLen;
        // 记录重采样输出data[0]填充多少SDL数据的索引, 也就说下次从哪里开始读取PCM数据到SDL缓冲区
        _aSwrOutFrameIdx+=fillLen;

    }
}

int VideoPlayer::decoderAudio() {
    _aMutex->lock();

    // 当木有列表没有音频包
    // 为什么用while?因为线程等待时,有可能被系统假唤醒.
//    while (_aPktList->empty()) {
//        // 线程阻塞等待, 直到有新加的音频包信号
//        _aMutex->wait();
//    }
    if (_aPktList->empty()) {
        _aMutex->unlock();
        return 0;
    }

    AVPacket pkt = _aPktList->front();
    _aPktList->pop_front();
    _aMutex->unlock();
    // 音频pkt包石见穿是用dts属性
    // 记录音频播放到的时间戳
    if (pkt.dts != AV_NOPTS_VALUE) {
        // dts还原成秒需要乘以一个单位time_base
        _aTime = av_q2d(_aStream->time_base) * pkt.dts;
        emit timeChanged(this);
    }

    // 发现音频的时间早于_aSeekTime直接丢弃.
    if (_aSeekTime >= 0) {
        if (_aTime < _aSeekTime) {
            return 0;
        }else {
            _aSeekTime = -1;
        }
    }

    // 发送数据到解码器
    int ret = avcodec_send_packet(_aDecodeCxt, &pkt);
    // 释放pkt
    av_packet_unref(&pkt);
    RET(avcodec_send_packet);
    /* 这里按理来说av_read_frame获取到的pkt包来解码, 有可能会出现一个pkt包解码出多个frame,
     * 那为什么不学之前aacDemo那样写个while循环来读到没有frame 为止呢?
     * 因为之前aacDemo从文件读编码数据到pkt->data的数据长度是由我们自己定义的(使用了官方建议空间大小),
     * 这里av_read_frame函数从码流每次读到的pkt数据, 刚好一个pkt包解码出一个frame,所以不用循环.
     * 不用循环还有一个好处, decoderAudio方法每次都要返回解码frame的大小,如果循环岂不是一个pkt返回很多次.
     */
    // 解码完, 拉数据到frame
    ret = avcodec_receive_frame(_aDecodeCxt, _aSwrInFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    }else RET(avcodec_receive_frame);

    // 重采样输出的样本数
    int aSwrOutSamples = av_rescale_rnd(_audioOutSpec.sampleRate,
                                        _aSwrInFrame->nb_samples,
                                        _aSwrInFrame->sample_rate,
                                        AV_ROUND_UP);

    // 由于解码出来的PCM数据与SDL要求的PCM格式不一致
    // 重采样, 成功返回重采样的样本个数, 失败返回错误码
    ret = swr_convert(_aSwrCxt,
                      _aSwrOutFrame->data,
                      aSwrOutSamples,
                      (const uint8_t **)_aSwrInFrame->data,
                      _aSwrInFrame->nb_samples);
    RET(swr_convert);

    return ret * _audioOutSpec.bytesPerSampleFrame;
}

void VideoPlayer::addAudioPkt(AVPacket &pkt) {
    _aMutex->lock();
    _aPktList->push_back(pkt);
    _aMutex->signal();
    _aMutex->unlock();
}

void VideoPlayer::clearAudioList() {
    _aMutex->lock();
    for(AVPacket &pkt : *_aPktList) {
        av_packet_unref(&pkt);
    }
    _aPktList->clear();
    _aMutex->unlock();
}

void VideoPlayer::freeAudio() {

    clearAudioList();
    swr_free(&_aSwrCxt);
    av_frame_free(&_aSwrInFrame);
    if (_aSwrOutFrame) {
        av_freep(&_aSwrOutFrame->data[0]);
        av_frame_free(&_aSwrOutFrame);
    }
    avcodec_free_context(&_aDecodeCxt);

    _aSwrOutFrameSize = 0;
    _aSwrOutFrameIdx = 0;
    _aTime = 0;
    _aSeekTime = -1;
    _aStream = nullptr;
    _hasAudio = false;
    _aCanFree = false;

    // 暂停播放
    SDL_PauseAudio(1);
    SDL_CloseAudio();
}
