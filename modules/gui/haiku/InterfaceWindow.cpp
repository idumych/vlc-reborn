/*****************************************************************************
 * InterfaceWindow.cpp: Haiku interface window class
 *****************************************************************************
 * Copyright (C) 1999-2004 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Jean-Marc Dressler <polux@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
 *          Tony Castley <tony@castley.net>
 *          Richard Shepherd <richard@rshepherd.demon.co.uk>
 *          Stephan Aßmus <superstippi@gmx.de>
 *          Eric Petit <titer@m0k.org>
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

/* System headers */
#include <InterfaceKit.h>
#include <AppKit.h>
#include <StorageKit.h>
#include <SupportKit.h>
#include <Drivers.h>
#include <fs_info.h>
#include <fcntl.h>
#include <unistd.h>

/* VLC headers */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <vlc_playlist.h>
#include <vlc_aout.h>
#include <vlc_vout.h>
#include <vlc_url.h>

/* Haiku interface headers */
#include "MsgVals.h"
#include "MediaControlView.h"
#include "PlayListWindow.h"
#include "MessagesWindow.h"
#include "PreferencesWindow.h"
#include "InterfaceWindow.h"

#define INTERFACE_UPDATE_PERIOD   100000
#define INTERFACE_UPDATE_TIMEOUT  500000
#define SETTINGS_FOLDER "VideoLAN Client"

static const float kSpeeds[7] = { .125f, .25f, .5f, 1.f, 2.f, 4.f, 8.f };
static const char * const kSpeedNames[7] =
    { "1/8x", "1/4x", "1/2x", "1x", "2x", "4x", "8x" };

// make_sure_frame_is_on_screen
bool
make_sure_frame_is_on_screen( BRect& frame )
{
    BScreen screen( B_MAIN_SCREEN_ID );
    if (frame.IsValid() && screen.IsValid()) {
        if (!screen.Frame().Contains(frame)) {
            // make sure frame fits in the screen
            if (frame.Width() > screen.Frame().Width())
                frame.right -= frame.Width() - screen.Frame().Width() + 10.0;
            if (frame.Height() > screen.Frame().Height())
                frame.bottom -= frame.Height() - screen.Frame().Height() + 30.0;
            // frame is now at the most the size of the screen
            if (frame.right > screen.Frame().right)
                frame.OffsetBy(-(frame.right - screen.Frame().right), 0.0);
            if (frame.bottom > screen.Frame().bottom)
                frame.OffsetBy(0.0, -(frame.bottom - screen.Frame().bottom));
            if (frame.left < screen.Frame().left)
                frame.OffsetBy((screen.Frame().left - frame.left), 0.0);
            if (frame.top < screen.Frame().top)
                frame.OffsetBy(0.0, (screen.Frame().top - frame.top));
        }
        return true;
    }
    return false;
}

// make_sure_frame_is_within_limits
void
make_sure_frame_is_within_limits( BRect& frame, float minWidth, float minHeight,
                                  float maxWidth, float maxHeight )
{
    if ( frame.Width() < minWidth )
        frame.right = frame.left + minWidth;
    if ( frame.Height() < minHeight )
        frame.bottom = frame.top + minHeight;
    if ( frame.Width() > maxWidth )
        frame.right = frame.left + maxWidth;
    if ( frame.Height() > maxHeight )
        frame.bottom = frame.top + maxHeight;
}

// get_volume_info
bool
get_volume_info( BVolume& volume, BString& volumeName, bool& isCDROM, BString& deviceName )
{
    bool success = false;
    isCDROM = false;
    deviceName = "";
    volumeName = "";
    char name[B_FILE_NAME_LENGTH];
    if ( volume.GetName( name ) >= B_OK )    // disk is currently mounted
    {
        volumeName = name;
        dev_t dev = volume.Device();
        fs_info info;
        if ( fs_stat_dev( dev, &info ) == B_OK )
        {
            success = true;
            deviceName = info.device_name;
            if ( volume.IsReadOnly() )
            {
                int i_dev = open( info.device_name, O_RDONLY );
                if ( i_dev >= 0 )
                {
                    device_geometry g;
                    if ( ioctl( i_dev, B_GET_GEOMETRY, &g, sizeof( g ) ) >= 0 )
                        isCDROM = ( g.device_type == B_CD );
                    close( i_dev );
                }
            }
        }
     }
     return success;
}

/* Playlist callbacks run on other threads: just poke the window */
static int PlaylistChanged( vlc_object_t *p_this, const char * psz_variable,
                            vlc_value_t old_val, vlc_value_t new_val,
                            void * param )
{
    VLC_UNUSED( p_this ); VLC_UNUSED( psz_variable );
    VLC_UNUSED( old_val ); VLC_UNUSED( new_val );
    BMessenger * messenger = (BMessenger *) param;
    messenger->SendMessage( PLAYLIST_CHANGED );
    return VLC_SUCCESS;
}

static const char * const kPlaylistVariables[] = {
    "playlist-item-append", "playlist-item-deleted", "input-current",
    "item-change", /* names and durations become known */
};

/*****************************************************************************
 * LocationWindow: asks for a network stream address
 *****************************************************************************/
