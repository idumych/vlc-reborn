/*****************************************************************************
 * ModernPlaylist.h: the playlist of the modern Haiku interface
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

#ifndef HAIKU_MODERN_PLAYLIST_H
#define HAIKU_MODERN_PLAYLIST_H

#include <View.h>

class BListView;
class BTextControl;
class PlaylistHeader;
class PlaylistList;

/* The list of VLC 3's main window: title, album and duration columns,
 * the current item in bold, a search filter above */
class ModernPlaylist : public BView
{
 public:
                            ModernPlaylist( intf_thread_t * p_intf );

    virtual void            AttachedToWindow();
    virtual void            MessageReceived( BMessage * message );

            void            Update();
            void            RemoveSelected();
            void            PlaySelected();

 private:
            void            _PlayItem( int32 index );
            void            _ShowContextMenu( BPoint where );

    intf_thread_t         * p_intf;
    BTextControl          * fFilter;
    PlaylistHeader        * fHeader;
    PlaylistList          * fList;

    friend class PlaylistList;
};

#endif    // HAIKU_MODERN_PLAYLIST_H
