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

static opus_uint32 char_to_int(unsigned char ch[4])
{
    return ((opus_uint32)ch[0] << 24) | ((opus_uint32)ch[1] << 16)
        | ((opus_uint32)ch[2] << 8) | (opus_uint32)ch[3];
}

 
int main(int argc, char* argv[])
{
    int err;
    char *inFile, *outFile;
    FILE* fin = NULL;
    FILE* fout = NULL;
    OpusEncoder* enc = NULL;
    OpusDecoder* dec = NULL;
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
    int decode_only = 1;
    int max_frame_size = 48000 * 2;
    size_t num_read;
    int curr_read = 0;
    int sweep_bps = 0;
    int random_framesize = 0, newsize = 0, delayed_celt = 0;
    int sweep_max = 0, sweep_min = 0;
    int random_fec = 0;
    const int(*mode_list)[4] = NULL;
    int nb_modes_in_list = 0;
    int curr_mode = 0;
    int curr_mode_count = 0;
    int mode_switch_time = 48000;
    int nb_encoded = 0;
    int remaining = 0;
    int variable_duration = OPUS_FRAMESIZE_ARG;
    int delayed_decision = 0;
    int ret = EXIT_FAILURE; 
 
    tot_in = tot_out = 0;
    fprintf(stderr, "%s\n", opus_get_version_string()); //打印版本号信息

    args = 1;
     
    //采样率 (e:3 d:2)
    sampling_rate = (opus_int32)atol("48000");

    //帧长度 =  采样率/50  这里是选择但双通道 (e:4 d:3)
    frame_size = sampling_rate / 50;
    channels = 1;
    
    /* defaults: */
    use_vbr = 1;
    max_payload_bytes = MAX_PACKET; //最大装载量 在开始时候定义了 为 1500
    complexity = 10;        //复杂度
    use_inbandfec = 0;
    forcechannels = OPUS_AUTO;
    use_dtx = 0;
    packet_loss_perc = 0;

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

 
    dec = opus_decoder_create(sampling_rate, channels, &err);
    if (err != OPUS_OK) {
        fprintf(stderr, "Cannot create decoder: %s\n", opus_strerror(err));
        goto failure;
    }
    

    bandwidth_string = "auto bandwidth";
  
    fprintf(stderr, "Decoding with %ld Hz output (%d channels)\n",
        (long)sampling_rate, channels);
 

    //先更新in short*型  音频的原始数据
    in = (short*)malloc(max_frame_size * channels * sizeof(short));
    out = (short*)malloc(max_frame_size * channels * sizeof(short));
    /* We need to allocate for 16-bit PCM data, but we store it as unsigned char. */
    //我们需要为16位的PCM数据分配，但是我们将其存储为无符号字符
    fbytes = (unsigned char*)malloc(max_frame_size * channels * sizeof(short));
    data[0] = (unsigned char*)calloc(max_payload_bytes, sizeof(unsigned char));
    
    /**************************************************************************************/
    while (!stop) {
        if (delayed_celt) { // 
            frame_size = newsize;
            delayed_celt = 0;
        } else if (random_framesize && rand() % 20 == 0) {  //如果定义随机帧大小
            newsize = rand() % 6;
            switch (newsize) {
            case 0:
                newsize = sampling_rate / 400;
                break;
            case 1:
                newsize = sampling_rate / 200;
                break;
            case 2:
                newsize = sampling_rate / 100;
                break;
            case 3:
                newsize = sampling_rate / 50;
                break;
            case 4:
                newsize = sampling_rate / 25;
                break;
            case 5:
                newsize = 3 * sampling_rate / 50;
                break;
            }
            while (newsize < sampling_rate / 25 && bitrate_bps - abs(sweep_bps) <= 3 * 12 * sampling_rate / newsize)
                newsize *= 2;
            if (newsize < sampling_rate / 100 && frame_size >= sampling_rate / 100) {
                opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(MODE_CELT_ONLY));
                delayed_celt = 1;
            } else {
                frame_size = newsize;
            }
        }
        if (random_fec && rand() % 30 == 0) {
            opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(rand() % 4 == 0));//	OPUS_SET_INBAND_FEC 配置编码器使用带内前向纠错。
        } 
        unsigned char ch[4];
        num_read = fread(ch, 1, 4, fin);//从文件中读数据
        if (num_read != 4)              //如果没读出四块 就退出
            break;
        len[toggle] = char_to_int(ch);  //讲读出来的字符型转换成int 型       toggle第一次出现地  目前为零 
        if (len[toggle] > max_payload_bytes || len[toggle] < 0) {       
            fprintf(stderr, "Invalid payload length: %d\n", len[toggle]);
            break;
        }
        num_read = fread(ch, 1, 4, fin);
        if (num_read != 4)
            break;
        enc_final_range[toggle] = char_to_int(ch);
        num_read = fread(data[toggle], 1, len[toggle], fin);
        if (num_read != (size_t)len[toggle]) {
            fprintf(stderr, "Ran out of input, "
                            "expecting %d bytes got %d\n",
                len[toggle], (int)num_read);
            break;
        }
         
 
       
        opus_int32 output_samples;
        lost = len[toggle] == 0 || (packet_loss_perc > 0 && rand() % 100 < packet_loss_perc);
        if (lost)
            opus_decoder_ctl(dec, OPUS_GET_LAST_PACKET_DURATION(&output_samples));
        else
            output_samples = max_frame_size;
        if (count >= use_inbandfec) {
            /* delay by one packet when using in-band FEC */
            if (use_inbandfec) {
                if (lost_prev) {
                    /* attempt to decode with in-band FEC from next packet */
                    opus_decoder_ctl(dec, OPUS_GET_LAST_PACKET_DURATION(&output_samples));
                    output_samples = opus_decode(dec, lost ? NULL : data[toggle], len[toggle], out, output_samples, 1);
                } else {
                    /* regular decode */
                    output_samples = max_frame_size;
                    output_samples = opus_decode(dec, data[1 - toggle], len[1 - toggle], out, output_samples, 0);
                }
            } else {
                output_samples = opus_decode(dec, lost ? NULL : data[toggle], len[toggle], out, output_samples, 0);
            }
            if (output_samples > 0) {
                if (!decode_only && tot_out + output_samples > tot_in) {
                    stop = 1;
                    output_samples = (opus_int32)(tot_in - tot_out);
                }
                if (output_samples > skip) {
                    int i;
                    for (i = 0; i < (output_samples - skip) * channels; i++) {
                        short s;
                        s = out[i + (skip * channels)];
                        fbytes[2 * i] = s & 0xFF;
                        fbytes[2 * i + 1] = (s >> 8) & 0xFF;
                    }
                    if (fwrite(fbytes, sizeof(short) * channels, output_samples - skip, fout) != (unsigned)(output_samples - skip)) {
                        fprintf(stderr, "Error writing.\n");
                        goto failure;
                    }
                    tot_out += output_samples - skip;
                }
                if (output_samples < skip)
                    skip -= output_samples;
                else
                    skip = 0;
            } else {
                fprintf(stderr, "error decoding frame: %s\n",
                    opus_strerror(output_samples));
            }
            tot_samples += output_samples;
        }

            opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&dec_final_range));
        /* compare final range encoder rng values of encoder and decoder */
        if (enc_final_range[toggle ^ use_inbandfec] != 0 && decode_only
            && !lost && !lost_prev
            && dec_final_range != enc_final_range[toggle ^ use_inbandfec]) {
            fprintf(stderr, "Error: Range coder state mismatch "
                            "between encoder and decoder "
                            "in frame %ld: 0x%8lx vs 0x%8lx\n",
                (long)count,
                (unsigned long)enc_final_range[toggle ^ use_inbandfec],
                (unsigned long)dec_final_range);
            goto failure;
        }

        lost_prev = lost;
        if (count >= use_inbandfec) {
            /* count bits */
            bits += len[toggle] * 8;
            bits_max = (len[toggle] * 8 > bits_max) ? len[toggle] * 8 : bits_max;
            bits2 += len[toggle] * len[toggle] * 64;
            if (!decode_only) {
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
        }
        count++;
        toggle = (toggle + use_inbandfec) & 1;
    }

    if (decode_only && count > 0)
        frame_size = (int)(tot_samples / count);
    count -= use_inbandfec;
    if (tot_samples >= 1 && count > 0 && frame_size) {
        /* Print out bitrate statistics */
        double var;
        fprintf(stderr, "average bitrate:             %7.3f kb/s\n",
            1e-3 * bits * sampling_rate / tot_samples);
        fprintf(stderr, "maximum bitrate:             %7.3f kb/s\n",
            1e-3 * bits_max * sampling_rate / frame_size);
        if (!decode_only)
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
    opus_decoder_destroy(dec);
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


