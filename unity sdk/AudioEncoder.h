#pragma once

#include "yyaudio.h"
#include "SdkConfig.h"
#include "rawfilewriter.h"

namespace yymobile
{

class CAudioEncoder {
public:
	CAudioEncoder(const char * name = NULL);
	virtual ~CAudioEncoder();

public:
	int prepare(int codec, int sampleFrequency, int chans, int bps, int bitrate, int quality);
	int encode(unsigned char *inputData, unsigned int length, unsigned char* outputData, bool silence);
	void reset();
	int getNumberOfPendingBytes();
    void setEncQuality(int quality);
    int getEncQuality() { return mQuality; }
	void setBitRate(int quality);
	void int_to_char(int i, unsigned char ch[4]);
private:
	AudioEncoder* m_encoder;
	CFileWriter m_encodeFileWriter;
	int m_codec;
	int mSampleRate;
	int mChannels;
	int mBps;
	int mBitRate;
	int mQuality;
	
	char mName[64];
};

}
