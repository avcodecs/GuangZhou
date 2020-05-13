#ifdef CODEC_HAVE_OPUS

#ifndef __FDK_OPUS_ENCODER__
#define __FDK_OPUS_ENCODER__


#include "yyaudio.h"
#include "opus.h"
#include "resampler.h"
#include "InfoAudioRingBuffer.h"
#include "SpeexResampler.h"


#define OPUS_APPLICATION_VOIP                2048
#define OPUS_APPLICATION_AUDIO               2049
#define FAILURE_ENCODER     -1
#define SUCCESS_ENCIDER		0
#define MAX_PACKET         1500
#define ENCODERMAXLENMS   20
namespace yymobile
{

class COpusEncoder : public AudioEncoder
{
public: 
	COpusEncoder(int sampleFrequency, int channels, int bps,int bitrate, int BR_Index);
	virtual ~COpusEncoder();

	virtual int Start();
    virtual void Stop();
    virtual void ClearBuffer() {}
    virtual int  Encode(const unsigned char* inputData, unsigned int len, unsigned char* outputData);
	virtual unsigned int GetEncFinalRange(){return enc_final_range;};
	virtual void AdjustEncQuality(int quality);
	virtual void setBitRate(int bitrate);	

private:

	void resetEncBitRateQuality();

	int mBitRate;
	int mChannels;
	int mSampleRate;
	int mBandwidth;
	int mUseVbr;
	int mComplexity;
	int mUseInbandfec;
	int mUseDTX;
	int mBPS;     //bit per sample
	int mBitRateIndex;
	int mLastBitRateIndex;
	int m_EncCount;

	OpusEncoder * mOpusEncoder;
	unsigned int enc_final_range;
	int mEncoderSampleRate;
	CSpeexResampler * mOpusStereoReampler;
	static const int kMaxFrameLen = 8192*2;
};

} // namespace yymobile

#endif
#endif

