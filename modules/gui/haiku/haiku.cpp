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
#include <vlc_haiku.h>

#include "InterfaceWindow.h"
#include "MsgVals.h"

static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin ()
    set_shortname( "Haiku" )
    set_description( N_("Haiku native interface") )
    set_category( CAT_INTERFACE )
    set_subcategory( SUBCAT_INTERFACE_MAIN )
    /* Above Qt (151): this is the default interface on Haiku */
    set_capability( "interface", 160 )
    set_callbacks( Open, Close )
    add_shortcut( "haiku", "beos" )
    /* Window threads run this plugin's code until they are gone, which
     * Quit() doesn't wait for everywhere (VitruvianOS) */
    cannot_unload_broken_library()
vlc_module_end ()

struct intf_sys_t
{
    InterfaceWindow * p_window;
};

/*****************************************************************************
 * Open: create the interface window
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    intf_thread_t * p_intf = (intf_thread_t*) p_this;

    p_intf->p_sys = (intf_sys_t*) malloc( sizeof( intf_sys_t ) );
    if( !p_intf->p_sys )
        return VLC_ENOMEM;

    if( vlc_haiku_app_Hold( p_intf ) )
    {
        free( p_intf->p_sys );
        return VLC_EGENERIC;
    }

    /* Bottom left of the screen, where it was in 2001 */
    BScreen screen( B_MAIN_SCREEN_ID );
    BRect rect   = screen.Frame();
    rect.top     = rect.bottom - 100;
    rect.bottom -= 50;
    rect.left   += 50;
    rect.right   = rect.left + 350;
    p_intf->p_sys->p_window =
        new InterfaceWindow( p_intf, rect, "VideoLAN Client " VERSION
                                           " (Haiku interface)" );

    /* Files opened from Tracker while VLC runs come to the window */
    vlc_haiku_app_SetTarget( p_intf->p_sys->p_window );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy the interface window
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    intf_thread_t *p_intf = (intf_thread_t*) p_this;
    InterfaceWindow * p_window = p_intf->p_sys->p_window;

    vlc_haiku_app_SetTarget( NULL );
    p_window->Detach();
    /* Wait until the window is gone: on VitruvianOS, Quit() from another
     * thread (and wait_for_thread()) can return before it is */
    BMessenger messenger( p_window );
    if( p_window->Lock() )
        p_window->Quit();
    for( int i = 0; i < 200 && messenger.IsValid(); i++ )
        snooze( 10000 );

    free( p_intf->p_sys );
    vlc_haiku_app_Release();
}
