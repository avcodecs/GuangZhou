#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "debug.h"
#include "opus.h"
#include "opus_multistream.h"
#include "opus_private.h"
#include "opus_types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKET 1500

void print_usage(char* argv[])
{
    fprintf(stderr, "Usage: %s [-e] <application> <sampling rate (Hz)> <channels (1/2)> "
                    "<bits per second>  [options] <input> <output>\n",
        argv[0]);
    fprintf(stderr, "       %s -d <sampling rate (Hz)> <channels (1/2)> "
                    "[options] <input> <output>\n\n",
        argv[0]);
    fprintf(stderr, "application: voip | audio | restricted-lowdelay\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "-e                   : only runs the encoder (output the bit-stream)\n");
    fprintf(stderr, "-d                   : only runs the decoder (reads the bit-stream as input)\n");
    fprintf(stderr, "-cbr                 : enable constant bitrate; default: variable bitrate\n");
    fprintf(stderr, "-cvbr                : enable constrained variable bitrate; default: unconstrained\n");
    fprintf(stderr, "-delayed-decision    : use look-ahead for speech/music detection (experts only); default: disabled\n");
    fprintf(stderr, "-bandwidth <NB|MB|WB|SWB|FB> : audio bandwidth (from narrowband to fullband); default: sampling rate\n");
    fprintf(stderr, "-framesize <2.5|5|10|20|40|60|80|100|120> : frame size in ms; default: 20 \n");
    fprintf(stderr, "-max_payload <bytes> : maximum payload size in bytes, default: 1024\n");
    fprintf(stderr, "-complexity <comp>   : complexity, 0 (lowest) ... 10 (highest); default: 10\n");
    fprintf(stderr, "-inbandfec           : enable SILK inband FEC\n");
    fprintf(stderr, "-forcemono           : force mono encoding, even for stereo input\n");
    fprintf(stderr, "-dtx                 : enable SILK DTX\n");
    fprintf(stderr, "-loss <perc>         : simulate packet loss, in percent (0-100); default: 0\n");
}

static void int_to_char(opus_uint32 i, unsigned char ch[4])
{
    ch[0] = i >> 24;
    ch[1] = (i >> 16) & 0xFF;
    ch[2] = (i >> 8) & 0xFF;
    ch[3] = i & 0xFF;
}
 
int main(int argc, char* argv[])
{
    int err;
    char *inFile, *outFile;
    FILE* fin = NULL;
    FILE* fout = NULL;
    OpusEncoder* enc = NULL; 
    int args;
    int len[2];
    int frame_size, channels;
    opus_int32 bitrate_bps = 0;
    unsigned char* data[2] = { NULL, NULL };
    unsigned char* fbytes = NULL;
    opus_int32 sampling_rate;
    int use_vbr;
    int max_payload_bytes;
    int complexity;
    int use_inbandfec;
    int use_dtx;
    int forcechannels;
    int cvbr = 0;
    int packet_loss_perc;
    opus_int32 count = 0, count_act = 0;
    int k;
    opus_int32 skip = 0;
    int stop = 0;
    short* in = NULL;
    short* out = NULL;
    int application = OPUS_APPLICATION_AUDIO;
    double bits = 0.0, bits_max = 0.0, bits_act = 0.0, bits2 = 0.0, nrg;
    double tot_samples = 0;
    opus_uint64 tot_in, tot_out;
    int bandwidth = OPUS_AUTO;
    const char* bandwidth_string;
    int lost = 0, lost_prev = 1;
    int toggle = 0;
    opus_uint32 enc_final_range[2];
    opus_uint32 dec_final_range;
    int encode_only = 1;
    int max_frame_size = 48000 * 2;
    size_t num_read;
    int curr_read = 0;
    int sweep_bps = 0;
    int random_framesize = 0, newsize = 0, delayed_celt = 0;
    int sweep_max = 0, sweep_min = 0;
    int random_fec = 0; 
    int nb_modes_in_list = 0;
    int curr_mode = 0;
    int curr_mode_count = 0;
    int mode_switch_time = 48000;
    int nb_encoded = 0;
    int remaining = 0;
    int variable_duration = OPUS_FRAMESIZE_ARG;
    int ret = EXIT_FAILURE;
    //  这里的args设计的非常好
    //  从传参的顺序 每次经过一次检测 自加1
    
    tot_in = tot_out = 0;
    fprintf(stderr, "%s\n", opus_get_version_string()); //打印版本号信息

    args = 1;
  
    application = OPUS_APPLICATION_VOIP;
      
    //采样率 (e:3 d:2)
    sampling_rate = (opus_int32)atol("48000"); 
   
    //帧长度 =  采样率/50  这里是选择但双通道 (e:4 d:3)
    frame_size = sampling_rate / 50;
    channels = 1;  

    //编码通道  比特率的赋值 (e:5 d:4)
    bitrate_bps = (opus_int32)atol("16000"); 
    
     
    /* defaults: */
    use_vbr = 1;
    max_payload_bytes = MAX_PACKET; //最大装载量 在开始时候定义了 为 1500
    complexity = 10;        //复杂度
    use_inbandfec = 0;
    forcechannels = OPUS_AUTO;
    use_dtx = 0;
    packet_loss_perc = 0;

    if (sweep_max)      //清扫的有最大值  我就设立最小值位比特率
        sweep_min = bitrate_bps;

    //如果设定的最大装载量比零小 比最大的装载量更大 合理吗？ 不合理 下机！
    if (max_payload_bytes < 0 || max_payload_bytes > MAX_PACKET) {
        fprintf(stderr, "max_payload_bytes must be between 0 and %d\n",
            MAX_PACKET);
        goto failure;
    }

    //需要被编码的文件 让我赋值给fin 
    inFile = argv[argc - 2];
    fin = fopen(inFile, "rb");
    if (!fin) {
        fprintf(stderr, "Could not open input file %s\n", argv[argc - 2]);
        goto failure;
    }
    

    //输出文件  只读方式打开
    outFile = argv[argc - 1];
    fout = fopen(outFile, "wb+");
    if (!fout) {
        fprintf(stderr, "Could not open output file %s\n", argv[argc - 1]);
        goto failure;
    }

 
    //首先创造出编码器 不然怎么编码呢
    enc = opus_encoder_create(sampling_rate, channels, application, &err);
    if (err != OPUS_OK) {
        fprintf(stderr, "Cannot create encoder: %s\n", opus_strerror(err));
        goto failure;
    }
    //编码器信息写好 对 信息写好 
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));
    opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(bandwidth));
    opus_encoder_ctl(enc, OPUS_SET_VBR(use_vbr));
    opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(cvbr));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(use_inbandfec));
    opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS(forcechannels));
    opus_encoder_ctl(enc, OPUS_SET_DTX(use_dtx));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));

    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&skip));
    opus_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(16));
    opus_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(variable_duration));
 
    bandwidth_string = "auto bandwidth";
    
    fprintf(stderr, "Encoding %ld Hz input at %.3f kb/s "
                    "in %s with %d-sample frames.\n",
        (long)sampling_rate, bitrate_bps * 0.001,
        bandwidth_string, frame_size);

    //先更新in short*型  音频的原始数据
    in = (short*)malloc(max_frame_size * channels * sizeof(short));
    out = (short*)malloc(max_frame_size * channels * sizeof(short));
    /* We need to allocate for 16-bit PCM data, but we store it as unsigned char. */
    //我们需要为16位的PCM数据分配，但是我们将其存储为无符号字符
    fbytes = (unsigned char*)malloc(max_frame_size * channels * sizeof(short));
    data[0] = (unsigned char*)calloc(max_payload_bytes, sizeof(unsigned char));
    if (use_inbandfec) {    //要是设了inbandfec 我就有值  没有设 他啥也不是
        data[1] = (unsigned char*)calloc(max_payload_bytes, sizeof(unsigned char));
    }

    while (!stop) {
               //没错 这里就是 编码通道
            int i;
            num_read = fread(fbytes, sizeof(short) * channels, frame_size - remaining, fin); //fbytes空间已分配  
            curr_read = (int)num_read;
            tot_in += curr_read;
            for (i = 0; i < curr_read * channels; i++) {
                opus_int32 s;
                s = fbytes[2 * i + 1] << 8 | fbytes[2 * i];
                s = ((s & 0xFFFF) ^ 0x8000) - 0x8000;
                in[i + remaining * channels] = s;
            }
            if (curr_read + remaining < frame_size) {
                for (i = (curr_read + remaining) * channels; i < frame_size * channels; i++)
                    in[i] = 0;
                if (encode_only)
                    stop = 1;
            }
            len[toggle] = opus_encode(enc, in, frame_size, data[toggle], max_payload_bytes);
            nb_encoded = opus_packet_get_samples_per_frame(data[toggle], sampling_rate) * opus_packet_get_nb_frames(data[toggle], len[toggle]);
            remaining = frame_size - nb_encoded;
            for (i = 0; i < remaining * channels; i++)
                in[i] = in[nb_encoded * channels + i];
            if (sweep_bps != 0) {
                bitrate_bps += sweep_bps;
                if (sweep_max) {
                    if (bitrate_bps > sweep_max)
                        sweep_bps = -sweep_bps;
                    else if (bitrate_bps < sweep_min)
                        sweep_bps = -sweep_bps;
                }
                /* safety */
                if (bitrate_bps < 1000)
                    bitrate_bps = 1000;
                opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));
            }
            opus_encoder_ctl(enc, OPUS_GET_FINAL_RANGE(&enc_final_range[toggle]));
            if (len[toggle] < 0) {
                fprintf(stderr, "opus_encode() returned %d\n", len[toggle]);
                goto failure;
            }
            curr_mode_count += frame_size;
            if (curr_mode_count > mode_switch_time && curr_mode < nb_modes_in_list - 1) {
                curr_mode++;
                curr_mode_count = 0;
            }
        

  
            unsigned char int_field[4];
            int_to_char(len[toggle], int_field);
            if (fwrite(int_field, 1, 4, fout) != 4) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
            int_to_char(enc_final_range[toggle], int_field);
            if (fwrite(int_field, 1, 4, fout) != 4) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
            if (fwrite(data[toggle], 1, len[toggle], fout) != (unsigned)len[toggle]) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
            tot_samples += nb_encoded;
     

        lost_prev = lost;
        if (count >= use_inbandfec) {
            /* count bits */
            bits += len[toggle] * 8;
            bits_max = (len[toggle] * 8 > bits_max) ? len[toggle] * 8 : bits_max;
            bits2 += len[toggle] * len[toggle] * 64;
             
                nrg = 0.0;
                for (k = 0; k < frame_size * channels; k++) {
                    nrg += in[k] * (double)in[k];
                }
                nrg /= frame_size * channels;
                if (nrg > 1e5) {
                    bits_act += len[toggle] * 8;
                    count_act++;
                } 
        }
        count++;
        toggle = (toggle + use_inbandfec) & 1;
    }

    count -= use_inbandfec;
    if (tot_samples >= 1 && count > 0 && frame_size) {
        /* Print out bitrate statistics */
        double var;
        fprintf(stderr, "average bitrate:             %7.3f kb/s\n",
            1e-3 * bits * sampling_rate / tot_samples);
        fprintf(stderr, "maximum bitrate:             %7.3f kb/s\n",
            1e-3 * bits_max * sampling_rate / frame_size);
         
            fprintf(stderr, "active bitrate:              %7.3f kb/s\n",
                1e-3 * bits_act * sampling_rate / (1e-15 + frame_size * (double)count_act));
        var = bits2 / count - bits * bits / (count * (double)count);
        if (var < 0)
            var = 0;
        fprintf(stderr, "bitrate standard deviation:  %7.3f kb/s\n",
            1e-3 * sqrt(var) * sampling_rate / frame_size);
    } else {
        fprintf(stderr, "bitrate statistics are undefined\n");
    }
    silk_TimerSave("opus_timing.txt");
    ret = EXIT_SUCCESS;
failure:
    opus_encoder_destroy(enc); 
    free(data[0]);
    free(data[1]);
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    free(in);
    free(out);
    free(fbytes);
    return ret;
}


