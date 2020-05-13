#ifdef CODEC_HAVE_SILK

#ifndef SILKENCODER_H
#define SILKENCODER_H

#include "yyaudio.h"
#include "SKP_Silk_SDK_API.h"
#include "resampler.h"
#include "InfoAudioRingBuffer.h"

namespace yymobile
{

class CSilkEncoder: public AudioEncoder{
public:
	CSilkEncoder(int sampleFrequency, int channels, int bps, int bitrate, int quality);
	virtual ~CSilkEncoder();

    virtual int Start();
    virtual void Stop();
    virtual void ClearBuffer() {}
    virtual int  Encode(const unsigned char* inputData, unsigned int len, unsigned char* outputData);
    virtual void AdjustEncQuality(int quality);
	virtual void setBitRate(int bitrate);
	
	virtual unsigned int GetEncFinalRange(){return 0;}
private:
    void resetEncQuality();
private:
    void* m_pSilkEnc;

    int m_sampleFrequency;
    int m_channels;
    int m_bps;
	int m_bitrate;
    volatile int m_quality;
    int m_lastQuality;
    int m_inputFrameSize;
  	int m_useDTX;
  	int m_complexity;
    int m_EncCount;
    SKP_SILK_SDK_EncControlStruct m_encControl; // Struct for input to encoder

	//for resample 16k -> 8k
    webrtc::Resampler *m_resampler;
    char* m_resampled_data;
    int m_resample_data_size;
    audiosdk::ResampleAdapter mResamplerForSilk;
};

}

#endif // AMRNBENCODER_H

#endif
