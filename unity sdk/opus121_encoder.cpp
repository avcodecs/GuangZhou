#ifdef CODEC_HAVE_OPUS
#include "opus121_encoder.h"
#include "AudioUnitWrapper.h"
#include "trace_log.h"


static int OpusBitRate[] = {4000,6000,8000, 12000, 14000, 16000, 18000, 20000, 22000, 24000, 32000,48000, 64000, 96000, 128000};
static int BitRateIndexMax = 15;

namespace yymobile
{
COpusEncoder::COpusEncoder(int sampleFrequency,int channels,int bps,int bitrate,int BR_Index)
:mSampleRate(sampleFrequency)
,enc_final_range(0)
,mChannels(channels)
,mOpusEncoder(NULL)
,mComplexity(10),mUseVbr(0),mUseDTX(0)
,mUseInbandfec(0),mBandwidth(OPUS_AUTO),m_EncCount(0)
,mLastBitRateIndex(BR_Index),mBitRateIndex(BR_Index),mBPS(bps),mBitRate(bitrate)
,mEncoderSampleRate(48000)
{
    mOpusStereoReampler = new CSpeexResampler();
}
COpusEncoder::~COpusEncoder()
{
	Stop();
    if(mOpusStereoReampler != NULL){
        delete mOpusStereoReampler;
        mOpusStereoReampler = NULL;
    }
}
int COpusEncoder::Start()
{
	if(mBPS != 16 )
		return -1;

	int err;
    mOpusEncoder = opus_encoder_create(mEncoderSampleRate,mChannels,OPUS_APPLICATION_AUDIO,&err);
	if(err != OPUS_OK)
	{	
		return FAILURE_ENCODER;
	}
	opus_encoder_ctl(mOpusEncoder,OPUS_SET_BITRATE(OpusBitRate[mBitRateIndex]));
	opus_encoder_ctl(mOpusEncoder,OPUS_SET_BANDWIDTH(mBandwidth));
	//opus_encoder_ctl(mOpusEncoder, OPUS_SET_VBR(mUseVbr));
   	opus_encoder_ctl(mOpusEncoder, OPUS_SET_COMPLEXITY(mComplexity));
   	opus_encoder_ctl(mOpusEncoder, OPUS_SET_INBAND_FEC(mUseInbandfec));
   	opus_encoder_ctl(mOpusEncoder, OPUS_SET_DTX(mUseDTX));

	//opus_encoder_ctl(mOpusEncoder, OPUS_SET_VBR_CONSTRAINT(0));
	opus_encoder_ctl(mOpusEncoder, OPUS_SET_FORCE_CHANNELS(OPUS_AUTO));
	opus_encoder_ctl(mOpusEncoder, OPUS_SET_PACKET_LOSS_PERC(0));
    if(mSampleRate != 8000 || mSampleRate != 12000 ||mSampleRate != 16000||mSampleRate != 24000||mSampleRate != 48000){
        mOpusStereoReampler->Init(mChannels,mSampleRate,mEncoderSampleRate);
    }
    
	return 1;
}
void COpusEncoder::Stop()
{
	if(mOpusEncoder != NULL)
	{
		opus_encoder_destroy(mOpusEncoder);
		mOpusEncoder = NULL;
	}
}
int COpusEncoder::Encode(const unsigned char * inputData,unsigned int len,unsigned char * outputData)
{
    char outputResamplerBuffer[kMaxFrameLen];
    int resampLen = mOpusStereoReampler->Resample((char*)inputData,len,outputResamplerBuffer,kMaxFrameLen);
    if(mSampleRate == audiosdk::AudioUnitWrapper::kCaptureFormat_SampleRate) {
        if (++m_EncCount % 150 == 0) {
            resetEncBitRateQuality();
            m_EncCount = 0;
        }
    }

    int frame_size = mEncoderSampleRate/50;
    int encodeLength  = opus_encode(mOpusEncoder,(short*)outputResamplerBuffer,resampLen/2/mChannels,outputData,frame_size);    
    opus_encoder_ctl(mOpusEncoder, OPUS_GET_FINAL_RANGE(&enc_final_range));

	if(encodeLength < 0 )
	{
		return 0;
	}
	return encodeLength;
}

void COpusEncoder::AdjustEncQuality(int quality)
{
    if (quality > BitRateIndexMax || quality < 0)
    {
        return;
    }
    if (mBitRateIndex!= quality)
    {
        mBitRateIndex= quality;
    }
}

void COpusEncoder::resetEncBitRateQuality()
{
	if (mLastBitRateIndex> mBitRateIndex && mLastBitRateIndex > 0 && mBitRateIndex <= BitRateIndexMax)
    {
    	mLastBitRateIndex--;
    	opus_encoder_ctl(mOpusEncoder,OPUS_SET_BITRATE(OpusBitRate[mLastBitRateIndex]));
    }
    if (mLastBitRateIndex < mBitRateIndex && mLastBitRateIndex >= 0 && mBitRateIndex < BitRateIndexMax)
    {
	    mLastBitRateIndex++;
		opus_encoder_ctl(mOpusEncoder,OPUS_SET_BITRATE(OpusBitRate[mLastBitRateIndex]));
    }
	int bitrate;
	opus_encoder_ctl(mOpusEncoder, OPUS_GET_BITRATE(&bitrate));
	//__android_log_print(ANDROID_LOG_ERROR, "fantest", "cur bit rate:%d, mLastBitRateIndex, %d, mBitRateIndex, %d", bitrate, mLastBitRateIndex, mBitRateIndex);   
}
void COpusEncoder::setBitRate(int bitrate) {
	mBitRate = bitrate;
	LOGD("[opus_encoder] setBitRate=%d",mBitRate);
	for (int qualityIndex = 0; qualityIndex < sizeof(OpusBitRate) / sizeof(int); qualityIndex++) {
		if (OpusBitRate[qualityIndex] >= mBitRate) {
			mBitRateIndex = qualityIndex;

			break;
		}
	}
}

}
#endif
