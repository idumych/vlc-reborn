/*****************************************************************************
 * PlayListWindow.h: Haiku playlist window
 *****************************************************************************
 * Copyright (C) 1999, 2000, 2001 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Jean-Marc Dressler <polux@via.ecp.fr>
 *          Tony Castley <tcastley@mail.powerup.com.au>
 *          Richard Shepherd <richard@rshepherd.demon.co.uk>
 *          Stephan Aßmus <stippi@yellowbites.com>
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

#ifndef HAIKU_PLAY_LIST_WINDOW_H
#define HAIKU_PLAY_LIST_WINDOW_H

#include <Window.h>

class BListView;
class BMenuItem;
class PlaylistView;

class PlayListWindow : public BWindow
{
 public:
                            PlayListWindow( BRect frame,
                                            const char* name,
                                            intf_thread_t *p_interface );
    virtual                 ~PlayListWindow();

                            // BWindow
    virtual bool            QuitRequested();
    virtual void            MessageReceived( BMessage *message );

                            // PlayListWindow
            void            ReallyQuit();
            void            UpdatePlaylist();

 private:
            void            _PlayItem( int32 index );
            void            _RemoveSelected();

    PlaylistView*           fListView;
    BMenuItem*              fRemoveMI;
    intf_thread_t*          p_intf;
};

#endif    // HAIKU_PLAY_LIST_WINDOW_H
