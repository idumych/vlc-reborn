/*****************************************************************************
 * MainWindow.h: what the interface module needs from its main window
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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

#ifndef HAIKU_MAIN_WINDOW_H
#define HAIKU_MAIN_WINDOW_H

#include <Window.h>

#include <vlc_vout_window.h>

/* The classic (BeOS) and the modern (VLC 3) interface windows */
class MainWindow : public BWindow
{
 public:
                            MainWindow( BRect frame, const char * title,
                                        window_look look, window_feel feel,
                                        uint32 flags )
                                : BWindow( frame, title, look, feel, flags ),
                                  fRetiring( false ) {}

    /* Stops the callbacks and child windows before the window goes away;
     * called from the interface Close() */
    virtual void            Detach() = 0;

    /* Goes away for the other interface style: called by
     * SwitchInterface() from the window's own thread */
    virtual void            Retire()
                            {
                                Detach();
                                fRetiring = true;
                                Hide();
                                PostMessage( B_QUIT_REQUESTED );
                            }

 protected:
    bool                    fRetiring;
};

/* haiku.cpp: replaces the main window by one of the other style
 * ("classic" or "modern"), from the old window's thread */
void SwitchInterface( intf_thread_t * p_intf, const char * psz_style );

/* ModernWindow.cpp: the "vout window" provider of the modern interface */
int  ModernVideoWindowOpen( vout_window_t *, const vout_window_cfg_t * );
void ModernVideoWindowClose( vout_window_t * );

#endif    // HAIKU_MAIN_WINDOW_H
