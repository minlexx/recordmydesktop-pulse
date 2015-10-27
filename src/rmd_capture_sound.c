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
#include "rmd_capture_sound.h"

#include "rmd_jack.h"
#include "rmd_opendev.h"
#include "rmd_types.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>


void *CaptureSound(ProgData *pdata){

#ifdef HAVE_LIBASOUND
    int frames=pdata->periodsize;
    // ^^ approximate period size in frames
#endif
    //start capturing only after first frame is taken
    usleep(pdata->frametime);

    // while recording, loop
    while(pdata->running) {
        int sret=0;
        SndBuffer *newbuf,*tmp;
        if (pdata->paused) {
#ifdef HAVE_LIBASOUND
#ifdef HAVE_LIBPULSE_SIMPLE
            if( pdata->using_pulseaudio ) {
                // compiled with PA support AND actually using PA
                // just wait on mutex, don't do anything
                pthread_mutex_lock(&pdata->pause_mutex);
                pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
                pthread_mutex_unlock(&pdata->pause_mutex);
                // discard any data in audio buffer
                int pa_error = 0;
                pa_simple_flush(pdata->paconn_handle, &pa_error);
            }
#endif
            if( !pdata->using_pulseaudio ) {
                if( !pdata->hard_pause ) {
                    snd_pcm_pause(pdata->sound_handle,1);
                    pthread_mutex_lock(&pdata->pause_mutex);
                    pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
                    pthread_mutex_unlock(&pdata->pause_mutex);
                    snd_pcm_pause(pdata->sound_handle,0);
                } else {
                    // device doesn't support pause(is this the norm?mine doesn't)
                    // if sound device doesn't support pause
                    // we have to close and reopen
                    snd_pcm_close(pdata->sound_handle);
                    pthread_mutex_lock(&pdata->pause_mutex);
                    pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
                    pthread_mutex_unlock(&pdata->pause_mutex);
                    pdata->sound_handle=
                        OpenDev(pdata->args.device,
                                &pdata->args.channels,
                                &pdata->args.frequency,
                                &pdata->args.buffsize,
                                NULL,
                                NULL,
                                NULL//let's hope that the device capabilities
                                    //didn't magically change
                                );
                    if(pdata->sound_handle==NULL){
                        fprintf(stderr,"Couldn't reopen sound device.Exiting\n");
                        pdata->running = FALSE;
                        errno=3;
                        pthread_exit(&errno);
                    }
                }
            } // not using pulseaudio
#else
            close(pdata->sound_handle);
            pthread_mutex_lock(&pdata->pause_mutex);
            pthread_cond_wait(&pdata->pause_cond, &pdata->pause_mutex);
            pthread_mutex_unlock(&pdata->pause_mutex);
            pdata->sound_handle=
                OpenDev(pdata->args.device,
                        pdata->args.channels,
                        pdata->args.frequency);
            if(pdata->sound_handle<0){
                fprintf(stderr,"Couldn't reopen sound device.Exiting\n");
                pdata->running = FALSE;
                errno=3;
                pthread_exit(&errno);
            }
#endif
        }

        //create new buffer
        newbuf=(SndBuffer *)malloc(sizeof(SndBuffer));
        newbuf->data = NULL;
        size_t newbuf_datasize = 0;
#ifdef HAVE_LIBASOUND
        if( !pdata->using_pulseaudio) {
            // using pure alsa
            newbuf->data=(signed char *)malloc(frames*pdata->sound_framesize);
            newbuf_datasize = frames*pdata->sound_framesize;
            newbuf->datasize = newbuf_datasize;
        } else {
            // for pulseaudio allocate buffer as for #else branch
            newbuf->data=(signed char *)malloc((pdata->args.buffsize << 1) * pdata->args.channels);
            newbuf_datasize = (pdata->args.buffsize << 1) * pdata->args.channels;
            newbuf->datasize = newbuf_datasize;
        }
#else
        newbuf->data=(signed char *)malloc(((pdata->args.buffsize<<1)*
                                            pdata->args.channels));
        newbuf_datasize = (pdata->args.buffsize << 1) * pdata->args.channels;
        newbuf->datasize = newbuf_datasize;
#endif
        newbuf->next=NULL;

        //read data into new buffer
#ifdef HAVE_LIBASOUND
#ifdef HAVE_LIBPULSE_SIMPLE
        if( pdata->using_pulseaudio ) {
            int pa_error = 0;
            int ret = pa_simple_read(pdata->paconn_handle, newbuf->data,
                    newbuf_datasize, &pa_error);
            if( ret < 0 ) {
                fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(pa_error));
                pdata->running = FALSE;
            }
        }
#endif
        if( !pdata->using_pulseaudio) {
            // using pure alsa
            // read interleaved PCM data until `frames` num frames read
            while(sret<frames){
                int temp_sret=snd_pcm_readi(pdata->sound_handle,
                                    newbuf->data+pdata->sound_framesize*sret,
                                    frames-sret);
                if(temp_sret==-EPIPE){
                    fprintf(stderr,"%s: Overrun occurred.\n",
                                   snd_strerror(temp_sret));
                    snd_pcm_prepare(pdata->sound_handle);
                }
                else if (temp_sret<0){
                    fprintf(stderr,"An error occured while reading sound data:\n"
                                   " %s\n",
                                   snd_strerror(temp_sret));
                    snd_pcm_prepare(pdata->sound_handle);
                }
                else
                    sret+=temp_sret;
            }
        }
#else
        sret=0;
        //oss recording loop
        do{
            int temp_sret=read(pdata->sound_handle,
                               &newbuf->data[sret],
                               ((pdata->args.buffsize<<1)*
                                pdata->args.channels)-sret);
            if(temp_sret<0){
                fprintf(stderr,"An error occured while reading from soundcard"
                               "%s\n"
                               "Error description:\n"
                               "%s\n",pdata->args.device,strerror(errno));
            }
            else
                sret+=temp_sret;
        }while(sret<((pdata->args.buffsize<<1)*
                     pdata->args.channels));
#endif
        // queue the new buffer
        pthread_mutex_lock( &pdata->sound_buffer_mutex );
        tmp = pdata->sound_buffer;
        if( pdata->sound_buffer == NULL ) {
            pdata->sound_buffer = newbuf;
        } else {
            // go by the linked list till the end
            while( tmp->next != NULL )
                tmp = tmp->next;
            tmp->next = newbuf;
        }
        pthread_mutex_unlock( &pdata->sound_buffer_mutex );

        //signal that there are data to be proccessed
        pthread_mutex_lock( &pdata->snd_buff_ready_mutex );
        pthread_cond_signal( &pdata->sound_data_read );
        pthread_mutex_unlock( &pdata->snd_buff_ready_mutex );
    }
#ifdef HAVE_LIBASOUND
#ifdef HAVE_LIBPULSE_SIMPLE
    if( pdata->using_pulseaudio ) {
        if( pdata->paconn_handle ) {
            pa_simple_free( pdata->paconn_handle );
            pdata->paconn_handle = NULL;
        }
    }
#endif
    if( !pdata->using_pulseaudio)
        snd_pcm_close( pdata->sound_handle );
#else
    close(pdata->sound_handle);
#endif
    pthread_exit( &errno );
}

