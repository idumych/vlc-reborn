/*****************************************************************************
 * MessagesWindow.h
 *****************************************************************************
 * Copyright (C) 1999, 2000, 2001 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Eric Petit <titer@videolan.org>
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

#ifndef HAIKU_MESSAGES_WINDOW_H
#define HAIKU_MESSAGES_WINDOW_H

#include <Window.h>

class BScrollView;
class BTextView;

class MessagesWindow : public BWindow
{
    public:
                             MessagesWindow( intf_thread_t * p_intf,
                                             BRect frame, const char * name );
        virtual              ~MessagesWindow();
        virtual void         FrameResized( float, float );
        virtual void         MessageReceived( BMessage * );
        virtual bool         QuitRequested();

        /* Starts collecting LibVLC's log. Until then the log goes to the
         * terminal as usual. */
        void                 Subscribe();
        void                 ReallyQuit();

    private:
        static void          LogCallback( void *, int, const vlc_log_t *,
                                          const char *, va_list );
        void                 _AddMessage( int type, const char * module,
                                          const char * object_type,
                                          const char * text );

        intf_thread_t      * p_intf;
        bool                 fSubscribed;
        int                  fVerbosity;
        BTextView *          fMessagesView;
        BScrollView *        fScrollView;
};

#endif    // HAIKU_MESSAGES_WINDOW_H