class LocationWindow : public BWindow
{
 public:
    LocationWindow( BRect frame, BMessenger target )
        : BWindow( frame, _("Open Location"), B_TITLED_WINDOW_LOOK,
                   B_MODAL_APP_WINDOW_FEEL,
                   B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
                   | B_AUTO_UPDATE_SIZE_LIMITS ),
          fTarget( target )
    {
        BView * background = new BView( Bounds(), "background",
                                        B_FOLLOW_ALL, B_WILL_DRAW );
        background->SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
        AddChild( background );

        BRect r( Bounds().InsetByCopy( 10, 10 ) );
        r.bottom = r.top + 20;
        fText = new BTextControl( r, "url", _("URL:"), "",
                                  NULL, B_FOLLOW_LEFT_RIGHT );
        fText->SetDivider( fText->StringWidth( _("URL:") ) + 8 );
        background->AddChild( fText );

        BButton * open = new BButton( BRect( 0, 0, 80, 24 ), "open",
                                      _("Open"), new BMessage( LOCATION_ENTERED ),
                                      B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM );
        open->ResizeToPreferred();
        open->MoveTo( Bounds().right - 10 - open->Bounds().Width(),
                      Bounds().bottom - 10 - open->Bounds().Height() );
        background->AddChild( open );
        SetDefaultButton( open );

        BButton * cancel = new BButton( BRect( 0, 0, 80, 24 ), "cancel",
                                        _("Cancel"), new BMessage( B_QUIT_REQUESTED ),
                                        B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM );
        cancel->ResizeToPreferred();
        cancel->MoveTo( open->Frame().left - 8 - cancel->Bounds().Width(),
                        open->Frame().top );
        background->AddChild( cancel );

        fText->MakeFocus( true );
    }

    virtual void MessageReceived( BMessage * message )
    {
        if ( message->what == LOCATION_ENTERED )
        {
            BMessage msg( LOCATION_ENTERED );
            msg.AddString( "url", fText->Text() );
            fTarget.SendMessage( &msg );
            Quit();
            return;
        }
        BWindow::MessageReceived( message );
    }

 private:
    BMessenger      fTarget;
    BTextControl *  fText;
};

/*****************************************************************************
 * InterfaceWindow
 *****************************************************************************/

