/*****************************************************************************
 * haiku.cpp: Haiku native interface
 *****************************************************************************
 * Copyright (C) 1999-2010 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Jean-Marc Dressler <polux@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
 *          Tony Castley <tony@castley.net>
 *          Richard Shepherd <richard@rshepherd.demon.co.uk>
 *          Stephan Aßmus <stippi@yellowbites.com>
 *          Eric Petit <titer@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*
 * The BeOS interface of VLC 0.x (removed in 2010), brought back for Haiku:
 * a small window with a menu bar, a seek slider, transport buttons and a
 * volume slider. Videos play in their own BBitmap output windows.
 *
 * Or, with --haiku-interface=modern, the interface of VLC 3 made of Haiku
 * views: the video plays in the main window, above the seek bar and the
 * controls, and the playlist can take its place.
 */

#include <InterfaceKit.h>
#include <Application.h>
#include <Message.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <vlc_playlist.h>
#include <vlc_vout_window.h>
#include <vlc_haiku.h>

#include "InterfaceWindow.h"
#include "ModernWindow.h"
#include "MsgVals.h"

static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );
static int  WindowOpen( vout_window_t *, const vout_window_cfg_t * );
static void WindowClose( vout_window_t * );

#define STYLE_TEXT N_("Interface style")
#define STYLE_LONGTEXT N_("Classic is the BeOS interface of VLC 0.x, " \
    "with each video in its own window. Modern is the interface of VLC 3, " \
    "with the video in the main window. View > Classic Interface and " \
    "Window > Modern Interface switch between them.")

static const char *const ppsz_style_values[] = { "classic", "modern" };
static const char *const ppsz_style_texts[] =
    { N_("Classic (BeOS)"), N_("Modern (VLC 3)") };

vlc_module_begin ()
    set_shortname( "Haiku" )
    set_description( N_("Haiku native interface") )
    set_category( CAT_INTERFACE )
    set_subcategory( SUBCAT_INTERFACE_MAIN )
    /* Above Qt (151): this is the default interface on Haiku */
    set_capability( "interface", 160 )
    set_callbacks( Open, Close )
    add_shortcut( "haiku", "beos" )
    add_string( "haiku-interface", "classic", STYLE_TEXT, STYLE_LONGTEXT,
                false )
        change_string_list( ppsz_style_values, ppsz_style_texts )
    /* Window threads run this plugin's code until they are gone, which
     * Quit() doesn't wait for everywhere (Haiku API on Linux) */
    cannot_unload_broken_library()

    add_submodule ()
        set_description( N_("Haiku modern interface video window") )
        /* Only while the modern interface runs */
        set_capability( "vout window", 50 )
        set_callbacks( WindowOpen, WindowClose )
        add_shortcut( "haiku" )
vlc_module_end ()

struct intf_sys_t
{
    vlc_mutex_t   lock;         /* protects p_window */
    MainWindow  * p_window;
    char        * psz_style;    /* style of p_window */

    /* Videos are stopped and played again when the interface changes */
    thread_id     resume_thread;
    int           i_resume_id;
    mtime_t       i_resume_time;
    bool          b_resume_paused;
    volatile bool b_closing;
};

/*****************************************************************************
 * CreateWindow: a main window of the given style
 *****************************************************************************/
static MainWindow * CreateWindow( intf_thread_t * p_intf, const char * psz_style )
{
    if( !strcmp( psz_style, "modern" ) )
        return new ModernWindow( p_intf );

    /* Bottom left of the screen, where it was in 2001 */
    BScreen screen( B_MAIN_SCREEN_ID );
    BRect rect   = screen.Frame();
    rect.top     = rect.bottom - 100;
    rect.bottom -= 50;
    rect.left   += 50;
    rect.right   = rect.left + 350;
    return new InterfaceWindow( p_intf, rect, "VideoLAN Client " VERSION
                                              " (Haiku interface)" );
}

/*****************************************************************************
 * ResumeThread: plays the stopped video again in the new interface
 *****************************************************************************/
static int32 ResumeThread( void * data )
{
    intf_thread_t * p_intf = (intf_thread_t *) data;
    intf_sys_t * p_sys = p_intf->p_sys;
    playlist_t * p_playlist = pl_Get( p_intf );
    int i;

    /* Until the playlist has stopped, and the old video is gone */
    for( i = 0; i < 500 && !p_sys->b_closing; i++ )
    {
        PL_LOCK;
        bool b_stopped = playlist_Status( p_playlist ) == PLAYLIST_STOPPED;
        PL_UNLOCK;
        input_thread_t * p_input = playlist_CurrentInput( p_playlist );
        if( p_input )
            vlc_object_release( p_input );
        else if( b_stopped )
            break;
        snooze( 10000 );
    }
    if( p_sys->b_closing || i == 500 )
        return 0;

    PL_LOCK;
    playlist_item_t * p_item = playlist_ItemGetById( p_playlist,
                                                     p_sys->i_resume_id );
    if( p_item )
        playlist_ViewPlay( p_playlist, NULL, p_item );
    PL_UNLOCK;
    if( !p_item )
        return 0;

    /* Then back to where it was */
    for( i = 0; i < 500 && !p_sys->b_closing; i++ )
    {
        input_thread_t * p_input = playlist_CurrentInput( p_playlist );
        if( p_input )
        {
            bool b_playing = var_GetInteger( p_input, "state" ) == PLAYING_S;
            if( b_playing )
            {
                if( p_sys->i_resume_time > 0 )
                    var_SetInteger( p_input, "time", p_sys->i_resume_time );
                if( p_sys->b_resume_paused )
                    playlist_Pause( p_playlist );
            }
            vlc_object_release( p_input );
            if( b_playing )
                break;
        }
        snooze( 10000 );
    }
    return 0;
}

