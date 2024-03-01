#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QObject>
#include <list>
#include "condmutex.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
}

#define ERROR_BUF(ret) \
        char errBuff[1024]; \
        av_strerror(ret, errBuff, sizeof(errBuff));

#define CODE(func, code) \
    if (ret < 0) { \
        ERROR_BUF(ret); \
        qDebug() << #func << "error:" << errBuff; \
        code; \
        }

#define END(func) CODE(func, fataError();return;);
#define RET(func) CODE(func ,return ret;);
#define CONTINUE(func) CODE(func, continue;);
#define BREAK(func) CODE(func, break;);


class VideoPlayer : public QObject
{
    Q_OBJECT
public:
    // 状态
    typedef enum {
        Stopped = 0,
        Playing,
        Paused
    }State;
    // 音量
    typedef enum {
        Min = 0,
        Max = 100,
    }Volume;

    // 视频像素格式转换参数
    typedef struct {
        int width;
        int height;
        AVPixelFormat pixelFmt;
        int size;
    } VideoSwsSpec;

    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer();
    /** 播放*/
    void play();
    /** 停止*/
    void stop();
    /** 暂停*/
    void pause();
    /** 是否播放*/
    bool isPlaying();
    /** 返回播放器状态*/
    State getStatc();
    /** 设置文件路径*/
    void setFilename(QString filename);
    /** 获取视频总时长 单位是微妙*/
    int64_t getDuration();
    /** 获取当前播放时间*/
    int64_t getTime();
    /** 设置(seek)播放时间*/
    int64_t setTime(int time);
    /** 设置音量*/
    void setVolume(int volume);
    /** 返回音量*/
    int getVolume();
    /** 设置静音*/
    void setMute(bool mute);
    /** 返回音量*/
    bool isMute();


signals:
    void videoStatcChanged(VideoPlayer *player);
    void timeChanged(VideoPlayer *player);
    void videoInitFinished(VideoPlayer *player);
    void videoPlayFalied(VideoPlayer *player);
    void videoPlayFrameDecoded(VideoPlayer *player, uint8_t *data,VideoSwsSpec &spec);
private:
    /**********公共方法************/
    /** 文件路径*/
    char _filename[512];
    /** 当前的状态*/
    State _state = Stopped;
    /** 解封装上下文*/
    AVFormatContext *_fmtCxt = nullptr;
    /** 解封装上下文是否可以释放*/
    bool _fmtCxtCanFree = false;
    /** 音量*/
    int _volume = Max;
    /** 静音*/
    bool _mute = false;
    /** seek时间*/
    int64_t _seekTime = -1;

    // 初始化解码器
    int initDecoder(AVCodecContext **decodeCxt , AVMediaType type, AVStream **stream);
    /** 设置播放状态 */
    void setState(State state);
    /** 读取文件*/
    void readFile();
    /** 释放资源*/
    void free();
    void freeAudio();
    void freeVideo();
    /** 发生致命错误*/
    void fataError();


    /**********视频方法************/
    /** 视频解码上下文*/
    AVCodecContext *_vDecodeCxt = nullptr;
    /** 视频流*/
    AVStream *_vStream = nullptr;
    // 存放解码后的视频数据
    AVFrame *_vSwsInFrame = nullptr, *_vSwsOutFrame = nullptr;
    /** 视频格式数据转换的上下文*/
    SwsContext *_vSwsCxt = nullptr;
    /** 像素格式转换输出参数*/
    VideoSwsSpec _vSwsOutSpec;
    /** 视频seek到哪个时刻*/
    int64_t _vSeekTime = -1;
    /** 时钟 记录当前pkt播放时间戳*/
    double _vTime = 0;
    /** 存放视频包列表*/
    std::list<AVPacket> *_vPktList = nullptr;
    /** 视频包列表互斥锁*/
    CondMutex *_vMutex = nullptr;
    /** 视频资源是否可以释放*/
    bool _vCanFree = false;
    /** 是否有视频流*/
    bool _hasVideo = false;


    /** 初始化视频*/
    int initVideoInfo();
    /** 添加视频包到列表*/
    void addVideoPkt(AVPacket &pkt);
    /** 清除视频包列表*/
    void clearVideoList();
    /** 视频格式数据解码*/
    void decodervideo();
    /** 初始化视频格式转换*/
    int initSws();


    /**********音频方法************/
    // 音频重采样参数
    typedef struct {
        int sampleRate;
        AVSampleFormat fmt;
        int64_t chsLayout;
        int chs;
        int bytesPerSampleFrame;
    } AudioResampleSpec;
    /** 视频解码上下文*/
    AVCodecContext *_aDecodeCxt = nullptr;
    /** 音频流*/
    AVStream *_aStream = nullptr;
    /** 存放视频包列表*/
    std::list<AVPacket> *_aPktList = nullptr;
    /** 音频包列表互斥锁*/
    CondMutex *_aMutex = nullptr;
    /** 音频重采样上下文*/
    SwrContext *_aSwrCxt = nullptr;
    /** 音频重采样输入\输出格式*/
    AudioResampleSpec _audioInSpec, _audioOutSpec;
    /** 音频重采样输入\输出Frame*/
    AVFrame *_aSwrInFrame = nullptr, *_aSwrOutFrame = nullptr;
    /** 记录重采样后输出Frame的大小*/
    int _aSwrOutFrameSize = 0;
    /** 重采样输出PCM数据的索引(从哪个位置开始取出PCM数据到SDL缓冲区的索引)*/
    int _aSwrOutFrameIdx = 0;
    /** 音频seek到哪个时刻*/
    int64_t _aSeekTime = -1;
    /** 时钟 记录当前pkt播放时间戳*/
    double _aTime = 0;
    /** 视频资源是否可以释放*/
    bool _aCanFree = false;
    /** 是否有音频流*/
    bool _hasAudio = false;


    /** 初始化音频*/
    int initAudioInfo();
    /** 添加音频包到列表*/
    void addAudioPkt(AVPacket &pkt);
    /** 清除音频包列表*/
    void clearAudioList();
    /** 初始化SDL*/
    int initSDL();
    /** SDL回调函数*/
    static void audioSDLCallbackFunc(void *userdata, Uint8 * stream, int len);
    /** 实现SDL回调*/
    void audioSDLCallback(Uint8 * stream, int len);
    /** 音频包解码(返回解码后数据大小)*/
    int decoderAudio();
    /** 初始化重采样*/
    int initSwr();



};

#endif // VIDEOPLAYER_H