InterfaceWindow::InterfaceWindow( intf_thread_t * _p_intf, BRect frame,
                                  const char * name )
    : BWindow( frame, name, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
               B_NOT_ZOOMABLE | B_WILL_ACCEPT_FIRST_CLICK | B_ASYNCHRONOUS_CONTROLS ),

      /* Initializations */
      p_intf( _p_intf ),
      p_playlist( pl_Get( _p_intf ) ),

      fFilePanel( NULL ),
      fPreferencesWindow( NULL ),
      fUpdateRunner( NULL ),
      fLastUpdateTime( system_time() ),
      fSettings( new BMessage( 'sett' ) )
{
    char psz_tmp[1024];
#define ADD_ELLIPSIS( a ) \
    memset( psz_tmp, 0, 1024 ); \
    snprintf( psz_tmp, 1024, "%s%s", a, B_UTF8_ELLIPSIS );

    BScreen screen;
    BRect screen_rect = screen.Frame();
    BRect window_rect;
    window_rect.Set( screen_rect.right - 500,
                     screen_rect.top + 50,
                     screen_rect.right - 150,
                     screen_rect.top + 250 );
    fPlaylistWindow = new PlayListWindow( window_rect, _("Playlist"), p_intf );
    window_rect.Set( screen_rect.right - 550,
                     screen_rect.top + 290,
                     screen_rect.right - 150,
                     screen_rect.top + 490 );
    fMessagesWindow = new MessagesWindow( p_intf, window_rect, _("Messages") );

    // the media control view
    p_mediaControl = new MediaControlView( p_intf, BRect( 0.0, 0.0, 250.0, 50.0 ) );
    p_mediaControl->SetViewColor( ui_color( B_PANEL_BACKGROUND_COLOR ) );

    float width, height;
    p_mediaControl->GetPreferredSize( &width, &height );

    // set up the main menu
    fMenuBar = new BMenuBar( BRect(0.0, 0.0, width, 15.0), "main menu",
                             B_FOLLOW_NONE, B_ITEMS_IN_ROW, false );

    // make menu bar resize to correct height
    float menuWidth, menuHeight;
    fMenuBar->GetPreferredSize( &menuWidth, &menuHeight );
    fMenuBar->ResizeTo( width, menuHeight );    // don't change! it's a workarround!
    // take care of proper size for ourself
    height += fMenuBar->Bounds().Height();
    ResizeTo( width, height );

    p_mediaControl->MoveTo( fMenuBar->Bounds().LeftBottom() + BPoint(0.0, 1.0) );
    AddChild( fMenuBar );

    // Add the file Menu
    BMenu* fileMenu = new BMenu( _("File") );
    fMenuBar->AddItem( fileMenu );
    ADD_ELLIPSIS( _("Open File") );
    fileMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( OPEN_FILE ), 'O') );
    fileMenu->AddItem( new CDMenu( _("Open Disc") ) );
    ADD_ELLIPSIS( _("Open Location") );
    fileMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( OPEN_LOCATION ), 'L') );
    ADD_ELLIPSIS( _("Open Subtitles") );
    fileMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( LOAD_SUBFILE ) ) );

    fileMenu->AddSeparatorItem();
    ADD_ELLIPSIS( _("About") );
    fileMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( B_ABOUT_REQUESTED ) ) );
    fileMenu->AddItem( new BMenuItem( _("Quit"), new BMessage( B_QUIT_REQUESTED ), 'Q') );

    fLanguageMenu = new VariableMenu( p_intf, _("Language"), "audio-es",
                                      SELECT_CHANNEL );
    fSubtitlesMenu = new VariableMenu( p_intf, _("Subtitles"), "spu-es",
                                       SELECT_SUBTITLE );

    /* Add the Audio menu */
    fAudioMenu = new BMenu( _("Audio") );
    fMenuBar->AddItem ( fAudioMenu );
    fAudioMenu->AddItem( fLanguageMenu );
    fAudioMenu->AddItem( fSubtitlesMenu );
    fAudioMenu->AddSeparatorItem();
    fAudioMenu->AddItem( new BMenuItem( _("Increase Volume"),
                                        new BMessage( VOLUME_UP ), B_UP_ARROW ) );
    fAudioMenu->AddItem( new BMenuItem( _("Decrease Volume"),
                                        new BMessage( VOLUME_DOWN ), B_DOWN_ARROW ) );
    fAudioMenu->AddItem( fMuteMI = new BMenuItem( _("Mute"),
                                        new BMessage( VOLUME_MUTE ), 'M',
                                        B_SHIFT_KEY ) );

    fPrevTitleMI = new BMenuItem( _("Previous Title"), new BMessage( PREV_TITLE ) );
    fNextTitleMI = new BMenuItem( _("Next Title"), new BMessage( NEXT_TITLE ) );
    fPrevChapterMI = new BMenuItem( _("Previous Chapter"), new BMessage( PREV_CHAPTER ) );
    fNextChapterMI = new BMenuItem( _("Next Chapter"), new BMessage( NEXT_CHAPTER ) );

    /* Add the Navigation menu */
    fNavigationMenu = new BMenu( _("Navigation") );
    fMenuBar->AddItem( fNavigationMenu );
    fNavigationMenu->AddItem( new BMenuItem( _("Previous File"),
                                             new BMessage( PREV_FILE ), B_LEFT_ARROW ) );
    fNavigationMenu->AddItem( new BMenuItem( _("Next File"),
                                             new BMessage( NEXT_FILE ), B_RIGHT_ARROW ) );
    fNavigationMenu->AddSeparatorItem();
    fNavigationMenu->AddItem( fPrevTitleMI );
    fNavigationMenu->AddItem( fNextTitleMI );
    fNavigationMenu->AddItem( fTitleMenu = new VariableMenu( p_intf, _("Go to Title"),
                                                             "title", TOGGLE_TITLE ) );
    fNavigationMenu->AddSeparatorItem();
    fNavigationMenu->AddItem( fPrevChapterMI );
    fNavigationMenu->AddItem( fNextChapterMI );
    fNavigationMenu->AddItem( fChapterMenu = new VariableMenu( p_intf, _("Go to Chapter"),
                                                               "chapter", TOGGLE_CHAPTER ) );

    /* Add the Speed menu */
    fSpeedMenu = new BMenu( _("Speed") );
    fSpeedMenu->SetRadioMode( true );
    for( int i = 0; i < 7; i++ )
    {
        BMessage * message = new BMessage( NORMAL_PLAY );
        message->AddFloat( "rate", kSpeeds[i] );
        fSpeedMenu->AddItem( fSpeedMI[i] = new BMenuItem( kSpeedNames[i], message ) );
    }
    fMenuBar->AddItem( fSpeedMenu );

    /* Add the Window menu */
    BMenu * showMenu = new BMenu( _("Window") );
    ADD_ELLIPSIS( _("Playlist") );
    showMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( OPEN_PLAYLIST ), 'P') );
    ADD_ELLIPSIS( _("Messages") );
    showMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( OPEN_MESSAGES ), 'M' ) );
    ADD_ELLIPSIS( _("Preferences") );
    showMenu->AddItem( new BMenuItem( psz_tmp, new BMessage( OPEN_PREFERENCES ), 'S' ) );
    showMenu->AddSeparatorItem();
    showMenu->AddItem( fFullscreenMI = new BMenuItem( _("Fullscreen"),
                                      new BMessage( TOGGLE_FULL_SCREEN ), 'F' ) );
    showMenu->AddItem( fOnTopMI = new BMenuItem( _("Always on Top"),
                                      new BMessage( TOGGLE_ON_TOP ), 'T' ) );
    fMenuBar->AddItem( showMenu );

    // add the media control view after the menubar is complete
    // because it will set the window size limits in AttachedToWindow()
    // and the menubar needs to report the correct PreferredSize()
    AddChild( p_mediaControl );

    /* Wide enough for the title, as far as the size limits allow */
    float minWidth, maxWidth, minHeight, maxHeight;
    GetSizeLimits( &minWidth, &maxWidth, &minHeight, &maxHeight );
    float titleWidth = ceilf( be_bold_font->StringWidth( name ) ) + 60.0;
    if( titleWidth > Bounds().Width() )
        ResizeTo( titleWidth < maxWidth ? titleWidth : maxWidth,
                  Bounds().Height() );

    /* Follow the playlist */
    BMessenger * messenger = new BMessenger( this );
    for( size_t i = 0; i < ARRAY_SIZE( kPlaylistVariables ); i++ )
        var_AddCallback( p_playlist, kPlaylistVariables[i],
                         PlaylistChanged, messenger );
    fSettings->AddPointer( "messenger", messenger );

    fFullscreenMI->SetMarked( var_GetBool( p_playlist, "fullscreen" ) );
    fOnTopMI->SetMarked( var_GetBool( p_playlist, "video-on-top" ) );

    /* Prepare fow showing */
    _SetMenusEnabled( false );
    p_mediaControl->SetEnabled( false );

    _RestoreSettings();

    BMessage update( MSG_UPDATE );
    fUpdateRunner = new BMessageRunner( BMessenger( this ), &update,
                                        INTERFACE_UPDATE_PERIOD );

    Show();
}

