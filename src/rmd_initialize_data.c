/******************************************************************************
*                            recordMyDesktop                                  *
*******************************************************************************
*                                                                             *
*            Copyright (C) 2006,2007,2008 John Varouhakis                     *
*                                                                             *
*                                                                             *
*   This program is free software; you can redistribute it and/or modify      *
*   it under the terms of the GNU General Public License as published by      *
*   the Free Software Foundation; either version 2 of the License, or         *
*   (at your option) any later version.                                       *
*                                                                             *
*   This program is distributed in the hope that it will be useful,           *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
*   GNU General Public License for more details.                              *
*                                                                             *
*   You should have received a copy of the GNU General Public License         *
*   along with this program; if not, write to the Free Software               *
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA  *
*                                                                             *
*                                                                             *
*                                                                             *
*   For further information contact me at johnvarouhakis@gmail.com            *
******************************************************************************/

#include "config.h"
#include "rmd_initialize_data.h"

#include "rmd_cache.h"
#include "rmd_init_encoder.h"
#include "rmd_jack.h"
#include "rmd_make_dummy_pointer.h"
#include "rmd_opendev.h"
#include "rmd_yuv_utils.h"
#include "rmd_types.h"

#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBASOUND
static void FixBufferSize(snd_pcm_uframes_t *buffsize) {
    snd_pcm_uframes_t buffsize_t=*buffsize,
#else
static void FixBufferSize(u_int32_t *buffsize) {
    u_int32_t buffsize_t=*buffsize,
#endif
                          buffsize_ret=1;
    while(buffsize_t>1){
        buffsize_t>>=1;
        buffsize_ret<<=1;
    }
    // fix: output buffer size adjustment only if it really happened
    if( (int)buffsize_ret != ((int)*buffsize) )
        fprintf(stderr, "Buffer size adjusted to %d from %d frames.\n",
                   (int)buffsize_ret,(int)*buffsize);
}

int InitializeData(ProgData *pdata,
                   EncData *enc_data,
                   CacheData *cache_data){
    int i;

    fprintf(stderr,"Initializing...\n");
    MakeMatrices();
    if(pdata->args.have_dummy_cursor){
        pdata->dummy_pointer = MakeDummyPointer(pdata->dpy,
                                                &pdata->specs,
                                                16,
                                                pdata->args.cursor_color,
                                                0,
                                                &pdata->npxl);
        pdata->dummy_p_size=16;
    }
    else
        pdata->dummy_p_size=0;


    pdata->rect_root=NULL;
    pthread_mutex_init(&pdata->sound_buffer_mutex,NULL);
    pthread_mutex_init(&pdata->snd_buff_ready_mutex,NULL);
    pthread_mutex_init(&pdata->img_buff_ready_mutex,NULL);
    pthread_mutex_init(&pdata->theora_lib_mutex,NULL);
    pthread_mutex_init(&pdata->vorbis_lib_mutex,NULL);
    pthread_mutex_init(&pdata->libogg_mutex,NULL);
    pthread_mutex_init(&pdata->yuv_mutex,NULL);
    pthread_mutex_init(&pdata->pause_mutex, NULL);
    pthread_mutex_init(&pdata->time_mutex, NULL);
    pthread_cond_init(&pdata->time_cond,NULL);
    pthread_cond_init(&pdata->pause_cond,NULL);
    pthread_cond_init(&pdata->image_buffer_ready,NULL);
    pthread_cond_init(&pdata->sound_data_read,NULL);
    pthread_cond_init(&pdata->theora_lib_clean,NULL);
    pthread_cond_init(&pdata->vorbis_lib_clean,NULL);
    pdata->th_encoding_clean=pdata->v_encoding_clean=1;
    pdata->avd=0;
    pdata->sound_buffer=NULL;
    pdata->running             = TRUE;
    pdata->paused              = FALSE;
    pdata->aborted             = FALSE;
    pdata->pause_state_changed = FALSE;
    pdata->frames_total        = 0;
    pdata->frames_lost         = 0;
    pdata->encoder_busy        = FALSE;
    pdata->capture_busy        = FALSE;

    if(!pdata->args.nosound){
        if(!pdata->args.use_jack){
            FixBufferSize(&pdata->args.buffsize);
#ifdef HAVE_LIBASOUND
            // first indicate that we do not use pulse
            pdata->using_pulseaudio = FALSE;
#ifdef HAVE_LIBPULSE_SIMPLE
            // first try to connect to pulseaudio,
            // if compiled with pulse support
            pdata->paconn_handle = NULL;
            // fill PulseAudio format dpecifier
            static pa_sample_spec pa_sspec;
            pa_sspec.channels = (uint8_t)pdata->args.channels;
            pa_sspec.format = PA_SAMPLE_S16LE;
            pa_sspec.rate = pdata->args.frequency;
            // use maximum supported default values from PA server (recommended)
            static pa_buffer_attr pa_bufattr;
            pa_bufattr.fragsize = (uint32_t)-1;  // Recording only: fragment size.
            pa_bufattr.maxlength = (uint32_t)-1; // Maximum length of the buffer in bytes.
            pa_bufattr.minreq = (uint32_t)-1; // Playback only: minimum request.
            pa_bufattr.prebuf = (uint32_t)-1; // Playback only: pre-buffering.
            pa_bufattr.tlength = (uint32_t)-1; // Playback only: target length of the buffer.
            static int pa_error = 0;
            printf("Compiled with PulseAudio library version: %s\n",
                    pa_get_library_version());
            printf("Initializing audio: trying PulseAudio first...\n");
            pdata->paconn_handle = pa_simple_new(
                NULL, // default PA server
                PACKAGE_NAME, // app name
                PA_STREAM_RECORD,  // stream direction
                NULL, // default device
                "record", // stream name
                &pa_sspec,  // format spec
                NULL, // default channel map
                &pa_bufattr, // may be NULL for defaults, but we want tweak!
                &pa_error);
            if( pdata->paconn_handle == NULL ) {
                fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(pa_error));
                printf("PulseAudio failed, will try ALSA.\n");
            } else {
                // indicate that we successfully connected to PA server
                // now no need to use ALSA
                pdata->using_pulseaudio = TRUE;
                pdata->sound_framesize = pdata->args.channels * 2;
                pdata->periodsize = pdata->args.buffsize / pdata->sound_framesize; // ?
                // ^^ we need to initialize it here
                // how many frames fit in buffer
                pdata->hard_pause = FALSE; // do not support HW pause
                // time that a single audio frame lasts (microsecs)
                float aframe_time_microsecs = 1000000.0f / (float)pdata->args.frequency;
                // time that a sound buffer lasts (microsecs)
                pdata->periodtime = (unsigned int)( (float)aframe_time_microsecs * (float)pdata->periodsize);
                // output
                printf("Connected to PulseAudio server, will not try ALSA.\n");
                printf("  sound_framesize = %d bytes\n", pdata->sound_framesize);
                printf("  periodsize = %lu frames\n", pdata->periodsize);
                printf("  periodtime = %u microseconds\n", pdata->periodtime);
            }
#endif
            // try open ALSA if not compiled with pulse support
            //    or pulseaudio connection failed
            if( !pdata->using_pulseaudio ) {
                pdata->sound_handle=OpenDev( pdata->args.device,
                                            &pdata->args.channels,
                                            &pdata->args.frequency,
                                            &pdata->args.buffsize,
                                            &pdata->periodsize,
                                            &pdata->periodtime,
                                            &pdata->hard_pause);
                // snd_pcm_format_width — return the bit-width of the format
                pdata->sound_framesize=((snd_pcm_format_width(
                                         SND_PCM_FORMAT_S16_LE))/8)*
                                         pdata->args.channels;
                // ^^ size of one sound frame in bytes
                // pdata->periodsize is approximate period size in frames
            }

            if( (pdata->sound_handle == NULL) && !pdata->using_pulseaudio) {
#else
            pdata->sound_handle=OpenDev(pdata->args.device,
                                        pdata->args.channels,
                                        pdata->args.frequency);
            pdata->periodtime=(1000000*pdata->args.buffsize)/
                            ((pdata->args.channels<<1)*pdata->args.frequency);
            //when using OSS periodsize serves as an alias of buffsize
            pdata->periodsize=pdata->args.buffsize;
            pdata->sound_framesize=pdata->args.channels<<1;
            if(pdata->sound_handle<0){
#endif
                fprintf(stderr,"Error while opening/configuring ALSA soundcard %s\n"
                            "Try running with the --no-sound or specify a "
                            "correct device.\n",
                            pdata->args.device);
                return 3;
            }
        }
        else{
#ifdef HAVE_LIBJACK
            int jack_error=0;
            pdata->jdata->port_names=pdata->args.jack_port_names;
            pdata->jdata->nports=pdata->args.jack_nports;
            pdata->jdata->ringbuffer_secs=pdata->args.jack_ringbuffer_secs;
            pdata->jdata->snd_buff_ready_mutex=&pdata->snd_buff_ready_mutex;
            pdata->jdata->sound_data_read=&pdata->sound_data_read;
            pdata->jdata->capture_started=0;

            if((jack_error=StartJackClient(pdata->jdata))!=0)
                return jack_error;

            pdata->args.buffsize=pdata->jdata->buffersize;
            pdata->periodsize=pdata->args.buffsize;
            pdata->args.frequency=pdata->jdata->frequency;
            pdata->args.channels=pdata->jdata->nports;
            pdata->periodtime=(1000000*pdata->args.buffsize)/
                              pdata->args.frequency;
            pdata->sound_framesize=sizeof(jack_default_audio_sample_t)*
                                   pdata->jdata->nports;

#else
            fprintf(stderr,"Should not be here!\n");
            exit(-1);
#endif
        }
    }

    if(pdata->args.encOnTheFly)
        InitEncoder(pdata,enc_data,0);
    else
        InitCacheData(pdata,enc_data,cache_data);

    for(i=0;i<(pdata->enc_data->yuv.y_width*pdata->enc_data->yuv.y_height);i++)
        pdata->enc_data->yuv.y[i]=0;
    for(i=0;
        i<(pdata->enc_data->yuv.uv_width*pdata->enc_data->yuv.uv_height);
        i++){
        pdata->enc_data->yuv.v[i]=pdata->enc_data->yuv.u[i]=127;
    }

    yblocks=malloc(sizeof(u_int32_t)*(pdata->enc_data->yuv.y_width/Y_UNIT_WIDTH)*
                (pdata->enc_data->yuv.y_height/Y_UNIT_WIDTH));
    ublocks=malloc(sizeof(u_int32_t)*(pdata->enc_data->yuv.y_width/Y_UNIT_WIDTH)*
                (pdata->enc_data->yuv.y_height/Y_UNIT_WIDTH));
    vblocks=malloc(sizeof(u_int32_t)*(pdata->enc_data->yuv.y_width/Y_UNIT_WIDTH)*
                (pdata->enc_data->yuv.y_height/Y_UNIT_WIDTH));

    pdata->frametime=(1000000)/pdata->args.fps;
    return 0;

}

void SetupDefaultArgs(ProgArgs *args) {
    
    args->delay                = 0;
    args->windowid             = 0;
    args->x                    = 0;
    args->y                    = 0;
    args->width                = 0;
    args->height               = 0;
    args->rescue_path          = NULL;
    args->nosound              = 0;
    args->full_shots           = 0;
    args->follow_mouse         = 0;
    args->encOnTheFly          = 0;
    args->nowmcheck            = 0;
    args->overwrite            = 0;
    args->use_jack             = 0;
    args->noshared             = 0;
    args->no_encode            = 0;
    args->noframe              = 0;
    args->jack_nports          = 0;
    args->jack_ringbuffer_secs = 3.0;
    args->jack_port_names      = NULL;
    args->zerocompression      = 1;
    args->no_quick_subsample   = 1;
    args->cursor_color         = 1;
    args->have_dummy_cursor    = 0;
    args->xfixes_cursor        = 1;
    args->fps                  = 15;
    args->channels             = 1;
    args->frequency            = 22050;
    args->buffsize             = 4096;
    args->v_bitrate            = 0; // changed to 0 from 45000 by gentoo patch
    args->v_quality            = 63;
    args->s_quality            = 10;

    if (getenv("DISPLAY") != NULL) {
        args->display = (char *) malloc(strlen(getenv("DISPLAY")) + 1);
        strcpy(args->display, getenv("DISPLAY"));
    }
    else {
        args->display = NULL;
    }

    args->device = (char *) malloc(strlen(DEFAULT_AUDIO_DEVICE) + 1);
    strcpy(args->device, DEFAULT_AUDIO_DEVICE);

    args->workdir = (char *) malloc(5);
    strcpy(args->workdir, "/tmp");

    args->pause_shortcut = (char *) malloc(15);
    strcpy(args->pause_shortcut, "Control+Mod1+p");

    args->stop_shortcut = (char *) malloc(15);
    strcpy(args->stop_shortcut, "Control+Mod1+s");

    args->filename = (char *) malloc(8);
    strcpy(args->filename, "out.ogv");
}

void CleanUp(void){

    free(yblocks);
    free(ublocks);
    free(vblocks);

}
