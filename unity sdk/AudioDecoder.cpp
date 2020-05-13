#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "AudioDecoder.h"
#include "AudioParams.h"

#ifdef CODEC_HAVE_SPEEX
#include "speex_decoder.h"
#endif
//#include "amrwbdecoder.h"

#ifdef CODEC_HAVE_MP3
// UInt32 is also defined in apple_aac_decoder.h
#define UInt32 UInt32_MP3
#include "mp3_decoder.h"
#endif

#ifdef CODEC_HAVE_AAC
//#include "pvaac.h"
//#include "eaacdecoder.h"
//#include "pvaac_decoder.h"
#include "fdk_aac_decoder.h"
#include "fdk_aac_encoder.h"
#ifdef YYMOBILE_IOS
#include "apple_aac_decoder.h"
#endif
#endif

#ifdef CODEC_HAVE_SILK
#include "silk_decoder.h"
#endif

#ifdef CODEC_HAVE_G729
#include "g729_decoder.h"
#endif

#ifdef CODEC_HAVE_OPUS
#include "opus121_decoder.h"
#endif


#include "yyaudio.h"
#include "ConvertPCM.h"
#include "SdkConfig.h"
#include "AudioPlayUnit.h"

using namespace audiosdk;
uint8_t g_audio_config[4];
uint32_t g_audio_config_size;
static uint32_t audio_sample_rate[16] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350,0,0,0 };

enum AacHwCodecMgrCallbackType{
    GET = 0,
    UPDATE,
    RELEASE
};

