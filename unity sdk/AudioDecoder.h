#ifndef AUDIODE_DECODER_H
#define AUDIODE_DECODER_H


#include "yyaudio.h"
#include "audio_filter.h"
#include "rawfilewriter.h"
#include "speex/speex_resampler.h"
#include "SpeexResampler.h"

namespace yymobile
{
    enum
    {
        FDK_AOT_AAC_LC = 2,
        FDK_AOT_SBR = 5,
        FDK_AOT_PS = 29
    };
    
class CAudioDecoder {
public:
	CAudioDecoder();
	virtual ~CAudioDecoder();

public:
	bool prepare(int codec, int inputFrameSize, int sampleFrequency, int chans, int bps, int profile);
	int  decode(const unsigned char *inputData, unsigned int length, unsigned char* outputData);
	int  recover(int framenum, unsigned char* outputData);
	void Reset();
	void SetVolumeLevel(int level) { m_volAdjust.SetLevel(level);}
	int getOutputSampleRate() { return mOutSampleRate; }
	void setOutputSampleRate(int sr) { mOutSampleRate = sr; }
	int getOutputChannels() { return mOutChannel; }
	void setOutputChannels(int channels) { mOutChannel = channels; }
    int getDecodeDelayEst(int codecType, const uint8_t *payload, int payloadLen);
private:
    
    int resampleIfNeed(unsigned char* outputData, int len);
	bool isUseHwDecoder();
    
    static const int kMaxFrameSizeInByte = 8192 * 2;
    
	AudioDecoder *m_decoder;
	CAdjustVol m_volAdjust;
	CFileWriter m_fileWriter;
    
	int mOutSampleRate;
	int mOutChannel;
	SpeexResamplerState* mpResampler;
	int mLastResamplerChannels;
    AAC_HWCODEC_MGR_CALLBACK mAacHwCodecMgrCallback;
    bool mIsUseHwCodec;
	CSpeexResampler * mStereoReampler;
};

}

#endif
