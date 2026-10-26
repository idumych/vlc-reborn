/*****************************************************************************
 * ModernWindow.h: the modern (VLC 3) Haiku interface window
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

#ifndef HAIKU_MODERN_WINDOW_H
#define HAIKU_MODERN_WINDOW_H

#include <Menu.h>
#include <String.h>

#include "MainWindow.h"
#include "ModernControls.h"

class BFilePanel;
class BMenuBar;
class BMessageRunner;
class ControlBar;
class FullscreenController;
class InfoWindow;
class MessagesWindow;
class ModernPlaylist;
class PreferencesWindow;
class VideoArea;

/* The main window of VLC 3, made of Haiku views: menu bar, the video (or
 * the cone, or the playlist), the seek bar and the controls */
class ModernWindow : public MainWindow
{
 public:
                            ModernWindow( intf_thread_t * p_intf );
    virtual                 ~ModernWindow();

                            // BWindow
    virtual void            DispatchMessage( BMessage * message,
                                             BHandler * handler );
    virtual void            MessageReceived( BMessage * message );
    virtual bool            QuitRequested();

                            // MainWindow
    virtual void            Detach();
    virtual void            Retire();

    /* Waits until the windows left by interface switches are gone */
    static  void            WaitForAll();

    /* The menus of VLC 3, in the menu bar and in the popup menu */
            void            AddMainMenus( BMenu * menu, bool popup );

 private:
    friend int  ModernVideoWindowOpen( vout_window_t *, const vout_window_cfg_t * );
    friend void ModernVideoWindowClose( vout_window_t * );
    friend class VideoArea;

            BMenu *         _MediaMenu();
            BMenu *         _PlaybackMenu();
            BMenu *         _AudioMenu();
            BMenu *         _VideoMenu();
            BMenu *         _SubtitleMenu();
            BMenu *         _ToolsMenu();
            BMenu *         _ViewMenu();
            BMenu *         _HelpMenu();

            void            _UpdateInterface();
            void            _UpdateTitle( input_thread_t * p_input );
            void            _ShowPopupMenu();
            void            _ShowFilePanel( uint32 command, const char * title );
            void            _ShowFolderPanel();
            void            _ShowSavePanel();
            void            _ShowWindow( BWindow * window );
            void            _OpenRefs( BMessage * message, bool replace );
            void            _OpenURI( const char * psz_uri, bool play );
            void            _AddRecent( const char * psz_uri );
            void            _SetVariable( BMessage * message );
            void            _ChangeRate( uint32 command );

            void            _ToggleFullscreen();
            void            _SetFullscreen( bool fullscreen );
            void            _SetMinimal( bool minimal );
            void            _SetPlaylistShown( bool shown );
            void            _SetOnTop( bool onTop );
            void            _UpdateChrome();

            void            _VideoAttached();
            void            _VideoDetached();
            void            _VideoSize( int32 width, int32 height );
            void            _ReportVideoSize();
            void            _SendKey( int key );

            void            _RestoreSettings();
            void            _StoreSettings();

    intf_thread_t         * p_intf;
    playlist_t            * p_playlist;

    BMenuBar              * fMenuBar;
    VideoArea             * fVideoArea;
    ModernPlaylist        * fPlaylist;
    ControlBar            * fControls;
    FullscreenController  * fController;

    MessagesWindow        * fMessagesWindow;
    PreferencesWindow     * fPreferencesWindow;
    InfoWindow            * fInfoWindow;
    BFilePanel            * fFilePanel;
    BFilePanel            * fFolderPanel;
    BFilePanel            * fSavePanel;
    BMessageRunner        * fUpdateRunner;
    BMessage              * fSettings;
    BMessenger            * fCallbackMessenger;

    /* The video output's window, guarded by the embedding lock */
    vout_window_t         * fVideoWnd;
    bool                    fHasVideo;

    bool                    fFullscreen;
    bool                    fMinimal;
    bool                    fPlaylistShown;
    bool                    fOnTop;
    bool                    fRemaining;
    BRect                   fNormalFrame;   /* before fullscreen */
    BRect                   fIdleFrame;     /* before the video sized it */
    bigtime_t               fLastMouseMove;
    BPoint                  fLastMousePoint;
    int                     fCurrentItem;
    ControlState            fState;

    friend class StateMenu;
};

#endif    // HAIKU_MODERN_WINDOW_H