namespace yymobile
{
int g_DecNo = 0;

CAudioDecoder::CAudioDecoder() :
	m_decoder(NULL)
    , mOutSampleRate(0)
    , mOutChannel(0)
    , mpResampler(NULL)
    , mIsUseHwCodec(false)
    , mAacHwCodecMgrCallback(NULL)
{
	mStereoReampler = new CSpeexResampler();
}

CAudioDecoder::~CAudioDecoder() {
    if(m_fileWriter.IsOpen()) {
    	m_fileWriter.Close();
    }
	if (m_decoder) {
		m_decoder->Stop();
		delete m_decoder;
		m_decoder = NULL;
	}
	if(mStereoReampler != NULL){
		delete mStereoReampler;
		mStereoReampler = NULL;
	}
    if (mpResampler != NULL)
    {
        speex_resampler_destroy(mpResampler);
    }
    if(mIsUseHwCodec && mAacHwCodecMgrCallback != NULL)
    {
        mAacHwCodecMgrCallback(RELEASE);
    }
}
bool CAudioDecoder::isUseHwDecoder()
{
	return m_decoder->isHwDecoder();
}

bool CAudioDecoder::prepare(int codec, int inputFrameSize, int sampleFrequency,
		int chans, int bps, int profile) {
    //to do: set a suitable value for mDecodeDelayEst
	if (m_decoder) {
		LOGW("prepare called twice, release existing decoder");
		delete m_decoder;
		m_decoder = NULL;
	}
    if (mpResampler != NULL)
    {
        speex_resampler_destroy(mpResampler);
        mpResampler = NULL;
    }
    mOutSampleRate = sampleFrequency;
    mOutChannel = chans;
	LOGI("CAudioDecoder::prepare: codec=%d, inputFrameSize=%d, sampleFrequence=%d, channels=%d, bps=%d",codec, inputFrameSize, sampleFrequency, chans, bps);
	switch (codec) {
#ifdef CODEC_HAVE_OPUS
	case CodecOpus:
		LOGI("native OPUS");
		m_decoder =  new COpusDecoder(inputFrameSize, sampleFrequency, chans, bps);
	break;
#endif
#ifdef CODEC_HAVE_AAC
	case CodecAac:
	case CodecEaac:
        {
    		LOGI("native EAAC");
    		//assert(inputFrameSize == 0);
    		//assert(bps == 16);
                
            bool useHardwareCodec = false;
            AudioParams * ap = yymobile::getAudioParams();
            if (ap) {
                useHardwareCodec = ap->getParamFromIndex(USE_AUDIO_HARDWARE_CODEC) > 0;
            }
    #ifdef YYMOBILE_IOS
            if (useHardwareCodec) {   
                
                if(AudioPlayUnit::mAacHwCodecMgrCallback != NULL) {
                    LOGD("use AudioPlayUnit's AacHwCodecMgrCallback");
                    mAacHwCodecMgrCallback = AudioPlayUnit::mAacHwCodecMgrCallback;
                }
                
                int refCount = -1;
                if(mAacHwCodecMgrCallback != NULL) {
                    refCount = mAacHwCodecMgrCallback(GET);
                }
                    
                if(refCount == 0) {
                    m_decoder = new COSXAacDecoder(CFdkAacEncoder::FDK_AOT_PS);
                    mIsUseHwCodec = true;
                    if(mAacHwCodecMgrCallback != NULL) {
                        mAacHwCodecMgrCallback(UPDATE);
                    }
    
                    //Test if the system support the codec
                    if (m_decoder->Start()) {
                        m_decoder->Stop();
                        LOGI("use aac hardware codec in ios");
                    }
                    else {
                        mIsUseHwCodec = false;
                        delete m_decoder;
                        m_decoder = NULL;
                        if(mAacHwCodecMgrCallback != NULL) {
                            mAacHwCodecMgrCallback(RELEASE);
                        }
                    }
                 }

            }
                    
    #endif
            if (m_decoder == NULL) {
                m_decoder = new CFdkAacDecoder(inputFrameSize);
            }
        }
		break;
#endif
#ifdef CODEC_HAVE_SPEEX
	case CodecSpeexMode2:
	case CodecSpeexMode8:
		LOGI("native speex");
		m_decoder = new CSpeexDecoder(inputFrameSize, sampleFrequency, chans, bps);
		break;
#endif
#ifdef CODEC_HAVE_SILK
	case CodecSilk:
		LOGI("native silk");
		m_decoder = new CSilkDecoder(inputFrameSize, sampleFrequency, chans, bps);
		break;

	case CodecSilk8kSamp:
		LOGI("native silk 8k");
		m_decoder = new CSilkDecoder(inputFrameSize, sampleFrequency, chans, bps);
		break;
#endif
#ifdef CODEC_HAVE_G729
	case CodecG729:
		LOGI("native g729");
		m_decoder = new CG729Decoder(inputFrameSize, sampleFrequency, chans, bps);
		break;
#endif
#ifdef CODEC_HAVE_AMRWB
	case CodecAmrwb:
		LOGI("native amrwb");
		m_decoder = new CAmrWbDecoderPrivate();
		break;
#endif
#ifdef CODEC_HAVE_MP3
	case CodecMp3:
		LOGI("native Mp3");
		m_decoder = new CMp3Decoder(inputFrameSize);
		break;
#endif
#ifdef CODEC_HAVE_AMRWB
	case CodecAmrnb:
		LOGI("native amrnb");
		break;
#endif
	default:
		LOGI("### unsupported codec type for decoder: %d", codec);
		return false;
	}

	bool result = false;
	if (m_decoder != NULL) {
		result = m_decoder->Start();
		if (!result) {            
			delete m_decoder;
			m_decoder = NULL;
            if(mIsUseHwCodec) {
                mIsUseHwCodec = false;
                if(mAacHwCodecMgrCallback != NULL) {
                    mAacHwCodecMgrCallback(RELEASE);
                }
            }
		} else {
			if(audiosdk::SdkConfig::IsDumpEncodeEnabled()) {
				char buf[512];
				time_t timer = time(NULL);
				tm * t = localtime(&timer);
				char extbuf[48];
				sprintf(extbuf, "%02dh%02dm%02ds_%02d", t->tm_hour,t->tm_min,t->tm_sec, g_DecNo++);
				switch (codec) {
					case CodecOpus:
						sprintf(buf, "%s/opus_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecSpeexMode2:
					case CodecSpeexMode8:
						sprintf(buf, "%s/speex_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecEaac:
						sprintf(buf, "%s/eaac_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecSilk:
						sprintf(buf, "%s/silk_%s.raw",  g_debug_output_dir, extbuf);
						break;
					case CodecAmrwb:
						sprintf(buf, "%s/amrwb_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecMp3:
						sprintf(buf, "%s/mp3_%s.raw",  g_debug_output_dir,  extbuf);
						break;
					case CodecAmrnb:
						sprintf(buf, "%s/amrnb_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecSilk8kSamp:
						sprintf(buf, "%s/silk_8k_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecAac:
						sprintf(buf, "%s/aac_%s.raw", g_debug_output_dir, extbuf);
						break;
					case CodecG729:
						sprintf(buf, "%s/g729_%s.raw", g_debug_output_dir, extbuf);
						break;
				}
				LOGI("begin rec decoder file: %s", buf);
				m_fileWriter.Open(buf);

			}
		}
	}

	LOGD("********* Audio Decoder *********");
	LOGD("* codec: %s(%d)", kCodecNames[codec], codec);
	LOGD("* sample rate: %d", sampleFrequency);
	LOGD("* channels: %d", chans);
	LOGD("* bps: %d", bps);
	LOGD("*********************************");
	return result;
}

int CAudioDecoder::decode(const unsigned char *inputData, unsigned int length, unsigned char* outputData) {
	if(!m_decoder) {
		return -1;
	}
	int result = m_decoder->Decode(inputData, length, outputData);
    result = resampleIfNeed(outputData, result);
	if(result > 0) {
		m_volAdjust.Process((short*)outputData, result / 2);
		if (m_fileWriter.IsOpen())
		{
			m_fileWriter.Write(outputData, result);
		}
	}

	return result;
}

int  CAudioDecoder::recover(int framenum, unsigned char* outputData) {
	if(!m_decoder) {
		return -1;
	}
	int result = m_decoder->Recover(framenum, outputData);
    result = resampleIfNeed(outputData, result);
	if(result > 0) {
		m_volAdjust.Process((short*)outputData, result / 2);
		if (m_fileWriter.IsOpen())
		{
			m_fileWriter.Write((unsigned char*) outputData, result);
		}
	}
	return result;
}

void CAudioDecoder::Reset()
{
	if(m_decoder) {
		m_decoder->ClearBuffer();
	}
    if (mpResampler != NULL)
    {
        speex_resampler_reset_mem(mpResampler);
    }
}
    
int CAudioDecoder::resampleIfNeed(unsigned char *outputData, int len)
{
    int outLen = 0;
    int err = 0;
    if (len <= 0)
    {
        return outLen;
    }
    int decodedSampleRate = m_decoder->GetSampleRate();
    int decodedChannels = m_decoder->GetChannels();
    
    if (mOutChannel == 0) {
        mOutChannel = decodedChannels;
    }
    
    if (mOutSampleRate == 0) {
        mOutSampleRate = decodedSampleRate;
    }

    assert( mOutChannel>0 && decodedChannels>0 && mOutChannel<=2 && decodedChannels<=2 );
    assert( mOutSampleRate >= 0 && decodedSampleRate >= 0);
	// stereo  Resampler
	if(mOutChannel == 2 && decodedChannels == 2 && mOutSampleRate != decodedSampleRate)
	{
		char* pResampeIndata = (char* )outputData;
		char outputResamplerBuffer[kMaxFrameSizeInByte];
		if(mStereoReampler->GetInSampleRate() != decodedSampleRate || mStereoReampler->GetOutSampleRate() != mOutSampleRate){
			mStereoReampler->Init(2,decodedSampleRate,mOutSampleRate);
		}
		int resampLen = mStereoReampler->Resample(pResampeIndata,len,outputResamplerBuffer,kMaxFrameSizeInByte);
		memcpy((char*)outputData, (char*)outputResamplerBuffer, resampLen);
		return resampLen;
	}
    //only need channel convert
    if(decodedSampleRate == mOutSampleRate)
    {
    	if(decodedChannels == mOutChannel) {
    		outLen = len;
    	} else if(decodedChannels == 1 && mOutChannel == 2) {
            outLen = ConvertMonoToStereoInplace((char*)outputData, len);
    	} else if(decodedChannels == 2 && mOutChannel == 1) {
    		outLen = ConvertStereoToMonoInplace((char*)outputData, len);
    	}
    	return outLen;
    }

    //decodedSampleRate != mOutSampleRate, need resample
    //Temporarily we do not use speex stereo resampling, so we have to convert stereo to mono first
    outLen = len;
    char outputProcessedBuffer[kMaxFrameSizeInByte];

    //1. Convert to mono
    if(decodedChannels == 2) {
    	outLen = ConvertStereoToMonoInplace((char*)outputData, len);
    }
    //2. Check if mpResampler matches the resample requirement
    if (mpResampler == NULL) {
		mpResampler = speex_resampler_init(1, decodedSampleRate, mOutSampleRate, 3, &err);
		LOGD("decoder, need resample: %d -> %d", decodedSampleRate, mOutSampleRate);
    }
    else {
        spx_uint32_t lastInputRate = 0, lastOutputRate = 0;
		speex_resampler_get_rate(mpResampler, &lastInputRate, &lastOutputRate);
		if( lastInputRate != decodedSampleRate || lastOutputRate != mOutSampleRate) {
			LOGD("decoder resampler need reset: in_rate: %d -> %d, out_rate: %d -> %d",
					lastInputRate, decodedSampleRate, lastOutputRate, mOutSampleRate);
			speex_resampler_destroy(mpResampler);
			mpResampler = speex_resampler_init(1, decodedSampleRate, mOutSampleRate, 3, &err);
			mLastResamplerChannels = 1;
		}
    }

    //3. resample.
    if(mpResampler != NULL)
	{
		spx_int16_t *pResampeIn = (spx_int16_t*)outputData;
		spx_uint32_t inlen = outLen / sizeof(short);
        spx_uint32_t resamp_len = kMaxFrameSizeInByte / sizeof(short);
        err = speex_resampler_process_int(mpResampler, 0, pResampeIn, &inlen,
				(spx_int16_t*)outputProcessedBuffer, &resamp_len);
        outLen = resamp_len*sizeof(short);
	}

    //4. check if resampler init failed or resample failed
    if(mpResampler == NULL || err < 0) {
    	return outLen;
    }

    //5. convert to stereo if needed
    if(mOutChannel == 2) {
    	outLen = ConvertMonoToStereo((char*)outputProcessedBuffer, outLen, (char*)outputData);
    } else {
    	memcpy((char*)outputData, (char*)outputProcessedBuffer, outLen);
    }

    return outLen;
}

int CAudioDecoder::getDecodeDelayEst(int codecType, const uint8_t *payload, int payloadLen)
{
    unsigned char adts[7];
	unsigned int sr = 0;
	int mSampleRateIndex = 0;
	int mDecodeDelay = 0;
    if(codecType != NET_AAC || payloadLen <= 7)
    {
    	return 0;
    }
	if(isUseHwDecoder())
	{
		return 0;
	}
	else
	{
		memcpy(adts, payload, 7);
		mSampleRateIndex = ((adts[2] >> 2) & 0x0F);
		sr = audio_sample_rate[mSampleRateIndex];
        if (sr != 0)
        {
            mDecodeDelay = 1024 *1000/sr;
        }
	}
    return mDecodeDelay;
}


}
