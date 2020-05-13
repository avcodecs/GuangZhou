#ifdef CODEC_HAVE_SILK

#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "silk_encoder.h"
#include "trace_log.h"
#include "AudioUnitWrapper.h"
#ifdef YYMOBILE_ANDROID
extern "C" int android_getCpuCount();
#endif


#define FRAME_LENGTH_MS         20
static int silkbps[] = {6000,  8000, 12000, 14000, 16000, 18000, 20000, 22000, 24000, 40000, 100000	};

namespace yymobile
{

CSilkEncoder::CSilkEncoder(int sampleFrequency, int channels, int bps, int bitrate, int quality):
    m_sampleFrequency(sampleFrequency), m_channels(channels), m_bps(bps), m_bitrate(bitrate), m_quality(quality), 
    m_lastQuality(quality), m_pSilkEnc(NULL), m_useDTX(0), m_complexity(0), m_EncCount(0),
    m_resampler(NULL), m_resampled_data(NULL), m_resample_data_size(0),mResamplerForSilk("silkcoder Adapter")
{
    m_inputFrameSize = sampleFrequency* FRAME_LENGTH_MS  / 1000 * bps/8;
}

CSilkEncoder::~CSilkEncoder(){
    Stop();
}

int CSilkEncoder::Start(){
	if(m_bps != 16 )
		return -1;

	int encSizeBytes;
    int ret = SKP_Silk_SDK_Get_Encoder_Size( &encSizeBytes );
	if( ret ) {
		return -1;
	 }

	m_pSilkEnc = malloc( encSizeBytes );

    ret = SKP_Silk_SDK_InitEncoder( m_pSilkEnc, &m_encControl );
    if( ret ) {
		return -1;
	}

#ifdef YYMOBILE_ANDROID
    int coreCount = android_getCpuCount();
    if( coreCount >= 4 )
    {
        m_complexity=1;
		LOGD("### silk encoder, use complexity : %d, core count :%d", m_complexity, coreCount);
    }
#endif

	 /* Set Encoder parameters */
    m_encControl.API_sampleRate       = m_sampleFrequency;
    m_encControl.maxInternalSampleRate  = 24000;
    m_encControl.packetSize           = m_sampleFrequency * FRAME_LENGTH_MS/1000;
    m_encControl.packetLossPercentage = 0;
    m_encControl.useInBandFEC         = 0;
    m_encControl.useDTX               = m_useDTX;
    m_encControl.complexity           = m_complexity;
    m_encControl.bitRate              = silkbps[m_quality];

	/*
	if(m_sampleFrequency != yymobile::kDefaultSampleRate)
	{
		LOGD("### silk encoder, need resample: %d -> %d", yymobile::kDefaultSampleRate, m_sampleFrequency);
		m_resampler = new webrtc::Resampler(yymobile::kDefaultSampleRate, m_sampleFrequency, webrtc::kResamplerSynchronous);
		m_resample_data_size = m_inputFrameSize * 2;
		m_resampled_data = (char*) malloc(m_resample_data_size);

		LOGD("### silk encoder: input_frame(%d), output_frame(%d), resample_frame(%d)",
				m_inputFrameSize, (yymobile::kMaxEncodedBytesPerPacket + 2), m_resample_data_size);
	}
	*/
    
    if(m_sampleFrequency != yymobile::kSampleRate44K1)
    {
        m_resample_data_size = m_inputFrameSize * 2;
        m_resampled_data = (char*) malloc(m_resample_data_size);
    }
    return (yymobile::kMaxEncodedBytesPerPacket + 2);
}

void CSilkEncoder::Stop(){
	if(m_pSilkEnc != NULL){
		free(m_pSilkEnc);
		m_pSilkEnc = NULL;
	}

    if(m_resampler != NULL)
    {
    	delete m_resampler;
    	m_resampler = NULL;
    }

	if(m_resampled_data != NULL)
	{
		free(m_resampled_data);
		m_resampled_data = NULL;
		m_resample_data_size = 0;
	}
}

int CSilkEncoder::Encode(const unsigned char* inputData, unsigned int len, unsigned char* outputData){
	if(m_pSilkEnc == NULL || inputData == 0 || m_inputFrameSize > len){
		return -1;
	}
    if (m_sampleFrequency == audiosdk::AudioUnitWrapper::kCaptureFormat_SampleRate) {
        if (++m_EncCount >= 150) {
            resetEncQuality();
            m_EncCount = 0;
        }
    }
	int outputLength = 0;
	unsigned int input_len = len;
	const unsigned char* input_data = inputData;
/*
	if(m_resampler != NULL){
		int resamp_len = 0;
		m_resampler->Push((const WebRtc_Word16 *)inputData, len/2,
				(WebRtc_Word16 *)m_resampled_data, m_resample_data_size/2, resamp_len);
		input_data = (const unsigned char*)m_resampled_data;
		input_len = resamp_len*2;
	}
*/
/*
    if(m_sampleFrequency != yymobile::kSampleRate44K1)
    {
	    input_len = mResamplerForSilk.resample(kSampleRate44K1,m_channels,(char *)inputData,len,m_sampleFrequency,m_channels,(char *)m_resampled_data,m_resample_data_size);
    }
*/
	const char* cur = (const char *) input_data;
	short outLen = 0;
	while(m_inputFrameSize <= input_len){
		outLen = yymobile::kMaxEncodedBytesPerPacket;
		int ret = SKP_Silk_SDK_Encode( m_pSilkEnc, &m_encControl, (short*)cur, m_inputFrameSize/2, (SKP_uint8 *)(outputData + outputLength), &outLen);
        if(ret) break;

//#define TOC_ENCODE
#ifdef TOC_ENCODE
		debug_print_bytes((const char*)(m_data + m_nPos), outLen);
		SKP_Silk_TOC_struct skp_toc = {0};
		SKP_Silk_SDK_get_TOC((SKP_uint8 *)(m_data + m_nPos), outLen, &skp_toc);
		LOGD("[SKP_TOC]frames:%d, VAD: %d, %d, %d, %d, %d", skp_toc.framesInPacket,
				skp_toc.vadFlags[0], skp_toc.vadFlags[1],
				skp_toc.vadFlags[2], skp_toc.vadFlags[3], skp_toc.vadFlags[4]);
	
#endif

		outputLength += outLen;
		cur += m_inputFrameSize;
		input_len -= m_inputFrameSize;
	}

	if(input_len > 0){
		LOGW("silk encoder encode remain %d bytes", input_len);
	}
	return outputLength;
}

void CSilkEncoder::AdjustEncQuality(int quality)
{
    if (quality != 0 && quality != 1 && quality != 4 && quality != 8)
    {
        LOGD("EncQuality value error : %d", quality);
        return;
    }
    if (m_quality != quality)
    {
        m_quality = quality;
    }
}

void CSilkEncoder::resetEncQuality()
{
    if (m_lastQuality > m_quality && m_lastQuality >= 1 && m_lastQuality <= 8)
    {
        m_encControl.bitRate = silkbps[--m_lastQuality];
    }
    if (m_lastQuality < m_quality && m_lastQuality >= 0 && m_lastQuality < 8)
    {
        m_encControl.bitRate = silkbps[++m_lastQuality];
    }
	LOGD("[Silk_encoder] quality=%d",m_lastQuality);
}

void CSilkEncoder::setBitRate(int bitrate) {
	LOGD("[Silk_encoder] setBitRate=%d",bitrate);
	for (int qualityIndex = 0; qualityIndex < sizeof(silkbps) / sizeof(int); qualityIndex++) {
		if (silkbps[qualityIndex] >= bitrate) {
			m_quality = qualityIndex;
			break;
		}
	}
}

}

#endif
