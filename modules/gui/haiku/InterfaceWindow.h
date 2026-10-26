/*****************************************************************************
 * InterfaceWindow.h: Haiku interface window class prototype
 *****************************************************************************
 * Copyright (C) 1999-2004 the VideoLAN team
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

#ifndef HAIKU_INTERFACE_WINDOW_H
#define HAIKU_INTERFACE_WINDOW_H

#include <Menu.h>
#include <Messenger.h>
#include <Window.h>

#include "MainWindow.h"

class BFilePanel;
class BTextControl;
class BMenuBar;
class BMessageRunner;
class MediaControlView;
class MessagesWindow;
class PlayListWindow;
class PreferencesWindow;

class CDMenu : public BMenu
{
 public:
                            CDMenu( const char* name );
    virtual                 ~CDMenu();

    virtual void            AttachedToWindow();

 private:
    int                     GetCD();
};

/* Lists the choices of an input variable ("audio-es", "spu-es", "title",
 * "chapter") each time the menu opens */
class VariableMenu : public BMenu
{
 public:
                            VariableMenu( intf_thread_t * p_intf,
                                          const char * psz_name,
                                          const char * psz_variable,
                                          uint32 command );
    virtual                 ~VariableMenu();

    virtual void            AttachedToWindow();

 private:
    intf_thread_t         * p_intf;
    char                  * psz_variable;
    uint32                  i_command;
};

/* Asks for a network stream address; sends LOCATION_ENTERED with "url" */
class LocationWindow : public BWindow
{
 public:
                            LocationWindow( BRect frame, BMessenger target );
    virtual void            MessageReceived( BMessage * message );

 private:
    BMessenger              fTarget;
    BTextControl *          fText;
};

class InterfaceWindow : public MainWindow
{
 public:
                            InterfaceWindow( intf_thread_t * p_intf,
                                             BRect frame,
                                             const char * name );
    virtual                 ~InterfaceWindow();

                            // BWindow
    virtual void            FrameResized( float width, float height );
    virtual void            MessageReceived( BMessage* message );
    virtual bool            QuitRequested();

                            // InterfaceWindow
            void            UpdateInterface();
            bool            IsStopped() const;

    virtual void            Detach();

    MediaControlView*       p_mediaControl;

 private:
            void            _SetMenusEnabled( bool hasFile,
                                              bool hasChapters = false,
                                              bool hasTitles = false );
            void            _UpdateSpeedMenu( float rate );
            void            _ShowFilePanel( uint32 command,
                                            const char* windowTitle );
            void            _ShowWindow( BWindow* window );
            void            _OpenRefs( BMessage* message, bool replace );
            void            _SetVoutBool( const char* variable, bool value );
            void            _RestoreSettings();
            void            _StoreSettings();

    intf_thread_t         * p_intf;
    playlist_t            * p_playlist;

    BFilePanel*             fFilePanel;
    PlayListWindow*         fPlaylistWindow;
    MessagesWindow*         fMessagesWindow;
    PreferencesWindow*      fPreferencesWindow;
    BMessageRunner*         fUpdateRunner;
    BMenuBar*               fMenuBar;
    BMenuItem*              fNextTitleMI;
    BMenuItem*              fPrevTitleMI;
    BMenuItem*              fNextChapterMI;
    BMenuItem*              fPrevChapterMI;
    BMenuItem*              fFullscreenMI;
    BMenuItem*              fOnTopMI;
    BMenuItem*              fMuteMI;
    BMenuItem*              fSpeedMI[7];
    BMenu*                  fAudioMenu;
    BMenu*                  fNavigationMenu;
    BMenu*                  fTitleMenu;
    BMenu*                  fChapterMenu;
    BMenu*                  fLanguageMenu;
    BMenu*                  fSubtitlesMenu;
    BMenu*                  fSpeedMenu;
    bigtime_t               fLastUpdateTime;
    BMessage*               fSettings;  // we keep the message arround
                                        // for forward compatibility
};

// some global support functions
bool make_sure_frame_is_on_screen( BRect& frame );
void set_window_pos( BWindow* window, BRect frame );

status_t load_settings( BMessage* message,
                        const char* fileName,
                        const char* folder = NULL );

status_t save_settings( BMessage* message,
                        const char* fileName,
                        const char* folder = NULL );

#endif    // HAIKU_INTERFACE_WINDOW_H
