#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "AudioEncoder.h"

#ifdef CODEC_HAVE_SPEEX
#include "speex_encoder.h"
#endif
//#include "amrwbencoder.h"
#ifdef CODEC_HAVE_SILK
#include "silk_encoder.h"
#endif

#include "AudioParams.h"

#ifdef CODEC_HAVE_AAC
#include "fdk_aac_encoder.h"
#endif

#ifdef CODEC_HAVE_G729
#include "g729_encoder.h"
#endif

#ifdef CODEC_HAVE_OPUS
#include "opus121_encoder.h"
#endif


namespace yymobile
{

CAudioEncoder::CAudioEncoder(const char * name)
: m_encoder(NULL)
, m_codec(CodecUnknown)
{
	mSampleRate = SampleRate16K;
	mChannels = ChannelMono;
	mBps = BITS_PER_SAMPLE;
	mQuality = 1;
	mBitRate= BitRate16K;
	if(name != NULL) {
		strcpy(mName, name);
	} else {
		mName[0] = '\0';
	}
	mName[63] = '\0';
}

CAudioEncoder::~CAudioEncoder() {
    if(m_encodeFileWriter.IsOpen()) {
    	m_encodeFileWriter.Close();
    }
	if(m_encoder != NULL) {
		m_encoder->Stop();
		delete m_encoder;
		m_encoder = NULL;
	}
}

// return value is the output frame size
int CAudioEncoder::prepare(int codec, int samplerate,
		int chans, int bps, int bitrate, int quality) {

	if (m_encoder != NULL) {
		LOGW("prepare called twice, ignore");
		return -1;
	}

	switch (codec) {
#ifdef CODEC_HAVE_OPUS
		case CodecOpus:
		m_encoder = new COpusEncoder(samplerate,chans,bps,bitrate,quality);
		break;
#endif
#ifdef CODEC_HAVE_SPEEX
	case CodecSpeexMode2:
	case CodecSpeexMode8:
		m_encoder = new CSpeexEncoder(samplerate, chans, bps, bitrate, quality);
		break;
#endif
#ifdef CODEC_HAVE_AAC
	case CodecAac:
		m_encoder = new CFdkAacEncoder(samplerate, chans, bps, bitrate, quality);
		break;
#endif
#ifdef CODEC_HAVE_AMRWB
	case CodecAmrwb:
		m_encoder = new CAmrwbEncoder(samplerate, chans, bps, bitrate, quality);
		break;

	case CodecAmrnb:
		break;
#endif
#ifdef CODEC_HAVE_SILK
	case CodecSilk8kSamp:
	case CodecSilk:
		m_encoder = new CSilkEncoder(samplerate, chans, bps, bitrate, quality);
		break;
#endif
#ifdef CODEC_HAVE_G729
	case CodecG729:
		m_encoder = new CG729Encoder(samplerate, chans, bps, bitrate, quality);
		break;
#endif
	default:
		LOGE("### unsupported codec type for encoder:%d", codec);
		return -1;
	}

	int output_frame_size = 0;
	if (m_encoder != NULL) {
		output_frame_size = m_encoder->Start();
		if (output_frame_size <= 0) {
			// Encoder start failed
			delete m_encoder;
			m_encoder = NULL;
		} else {
			m_codec = codec;
		}
	}

	LOGI("********* Audio Encoder *********");
	LOGI("* codec: %s(%d)", kCodecNames[codec], codec);
	LOGI("* sample rate: %d", samplerate);
	LOGI("* channels: %d", chans);
	LOGI("* bit per sample: %d", bps);
	LOGI("*********************************");
	mSampleRate = samplerate;
	mChannels = chans;
	mBps = bps;
	mBitRate = bitrate;
	mQuality = quality;
	return output_frame_size;
}

void CAudioEncoder::reset()
{
	if(m_encoder == NULL) {
		return;
	}
	switch (m_codec) {
	case CodecSpeexMode2:
	case CodecSpeexMode8:
		LOGE("SPEEX encoder reset not supported");
		break;
	case CodecEaac:
		LOGE("EAAC encoder reset not supported");
		break;
	case CodecSilk:
	case CodecSilk8kSamp:
		LOGE("SILK encoder reset not supported");
		break;
	case CodecAmrwb:
		LOGE("AMRWB encoder reset not supported");
		break;
	case CodecMp3:
		LOGE("MP3 encoder reset not supported");
		break;
	case CodecAmrnb:
		LOGE("AMRNB encoder reset not supported");
		break;
	case CodecAac:
		if(m_encoder != NULL) {
			m_encoder->Stop();
			delete m_encoder;
		}
		m_encoder = new CFdkAacEncoder(mSampleRate, mChannels, mBps, mBitRate, mQuality);
		break;
	case CodecG729:
		LOGE("G729 encoder reset not supported");
		break;
	}

	int output_frame_size = 0;
	if (m_encoder != NULL) {
		output_frame_size = m_encoder->Start();
		if (output_frame_size <= 0) {
			// Encoder start failed
			delete m_encoder;
			m_encoder = NULL;
		}
	}

	LOGI("********* Audio Encoder Reset*********");
	LOGI("* codec: %s(%d)", kCodecNames[m_codec], m_codec);
	LOGI("* sample rate: %d", mSampleRate);
	LOGI("* channels: %d", mChannels);
	LOGI("* bit per sample: %d", mBps);
	LOGI("* quality: %d", mQuality);
	LOGI("* bitrate: %d", mBitRate);
	LOGI("*********************************");
}

int CAudioEncoder::encode(unsigned char *inputData, unsigned int length, unsigned char* outputData, bool silence) {
	if(m_encoder == NULL) {
		return 0;
	}

	if(audiosdk::SdkConfig::IsDumpEncodeEnabled() && !m_encodeFileWriter.IsOpen()) {
		char buf[512];
		switch (m_codec) {
		case CodecOpus:
			sprintf(buf, "%s/opus_enc%s.raw", g_debug_output_dir,mName);
			break;
		case CodecSpeexMode2:
		case CodecSpeexMode8:
			sprintf(buf, "%s/speex_enc%s.raw", g_debug_output_dir, mName);
			break;
		case CodecEaac:
			sprintf(buf, "%s/eaac_enc%s.raw", g_debug_output_dir, mName);
			break;
		case CodecSilk:
			sprintf(buf, "%s/silk_enc%s.raw",  g_debug_output_dir, mName);
			break;
		case CodecAmrwb:
			sprintf(buf, "%s/amrwb_enc%s.raw", g_debug_output_dir, mName);
			break;
		case CodecMp3:
			sprintf(buf, "%s/mp3_enc%s.raw",  g_debug_output_dir, mName);
			break;
		case CodecAmrnb:
			sprintf(buf, "%s/amrnb_enc%s.raw", g_debug_output_dir, mName);
			break;
		case CodecSilk8kSamp:
			sprintf(buf, "%s/silk_8k_enc%s.raw",  g_debug_output_dir, mName);
			break;
		case CodecAac:
			sprintf(buf, "%s/aac_enc%s.raw",  g_debug_output_dir, mName);
			break;
		case CodecG729:
			sprintf(buf, "%s/g729_enc%s.raw", g_debug_output_dir, mName);

			break;
		}
		LOGI("write encode file: %s", buf);
		m_encodeFileWriter.Open(buf);
	}
	int result = 0;
	result = m_encoder->Encode(inputData, length, outputData);
	if(result > 0 && audiosdk::SdkConfig::IsDumpEncodeEnabled()) { 
		if(m_codec == CodecOpus)
		{
			unsigned char int_field[4];
			int_to_char(result,int_field);
			m_encodeFileWriter.Write(int_field, 4);
			unsigned int enc_final_range =	m_encoder->GetEncFinalRange();
			int_to_char(enc_final_range,int_field);
			m_encodeFileWriter.Write(int_field, 4);
		}

		if(m_codec ==CodecSilk){
			m_encodeFileWriter.Write((unsigned char *)(&result), 2);
		}
		m_encodeFileWriter.Write(outputData, result);
	}
	return result;
}

int CAudioEncoder::getNumberOfPendingBytes() {
	int ret = 0;
	switch (m_codec) {
	case CodecAac:
		ret = m_encoder->getNumberOfPendingBytes();
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

void CAudioEncoder::setEncQuality(int quality) {
	if(mQuality != quality) {
		mQuality = quality;
		if (m_encoder) {
			if(m_codec == CodecAac) {
				LOGE("[EncThread] Switching AAC Encoder object! from quality %d to %d", mQuality, quality);
				if(m_encoder != NULL) {
					m_encoder->Stop();
					delete m_encoder;
				}
				m_encoder = new CFdkAacEncoder(mSampleRate, mChannels, mBps, mBitRate, quality);
				int output_frame_size = 0;
				if (m_encoder != NULL) {
					output_frame_size = m_encoder->Start();
					if (output_frame_size <= 0) {
						LOGE("[EncThread] Start AAC Encoder samplerate: %d, channel: %d, bps: :%d, objType: %d Failed!", mSampleRate, mChannels, mBitRate, quality);
						// Encoder start failed
						delete m_encoder;
						m_encoder = NULL;
					}
				}
			} else {
				m_encoder->AdjustEncQuality(quality);
			}
		}
	}
}
void CAudioEncoder::int_to_char(int i, unsigned char ch[4])
{
    ch[0] = i>>24;
    ch[1] = (i>>16)&0xFF;
    ch[2] = (i>>8)&0xFF;
    ch[3] = i&0xFF;
}

void CAudioEncoder::setBitRate(int bitrate) {
	if (mBitRate != bitrate) {
		mBitRate = bitrate;
		if (m_encoder) {
			m_encoder->setBitRate(bitrate);
		}
	}
}

}
