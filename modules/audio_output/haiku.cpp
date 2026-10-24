/*****************************************************************************
 * haiku.cpp : Haiku MediaKit audio output plugin for vlc
 *****************************************************************************
 * Copyright (C) 2010-2013 VLC authors and VideoLAN
 * Copyright (C) 2020 Gerasim Troeglazov (3dEyes**)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_haiku.h>

#include <float.h>
#include <sys/param.h>

#include <media/MediaDefs.h>
#include <media/MediaRoster.h>
#include <media/SoundPlayer.h>

#define FRAME_SIZE 2048
#define AUDIO_BUFFER_SIZE_IN_SECONDS 1.0

struct audio_buffer_t
{
    uint8_t    *data;
    int         read_pos;
    int         write_pos;
    int         length;
    int         size;
    vlc_mutex_t mutex;
    vlc_cond_t  cond;
};

typedef struct audio_buffer_t audio_buffer_t;

struct aout_sys_t
{
    audio_buffer_t*       buffer;
	BSoundPlayer*         player;
    bool                  mute;
    float                 volume;
    audio_sample_format_t format;
};

static int  Open     ( vlc_object_t * );
static void Close    ( vlc_object_t * );
static void Play     ( audio_output_t *_p_aout, block_t *block );
static void Pause    ( audio_output_t *, bool, mtime_t );
static void Flush    ( audio_output_t *, bool );
static int  TimeGet  ( audio_output_t *, mtime_t *restrict );
static int  VolumeSet(audio_output_t* aout, float volume);
static int  MuteSet  (audio_output_t* aout, bool mute);
static void ApplyVolume(aout_sys_t* sys);

static void PlayBuffer(void* cookie, void* buffer, size_t size, const media_raw_audio_format &format);

static int  CreateBuffer ( audio_output_t *, int );
static void DestroyBuffer( audio_output_t * );
static int  ReadBuffer   ( audio_output_t *, uint8_t *, int );
static int  WriteBuffer  ( audio_output_t *, uint8_t *, int );

vlc_module_begin ()
    set_shortname(N_("Haiku audio"))
    set_description(N_("Haiku Mediakit audio output"))
    set_capability( "audio output", 100 )
    set_category( CAT_AUDIO )
    set_subcategory( SUBCAT_AUDIO_AOUT )
    set_callbacks( Open, Close )
    add_shortcut("haiku")
vlc_module_end ()


static int Start ( audio_output_t *p_aout, audio_sample_format_t *fmt )
{
    aout_sys_t *p_sys = p_aout->sys;
    media_raw_audio_format hformat;
    
    int i_nb_channels;
    int i_bytes_per_frame;
    audio_sample_format_t format = *fmt;

    if( aout_FormatNbChannels( fmt ) == 0 )
        return VLC_EGENERIC;

    i_nb_channels = aout_FormatNbChannels( &format );

    if ( i_nb_channels >= 2 ) {
        i_nb_channels = 2;
        format.i_physical_channels = AOUT_CHANS_STEREO;
    } else {
        format.i_physical_channels = AOUT_CHAN_CENTER;
    }

    format.i_format = VLC_CODEC_S16N;

    aout_FormatPrepare( &format );

    i_bytes_per_frame = format.i_bytes_per_frame;

    hformat.frame_rate = fmt->i_rate;
    hformat.channel_count = i_nb_channels;
	hformat.format = media_raw_audio_format::B_AUDIO_SHORT;
	hformat.byte_order = B_MEDIA_HOST_ENDIAN;
	hformat.buffer_size = FRAME_SIZE * i_bytes_per_frame;
	
    p_sys->player = new BSoundPlayer(&hformat, "VLC Media Player", PlayBuffer, NULL, p_aout);

    if (p_sys->player->InitCheck() != B_OK) {
        msg_Err( p_aout, "cannot initialize BSoundPlayer");
		delete p_sys->player;
        p_sys->player = NULL;
        return VLC_EGENERIC;
    }

    format.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
    p_sys->format = *fmt = format;

    p_aout->time_get = TimeGet;
    p_aout->play     = Play;
    p_aout->pause    = Pause;
    p_aout->flush    = Flush;

    CreateBuffer( p_aout, AUDIO_BUFFER_SIZE_IN_SECONDS * format.i_rate * format.i_bytes_per_frame );
    
    ApplyVolume(p_sys);
                      
	p_sys->player->Start();

    return VLC_SUCCESS;
}

static void Play (audio_output_t *p_aout, block_t *block)
{
    aout_sys_t *p_sys = p_aout->sys;

    WriteBuffer( p_aout, block->p_buffer, block->i_buffer );

    p_sys->player->SetHasData(true);

    block_Release( block );
}

static void Stop ( audio_output_t *p_aout )
{
    aout_sys_t *p_sys = p_aout->sys;

    p_sys->player->SetHasData(false);
    p_sys->player->Stop(true, false);
    delete p_sys->player;
    p_sys->player = NULL;

    DestroyBuffer( p_aout );
}

static void PlayBuffer(void* cookie, void* p_buffer, size_t i_buf_size, const media_raw_audio_format &format)
{
	VLC_UNUSED( format );
	
    audio_output_t *p_aout = (audio_output_t *)cookie;
    size_t i_len;

    i_len = ReadBuffer( p_aout, (uint8_t*)p_buffer, i_buf_size );
    if(i_len < i_buf_size )
        memset(( uint8_t * )p_buffer + i_len, 0, i_buf_size - i_len );
}

static int Open (vlc_object_t *obj)
{
    audio_output_t *aout = (audio_output_t *)obj;
    aout_sys_t *sys = (aout_sys_t*)calloc( 1, sizeof( aout_sys_t ) );

    if( unlikely( sys == NULL ))
        return VLC_ENOMEM;

    /* The media kit needs a running BApplication */
    if( vlc_haiku_app_Hold( aout ) )
    {
        free( sys );
        return VLC_EGENERIC;
    }

    aout->sys = sys;
    aout->start = Start;
    aout->stop = Stop;
    aout->mute_set = MuteSet;
    aout->volume_set = VolumeSet;

    sys->player = NULL;
    sys->volume = 1.f;
    sys->mute = false;
    aout_VolumeReport(aout, sys->volume);
    aout_MuteReport(aout, sys->mute);

    return VLC_SUCCESS;
}

