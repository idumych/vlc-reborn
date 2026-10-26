/*****************************************************************************
 * InfoWindow.h: media information of the modern Haiku interface
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

#ifndef HAIKU_INFO_WINDOW_H
#define HAIKU_INFO_WINDOW_H

#include <Window.h>

class BOutlineListView;
class BTabView;
class BTextControl;

#define INFO_FIELDS 10

/* Tools > Media Information: the metadata and the codecs of the media
 * being played */
class InfoWindow : public BWindow
{
 public:
                            InfoWindow( intf_thread_t * p_intf, BRect frame );

    virtual bool            QuitRequested();
    virtual void            MessageReceived( BMessage * message );

            void            ShowTab( int32 tab );
            void            Update();
            void            ReallyQuit();

 private:
    intf_thread_t         * p_intf;
    BTabView              * fTabs;
    BTextControl          * fFields[INFO_FIELDS];
    BOutlineListView      * fCodecs;
};

#endif    // HAIKU_INFO_WINDOW_H