static bool ResumeRunning( intf_sys_t * p_sys )
{
    thread_info info;
    return p_sys->resume_thread >= B_OK
        && get_thread_info( p_sys->resume_thread, &info ) == B_OK;
}

/*****************************************************************************
 * SwitchInterface: the other main window, from the old one's thread
 *****************************************************************************/
void SwitchInterface( intf_thread_t * p_intf, const char * psz_style )
{
    intf_sys_t * p_sys = p_intf->p_sys;
    playlist_t * p_playlist = pl_Get( p_intf );

    if( !strcmp( psz_style, p_sys->psz_style ) )
        return;
    char * psz_new = strdup( psz_style );
    if( !psz_new )
        return;

    /* Remembered for the next time */
    config_PutPsz( p_intf, "haiku-interface", psz_style );
    config_SaveConfigFile( p_intf );

    /* Videos go from one kind of window to the other: stop them, and play
     * them again from the same point once they are gone */
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    if( p_input )
    {
        vout_thread_t ** pp_vouts;
        size_t i_vouts = 0;
        if( input_Control( p_input, INPUT_GET_VOUTS, &pp_vouts, &i_vouts )
                == VLC_SUCCESS )
        {
            for( size_t i = 0; i < i_vouts; i++ )
                vlc_object_release( pp_vouts[i] );
            free( pp_vouts );
        }
        if( i_vouts > 0 && !ResumeRunning( p_sys ) )
        {
            PL_LOCK;
            playlist_item_t * p_item = playlist_CurrentPlayingItem( p_playlist );
            p_sys->i_resume_id = p_item ? p_item->i_id : -1;
            PL_UNLOCK;
            p_sys->i_resume_time = var_GetInteger( p_input, "time" );
            p_sys->b_resume_paused =
                var_GetInteger( p_input, "state" ) == PAUSE_S;
            playlist_Stop( p_playlist );
            if( p_sys->i_resume_id >= 0 )
            {
                p_sys->resume_thread = spawn_thread( ResumeThread,
                        "vlc interface switch", B_NORMAL_PRIORITY, p_intf );
                if( p_sys->resume_thread >= B_OK )
                    resume_thread( p_sys->resume_thread );
            }
        }
        vlc_object_release( p_input );
    }

    MainWindow * p_window = CreateWindow( p_intf, psz_new );

    vlc_mutex_lock( &p_sys->lock );
    MainWindow * p_old = p_sys->p_window;
    p_sys->p_window = p_window;
    free( p_sys->psz_style );
    p_sys->psz_style = psz_new;
    vlc_mutex_unlock( &p_sys->lock );

    vlc_haiku_app_SetTarget( p_window );
    p_old->Retire();
}

/*****************************************************************************
 * Open: create the interface window
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    intf_thread_t * p_intf = (intf_thread_t*) p_this;

    intf_sys_t * p_sys = (intf_sys_t*) calloc( 1, sizeof( intf_sys_t ) );
    if( !p_sys )
        return VLC_ENOMEM;
    p_intf->p_sys = p_sys;

    if( vlc_haiku_app_Hold( p_intf ) )
    {
        free( p_sys );
        return VLC_EGENERIC;
    }

    vlc_mutex_init( &p_sys->lock );
    p_sys->resume_thread = -1;
    p_sys->psz_style = var_InheritString( p_intf, "haiku-interface" );
    if( !p_sys->psz_style || strcmp( p_sys->psz_style, "modern" ) )
    {
        free( p_sys->psz_style );
        p_sys->psz_style = strdup( "classic" );
    }
    p_sys->p_window = CreateWindow( p_intf, p_sys->psz_style );

    /* Files opened from Tracker while VLC runs come to the window */
    vlc_haiku_app_SetTarget( p_sys->p_window );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy the interface window
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    intf_thread_t *p_intf = (intf_thread_t*) p_this;
    intf_sys_t * p_sys = p_intf->p_sys;

    p_sys->b_closing = true;
    if( p_sys->resume_thread >= B_OK )
    {
        status_t ret;
        wait_for_thread( p_sys->resume_thread, &ret );
    }

    vlc_haiku_app_SetTarget( NULL );

    vlc_mutex_lock( &p_sys->lock );
    MainWindow * p_window = p_sys->p_window;
    vlc_mutex_unlock( &p_sys->lock );

    p_window->Detach();
    /* Videos in the modern window go away before it does */
    playlist_Deactivate( pl_Get( p_intf ) );

    /* Wait until the window is gone: with the Haiku API on Linux, Quit()
     * from another thread (and wait_for_thread()) can return before it is */
    BMessenger messenger( p_window );
    if( p_window->Lock() )
        p_window->Quit();
    for( int i = 0; i < 200 && messenger.IsValid(); i++ )
        snooze( 10000 );
    /* And so are modern windows left by a switch */
    ModernWindow::WaitForAll();

    vlc_mutex_destroy( &p_sys->lock );
    free( p_sys->psz_style );
    free( p_sys );
    vlc_haiku_app_Release();
}

/*****************************************************************************
 * WindowOpen, WindowClose: the modern window's video area for the video
 * output
 *****************************************************************************/
static int WindowOpen( vout_window_t * p_wnd, const vout_window_cfg_t * p_cfg )
{
    return ModernVideoWindowOpen( p_wnd, p_cfg );
}

static void WindowClose( vout_window_t * p_wnd )
{
    ModernVideoWindowClose( p_wnd );
}