static void Close( vlc_object_t *obj )
{
    audio_output_t *aout = (audio_output_t *)obj;
    aout_sys_t *sys = aout->sys;

    free(sys);
    vlc_haiku_app_Release();
}

static void Pause( audio_output_t *aout, bool pause, mtime_t date )
{
    VLC_UNUSED( date );

    aout_sys_t *sys = aout->sys;
    
    if (sys->player == NULL)
    	return;

    if (pause)
        sys->player->SetHasData(false);
    else
        sys->player->SetHasData(true);
}

static void Flush( audio_output_t *aout, bool drain )
{
    audio_buffer_t *buffer = aout->sys->buffer;

    vlc_mutex_lock( &buffer->mutex );

    if( drain ) {
        while( buffer->length > 0 )
            vlc_cond_wait( &buffer->cond, &buffer->mutex );
    } else {
        buffer->read_pos = buffer->write_pos;
        buffer->length   = 0;
    }

    vlc_mutex_unlock( &buffer->mutex );
}

static int TimeGet( audio_output_t *aout, mtime_t *restrict delay )
{
    aout_sys_t            *sys = aout->sys;
    audio_sample_format_t *format = &sys->format;
    audio_buffer_t        *buffer = sys->buffer;

    vlc_mutex_lock( &buffer->mutex );

    *delay = ( buffer->length / format->i_bytes_per_frame ) * CLOCK_FREQ /
             format->i_rate;

    vlc_mutex_unlock( &buffer->mutex );

    return 0;
}

/* Applies the stored volume and mute state to the player, if any */
static void ApplyVolume(aout_sys_t* sys)
{
    if (sys->player == NULL)
        return;

    if (sys->mute || sys->volume <= 0.f)
        sys->player->SetVolume(0.0);
    else
        sys->player->SetVolumeDB(lroundf(125.f * log10f(sys->volume)));
}

/* Volume and mute are kept across streams: the core sets them while no
 * stream is playing too */
static int VolumeSet(audio_output_t* aout, float volume)
{
    aout_sys_t* sys = (aout_sys_t*)aout->sys;

    sys->volume = volume;
    aout_VolumeReport(aout, volume);
    ApplyVolume(sys);
    return VLC_SUCCESS;
}

static int MuteSet(audio_output_t* aout, bool mute)
{
    aout_sys_t* sys = (aout_sys_t*)aout->sys;

    sys->mute = mute;
    aout_MuteReport(aout, mute);
    ApplyVolume(sys);
    return VLC_SUCCESS;
}

static int CreateBuffer( audio_output_t *aout, int size )
{
    audio_buffer_t *buffer;

    buffer = (audio_buffer_t*)calloc( 1, sizeof( *buffer ));
    if( !buffer )
        return -1;

    buffer->data = (uint8_t*)malloc( size );
    if( !buffer->data ) {
        free( buffer );
        return -1;
    }

    buffer->size = size;

    vlc_mutex_init( &buffer->mutex );
    vlc_cond_init( &buffer->cond );

    aout->sys->buffer = buffer;

    return 0;
}

static void DestroyBuffer( audio_output_t *aout )
{
    audio_buffer_t *buffer = aout->sys->buffer;

    vlc_mutex_destroy( &buffer->mutex );
    vlc_cond_destroy( &buffer->cond );

    free( buffer->data );
    free( buffer );
}

static int ReadBuffer( audio_output_t *aout, uint8_t *data, int size )
{
    audio_buffer_t *buffer = aout->sys->buffer;
    int             len;
    int             remain_len = 0;

    vlc_mutex_lock( &buffer->mutex );

    len = MIN( buffer->length, size );
    if( buffer->read_pos + len > buffer->size ) {
        remain_len  = len;
        len         = buffer->size - buffer->read_pos;
        remain_len -= len;
    }

    memcpy( data, buffer->data + buffer->read_pos, len );
    if( remain_len )
        memcpy( data + len, buffer->data, remain_len );

    len += remain_len;

    buffer->read_pos += len;
    buffer->read_pos %= buffer->size;

    buffer->length -= len;

    vlc_cond_signal( &buffer->cond );

    vlc_mutex_unlock( &buffer->mutex );

    return len;
}

static int WriteBuffer( audio_output_t *aout, uint8_t *data, int size )
{
    audio_buffer_t *buffer = aout->sys->buffer;
    int             len;
    int             remain_len = 0;

    vlc_mutex_lock( &buffer->mutex );

    while( buffer->length + size > buffer->size )
        vlc_cond_wait( &buffer->cond, &buffer->mutex );

    len = size;
    if( buffer->write_pos + len > buffer->size ) {
        remain_len  = len;
        len         = buffer->size - buffer->write_pos;
        remain_len -= len;
    }

    memcpy( buffer->data + buffer->write_pos, data, len );
    if( remain_len )
        memcpy( buffer->data, data + len, remain_len );

    buffer->write_pos += size;
    buffer->write_pos %= buffer->size;

    buffer->length += size;

    vlc_mutex_unlock( &buffer->mutex );

    return size;
}