InterfaceWindow::~InterfaceWindow()
{
    delete fUpdateRunner;
    delete fFilePanel;
    delete fSettings;
}

/*****************************************************************************
 * InterfaceWindow::Detach
 *****************************************************************************/
void
InterfaceWindow::Detach()
{
    BMessenger * messenger = NULL;
    fSettings->FindPointer( "messenger", (void **) &messenger );
    for( size_t i = 0; i < ARRAY_SIZE( kPlaylistVariables ); i++ )
        var_DelCallback( p_playlist, kPlaylistVariables[i],
                         PlaylistChanged, messenger );
    fSettings->RemoveName( "messenger" );
    delete messenger;

    if( Lock() )
    {
        delete fUpdateRunner;
        fUpdateRunner = NULL;
        Unlock();
    }

    fPlaylistWindow->ReallyQuit();
    fMessagesWindow->ReallyQuit();
    if( fPreferencesWindow )
        fPreferencesWindow->ReallyQuit();
}

/*****************************************************************************
 * InterfaceWindow::FrameResized
 *****************************************************************************/
void
InterfaceWindow::FrameResized(float width, float height)
{
    BRect r(Bounds());
    fMenuBar->MoveTo(r.LeftTop());
    fMenuBar->ResizeTo(r.Width(), fMenuBar->Bounds().Height());
    r.top += fMenuBar->Bounds().Height() + 1.0;
    p_mediaControl->MoveTo(r.LeftTop());
    p_mediaControl->ResizeTo(r.Width(), r.Height());
}

/*****************************************************************************
 * InterfaceWindow::MessageReceived
 *****************************************************************************/
