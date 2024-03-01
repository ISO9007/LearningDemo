#include "videoplayer.h"
#include <QDebug>
#include <thread>

int VideoPlayer::initVideoInfo() {
    // 初始化解码器
    int ret = initDecoder(&_vDecodeCxt, AVMEDIA_TYPE_VIDEO, &_vStream);
    RET(initDecoder);

    // 初始化转格式(因为Qt平台渲染只支持rgba, yuv420->rgba)
    ret = initSws();
    RET(initSws);

    return 0;
}

void VideoPlayer::addVideoPkt(AVPacket &pkt) {
    _vMutex->lock();
    _vPktList->push_back(pkt);
    _vMutex->signal();
    _vMutex->unlock();
}

int VideoPlayer::initSws() {
    // 宽高16的倍数
    int inW = _vDecodeCxt->width;
    int inH = _vDecodeCxt->height;

    // 像素格式转换输出参数
    _vSwsOutSpec.width = inW >> 4 << 4;
    _vSwsOutSpec.height = inH >> 4 << 4;
    _vSwsOutSpec.pixelFmt = AV_PIX_FMT_RGB24;
    _vSwsOutSpec.size = av_image_get_buffer_size(_vSwsOutSpec.pixelFmt, _vSwsOutSpec.width, _vSwsOutSpec.height, 1);

    // 获取像素数据格式转换上下文
    _vSwsCxt = sws_getContext(inW, inH, _vDecodeCxt->pix_fmt,
                              _vSwsOutSpec.width, _vSwsOutSpec.height, _vSwsOutSpec.pixelFmt,
                              // flags参数为选择哪个图片scale算法,参考官方源码怎么传的, 查询资料SWS_BICUBIC 这个性能好点
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!_vSwsCxt) {
        qDebug() << "sws_getContext error";
        return -1;
    }
    // 初始化Frame
    _vSwsInFrame = av_frame_alloc();
    if (!_vSwsInFrame) {
        qDebug() << "av_frame_alloc error";
        return -1;
    }

    _vSwsOutFrame = av_frame_alloc();
    if (!_vSwsOutFrame) {
        qDebug() << "av_frame_alloc error";
        return -1;
    }

    // 初始化_vSwsOutFrame->data[0]指向的内存区, 接受转换数据
    av_image_alloc(_vSwsOutFrame->data,
                   _vSwsOutFrame->linesize,
                   _vSwsOutSpec.width,
                   _vSwsOutSpec.height,
                   _vSwsOutSpec.pixelFmt, 1);

    return 0;
}

void VideoPlayer::decodervideo() {

    while (true) {
        // 视频暂停 如果没有seek操作continue
        if (_state == Paused  && _vSeekTime == -1) {
            continue;
        }

        if (_state == Stopped) {
            // 标记视频资源可以释放
            _vCanFree = true;
            break;
        }

        _vMutex->lock();
        // 获取视频包
        if (_vPktList->empty()) {
            _vMutex->unlock();
            continue;
        }

        AVPacket pkt = _vPktList->front();
        _vPktList->pop_front();
        _vMutex->unlock();
        // 视频时钟
        if (pkt.pts != AV_NOPTS_VALUE) {
            _vTime = av_q2d(_vStream->time_base) * pkt.pts;
        }

        // 发送数据到解码器
        int ret = avcodec_send_packet(_vDecodeCxt, &pkt);

        // 释放pkt
        av_packet_unref(&pkt);
        CONTINUE(avcodec_send_packet);

        // 相对音频,这里可以用循环
        while(true) {
            // 解码完, 拉数据到frame
            /* avcodec_receive_frame每次解码出来数据赋值给_vSwsInFrame->data,
             * 并且会将上次的_vSwsInFrame->data释放(注释有说明)
             * 注意点: 那最后一次解码会不会释放_vSwsInFrame->data呢?
             * 答案是会的,假如avcodec_receive_frame解码最后一次数据, 函数返回值ret照样有值的,
             * 又由于是最后解码数据了,函数会读一次看看是不是真到数据末尾了, 没有解码数据了ret就返回AVERROR_EOF,
             * 同时也释放了上次_vSwsInFrame->data.所以data不需要我们创建不需要手动释放
            */
            ret = avcodec_receive_frame(_vDecodeCxt, _vSwsInFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }else BREAK(avcodec_receive_frame);


            /* 每次seek操作, ffmpeg都会从seek时间GOP对应的I帧开始, 然后不断解码到seek对应的时间帧(可能是B帧/p帧, 或者刚好是I帧)
             * 根据h264原理, 如果是B帧或者P帧就从他们的参考帧开始解码, 所以不可避免有早于seek时间的视频包, 这些视频包我们作丢弃处理.
             *
             * 还有一点: 根据h264原理, 必须要做解码后判断丢弃包的时间, 如果avcodec_send_packet前判断丢弃, 之后的解码帧没有了前面的
             * 参考帧会出现画面撕裂.
             */
            // 发现视频的pkt包时钟比_vSeekTime还早就丢掉
            if (_vSeekTime >= 0) {
                // 小于seek时钟的帧丢掉
                if (_vTime < _vSeekTime) {
                    av_packet_unref(&pkt);
                    continue;
                }else {
                    _vSeekTime = -1;
                }
            }

            // 像素格式转换
            sws_scale(_vSwsCxt,
                      _vSwsInFrame->data, _vSwsInFrame->linesize,
                      // 从哪里开始读取数据, 0代表从第一行开始
                      0,
                      _vDecodeCxt->height,
                      _vSwsOutFrame->data, _vSwsOutFrame->linesize);

            if (_hasAudio) {// 有音频
                // 如果视频包过早被解码出来, 那需要等待对应的音频时刻到达
                while (_vTime > _aTime && _state == Playing && _aPktList->size() != 0) {
//                    SDL_Delay(5);
                }
            }else {
                // TODO

            }

            // 复制解码好的视频帧给新的内存区, 不直接传_vSwsOutFrame->data[0]地址出去,
            // 防止像素转换线程在调用sws_scale过程中写入新数据到_vSwsOutFrame->data[0],
            // 而外界渲染主线程渲染时又在读取_vSwsOutFrame->data[0]指向的内存区数据, 读到未完成转换的数据导致出错
            // 出bug: emit videoPlayFrameDecoded(this, _vSwsOutFrame->data[0], _vSwsOutSpec);

            // 将像素数据转换后, 拷贝一份出来
            uint8_t *data = (uint8_t *)av_malloc(_vSwsOutSpec.size);
            memcpy(data, _vSwsOutFrame->data[0], _vSwsOutSpec.size);
            emit videoPlayFrameDecoded(this, data, _vSwsOutSpec);

            qDebug() << "渲染了一帧" << _vTime << _aTime;

        }

    }
}

void VideoPlayer::clearVideoList() {
    _vMutex->lock();
    for(AVPacket &pkt : *_vPktList) {
        av_packet_unref(&pkt);
    }
    _vPktList->clear();
    _vMutex->unlock();
}

void VideoPlayer::freeVideo() {

    clearVideoList();
    avcodec_free_context(&_vDecodeCxt);
    av_frame_free(&_vSwsInFrame);
    if (_vSwsOutFrame) {
        av_freep(&_vSwsOutFrame->data[0]);
        av_frame_free(&_vSwsOutFrame);
    }
    sws_freeContext(_vSwsCxt);
    _vSwsCxt = nullptr;
    _vStream = nullptr;
    _vTime = 0;
    _vSeekTime = -1;
    _hasVideo = false;
    _vCanFree = false;

}