void InterfaceWindow::MessageReceived( BMessage * p_message )
{
    input_thread_t * p_input = NULL;

    switch( p_message->what )
    {
        case B_ABOUT_REQUESTED:
        {
            BAlert * alert = new BAlert( _("About"),
                "VideoLAN Client " VERSION " (Haiku interface)\n\n"
                COPYRIGHT_MESSAGE "\n\n"
                "The VideoLAN team <videolan@videolan.org>\n"
                "https://www.videolan.org/", _("OK") );
            alert->Go( NULL );
            break;
        }

        case OPEN_FILE:
            _ShowFilePanel( B_REFS_RECEIVED, _("VLC media player: Open Media Files") );
            break;

        case LOAD_SUBFILE:
            _ShowFilePanel( SUBFILE_RECEIVED, _("VLC media player: Open Subtitle File") );
            break;

        case OPEN_LOCATION:
        {
            BRect frame( 0, 0, 420, 80 );
            frame.OffsetTo( Frame().left, Frame().top - 120 );
            make_sure_frame_is_on_screen( frame );
            ( new LocationWindow( frame, BMessenger( this ) ) )->Show();
            break;
        }

        case LOCATION_ENTERED:
        {
            const char * psz_url;
            if( p_message->FindString( "url", &psz_url ) == B_OK && *psz_url )
            {
                char * psz_uri = strstr( psz_url, "://" ) ? strdup( psz_url )
                                 : vlc_path2uri( psz_url, NULL );
                if( psz_uri )
                {
                    playlist_Add( p_playlist, psz_uri, true );
                    free( psz_uri );
                }
            }
            break;
        }

        case OPEN_PLAYLIST:
            _ShowWindow( fPlaylistWindow );
            break;

        case OPEN_MESSAGES:
            fMessagesWindow->Subscribe();
            _ShowWindow( fMessagesWindow );
            break;

        case OPEN_PREFERENCES:
            if( !fPreferencesWindow )
            {
                /* Built on first use: it reads every module's options */
                BScreen screen( this );
                BRect s = screen.Frame();
                BRect frame( ( s.right - PREFS_WINDOW_WIDTH ) / 2,
                             ( s.bottom - PREFS_WINDOW_HEIGHT ) / 2,
                             ( s.right + PREFS_WINDOW_WIDTH ) / 2,
                             ( s.bottom + PREFS_WINDOW_HEIGHT ) / 2 );
                BRect saved;
                if( fSettings->FindRect( "settings frame", &saved ) == B_OK )
                {
                    make_sure_frame_is_on_screen( saved );
                    frame = saved;
                }
                fPreferencesWindow = new PreferencesWindow( p_intf, frame,
                                                            _("Preferences") );
            }
            _ShowWindow( fPreferencesWindow );
            break;

        case OPEN_DVD:
        {
            const char * psz_device;
            if( p_message->FindString( "device", &psz_device ) == B_OK )
            {
                char * psz_uri;
                if( asprintf( &psz_uri, "dvd://%s", psz_device ) != -1 )
                {
                    playlist_Add( p_playlist, psz_uri, true );
                    free( psz_uri );
                }
            }
            break;
        }

        case SUBFILE_RECEIVED:
        {
            entry_ref ref;
            if( p_message->FindRef( "refs", 0, &ref ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
            {
                BPath path( &ref );
                char * psz_uri = vlc_path2uri( path.Path(), NULL );
                if( psz_uri )
                {
                    input_AddSlave( p_input, SLAVE_TYPE_SPU, psz_uri,
                                    true, true, true );
                    free( psz_uri );
                }
            }
            break;
        }

        case STOP_PLAYBACK:
            playlist_Stop( p_playlist );
            p_mediaControl->SetStatus( -1, INPUT_RATE_DEFAULT );
            break;

        case START_PLAYBACK:
        case PAUSE_PLAYBACK:
            p_input = playlist_CurrentInput( p_playlist );
            if( p_input )
                playlist_TogglePause( p_playlist );
            else
                playlist_Play( p_playlist );
            break;

        case NORMAL_PLAY:
        {
            float rate;
            if( p_message->FindFloat( "rate", &rate ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetFloat( p_playlist, "rate", rate );
            break;
        }

        case VOLUME_CHG:
            playlist_VolumeSet( p_playlist, (float) p_mediaControl->GetVolume()
                                            / AOUT_VOLUME_DEFAULT );
            playlist_MuteSet( p_playlist, false );
            break;

        case VOLUME_UP:
            playlist_VolumeUp( p_playlist, 1, NULL );
            break;

        case VOLUME_DOWN:
            playlist_VolumeDown( p_playlist, 1, NULL );
            break;

        case VOLUME_MUTE:
            playlist_MuteToggle( p_playlist );
            break;

        case SELECT_CHANNEL:
        case SELECT_SUBTITLE:
        {
            int32 channel;
            const char * psz_var = p_message->what == SELECT_CHANNEL
                                   ? "audio-es" : "spu-es";
            if( p_message->FindInt32( "value", &channel ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetInteger( p_input, psz_var, channel );
            break;
        }

        case PREV_TITLE:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_TriggerCallback( p_input, "prev-title" );
            break;

        case NEXT_TITLE:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_TriggerCallback( p_input, "next-title" );
            break;

        case TOGGLE_TITLE:
        {
            int32 index;
            if( p_message->FindInt32( "value", &index ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetInteger( p_input, "title", index );
            break;
        }

        case PREV_CHAPTER:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_TriggerCallback( p_input, "prev-chapter" );
            break;

        case NEXT_CHAPTER:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_TriggerCallback( p_input, "next-chapter" );
            break;

        case TOGGLE_CHAPTER:
        {
            int32 index;
            if( p_message->FindInt32( "value", &index ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetInteger( p_input, "chapter", index );
            break;
        }

        case PREV_FILE:
            playlist_Prev( p_playlist );
            break;

        case NEXT_FILE:
            playlist_Next( p_playlist );
            break;

        case NAVIGATE_PREV:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
            {
                /* First try to go to previous chapter */
                if( var_GetInteger( p_input, "chapter" ) > 0 )
                {
                    var_TriggerCallback( p_input, "prev-chapter" );
                    break;
                }

                /* Try to go to previous title */
                if( var_GetInteger( p_input, "title" ) > 0 )
                {
                    var_TriggerCallback( p_input, "prev-title" );
                    break;
                }
            }
            /* Try to go to previous file */
            playlist_Prev( p_playlist );
            break;

        case NAVIGATE_NEXT:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
            {
                /* First try to go to next chapter */
                int i_chapter_count = var_CountChoices( p_input, "chapter" );
                if( i_chapter_count > var_GetInteger( p_input, "chapter" ) + 1 )
                {
                    var_TriggerCallback( p_input, "next-chapter" );
                    break;
                }

                /* Try to go to next title */
                int i_title_count = var_CountChoices( p_input, "title" );
                if( i_title_count > var_GetInteger( p_input, "title" ) + 1 )
                {
                    var_TriggerCallback( p_input, "next-title" );
                    break;
                }
            }
            /* Try to go to next file */
            playlist_Next( p_playlist );
            break;

        case TOGGLE_FULL_SCREEN:
        {
            bool b_fs = !var_GetBool( p_playlist, "fullscreen" );
            var_SetBool( p_playlist, "fullscreen", b_fs );
            _SetVoutBool( "fullscreen", b_fs );
            fFullscreenMI->SetMarked( b_fs );
            break;
        }

        case TOGGLE_ON_TOP:
        {
            bool b_top = !var_GetBool( p_playlist, "video-on-top" );
            var_SetBool( p_playlist, "video-on-top", b_top );
            _SetVoutBool( "video-on-top", b_top );
            fOnTopMI->SetMarked( b_top );
            break;
        }

        // drag'n'drop and system messages
        case MSG_SOUNDPLAY:
            // convert soundplay drag'n'drop message (containing paths)
            // to normal message (containing refs)
            {
                const char* path;
                for ( int32 i = 0; p_message->FindString( "path", i, &path ) == B_OK; i++ )
                {
                    entry_ref ref;
                    if ( get_ref_for_path( path, &ref ) == B_OK )
                        p_message->AddRef( "refs", &ref );
                }
            }
            // fall through
        case B_REFS_RECEIVED:
        case B_SIMPLE_DATA:
            /* file(s) opened by the File menu -> append to the playlist;
               file(s) opened by drag & drop -> replace playlist;
               file(s) opened by 'shift' + drag & drop -> append */
            _OpenRefs( p_message, p_message->WasDropped()
                                  && !( modifiers() & B_SHIFT_KEY ) );
            break;

        case PLAYLIST_CHANGED:
            if( fPlaylistWindow->Lock() )
            {
                fPlaylistWindow->UpdatePlaylist();
                fPlaylistWindow->Unlock();
            }
            break;

        case MSG_UPDATE:
            UpdateInterface();
            break;

        default:
            BWindow::MessageReceived( p_message );
            break;
    }

    if( p_input )
        vlc_object_release( p_input );
}

/*****************************************************************************
 * InterfaceWindow::QuitRequested
 *****************************************************************************/
bool InterfaceWindow::QuitRequested()
{
    _StoreSettings();

    /* The window is closed by the interface module when LibVLC stops */
    libvlc_Quit( p_intf->obj.libvlc );
    return false;
}

/*****************************************************************************
 * InterfaceWindow::UpdateInterface
 *****************************************************************************/
void InterfaceWindow::UpdateInterface()
{
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );

    if( p_input )
    {
        bool hasTitles   = var_CountChoices( p_input, "title" ) > 1;
        bool hasChapters = var_CountChoices( p_input, "chapter" ) > 1;
        float rate = var_GetFloat( p_input, "rate" );

        p_mediaControl->SetEnabled( true );
        p_mediaControl->SetStatus( var_GetInteger( p_input, "state" ),
                                   INPUT_RATE_DEFAULT / rate );
        p_mediaControl->SetProgress( var_GetFloat( p_input, "position" ) );
        _SetMenusEnabled( true, hasChapters, hasTitles );
        _UpdateSpeedMenu( rate );

        bool muted = playlist_MuteGet( p_playlist ) > 0;
        p_mediaControl->SetAudioEnabled( true );
        p_mediaControl->SetMuted( muted );
        fMuteMI->SetMarked( muted );

        vlc_object_release( p_input );
    }
    else
    {
        p_mediaControl->SetStatus( -1, INPUT_RATE_DEFAULT );
        p_mediaControl->SetAudioEnabled( false );
        _SetMenusEnabled( false );

        PL_LOCK;
        bool b_empty = playlist_IsEmpty( p_playlist );
        PL_UNLOCK;
        p_mediaControl->SetEnabled( !b_empty );
        p_mediaControl->SetProgress( 0 );
    }

    fFullscreenMI->SetMarked( var_GetBool( p_playlist, "fullscreen" ) );
    fLastUpdateTime = system_time();
}

/*****************************************************************************
 * InterfaceWindow::IsStopped
 *****************************************************************************/
bool
InterfaceWindow::IsStopped() const
{
    return (system_time() - fLastUpdateTime > INTERFACE_UPDATE_TIMEOUT);
}

/*****************************************************************************
 * InterfaceWindow::_SetMenusEnabled
 *****************************************************************************/
void
InterfaceWindow::_SetMenusEnabled(bool hasFile, bool hasChapters, bool hasTitles)
{
    if (!hasFile)
    {
        hasChapters = false;
        hasTitles = false;
    }
    if ( fNextChapterMI->IsEnabled() != hasChapters )
         fNextChapterMI->SetEnabled( hasChapters );
    if ( fPrevChapterMI->IsEnabled() != hasChapters )
         fPrevChapterMI->SetEnabled( hasChapters );
    if ( fChapterMenu->IsEnabled() != hasChapters )
         fChapterMenu->SetEnabled( hasChapters );
    if ( fNextTitleMI->IsEnabled() != hasTitles )
         fNextTitleMI->SetEnabled( hasTitles );
    if ( fPrevTitleMI->IsEnabled() != hasTitles )
         fPrevTitleMI->SetEnabled( hasTitles );
    if ( fTitleMenu->IsEnabled() != hasTitles )
         fTitleMenu->SetEnabled( hasTitles );
    if ( fLanguageMenu->IsEnabled() != hasFile )
         fLanguageMenu->SetEnabled( hasFile );
    if ( fSubtitlesMenu->IsEnabled() != hasFile )
         fSubtitlesMenu->SetEnabled( hasFile );
    if ( fSpeedMenu->IsEnabled() != hasFile )
         fSpeedMenu->SetEnabled( hasFile );
}

/*****************************************************************************
 * InterfaceWindow::_UpdateSpeedMenu
 *****************************************************************************/
void
InterfaceWindow::_UpdateSpeedMenu( float rate )
{
    for( int i = 0; i < 7; i++ )
    {
        if( fabsf( rate - kSpeeds[i] ) < kSpeeds[i] / 100.f )
        {
            if( !fSpeedMI[i]->IsMarked() )
                fSpeedMI[i]->SetMarked( true );
            return;
        }
    }
    /* Some other rate: unmark all */
    BMenuItem * marked = fSpeedMenu->FindMarked();
    if( marked )
        marked->SetMarked( false );
}

/*****************************************************************************
 * InterfaceWindow::_ShowFilePanel
 *****************************************************************************/
void
InterfaceWindow::_ShowFilePanel( uint32 command, const char* windowTitle )
{
    if( !fFilePanel )
    {
        fFilePanel = new BFilePanel( B_OPEN_PANEL, NULL, NULL,
                                     B_FILE_NODE | B_DIRECTORY_NODE );
        fFilePanel->SetTarget( this );
    }
    fFilePanel->Window()->SetTitle( windowTitle );
    BMessage message( command );
    fFilePanel->SetMessage( &message );
    if ( !fFilePanel->IsShowing() )
    {
        fFilePanel->Refresh();
        fFilePanel->Show();
    }
}

/*****************************************************************************
 * InterfaceWindow::_ShowWindow
 *****************************************************************************/
void
InterfaceWindow::_ShowWindow( BWindow* window )
{
    if( window->Lock() )
    {
        if( window->IsHidden() )
            window->Show();
        else
            window->Activate();
        window->Unlock();
    }
}

/*****************************************************************************
 * InterfaceWindow::_OpenRefs: adds files (or folders) to the playlist
 *****************************************************************************/
void
InterfaceWindow::_OpenRefs( BMessage* message, bool replace )
{
    int32 count;
    type_code dummy;
    if( message->GetInfo( "refs", &dummy, &count ) != B_OK || count < 1 )
        return;

    if( replace )
        playlist_Clear( p_playlist, pl_Unlocked );

    entry_ref ref;
    for( int i = 0; message->FindRef( "refs", i, &ref ) == B_OK; i++ )
    {
        BPath path( &ref );
        if( path.InitCheck() != B_OK )
            continue;

        char * psz_uri = vlc_path2uri( path.Path(), NULL );
        if( psz_uri )
        {
            /* Play the first one, queue the others */
            playlist_Add( p_playlist, psz_uri, i == 0 );
            free( psz_uri );
        }
    }
}

/*****************************************************************************
 * InterfaceWindow::_SetVoutBool: applies a setting to the open videos
 *****************************************************************************/
void
InterfaceWindow::_SetVoutBool( const char* variable, bool value )
{
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    if( !p_input )
        return;

    vout_thread_t ** pp_vouts;
    size_t i_vouts;
    if( input_Control( p_input, INPUT_GET_VOUTS, &pp_vouts, &i_vouts ) == VLC_SUCCESS )
    {
        for( size_t i = 0; i < i_vouts; i++ )
        {
            var_SetBool( pp_vouts[i], variable, value );
            vlc_object_release( pp_vouts[i] );
        }
        free( pp_vouts );
    }
    vlc_object_release( p_input );
}

// set_window_pos
void
set_window_pos( BWindow* window, BRect frame )
{
    // sanity checks: make sure window is not too big/small
    // and that it's not off-screen
    float minWidth, maxWidth, minHeight, maxHeight;
    window->GetSizeLimits( &minWidth, &maxWidth, &minHeight, &maxHeight );

    make_sure_frame_is_within_limits( frame,
                                      minWidth, minHeight, maxWidth, maxHeight );
    if ( make_sure_frame_is_on_screen( frame ) )
    {
        window->MoveTo( frame.LeftTop() );
        window->ResizeTo( frame.Width(), frame.Height() );
    }
}

// launch_window
void
launch_window( BWindow* window, bool showing )
{
    if ( window->Lock() )
    {
        if ( showing )
        {
            if ( window->IsHidden() )
                window->Show();
        }
        else
        {
            if ( !window->IsHidden() )
                window->Hide();
        }
        window->Unlock();
    }
}

/*****************************************************************************
 * InterfaceWindow::_RestoreSettings
 *****************************************************************************/
void
InterfaceWindow::_RestoreSettings()
{
    if ( load_settings( fSettings, "interface_settings", SETTINGS_FOLDER ) == B_OK )
    {
        BRect frame;
        if ( fSettings->FindRect( "main frame", &frame ) == B_OK )
            set_window_pos( this, frame );
        if (fSettings->FindRect( "playlist frame", &frame ) == B_OK )
            set_window_pos( fPlaylistWindow, frame );
        if (fSettings->FindRect( "messages frame", &frame ) == B_OK )
            set_window_pos( fMessagesWindow, frame );

        bool showing;
        if ( fSettings->FindBool( "playlist showing", &showing ) == B_OK )
            launch_window( fPlaylistWindow, showing );
        if ( fSettings->FindBool( "messages showing", &showing ) == B_OK && showing )
        {
            fMessagesWindow->Subscribe();
            launch_window( fMessagesWindow, showing );
        }
    }
}

/*****************************************************************************
 * InterfaceWindow::_StoreSettings
 *****************************************************************************/
void
InterfaceWindow::_StoreSettings()
{
    /* Save the windows positions */
    if ( fSettings->ReplaceRect( "main frame", Frame() ) != B_OK )
        fSettings->AddRect( "main frame", Frame() );
    if ( fPlaylistWindow->Lock() )
    {
        if (fSettings->ReplaceRect( "playlist frame", fPlaylistWindow->Frame() ) != B_OK)
            fSettings->AddRect( "playlist frame", fPlaylistWindow->Frame() );
        if (fSettings->ReplaceBool( "playlist showing", !fPlaylistWindow->IsHidden() ) != B_OK)
            fSettings->AddBool( "playlist showing", !fPlaylistWindow->IsHidden() );
        fPlaylistWindow->Unlock();
    }
    if ( fMessagesWindow->Lock() )
    {
        if (fSettings->ReplaceRect( "messages frame", fMessagesWindow->Frame() ) != B_OK)
            fSettings->AddRect( "messages frame", fMessagesWindow->Frame() );
        if (fSettings->ReplaceBool( "messages showing", !fMessagesWindow->IsHidden() ) != B_OK)
            fSettings->AddBool( "messages showing", !fMessagesWindow->IsHidden() );
        fMessagesWindow->Unlock();
    }

    if ( fPreferencesWindow && fPreferencesWindow->Lock() )
    {
        if (fSettings->ReplaceRect( "settings frame", fPreferencesWindow->Frame() ) != B_OK)
            fSettings->AddRect( "settings frame", fPreferencesWindow->Frame() );
        fPreferencesWindow->Unlock();
    }

    /* Don't store the runtime pointer */
    BMessage settings( *fSettings );
    settings.RemoveName( "messenger" );
    save_settings( &settings, "interface_settings", SETTINGS_FOLDER );
}


/*****************************************************************************
 * CDMenu::CDMenu
 *****************************************************************************/
CDMenu::CDMenu(const char *name)
      : BMenu(name)
{
}

/*****************************************************************************
 * CDMenu::~CDMenu
 *****************************************************************************/
CDMenu::~CDMenu()
{
}

/*****************************************************************************
 * CDMenu::AttachedToWindow
 *****************************************************************************/
void CDMenu::AttachedToWindow(void)
{
    // remove all items
    while ( BMenuItem* item = RemoveItem( (int32)0 ) )
        delete item;
    if ( GetCD() == 0 )
    {
        BMenuItem* item = new BMenuItem( _("No disc found"), NULL );
        item->SetEnabled( false );
        AddItem( item );
    }
    BMenu::AttachedToWindow();
}

/*****************************************************************************
 * CDMenu::GetCD: lists the mounted CDs and DVDs
 *****************************************************************************/
int CDMenu::GetCD()
{
    int i_count = 0;
    BVolumeRoster volRoster;
    BVolume vol;
    status_t status = volRoster.GetNextVolume( &vol );
    while ( status ==  B_NO_ERROR )
    {
        BString deviceName;
        BString volumeName;
        bool isCDROM;
        if ( get_volume_info( vol, volumeName, isCDROM, deviceName )
             && isCDROM )
        {
            BMessage* msg = new BMessage( OPEN_DVD );
            msg->AddString( "device", deviceName.String() );
            BMenuItem* item = new BMenuItem( volumeName.String(), msg );
            AddItem( item );
            i_count++;
        }
        vol.Unset();
        status = volRoster.GetNextVolume( &vol );
    }
    return i_count;
}

/*****************************************************************************
 * VariableMenu
 *****************************************************************************/
VariableMenu::VariableMenu( intf_thread_t * _p_intf, const char * psz_name,
                            const char * _psz_variable, uint32 command )
    : BMenu( psz_name ),
      p_intf( _p_intf ),
      psz_variable( strdup( _psz_variable ) ),
      i_command( command )
{
}

VariableMenu::~VariableMenu()
{
    free( psz_variable );
}

void VariableMenu::AttachedToWindow()
{
    BMenuItem * item;

    // remove all items
    while( ( item = RemoveItem( (int32)0 ) ) )
    {
        delete item;
    }

    SetRadioMode( true );

    input_thread_t * p_input = pl_CurrentInput( p_intf );
    if( !p_input )
    {
        BMenu::AttachedToWindow();
        return;
    }

    vlc_value_t val_list, text_list;
    int64_t i_current = var_GetInteger( p_input, psz_variable );
    if( var_Change( p_input, psz_variable, VLC_VAR_GETCHOICES,
                    &val_list, &text_list ) == VLC_SUCCESS )
    {
        for( int i = 0; i < val_list.p_list->i_count; i++ )
        {
            BMessage * message = new BMessage( i_command );
            message->AddInt32( "value", val_list.p_list->p_values[i].i_int );
            const char * psz_text = text_list.p_list->p_values[i].psz_string;
            BString label;
            if( psz_text && *psz_text )
                label = psz_text;
            else
                label << val_list.p_list->p_values[i].i_int;
            item = new BMenuItem( label.String(), message );
            if( val_list.p_list->p_values[i].i_int == i_current )
            {
                item->SetMarked( true );
            }
            AddItem( item );
        }
        var_FreeList( &val_list, &text_list );
    }

    vlc_object_release( p_input );

    BMenu::AttachedToWindow();
}


/*****************************************************************************
 * load_settings
 *****************************************************************************/
status_t
load_settings( BMessage* message, const char* fileName, const char* folder )
{
    status_t ret = B_BAD_VALUE;
    if ( message )
    {
        BPath path;
        if ( ( ret = find_directory( B_USER_SETTINGS_DIRECTORY, &path ) ) == B_OK )
        {
            // passing folder is optional
            if ( folder )
                ret = path.Append( folder );
            if ( ret == B_OK && ( ret = path.Append( fileName ) ) == B_OK )
            {
                BFile file( path.Path(), B_READ_ONLY );
                if ( ( ret = file.InitCheck() ) == B_OK )
                {
                    ret = message->Unflatten( &file );
                    file.Unset();
                }
            }
        }
    }
    return ret;
}

/*****************************************************************************
 * save_settings
 *****************************************************************************/
status_t
save_settings( BMessage* message, const char* fileName, const char* folder )
{
    status_t ret = B_BAD_VALUE;
    if ( message )
    {
        BPath path;
        if ( ( ret = find_directory( B_USER_SETTINGS_DIRECTORY, &path ) ) == B_OK )
        {
            // passing folder is optional
            if ( folder && ( ret = path.Append( folder ) ) == B_OK )
                ret = create_directory( path.Path(), 0777 );
            if ( ret == B_OK && ( ret = path.Append( fileName ) ) == B_OK )
            {
                BFile file( path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE );
                if ( ( ret = file.InitCheck() ) == B_OK )
                {
                    ret = message->Flatten( &file );
                    file.Unset();
                }
            }
        }
    }
    return ret;
}
