/*****************************************************************************
 * ModernWindow.cpp: the modern (VLC 3) Haiku interface window
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

/*
 * The main window of VLC 3's Qt interface, rebuilt with Haiku views. The
 * video output draws in the window's video area: the interface module
 * provides it as a "vout window" of type VOUT_WINDOW_TYPE_HAIKU, whose
 * handle is a BView.
 */

#include <InterfaceKit.h>
#include <AppKit.h>
#include <StorageKit.h>
#include <SupportKit.h>
#include <Alert.h>
#include <Clipboard.h>
#include <LayoutBuilder.h>
#include <PopUpMenu.h>
#include <SeparatorView.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <vlc_playlist.h>
#include <vlc_aout.h>
#include <vlc_vout.h>
#include <vlc_vout_window.h>
#include <vlc_vout_display.h>
#include <vlc_actions.h>
#include <vlc_url.h>

#include "MsgVals.h"
#include "InterfaceWindow.h"
#include "InfoWindow.h"
#include "MessagesWindow.h"
#include "ModernControls.h"
#include "ModernPlaylist.h"
#include "PreferencesWindow.h"
#include "ModernWindow.h"

#define UPDATE_PERIOD        100000
#define CONTROLLER_TIMEOUT  2500000
#define SETTINGS_FOLDER     "VideoLAN Client"
#define RECENT_MAX          10
#define APP_TITLE           _("VLC media player")

/* The window that takes videos, and the windows still alive */
static vlc_mutex_t sEmbedLock = VLC_STATIC_MUTEX;
static ModernWindow * sEmbedder = NULL;
static int32 sWindowCount = 0;

/* Recently played media, shared with the menus' threads */
static vlc_mutex_t sRecentLock = VLC_STATIC_MUTEX;
static BString sRecent[RECENT_MAX];
static int sRecentCount = 0;

static void SetTargets( BMenu * menu, BHandler * target )
{
    for( int32 i = 0; BMenuItem * item = menu->ItemAt( i ); i++ )
    {
        if( item->Submenu() )
            SetTargets( item->Submenu(), target );
        else if( !item->Target() )
            item->SetTarget( target );
    }
}

static BString Ellipsis( const char * text )
{
    BString s( text );
    s << B_UTF8_ELLIPSIS;
    return s;
}

/*****************************************************************************
 * Key translation, as in the video output
 *****************************************************************************/
static int KeyToVLC( const BMessage * msg )
{
    int32 key = 0, raw_char = 0;
    uint32 modifiers = 0;
    const char * bytes = NULL;

    msg->FindInt32( "key", &key );
    msg->FindInt32( "raw_char", &raw_char );
    msg->FindInt32( "modifiers", (int32 *) &modifiers );
    msg->FindString( "bytes", &bytes );

    int code = KEY_UNSET;
    switch( raw_char )
    {
        case B_ESCAPE:      code = KEY_ESC; break;
        case B_BACKSPACE:   code = KEY_BACKSPACE; break;
        case B_TAB:         return KEY_UNSET;  /* keyboard navigation */
        case B_ENTER:       code = KEY_ENTER; break;
        case B_SPACE:       code = ' '; break;
        case B_LEFT_ARROW:  code = KEY_LEFT; break;
        case B_RIGHT_ARROW: code = KEY_RIGHT; break;
        case B_UP_ARROW:    code = KEY_UP; break;
        case B_DOWN_ARROW:  code = KEY_DOWN; break;
        case B_INSERT:      code = KEY_INSERT; break;
        case B_DELETE:      code = KEY_DELETE; break;
        case B_HOME:        code = KEY_HOME; break;
        case B_END:         code = KEY_END; break;
        case B_PAGE_UP:     code = KEY_PAGEUP; break;
        case B_PAGE_DOWN:   code = KEY_PAGEDOWN; break;
        case B_FUNCTION_KEY:
            if( key >= B_F1_KEY && key <= B_F12_KEY )
                code = KEY_F1 + ( key - B_F1_KEY );
            else if( key == B_PAUSE_KEY )
                code = KEY_PAUSE;
            break;
        default:
            if( ( modifiers & ( B_COMMAND_KEY | B_CONTROL_KEY ) ) == 0
             && bytes != NULL && (unsigned char) bytes[0] >= 0x20
             && (unsigned char) bytes[0] < 0x80 )
                return bytes[0];
            if( raw_char >= 0x20 && raw_char < 0x7f )
                code = raw_char;
            break;
    }
    if( code == KEY_UNSET )
        return KEY_UNSET;
    if( modifiers & B_SHIFT_KEY )
        code |= KEY_MODIFIER_SHIFT;
    if( modifiers & B_COMMAND_KEY )
        code |= KEY_MODIFIER_CTRL;
    if( modifiers & B_CONTROL_KEY )
        code |= KEY_MODIFIER_ALT;
    if( modifiers & B_OPTION_KEY )
        code |= KEY_MODIFIER_META;
    return code;
}

/*****************************************************************************
 * VideoArea: where the video output adds its view; the cone without video
 *****************************************************************************/
class VideoArea : public BView
{
 public:
    VideoArea( ModernWindow * window )
        : BView( "video area", B_WILL_DRAW | B_FRAME_EVENTS
                               | B_FULL_UPDATE_ON_RESIZE ),
          fWindow( window )
    {
        SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    }

    void SetInfo( const char * title, const char * artist )
    {
        if( fTitle != title || fArtist != artist )
        {
            fTitle = title;
            fArtist = artist;
            Invalidate();
        }
    }

    virtual void Draw( BRect )
    {
        if( CountChildren() > 0 )
            return;

        BRect b = Bounds();
        float unit = be_plain_font->Size();
        float size = std::min( std::min( b.Width(), b.Height() ) * .5f,
                               unit * 16 );
        float textHeight = fTitle.Length() ? unit * 4 : 0;
        BRect cone( 0, 0, size, size );
        cone.OffsetTo( floorf( b.left + ( b.Width() - size ) / 2 ),
                       floorf( b.top + ( b.Height() - size - textHeight ) / 2 ) );
        DrawCone( this, cone );

        if( fTitle.Length() && b.Height() > size + textHeight )
        {
            SetHighUIColor( B_PANEL_TEXT_COLOR );
            SetDrawingMode( B_OP_OVER );
            BFont font( be_bold_font );
            font.SetSize( unit * 1.3f );
            BString text( fTitle );
            font.TruncateString( &text, B_TRUNCATE_END, b.Width() - unit * 2 );
            SetFont( &font );
            float y = cone.bottom + unit * 2;
            DrawString( text.String(),
                        BPoint( floorf( ( b.Width() - font.StringWidth( text ) ) / 2 ), y ) );
            if( fArtist.Length() )
            {
                font = *be_plain_font;
                text = fArtist;
                font.TruncateString( &text, B_TRUNCATE_END, b.Width() - unit * 2 );
                SetFont( &font );
                DrawString( text.String(),
                            BPoint( floorf( ( b.Width() - font.StringWidth( text ) ) / 2 ),
                                    y + unit * 1.5f ) );
            }
            SetDrawingMode( B_OP_COPY );
        }
    }

    virtual void FrameResized( float width, float height )
    {
        BView::FrameResized( width, height );
        FitChildren();
        Invalidate();
        fWindow->_ReportVideoSize();
    }

    /* Views in a layout don't resize their children by resizing mode:
     * the video output's view always covers the area */
    void FitChildren()
    {
        BRect b = Bounds();
        for( int32 i = 0; BView * child = ChildAt( i ); i++ )
            if( child->Frame() != b )
            {
                child->MoveTo( b.LeftTop() );
                child->ResizeTo( b.Width(), b.Height() );
            }
    }

    virtual void MouseDown( BPoint where )
    {
        BMessage * msg = Window()->CurrentMessage();
        uint32 buttons = 0;
        int32 clicks = 1;
        if( msg )
        {
            msg->FindInt32( "buttons", (int32 *) &buttons );
            msg->FindInt32( "clicks", &clicks );
        }
        if( buttons & B_SECONDARY_MOUSE_BUTTON )
            Window()->PostMessage( POPUP_MENU );
        else if( clicks == 2 )
            Window()->PostMessage( TOGGLE_FULL_SCREEN );
        (void) where;
    }

    virtual BSize MinSize()
    {
        return BSize( 64, 36 );
    }

    virtual BSize MaxSize()
    {
        return BSize( B_SIZE_UNLIMITED, B_SIZE_UNLIMITED );
    }

    virtual BSize PreferredSize()
    {
        float unit = be_plain_font->Size();
        return BSize( unit * 40, unit * 22 );
    }

 private:
    ModernWindow  * fWindow;
    BString         fTitle;
    BString         fArtist;
};

/*****************************************************************************
 * FullscreenController: the controls over a fullscreen video
 *****************************************************************************/
class FullscreenController : public BWindow
{
 public:
    FullscreenController( BMessenger target )
        : BWindow( BRect( 0, 0, 500, 60 ), "controller", B_BORDERED_WINDOW_LOOK,
                   B_FLOATING_ALL_WINDOW_FEEL,
                   B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
                   | B_NOT_MINIMIZABLE | B_AVOID_FOCUS
                   | B_WILL_ACCEPT_FIRST_CLICK | B_ASYNCHRONOUS_CONTROLS )
    {
        fBar = new ControlBar( "controller", true );
        fBar->SetTarget( target );
        BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
            .Add( fBar );

        /* Start the window thread, hidden */
        Hide();
        Show();
    }

    ControlBar * Bar() const { return fBar; }

    /* At the bottom of the screen; call locked */
    void Place( BRect screen )
    {
        float unit = be_plain_font->Size();
        float width = std::min( screen.Width() * .7f, unit * 50 );
        float height = fBar->PreferredSize().height;
        ResizeTo( width, height );
        MoveTo( floorf( screen.left + ( screen.Width() - width ) / 2 ),
                floorf( screen.bottom - height - unit * 3 ) );
    }

    void ReallyQuit()
    {
        BMessenger messenger( this );
        Lock();
        Quit();
        for( int i = 0; i < 200 && messenger.IsValid(); i++ )
            snooze( 10000 );
    }

 private:
    ControlBar    * fBar;
};

/*****************************************************************************
 * GotoTimeWindow: Playback > Jump to Specific Time
 *****************************************************************************/
class GotoTimeWindow : public BWindow
{
 public:
    GotoTimeWindow( BRect frame, BMessenger target, int64_t time )
        : BWindow( frame, _("Go to Time"), B_TITLED_WINDOW_LOOK,
                   B_MODAL_APP_WINDOW_FEEL,
                   B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
                   | B_AUTO_UPDATE_SIZE_LIMITS ),
          fTarget( target )
    {
        char text[32];
        format_time( text, sizeof( text ), time );
        fText = new BTextControl( "time", _("Go to time"), text, NULL );
        BButton * go = new BButton( "go", _("Go"), new BMessage( TIME_ENTERED ) );
        BButton * cancel = new BButton( "cancel", _("Cancel"),
                                        new BMessage( B_QUIT_REQUESTED ) );
        BLayoutBuilder::Group<>( this, B_VERTICAL )
            .SetInsets( B_USE_WINDOW_SPACING )
            .Add( fText )
            .AddGroup( B_HORIZONTAL )
                .AddGlue()
                .Add( cancel )
                .Add( go )
            .End();
        SetDefaultButton( go );
        fText->MakeFocus( true );
        fText->TextView()->SelectAll();
    }

    virtual void MessageReceived( BMessage * message )
    {
        if( message->what == TIME_ENTERED )
        {
            /* [[h:]m:]s */
            int64_t secs = 0;
            BString text( fText->Text() );
            int32 start = 0;
            for( ;; )
            {
                int32 colon = text.FindFirst( ':', start );
                BString part;
                text.CopyInto( part, start, ( colon < 0 ? text.Length() : colon ) - start );
                secs = secs * 60 + atoi( part.String() );
                if( colon < 0 )
                    break;
                start = colon + 1;
            }
            BMessage msg( TIME_ENTERED );
            msg.AddInt64( "time", secs * CLOCK_FREQ );
            fTarget.SendMessage( &msg );
            Quit();
            return;
        }
        BWindow::MessageReceived( message );
    }

 private:
    BMessenger      fTarget;
    BTextControl  * fText;
};

/*****************************************************************************
 * Menus that follow VLC's state when they open
 *****************************************************************************/
enum { OBJECT_INPUT, OBJECT_VOUT, OBJECT_AOUT, OBJECT_PLAYLIST };

static vlc_object_t * HoldObject( intf_thread_t * p_intf, int kind )
{
    playlist_t * p_playlist = pl_Get( p_intf );
    if( kind == OBJECT_PLAYLIST )
        return (vlc_object_t *) vlc_object_hold( p_playlist );
    if( kind == OBJECT_AOUT )
        return (vlc_object_t *) playlist_GetAout( p_playlist );

    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    if( !p_input || kind == OBJECT_INPUT )
        return (vlc_object_t *) p_input;
    vout_thread_t * p_vout = input_GetVout( p_input );
    vlc_object_release( p_input );
    return (vlc_object_t *) p_vout;
}

/* The choices of a VLC variable: "audio-es", "zoom", "stereo-mode"... */
class VarMenu : public BMenu
{
 public:
    VarMenu( intf_thread_t * p_intf, BHandler * target, const char * label,
             int kind, const char * variable )
        : BMenu( label ), p_intf( p_intf ), fTarget( target ), fKind( kind ),
          fVariable( variable )
    {
        SetRadioMode( false );
    }

    bool IsAvailable()
    {
        vlc_object_t * p_obj = HoldObject( p_intf, fKind );
        if( !p_obj )
            return false;
        bool ok = var_Type( p_obj, fVariable.String() ) != 0
               && var_CountChoices( p_obj, fVariable.String() ) > 0;
        vlc_object_release( p_obj );
        return ok;
    }

    virtual void AttachedToWindow()
    {
        while( BMenuItem * item = RemoveItem( (int32) 0 ) )
            delete item;

        vlc_object_t * p_obj = HoldObject( p_intf, fKind );
        const char * var = fVariable.String();
        int type = p_obj ? var_Type( p_obj, var ) & VLC_VAR_CLASS : 0;
        vlc_value_t val, val_list, text_list;

        if( type && var_Get( p_obj, var, &val ) == VLC_SUCCESS )
        {
            if( var_Change( p_obj, var, VLC_VAR_GETCHOICES,
                            &val_list, &text_list ) == VLC_SUCCESS )
            {
                for( int i = 0; i < val_list.p_list->i_count; i++ )
                {
                    vlc_value_t * v = &val_list.p_list->p_values[i];
                    const char * text = text_list.p_list->p_values[i].psz_string;
                    BMessage * msg = new BMessage( SET_VARIABLE );
                    msg->AddInt32( "object", fKind );
                    msg->AddString( "variable", var );
                    BString label;
                    bool marked = false;
                    switch( type )
                    {
                        case VLC_VAR_INTEGER:
                            msg->AddInt64( "value", v->i_int );
                            label << v->i_int;
                            marked = v->i_int == val.i_int;
                            break;
                        case VLC_VAR_FLOAT:
                            msg->AddFloat( "value", v->f_float );
                            label << v->f_float;
                            marked = v->f_float == val.f_float;
                            break;
                        case VLC_VAR_STRING:
                            msg->AddString( "value", v->psz_string ? v->psz_string : "" );
                            label = v->psz_string;
                            marked = v->psz_string && val.psz_string
                                  && !strcmp( v->psz_string, val.psz_string );
                            break;
                    }
                    if( text && *text )
                        label = text;
                    BMenuItem * item = new BMenuItem( label.String(), msg );
                    item->SetMarked( marked );
                    item->SetTarget( fTarget );
                    AddItem( item );
                }
                var_FreeList( &val_list, &text_list );
            }
            if( type == VLC_VAR_STRING )
                free( val.psz_string );
        }
        if( p_obj )
            vlc_object_release( p_obj );

        if( CountItems() == 0 )
        {
            BMenuItem * item = new BMenuItem( _("Empty"), NULL );
            item->SetEnabled( false );
            AddItem( item );
        }
        BMenu::AttachedToWindow();
    }

 private:
    intf_thread_t * p_intf;
    BHandler      * fTarget;
    int             fKind;
    BString         fVariable;
};

/* Media > Open Disc, with the window as the target */
class DiscMenu : public CDMenu
{
 public:
    DiscMenu( const char * label, BHandler * target )
        : CDMenu( label ), fTarget( target ) {}

    virtual void AttachedToWindow()
    {
        CDMenu::AttachedToWindow();
        SetTargetForItems( fTarget );
    }

 private:
    BHandler    * fTarget;
};

/* Media > Open Recent Media */
class RecentMenu : public BMenu
{
 public:
    RecentMenu( const char * label, BHandler * target )
        : BMenu( label ), fTarget( target ) {}

    virtual void AttachedToWindow()
    {
        while( BMenuItem * item = RemoveItem( (int32) 0 ) )
            delete item;

        vlc_mutex_lock( &sRecentLock );
        for( int i = 0; i < sRecentCount; i++ )
        {
            BMessage * msg = new BMessage( OPEN_RECENT );
            msg->AddString( "uri", sRecent[i] );
            char * psz_path = vlc_uri2path( sRecent[i].String() );
            BString label;
            if( psz_path )
            {
                const char * slash = strrchr( psz_path, '/' );
                label = slash && slash[1] ? slash + 1 : psz_path;
                free( psz_path );
            }
            else
                label = sRecent[i];
            BMenuItem * item = new BMenuItem( label.String(), msg );
            item->SetTarget( fTarget );
            AddItem( item );
        }
        bool empty = sRecentCount == 0;
        vlc_mutex_unlock( &sRecentLock );

        if( !empty )
            AddSeparatorItem();
        BMenuItem * clear = new BMenuItem( _("Clear"), new BMessage( CLEAR_RECENT ) );
        clear->SetEnabled( !empty );
        clear->SetTarget( fTarget );
        AddItem( clear );
        BMenu::AttachedToWindow();
    }

 private:
    BHandler    * fTarget;
};

/* A menu whose items show VLC's state: marks, enabled, Play or Pause */
class StateMenu : public BMenu
{
 public:
    StateMenu( const char * label, ModernWindow * window, intf_thread_t * p_intf )
        : BMenu( label ), fWindow( window ), p_intf( p_intf ) {}

    virtual void AttachedToWindow()
    {
        playlist_t * p_playlist = pl_Get( p_intf );
        input_thread_t * p_input = playlist_CurrentInput( p_playlist );
        vout_thread_t * p_vout = p_input ? input_GetVout( p_input ) : NULL;
        PL_LOCK;
        bool b_empty = playlist_IsEmpty( p_playlist );
        PL_UNLOCK;
        bool b_playing = p_input
                      && var_GetInteger( p_input, "state" ) == PLAYING_S;

        for( int32 i = 0; BMenuItem * item = ItemAt( i ); i++ )
        {
            if( VarMenu * var = dynamic_cast<VarMenu *>( item->Submenu() ) )
            {
                item->SetEnabled( var->IsAvailable() );
                continue;
            }
            if( !item->Message() )
                continue;
            switch( item->Message()->what )
            {
                case TOGGLE_FULL_SCREEN:
                    item->SetMarked( fWindow->fFullscreen );
                    break;
                case TOGGLE_ON_TOP:
                    item->SetMarked( fWindow->fOnTop );
                    break;
                case TOGGLE_MINIMAL:
                    item->SetMarked( fWindow->fMinimal );
                    break;
                case TOGGLE_PLAYLIST:
                    item->SetMarked( fWindow->fPlaylistShown );
                    break;
                case VOLUME_MUTE:
                    item->SetMarked( playlist_MuteGet( p_playlist ) > 0 );
                    break;
                case QUIT_AT_END:
                    item->SetMarked( var_GetBool( p_playlist, "play-and-exit" ) );
                    break;
                case START_PLAYBACK:
                    item->SetLabel( b_playing ? _("Pause") : _("Play") );
                    item->SetEnabled( p_input || !b_empty );
                    break;
                case PREV_FILE:
                case NEXT_FILE:
                    item->SetEnabled( !b_empty );
                    break;
                case STOP_PLAYBACK:
                case JUMP_FORWARD:
                case JUMP_BACKWARD:
                case GOTO_TIME:
                case RATE_FASTER:
                case RATE_FASTER_FINE:
                case RATE_NORMAL:
                case RATE_SLOWER_FINE:
                case RATE_SLOWER:
                case LOAD_SUBFILE:
                    item->SetEnabled( p_input != NULL );
                    break;
                case TAKE_SNAPSHOT:
                    item->SetEnabled( p_vout != NULL );
                    break;
            }
        }
        if( p_vout )
            vlc_object_release( p_vout );
        if( p_input )
            vlc_object_release( p_input );
        BMenu::AttachedToWindow();
    }

 private:
    ModernWindow  * fWindow;
    intf_thread_t * p_intf;
};

/*****************************************************************************
 * Callbacks: they run on other threads, so they only poke the window
 *****************************************************************************/
static int PlaylistChanged( vlc_object_t *, const char *, vlc_value_t,
                            vlc_value_t, void * param )
{
    ( (BMessenger *) param )->SendMessage( PLAYLIST_CHANGED );
    return VLC_SUCCESS;
}

static int PopupMenuChanged( vlc_object_t *, const char *, vlc_value_t,
                             vlc_value_t new_val, void * param )
{
    if( new_val.b_bool )
        ( (BMessenger *) param )->SendMessage( POPUP_MENU );
    return VLC_SUCCESS;
}

static const char * const kPlaylistVariables[] = {
    "playlist-item-append", "playlist-item-deleted", "input-current",
    "item-change",
};

/*****************************************************************************
 * ModernWindow
 *****************************************************************************/
ModernWindow::ModernWindow( intf_thread_t * _p_intf )
    : MainWindow( BRect( 0, 0, 520, 380 ), APP_TITLE, B_TITLED_WINDOW_LOOK,
                  B_NORMAL_WINDOW_FEEL,
                  B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS ),
      p_intf( _p_intf ),
      p_playlist( pl_Get( _p_intf ) ),
      fController( NULL ),
      fPreferencesWindow( NULL ),
      fInfoWindow( NULL ),
      fFilePanel( NULL ),
      fFolderPanel( NULL ),
      fSavePanel( NULL ),
      fUpdateRunner( NULL ),
      fSettings( new BMessage( 'sett' ) ),
      fVideoWnd( NULL ),
      fHasVideo( false ),
      fFullscreen( false ),
      fMinimal( false ),
      fPlaylistShown( false ),
      fOnTop( false ),
      fRemaining( false ),
      fLastMouseMove( 0 ),
      fCurrentItem( -1 )
{
    atomic_add( &sWindowCount, 1 );
    memset( &fState, 0, sizeof( fState ) );

    fMenuBar = new BMenuBar( "menu bar" );
    AddMainMenus( fMenuBar, false );

    fVideoArea = new VideoArea( this );
    fPlaylist = new ModernPlaylist( p_intf );
    fControls = new ControlBar( "controls", false );
    fControls->SetTarget( BMessenger( this ) );

    BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
        .Add( fMenuBar )
        .Add( fVideoArea, 1.0f )
        .Add( fPlaylist, 1.0f )
        .Add( new BSeparatorView( B_HORIZONTAL, B_PLAIN_BORDER ) )
        .Add( fControls );
    fPlaylist->Hide();

    BScreen screen( B_MAIN_SCREEN_ID );
    BRect window_rect, s = screen.Frame();
    window_rect.Set( s.right - 550, s.top + 290, s.right - 150, s.top + 490 );
    fMessagesWindow = new MessagesWindow( p_intf, window_rect, _("Messages") );

    fController = new FullscreenController( BMessenger( this ) );

    /* Default size and place: the middle of the screen */
    float unit = be_plain_font->Size();
    ResizeTo( roundf( unit * 44 ), roundf( unit * 32 ) );
    CenterOnScreen();
    _RestoreSettings();

    /* Follow the playlist and the popup menu requests */
    fCallbackMessenger = new BMessenger( this );
    for( size_t i = 0; i < ARRAY_SIZE( kPlaylistVariables ); i++ )
        var_AddCallback( p_playlist, kPlaylistVariables[i],
                         PlaylistChanged, fCallbackMessenger );
    var_AddCallback( p_intf->obj.libvlc, "intf-popupmenu",
                     PopupMenuChanged, fCallbackMessenger );

    fOnTop = var_GetBool( p_playlist, "video-on-top" );
    if( fOnTop )
        SetFeel( B_FLOATING_ALL_WINDOW_FEEL );
    _UpdateChrome();
    _UpdateInterface();

    BMessage update( MSG_UPDATE );
    fUpdateRunner = new BMessageRunner( BMessenger( this ), &update,
                                        UPDATE_PERIOD );

    /* Videos from now on come to this window */
    vlc_mutex_lock( &sEmbedLock );
    sEmbedder = this;
    vlc_mutex_unlock( &sEmbedLock );

    Show();
}

ModernWindow::~ModernWindow()
{
    delete fUpdateRunner;
    delete fFilePanel;
    delete fFolderPanel;
    delete fSavePanel;
    delete fSettings;
    atomic_add( &sWindowCount, -1 );
}

void ModernWindow::WaitForAll()
{
    for( int i = 0; i < 300 && atomic_get( &sWindowCount ) > 0; i++ )
        snooze( 10000 );
}

/*****************************************************************************
 * Menus
 *****************************************************************************/
void ModernWindow::AddMainMenus( BMenu * menu, bool popup )
{
    if( popup )
    {
        /* As in VLC 3: the playback controls first */
        menu->AddItem( new BMenuItem( _("Play"), new BMessage( START_PLAYBACK ) ) );
        menu->AddItem( new BMenuItem( _("Stop"), new BMessage( STOP_PLAYBACK ) ) );
        menu->AddItem( new BMenuItem( _("Previous"), new BMessage( PREV_FILE ) ) );
        menu->AddItem( new BMenuItem( _("Next"), new BMessage( NEXT_FILE ) ) );
        menu->AddSeparatorItem();
        menu->AddItem( _PlaybackMenu() );
        menu->AddItem( _AudioMenu() );
        menu->AddItem( _VideoMenu() );
        menu->AddItem( _SubtitleMenu() );
        menu->AddItem( _ToolsMenu() );
        menu->AddItem( _ViewMenu() );
        menu->AddSeparatorItem();
        menu->AddItem( new BMenuItem( Ellipsis( _("Open Media") ),
                                      new BMessage( OPEN_FILE ) ) );
        menu->AddItem( new BMenuItem( _("Quit"), new BMessage( B_QUIT_REQUESTED ) ) );
    }
    else
    {
        menu->AddItem( _MediaMenu() );
        menu->AddItem( _PlaybackMenu() );
        menu->AddItem( _AudioMenu() );
        menu->AddItem( _VideoMenu() );
        menu->AddItem( _SubtitleMenu() );
        menu->AddItem( _ToolsMenu() );
        menu->AddItem( _ViewMenu() );
        menu->AddItem( _HelpMenu() );
    }
    SetTargets( menu, this );
}

BMenu * ModernWindow::_MediaMenu()
{
    BMenu * menu = new StateMenu( _("Media"), this, p_intf );
    menu->AddItem( new BMenuItem( Ellipsis( _("Open File") ),
                                  new BMessage( OPEN_FILE ), 'O' ) );
    menu->AddItem( new BMenuItem( Ellipsis( _("Open Folder") ),
                                  new BMessage( OPEN_FOLDER ), 'F' ) );
    menu->AddItem( new DiscMenu( _("Open Disc"), this ) );
    menu->AddItem( new BMenuItem( Ellipsis( _("Open Network Stream") ),
                                  new BMessage( OPEN_LOCATION ), 'N' ) );
    menu->AddItem( new BMenuItem( _("Open Location from clipboard"),
                                  new BMessage( OPEN_CLIPBOARD ), 'V' ) );
    menu->AddItem( new RecentMenu( _("Open Recent Media"), this ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( Ellipsis( _("Save Playlist to File") ),
                                  new BMessage( SAVE_PLAYLIST ), 'Y' ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Quit at the end of playlist"),
                                  new BMessage( QUIT_AT_END ) ) );
    menu->AddItem( new BMenuItem( _("Quit"), new BMessage( B_QUIT_REQUESTED ), 'Q' ) );
    return menu;
}

BMenu * ModernWindow::_PlaybackMenu()
{
    BMenu * menu = new StateMenu( _("Playback"), this, p_intf );
    menu->AddItem( new VarMenu( p_intf, this, _("Title"), OBJECT_INPUT, "title" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Chapter"), OBJECT_INPUT, "chapter" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Program"), OBJECT_INPUT, "program" ) );
    menu->AddSeparatorItem();

    BMenu * speed = new StateMenu( _("Speed"), this, p_intf );
    speed->AddItem( new BMenuItem( _("Faster"), new BMessage( RATE_FASTER ) ) );
    speed->AddItem( new BMenuItem( _("Faster (fine)"), new BMessage( RATE_FASTER_FINE ) ) );
    speed->AddItem( new BMenuItem( _("Normal Speed"), new BMessage( RATE_NORMAL ) ) );
    speed->AddItem( new BMenuItem( _("Slower (fine)"), new BMessage( RATE_SLOWER_FINE ) ) );
    speed->AddItem( new BMenuItem( _("Slower"), new BMessage( RATE_SLOWER ) ) );
    menu->AddItem( speed );
    menu->AddSeparatorItem();

    menu->AddItem( new BMenuItem( _("Jump Forward"), new BMessage( JUMP_FORWARD ) ) );
    menu->AddItem( new BMenuItem( _("Jump Backward"), new BMessage( JUMP_BACKWARD ) ) );
    menu->AddItem( new BMenuItem( Ellipsis( _("Jump to Specific Time") ),
                                  new BMessage( GOTO_TIME ), 'T' ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Play"), new BMessage( START_PLAYBACK ) ) );
    menu->AddItem( new BMenuItem( _("Stop"), new BMessage( STOP_PLAYBACK ) ) );
    menu->AddItem( new BMenuItem( _("Previous"), new BMessage( PREV_FILE ) ) );
    menu->AddItem( new BMenuItem( _("Next"), new BMessage( NEXT_FILE ) ) );
    return menu;
}

BMenu * ModernWindow::_AudioMenu()
{
    BMenu * menu = new StateMenu( _("Audio"), this, p_intf );
    menu->AddItem( new VarMenu( p_intf, this, _("Audio Track"), OBJECT_INPUT, "audio-es" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Stereo Mode"), OBJECT_AOUT, "stereo-mode" ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Increase Volume"), new BMessage( VOLUME_UP ),
                                  B_UP_ARROW ) );
    menu->AddItem( new BMenuItem( _("Decrease Volume"), new BMessage( VOLUME_DOWN ),
                                  B_DOWN_ARROW ) );
    menu->AddItem( new BMenuItem( _("Mute"), new BMessage( VOLUME_MUTE ) ) );
    return menu;
}

BMenu * ModernWindow::_VideoMenu()
{
    BMenu * menu = new StateMenu( _("Video"), this, p_intf );
    menu->AddItem( new VarMenu( p_intf, this, _("Video Track"), OBJECT_INPUT, "video-es" ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Fullscreen"), new BMessage( TOGGLE_FULL_SCREEN ),
                                  'F', B_SHIFT_KEY ) );
    menu->AddItem( new BMenuItem( _("Always on Top"), new BMessage( TOGGLE_ON_TOP ) ) );
    menu->AddSeparatorItem();
    menu->AddItem( new VarMenu( p_intf, this, _("Zoom"), OBJECT_VOUT, "zoom" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Aspect Ratio"), OBJECT_VOUT, "aspect-ratio" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Crop"), OBJECT_VOUT, "crop" ) );
    menu->AddSeparatorItem();
    menu->AddItem( new VarMenu( p_intf, this, _("Deinterlace"), OBJECT_VOUT, "deinterlace" ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Deinterlace mode"), OBJECT_VOUT,
                                "deinterlace-mode" ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Take Snapshot"), new BMessage( TAKE_SNAPSHOT ) ) );
    return menu;
}

BMenu * ModernWindow::_SubtitleMenu()
{
    BMenu * menu = new StateMenu( _("Subtitle"), this, p_intf );
    menu->AddItem( new BMenuItem( Ellipsis( _("Add Subtitle File") ),
                                  new BMessage( LOAD_SUBFILE ) ) );
    menu->AddItem( new VarMenu( p_intf, this, _("Sub Track"), OBJECT_INPUT, "spu-es" ) );
    return menu;
}

BMenu * ModernWindow::_ToolsMenu()
{
    BMenu * menu = new StateMenu( _("Tools"), this, p_intf );
    menu->AddItem( new BMenuItem( Ellipsis( _("Media Information") ),
                                  new BMessage( OPEN_INFO ), 'I' ) );
    menu->AddItem( new BMenuItem( Ellipsis( _("Codec Information") ),
                                  new BMessage( OPEN_CODEC_INFO ), 'J' ) );
    menu->AddItem( new BMenuItem( Ellipsis( _("Messages") ),
                                  new BMessage( OPEN_MESSAGES ), 'M' ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( Ellipsis( _("Preferences") ),
                                  new BMessage( OPEN_PREFERENCES ), 'P' ) );
    return menu;
}

BMenu * ModernWindow::_ViewMenu()
{
    BMenu * menu = new StateMenu( _("View"), this, p_intf );
    menu->AddItem( new BMenuItem( _("Playlist"), new BMessage( TOGGLE_PLAYLIST ), 'L' ) );
    menu->AddSeparatorItem();
    menu->AddItem( new BMenuItem( _("Minimal Interface"), new BMessage( TOGGLE_MINIMAL ),
                                  'H' ) );
    menu->AddItem( new BMenuItem( _("Fullscreen Interface"),
                                  new BMessage( TOGGLE_FULL_SCREEN ) ) );
    menu->AddItem( new BMenuItem( _("Always on Top"), new BMessage( TOGGLE_ON_TOP ) ) );
    menu->AddSeparatorItem();
    BMessage * classic = new BMessage( SWITCH_INTERFACE );
    classic->AddString( "style", "classic" );
    menu->AddItem( new BMenuItem( _("Classic Interface"), classic ) );
    return menu;
}

BMenu * ModernWindow::_HelpMenu()
{
    BMenu * menu = new BMenu( _("Help") );
    menu->AddItem( new BMenuItem( Ellipsis( _("About") ),
                                  new BMessage( B_ABOUT_REQUESTED ) ) );
    return menu;
}

/*****************************************************************************
 * ModernWindow::Detach, Retire
 *****************************************************************************/
void ModernWindow::Detach()
{
    /* No more videos for this window */
    vlc_mutex_lock( &sEmbedLock );
    if( sEmbedder == this )
        sEmbedder = NULL;
    vlc_mutex_unlock( &sEmbedLock );

    for( size_t i = 0; i < ARRAY_SIZE( kPlaylistVariables ); i++ )
        var_DelCallback( p_playlist, kPlaylistVariables[i],
                         PlaylistChanged, fCallbackMessenger );
    var_DelCallback( p_intf->obj.libvlc, "intf-popupmenu",
                     PopupMenuChanged, fCallbackMessenger );
    delete fCallbackMessenger;
    fCallbackMessenger = NULL;

    if( Lock() )
    {
        delete fUpdateRunner;
        fUpdateRunner = NULL;
        Unlock();
    }

    /* Messages still queued must not find them */
    FullscreenController * controller = fController;
    PreferencesWindow * preferences = fPreferencesWindow;
    InfoWindow * info = fInfoWindow;
    if( Lock() )
    {
        fController = NULL;
        fPreferencesWindow = NULL;
        fInfoWindow = NULL;
        Unlock();
    }
    controller->ReallyQuit();
    fMessagesWindow->ReallyQuit();
    if( preferences )
        preferences->ReallyQuit();
    if( info )
        info->ReallyQuit();
}

void ModernWindow::Retire()
{
    _StoreSettings();
    Detach();
    fRetiring = true;
    Hide();

    /* A video still in the window keeps it until the video goes away */
    vlc_mutex_lock( &sEmbedLock );
    bool b_video = fVideoWnd != NULL;
    vlc_mutex_unlock( &sEmbedLock );
    if( !b_video )
        PostMessage( B_QUIT_REQUESTED );
}

bool ModernWindow::QuitRequested()
{
    if( fRetiring )
        return true;
    _StoreSettings();

    /* The window is closed by the interface module when LibVLC stops */
    libvlc_Quit( p_intf->obj.libvlc );
    return false;
}

/*****************************************************************************
 * ModernWindow::DispatchMessage: keys go to VLC's hotkeys, mouse moves show
 * the fullscreen controller
 *****************************************************************************/
void ModernWindow::DispatchMessage( BMessage * message, BHandler * handler )
{
    if( message->what == B_KEY_DOWN && !fRetiring )
    {
        uint32 modifiers = 0;
        int32 raw = 0;
        message->FindInt32( "modifiers", (int32 *) &modifiers );
        message->FindInt32( "raw_char", &raw );
        BView * focus = CurrentFocus();
        bool text = dynamic_cast<BTextView *>( focus ) != NULL;
        bool list = dynamic_cast<BListView *>( focus ) != NULL
                 && ( raw == B_UP_ARROW || raw == B_DOWN_ARROW
                   || raw == B_ENTER || raw == B_DELETE || raw == B_BACKSPACE
                   || raw == B_HOME || raw == B_END
                   || raw == B_PAGE_UP || raw == B_PAGE_DOWN );
        /* The video output's view sends its keys itself */
        bool video = false;
        for( BView * v = focus; v && v != fVideoArea; v = v->Parent() )
            if( v->Parent() == fVideoArea )
                video = true;

        if( !( modifiers & B_COMMAND_KEY ) && !text && !list && !video )
        {
            if( raw == B_ESCAPE && fFullscreen && !fHasVideo )
            {
                _SetFullscreen( false );
                return;
            }
            int code = KeyToVLC( message );
            if( code != KEY_UNSET )
            {
                _SendKey( code );
                return;
            }
        }
    }
    else if( message->what == B_MOUSE_MOVED && fFullscreen )
    {
        BPoint where;
        if( message->FindPoint( "screen_where", &where ) != B_OK )
            message->FindPoint( "where", &where );
        if( where != fLastMousePoint )
        {
            fLastMousePoint = where;
            fLastMouseMove = system_time();
            if( fController && fController->Lock() )
            {
                if( fController->IsHidden() )
                {
                    fController->Bar()->Update( fState );
                    fController->Show();
                }
                fController->Unlock();
            }
        }
    }
    BWindow::DispatchMessage( message, handler );
}

void ModernWindow::_SendKey( int key )
{
    var_SetInteger( p_intf->obj.libvlc, "key-pressed", key );
}

/*****************************************************************************
 * ModernWindow::MessageReceived
 *****************************************************************************/
void ModernWindow::MessageReceived( BMessage * p_message )
{
    input_thread_t * p_input = NULL;

    switch( p_message->what )
    {
        case B_ABOUT_REQUESTED:
        {
            BAlert * alert = new BAlert( _("About"),
                "VLC media player " VERSION " (Haiku interface)\n\n"
                COPYRIGHT_MESSAGE "\n\n"
                "The VideoLAN team <videolan@videolan.org>\n"
                "https://www.videolan.org/", _("OK") );
            alert->Go( NULL );
            break;
        }

        /* Media */
        case OPEN_FILE:
            _ShowFilePanel( B_REFS_RECEIVED, _("Open Media Files") );
            break;

        case OPEN_FOLDER:
            _ShowFolderPanel();
            break;

        case LOAD_SUBFILE:
            _ShowFilePanel( SUBFILE_RECEIVED, _("Open Subtitle File") );
            break;

        case OPEN_LOCATION:
        {
            BRect frame( 0, 0, 420, 80 );
            frame.OffsetTo( Frame().left + ( Frame().Width() - 420 ) / 2,
                            Frame().top + 60 );
            make_sure_frame_is_on_screen( frame );
            ( new LocationWindow( frame, BMessenger( this ) ) )->Show();
            break;
        }

        case LOCATION_ENTERED:
        case OPEN_RECENT:
        {
            const char * psz_url;
            if( ( p_message->FindString( "url", &psz_url ) == B_OK
               || p_message->FindString( "uri", &psz_url ) == B_OK ) && *psz_url )
                _OpenURI( psz_url, true );
            break;
        }

        case OPEN_CLIPBOARD:
        {
            BString text;
            if( be_clipboard->Lock() )
            {
                const char * data;
                ssize_t length;
                BMessage * clip = be_clipboard->Data();
                if( clip && clip->FindData( "text/plain", B_MIME_TYPE,
                                            (const void **) &data, &length ) == B_OK )
                    text.SetTo( data, length );
                be_clipboard->Unlock();
            }
            text.Trim();
            if( text.Length() )
                _OpenURI( text.String(), true );
            break;
        }

        case CLEAR_RECENT:
            vlc_mutex_lock( &sRecentLock );
            sRecentCount = 0;
            vlc_mutex_unlock( &sRecentLock );
            break;

        case OPEN_DVD:
        {
            const char * psz_device;
            if( p_message->FindString( "device", &psz_device ) == B_OK )
            {
                char * psz_uri;
                if( asprintf( &psz_uri, "dvd://%s", psz_device ) != -1 )
                {
                    _OpenURI( psz_uri, true );
                    free( psz_uri );
                }
            }
            break;
        }

        case SAVE_PLAYLIST:
            _ShowSavePanel();
            break;

        case B_SAVE_REQUESTED:
        {
            entry_ref dir;
            const char * name;
            if( p_message->FindRef( "directory", &dir ) == B_OK
             && p_message->FindString( "name", &name ) == B_OK )
            {
                BPath path( &dir );
                BString file( name );
                const char * psz_type = "export-m3u8";
                if( file.IFindLast( ".xspf" ) == file.Length() - 5 )
                    psz_type = "export-xspf";
                else if( file.IFindLast( ".html" ) == file.Length() - 5 )
                    psz_type = "export-html";
                else if( file.IFindLast( ".m3u" ) == file.Length() - 4 )
                    psz_type = "export-m3u";
                else if( file.IFindLast( ".m3u8" ) != file.Length() - 5 )
                    file << ".m3u8";
                path.Append( file.String() );
                playlist_Export( p_playlist, path.Path(), true, psz_type );
            }
            break;
        }

        case QUIT_AT_END:
            var_ToggleBool( p_playlist, "play-and-exit" );
            break;

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

        case MSG_SOUNDPLAY:
        {
            const char * path;
            for( int32 i = 0; p_message->FindString( "path", i, &path ) == B_OK; i++ )
            {
                entry_ref ref;
                if( get_ref_for_path( path, &ref ) == B_OK )
                    p_message->AddRef( "refs", &ref );
            }
        }
        /* fall through */
        case B_REFS_RECEIVED:
        case B_SIMPLE_DATA:
        case FOLDER_RECEIVED:
            _OpenRefs( p_message, false );
            break;

        /* Playback */
        case START_PLAYBACK:
        case PAUSE_PLAYBACK:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                playlist_TogglePause( p_playlist );
            else
                playlist_Play( p_playlist );
            break;

        case STOP_PLAYBACK:
            playlist_Stop( p_playlist );
            break;

        case PREV_FILE:
            playlist_Prev( p_playlist );
            break;

        case NEXT_FILE:
            playlist_Next( p_playlist );
            break;

        case SEEK_PLAYBACK:
        {
            float position;
            if( p_message->FindFloat( "position", &position ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetFloat( p_input, "position", position );
            break;
        }

        case RATE_FASTER:
        case RATE_FASTER_FINE:
        case RATE_NORMAL:
        case RATE_SLOWER_FINE:
        case RATE_SLOWER:
            _ChangeRate( p_message->what );
            break;

        case JUMP_FORWARD:
            var_SetInteger( p_intf->obj.libvlc, "key-action",
                            ACTIONID_JUMP_FORWARD_SHORT );
            break;

        case JUMP_BACKWARD:
            var_SetInteger( p_intf->obj.libvlc, "key-action",
                            ACTIONID_JUMP_BACKWARD_SHORT );
            break;

        case GOTO_TIME:
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
            {
                BRect frame( 0, 0, 280, 80 );
                frame.OffsetTo( Frame().left + ( Frame().Width() - 280 ) / 2,
                                Frame().top + 60 );
                make_sure_frame_is_on_screen( frame );
                ( new GotoTimeWindow( frame, BMessenger( this ),
                                      var_GetInteger( p_input, "time" ) ) )->Show();
            }
            break;

        case TIME_ENTERED:
        {
            int64 time;
            if( p_message->FindInt64( "time", &time ) == B_OK
             && ( p_input = playlist_CurrentInput( p_playlist ) ) )
                var_SetInteger( p_input, "time", time );
            break;
        }

        case TOGGLE_LOOP:
        {
            /* Off, then all, then one, as in VLC 3 */
            bool loop = var_GetBool( p_playlist, "loop" );
            bool repeat = var_GetBool( p_playlist, "repeat" );
            if( repeat )
                var_SetBool( p_playlist, "repeat", false );
            else if( loop )
            {
                var_SetBool( p_playlist, "loop", false );
                var_SetBool( p_playlist, "repeat", true );
            }
            else
                var_SetBool( p_playlist, "loop", true );
            _UpdateInterface();
            break;
        }

        case TOGGLE_RANDOM:
            var_ToggleBool( p_playlist, "random" );
            _UpdateInterface();
            break;

        case TOGGLE_REMAINING:
            fRemaining = !fRemaining;
            _UpdateInterface();
            break;

        case SET_VARIABLE:
            _SetVariable( p_message );
            break;

        /* Audio */
        case VOLUME_CHG:
        {
            int32 volume;
            if( p_message->FindInt32( "volume", &volume ) == B_OK )
            {
                playlist_VolumeSet( p_playlist, volume / 100.f );
                playlist_MuteSet( p_playlist, false );
            }
            break;
        }

        case VOLUME_UP:
            playlist_VolumeUp( p_playlist, 1, NULL );
            break;

        case VOLUME_DOWN:
            playlist_VolumeDown( p_playlist, 1, NULL );
            break;

        case VOLUME_MUTE:
            playlist_MuteToggle( p_playlist );
            _UpdateInterface();
            break;

        /* Video */
        case TOGGLE_FULL_SCREEN:
            _ToggleFullscreen();
            break;

        case TOGGLE_ON_TOP:
        {
            bool b_top = !fOnTop;
            vout_thread_t * p_vout = NULL;
            if( ( p_input = playlist_CurrentInput( p_playlist ) ) )
                p_vout = input_GetVout( p_input );
            if( p_vout )
            {
                var_SetBool( p_vout, "video-on-top", b_top );
                vlc_object_release( p_vout );
            }
            _SetOnTop( b_top );
            break;
        }

        case TAKE_SNAPSHOT:
            var_SetInteger( p_intf->obj.libvlc, "key-action", ACTIONID_SNAPSHOT );
            break;

        /* Tools */
        case OPEN_INFO:
        case OPEN_CODEC_INFO:
            if( fRetiring )
                break;
            if( !fInfoWindow )
            {
                BRect frame( 0, 0, 480, 400 );
                frame.OffsetTo( Frame().left + 40, Frame().top + 40 );
                make_sure_frame_is_on_screen( frame );
                fInfoWindow = new InfoWindow( p_intf, frame );
            }
            if( fInfoWindow->Lock() )
            {
                fInfoWindow->Update();
                fInfoWindow->ShowTab( p_message->what == OPEN_INFO ? 0 : 1 );
                fInfoWindow->Unlock();
            }
            _ShowWindow( fInfoWindow );
            break;

        case OPEN_MESSAGES:
            fMessagesWindow->Subscribe();
            _ShowWindow( fMessagesWindow );
            break;

        case OPEN_PREFERENCES:
            if( fRetiring )
                break;
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

        /* View */
        case TOGGLE_PLAYLIST:
            _SetPlaylistShown( !fPlaylistShown );
            break;

        case TOGGLE_MINIMAL:
            _SetMinimal( !fMinimal );
            break;

        case SWITCH_INTERFACE:
        {
            const char * psz_style;
            if( p_message->FindString( "style", &psz_style ) == B_OK )
                SwitchInterface( p_intf, psz_style );
            break;
        }

        case POPUP_MENU:
            _ShowPopupMenu();
            break;

        /* From the video output's window */
        case VIDEO_SIZE:
        {
            int32 width, height;
            if( p_message->FindInt32( "width", &width ) == B_OK
             && p_message->FindInt32( "height", &height ) == B_OK )
                _VideoSize( width, height );
            break;
        }

        case VIDEO_FULLSCREEN:
            _SetFullscreen( p_message->FindBool( "fullscreen" ) );
            break;

        case VIDEO_STATE:
            _SetOnTop( p_message->FindBool( "on-top" ) );
            break;

        case VIDEO_HIDE_CURSOR:
            if( BView * view = fVideoArea->ChildAt( 0 ) )
                BMessenger( view ).SendMessage( VIDEO_HIDE_CURSOR );
            break;

        case VIDEO_DETACHED:
        {
            vlc_mutex_lock( &sEmbedLock );
            bool b_video = fVideoWnd != NULL;
            vlc_mutex_unlock( &sEmbedLock );
            if( !b_video )
                _VideoDetached();
            break;
        }

        case PLAYLIST_CHANGED:
            if( fPlaylistShown )
                fPlaylist->Update();
            if( fInfoWindow && fInfoWindow->Lock() )
            {
                if( !fInfoWindow->IsHidden() )
                    fInfoWindow->Update();
                fInfoWindow->Unlock();
            }
            break;

        case MSG_UPDATE:
            _UpdateInterface();
            break;

        default:
            BWindow::MessageReceived( p_message );
            break;
    }

    if( p_input )
        vlc_object_release( p_input );
}

/*****************************************************************************
 * ModernWindow::_UpdateInterface
 *****************************************************************************/
void ModernWindow::_UpdateInterface()
{
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    ControlState s;
    memset( &s, 0, sizeof( s ) );

    PL_LOCK;
    s.can_play = !playlist_IsEmpty( p_playlist );
    playlist_item_t * p_item = playlist_CurrentPlayingItem( p_playlist );
    int i_item = p_item && p_input ? p_item->i_id : -1;
    PL_UNLOCK;

    if( p_input )
    {
        int state = var_GetInteger( p_input, "state" );
        s.has_input = true;
        s.playing = state == PLAYING_S || state == OPENING_S;
        s.can_seek = var_GetBool( p_input, "can-seek" );
        s.position = var_GetFloat( p_input, "position" );
        s.time = var_GetInteger( p_input, "time" );
        s.length = var_GetInteger( p_input, "length" );
    }

    float volume = playlist_VolumeGet( p_playlist );
    s.volume = volume >= 0 ? (int32) roundf( volume * 100 ) : fState.volume;
    if( volume < 0 && fState.volume == 0 && !fState.has_input )
        s.volume = 100;
    s.muted = playlist_MuteGet( p_playlist ) > 0;
    s.fullscreen = fFullscreen;
    s.playlist_shown = fPlaylistShown;
    s.random = var_GetBool( p_playlist, "random" );
    s.loop = var_GetBool( p_playlist, "repeat" ) ? 2
           : var_GetBool( p_playlist, "loop" ) ? 1 : 0;
    s.remaining = fRemaining;

    fControls->Update( s );
    if( fFullscreen && fController && fController->Lock() )
    {
        if( !fController->IsHidden() )
        {
            fController->Bar()->Update( s );
            /* Hidden again when the mouse stops, unless it is over it */
            BPoint where;
            uint32 buttons;
            fControls->GetMouse( &where, &buttons, false );
            fControls->ConvertToScreen( &where );
            if( system_time() - fLastMouseMove > CONTROLLER_TIMEOUT
             && !fController->Frame().Contains( where ) )
                fController->Hide();
        }
        fController->Unlock();
    }
    fState = s;

    /* A new media: the title, and the recent list */
    if( i_item != fCurrentItem )
    {
        fCurrentItem = i_item;
        _UpdateTitle( p_input );
        if( p_input )
        {
            char * psz_uri = input_item_GetURI( input_GetItem( p_input ) );
            if( psz_uri )
                _AddRecent( psz_uri );
            free( psz_uri );
        }
    }
    else if( p_input && ( system_time() / UPDATE_PERIOD ) % 10 == 0 )
        /* Names arrive with the metadata */
        _UpdateTitle( p_input );

    if( p_input )
        vlc_object_release( p_input );
}

void ModernWindow::_UpdateTitle( input_thread_t * p_input )
{
    BString title, artist;
    if( p_input )
    {
        input_item_t * p_item = input_GetItem( p_input );
        char * psz_title = input_item_GetNowPlayingFb( p_item );
        if( !psz_title || !*psz_title )
        {
            free( psz_title );
            psz_title = input_item_GetTitleFbName( p_item );
        }
        char * psz_artist = input_item_GetArtist( p_item );
        title = psz_title ? psz_title : "";
        artist = psz_artist ? psz_artist : "";
        free( psz_title );
        free( psz_artist );
    }

    BString window( title );
    if( window.Length() )
        window << " - ";
    window << APP_TITLE;
    if( window != Title() )
        SetTitle( window.String() );
    fVideoArea->SetInfo( title.String(), artist.String() );
}

void ModernWindow::_AddRecent( const char * psz_uri )
{
    vlc_mutex_lock( &sRecentLock );
    int i;
    for( i = 0; i < sRecentCount; i++ )
        if( sRecent[i] == psz_uri )
            break;
    if( i == sRecentCount && sRecentCount < RECENT_MAX )
        sRecentCount++;
    if( i == RECENT_MAX )
        i = RECENT_MAX - 1;
    for( ; i > 0; i-- )
        sRecent[i] = sRecent[i - 1];
    sRecent[0] = psz_uri;
    vlc_mutex_unlock( &sRecentLock );
}

/*****************************************************************************
 * Opening media
 *****************************************************************************/
void ModernWindow::_OpenURI( const char * psz_url, bool play )
{
    char * psz_uri = strstr( psz_url, "://" ) ? strdup( psz_url )
                   : vlc_path2uri( psz_url, NULL );
    if( psz_uri )
    {
        playlist_Add( p_playlist, psz_uri, play );
        free( psz_uri );
    }
}

void ModernWindow::_OpenRefs( BMessage * message, bool replace )
{
    int32 count;
    type_code dummy;
    if( message->GetInfo( "refs", &dummy, &count ) != B_OK || count < 1 )
        return;

    if( replace )
        playlist_Clear( p_playlist, pl_Unlocked );

    /* Like VLC 3: the first one plays, unless the playlist only takes them */
    bool play = !message->GetBool( "enqueue", false );
    entry_ref ref;
    for( int i = 0; message->FindRef( "refs", i, &ref ) == B_OK; i++ )
    {
        BPath path( &ref );
        if( path.InitCheck() != B_OK )
            continue;
        char * psz_uri = vlc_path2uri( path.Path(), NULL );
        if( psz_uri )
        {
            playlist_Add( p_playlist, psz_uri, play && i == 0 );
            free( psz_uri );
        }
    }
}

void ModernWindow::_ShowFilePanel( uint32 command, const char * title )
{
    if( !fFilePanel )
    {
        fFilePanel = new BFilePanel( B_OPEN_PANEL, NULL, NULL,
                                     B_FILE_NODE | B_DIRECTORY_NODE );
        fFilePanel->SetTarget( this );
    }
    BString full( APP_TITLE );
    full << ": " << title;
    fFilePanel->Window()->SetTitle( full.String() );
    BMessage message( command );
    fFilePanel->SetMessage( &message );
    if( !fFilePanel->IsShowing() )
    {
        fFilePanel->Refresh();
        fFilePanel->Show();
    }
}

void ModernWindow::_ShowFolderPanel()
{
    if( !fFolderPanel )
    {
        BMessage message( FOLDER_RECEIVED );
        fFolderPanel = new BFilePanel( B_OPEN_PANEL, NULL, NULL,
                                       B_DIRECTORY_NODE, false, &message );
        fFolderPanel->SetTarget( this );
        fFolderPanel->SetButtonLabel( B_DEFAULT_BUTTON, _("Open") );
        BString full( APP_TITLE );
        full << ": " << _("Open Folder");
        fFolderPanel->Window()->SetTitle( full.String() );
    }
    if( !fFolderPanel->IsShowing() )
        fFolderPanel->Show();
}

void ModernWindow::_ShowSavePanel()
{
    if( !fSavePanel )
    {
        fSavePanel = new BFilePanel( B_SAVE_PANEL );
        fSavePanel->SetTarget( this );
        BString full( APP_TITLE );
        full << ": " << _("Save Playlist to File");
        fSavePanel->Window()->SetTitle( full.String() );
        fSavePanel->SetSaveText( "playlist.m3u8" );
    }
    if( !fSavePanel->IsShowing() )
        fSavePanel->Show();
}

void ModernWindow::_ShowWindow( BWindow * window )
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

void ModernWindow::_ShowPopupMenu()
{
    BPopUpMenu * menu = new BPopUpMenu( "popup", false, false );
    /* The top level shows the state too */
    StateMenu * state = new StateMenu( "", this, p_intf );
    AddMainMenus( state, true );
    while( BMenuItem * item = state->RemoveItem( (int32) 0 ) )
        menu->AddItem( item );
    delete state;
    menu->SetAsyncAutoDestruct( true );

    /* Play or Pause */
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    if( p_input )
    {
        if( var_GetInteger( p_input, "state" ) == PLAYING_S )
            menu->ItemAt( 0 )->SetLabel( _("Pause") );
        vlc_object_release( p_input );
    }

    BPoint where;
    uint32 buttons;
    fVideoArea->GetMouse( &where, &buttons, false );
    fVideoArea->ConvertToScreen( &where );
    menu->Go( where, true, false, true );
}

/*****************************************************************************
 * VLC variables and rates
 *****************************************************************************/
void ModernWindow::_SetVariable( BMessage * message )
{
    int32 kind;
    const char * var;
    if( message->FindInt32( "object", &kind ) != B_OK
     || message->FindString( "variable", &var ) != B_OK )
        return;

    vlc_object_t * p_obj = HoldObject( p_intf, kind );
    if( !p_obj )
        return;
    int64 i;
    float f;
    const char * psz;
    if( message->FindInt64( "value", &i ) == B_OK )
        var_SetInteger( p_obj, var, i );
    else if( message->FindFloat( "value", &f ) == B_OK )
        var_SetFloat( p_obj, var, f );
    else if( message->FindString( "value", &psz ) == B_OK )
        var_SetString( p_obj, var, psz );
    vlc_object_release( p_obj );
}

void ModernWindow::_ChangeRate( uint32 command )
{
    float rate = var_GetFloat( p_playlist, "rate" );
    switch( command )
    {
        case RATE_FASTER:
            var_TriggerCallback( p_playlist, "rate-faster" );
            return;
        case RATE_SLOWER:
            var_TriggerCallback( p_playlist, "rate-slower" );
            return;
        case RATE_NORMAL:
            rate = 1.f;
            break;
        case RATE_FASTER_FINE:
            rate *= 1.1f;
            break;
        case RATE_SLOWER_FINE:
            rate /= 1.1f;
            break;
    }
    rate = std::min( 32.f, std::max( 1.f / 32, rate ) );
    var_SetFloat( p_playlist, "rate", rate );
}

/*****************************************************************************
 * Window states: fullscreen, minimal, playlist, on top
 *****************************************************************************/
static void SetVisible( BView * view, bool visible )
{
    if( view->IsHidden( view ) == visible )
    {
        if( visible )
            view->Show();
        else
            view->Hide();
    }
}

void ModernWindow::_UpdateChrome()
{
    bool bars = !fFullscreen && !fMinimal;
    bool list = fPlaylistShown && !fFullscreen;
    SetVisible( fMenuBar, bars );
    SetVisible( fControls, bars );
    /* The separator above the controls */
    SetVisible( fControls->PreviousSibling(), bars );
    SetVisible( fPlaylist, list );
    SetVisible( fVideoArea, !list );
}

void ModernWindow::_ToggleFullscreen()
{
    bool b_fs = !fFullscreen;
    vout_thread_t * p_vout = NULL;

    vlc_mutex_lock( &sEmbedLock );
    bool b_video = fVideoWnd != NULL;
    vlc_mutex_unlock( &sEmbedLock );
    input_thread_t * p_input = playlist_CurrentInput( p_playlist );
    if( p_input )
    {
        if( b_video )
            p_vout = input_GetVout( p_input );
        vlc_object_release( p_input );
    }
    if( p_vout )
    {
        /* The video output asks its window, which is this one */
        var_SetBool( p_vout, "fullscreen", b_fs );
        vlc_object_release( p_vout );
    }
    else
        _SetFullscreen( b_fs );
}

void ModernWindow::_SetFullscreen( bool b_fs )
{
    if( b_fs == fFullscreen )
        return;
    fFullscreen = b_fs;

    if( b_fs )
    {
        fNormalFrame = Frame();
        BScreen screen( this );
        BRect frame = screen.Frame();
        SetLook( B_NO_BORDER_WINDOW_LOOK );
        SetFeel( B_FLOATING_ALL_WINDOW_FEEL );
        SetFlags( Flags() | B_NOT_RESIZABLE | B_NOT_MOVABLE );
        _UpdateChrome();
        MoveTo( frame.LeftTop() );
        ResizeTo( frame.Width(), frame.Height() );
        Activate();
        if( fController && fController->Lock() )
        {
            fController->Place( frame );
            fController->Unlock();
        }
        fLastMouseMove = system_time();
    }
    else
    {
        if( fController && fController->Lock() )
        {
            if( !fController->IsHidden() )
                fController->Hide();
            fController->Unlock();
        }
        SetLook( B_TITLED_WINDOW_LOOK );
        SetFeel( fOnTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL );
        SetFlags( Flags() & ~( B_NOT_RESIZABLE | B_NOT_MOVABLE ) );
        _UpdateChrome();
        MoveTo( fNormalFrame.LeftTop() );
        ResizeTo( fNormalFrame.Width(), fNormalFrame.Height() );
    }
    var_SetBool( p_playlist, "fullscreen", b_fs );
    _UpdateInterface();
}

void ModernWindow::_SetMinimal( bool minimal )
{
    fMinimal = minimal;
    _UpdateChrome();
}

void ModernWindow::_SetPlaylistShown( bool shown )
{
    fPlaylistShown = shown;
    if( shown )
        fPlaylist->Update();
    _UpdateChrome();
    _UpdateInterface();
}

void ModernWindow::_SetOnTop( bool onTop )
{
    fOnTop = onTop;
    var_SetBool( p_playlist, "video-on-top", onTop );
    if( !fFullscreen )
        SetFeel( onTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL );
}

/*****************************************************************************
 * The video
 *****************************************************************************/
void ModernWindow::_VideoAttached()
{
    fHasVideo = true;
    if( !fFullscreen )
        fIdleFrame = Frame();
    if( fPlaylistShown )
    {
        fPlaylistShown = false;
        _UpdateChrome();
    }
    fVideoArea->Invalidate();
}

void ModernWindow::_VideoDetached()
{
    fHasVideo = false;
    if( fFullscreen )
        _SetFullscreen( false );
    if( fIdleFrame.IsValid() && !fRetiring )
        ResizeTo( fIdleFrame.Width(), fIdleFrame.Height() );
    fIdleFrame = BRect();
    fVideoArea->Invalidate();
    if( fRetiring )
        PostMessage( B_QUIT_REQUESTED );
}

void ModernWindow::_VideoSize( int32 width, int32 height )
{
    if( fFullscreen || IsMinimized() || fRetiring || width <= 0 || height <= 0 )
    {
        _ReportVideoSize();
        return;
    }

    /* Up to date sizes of the window and the video area */
    Layout( true );
    BRect area = fVideoArea->Bounds();
    float chromeW = Bounds().Width() - area.Width();
    float chromeH = Bounds().Height() - area.Height();

    BScreen screen( this );
    BRect s = screen.Frame();
    float maxW = ( s.Width() + 1 ) * .9f - chromeW;
    float maxH = ( s.Height() + 1 ) * .85f - chromeH;
    float w = width, h = height;
    if( w > maxW )
    {
        h = h * maxW / w;
        w = maxW;
    }
    if( h > maxH )
    {
        w = w * maxH / h;
        h = maxH;
    }

    float newW = roundf( w + chromeW - 1 ), newH = roundf( h + chromeH - 1 );
    if( newW == Bounds().Width() && newH == Bounds().Height() )
    {
        _ReportVideoSize();
        return;
    }
    ResizeTo( newW, newH );

    /* Still on screen */
    BRect frame = Frame();
    float dx = 0, dy = 0;
    if( frame.right > s.right )
        dx = s.right - frame.right;
    if( frame.bottom > s.bottom )
        dy = s.bottom - frame.bottom;
    if( frame.left + dx < s.left )
        dx = s.left - frame.left;
    if( frame.top + dy < s.top + 20 )
        dy = s.top + 20 - frame.top;
    if( dx || dy )
        MoveBy( dx, dy );
    /* The video area reports its new size when it gets it */
}

void ModernWindow::_ReportVideoSize()
{
    fVideoArea->FitChildren();
    vlc_mutex_lock( &sEmbedLock );
    if( fVideoWnd && !fVideoArea->IsHidden( fVideoArea ) )
    {
        BRect b = fVideoArea->Bounds();
        vout_window_ReportSize( fVideoWnd, b.IntegerWidth() + 1,
                                b.IntegerHeight() + 1 );
    }
    vlc_mutex_unlock( &sEmbedLock );
}

/*****************************************************************************
 * Settings
 *****************************************************************************/
void ModernWindow::_RestoreSettings()
{
    if( load_settings( fSettings, "interface_settings", SETTINGS_FOLDER ) != B_OK )
        return;

    BRect frame;
    if( fSettings->FindRect( "modern frame", &frame ) == B_OK )
        set_window_pos( this, frame );
    if( fSettings->FindRect( "messages frame", &frame ) == B_OK )
        set_window_pos( fMessagesWindow, frame );
    fPlaylistShown = fSettings->GetBool( "modern playlist", false );
    fRemaining = fSettings->GetBool( "modern remaining", false );

    vlc_mutex_lock( &sRecentLock );
    const char * psz;
    for( sRecentCount = 0; sRecentCount < RECENT_MAX
         && fSettings->FindString( "recent", sRecentCount, &psz ) == B_OK; sRecentCount++ )
        sRecent[sRecentCount] = psz;
    vlc_mutex_unlock( &sRecentLock );
}

void ModernWindow::_StoreSettings()
{
    BRect frame = fFullscreen ? fNormalFrame : Frame();
    fSettings->RemoveName( "modern frame" );
    fSettings->AddRect( "modern frame", frame );
    fSettings->RemoveName( "modern playlist" );
    fSettings->AddBool( "modern playlist", fPlaylistShown );
    fSettings->RemoveName( "modern remaining" );
    fSettings->AddBool( "modern remaining", fRemaining );
    if( fMessagesWindow->Lock() )
    {
        fSettings->RemoveName( "messages frame" );
        fSettings->AddRect( "messages frame", fMessagesWindow->Frame() );
        fMessagesWindow->Unlock();
    }
    if( fPreferencesWindow && fPreferencesWindow->Lock() )
    {
        fSettings->RemoveName( "settings frame" );
        fSettings->AddRect( "settings frame", fPreferencesWindow->Frame() );
        fPreferencesWindow->Unlock();
    }

    fSettings->RemoveName( "recent" );
    vlc_mutex_lock( &sRecentLock );
    for( int i = 0; i < sRecentCount; i++ )
        fSettings->AddString( "recent", sRecent[i] );
    vlc_mutex_unlock( &sRecentLock );

    save_settings( fSettings, "interface_settings", SETTINGS_FOLDER );
}

/*****************************************************************************
 * The "vout window" provider: the video area of the modern window
 *****************************************************************************/
static int VideoWindowControl( vout_window_t * p_wnd, int query, va_list args )
{
    ModernWindow * window = (ModernWindow *) p_wnd->sys;
    BMessenger messenger( window );

    switch( query )
    {
        case VOUT_WINDOW_SET_STATE:
        {
            unsigned state = va_arg( args, unsigned );
            BMessage msg( VIDEO_STATE );
            msg.AddBool( "on-top", ( state & VOUT_WINDOW_STATE_ABOVE ) != 0 );
            messenger.SendMessage( &msg );
            return VLC_SUCCESS;
        }
        case VOUT_WINDOW_SET_SIZE:
        {
            unsigned width = va_arg( args, unsigned );
            unsigned height = va_arg( args, unsigned );
            BMessage msg( VIDEO_SIZE );
            msg.AddInt32( "width", width );
            msg.AddInt32( "height", height );
            messenger.SendMessage( &msg );
            return VLC_SUCCESS;
        }
        case VOUT_WINDOW_SET_FULLSCREEN:
        {
            BMessage msg( VIDEO_FULLSCREEN );
            msg.AddBool( "fullscreen", va_arg( args, int ) != 0 );
            messenger.SendMessage( &msg );
            return VLC_SUCCESS;
        }
        case VOUT_WINDOW_HIDE_MOUSE:
            if( va_arg( args, int ) )
                messenger.SendMessage( VIDEO_HIDE_CURSOR );
            return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

int ModernVideoWindowOpen( vout_window_t * p_wnd, const vout_window_cfg_t * cfg )
{
    if( cfg->type != VOUT_WINDOW_TYPE_INVALID && cfg->type != VOUT_WINDOW_TYPE_HAIKU )
        return VLC_EGENERIC;
    if( cfg->is_standalone )
        return VLC_EGENERIC;

    vlc_mutex_lock( &sEmbedLock );
    ModernWindow * window = sEmbedder;
    if( !window || window->fVideoWnd )
    {
        /* No modern window, or it already has a video */
        vlc_mutex_unlock( &sEmbedLock );
        return VLC_EGENERIC;
    }
    window->fVideoWnd = p_wnd;
    vlc_mutex_unlock( &sEmbedLock );

    p_wnd->type = VOUT_WINDOW_TYPE_HAIKU;
    p_wnd->handle.bview = window->fVideoArea;
    p_wnd->control = VideoWindowControl;
    p_wnd->info.has_double_click = false;
    p_wnd->sys = (vout_window_sys_t *) window;

    /* Show the video area before the video output draws in it. The window
     * stays until this one is closed (see Retire() and Close()). */
    if( window->LockWithTimeout( 3000000 ) == B_OK )
    {
        window->_VideoAttached();
        window->Unlock();
    }
    msg_Dbg( p_wnd, "video in the interface window" );
    return VLC_SUCCESS;
}

void ModernVideoWindowClose( vout_window_t * p_wnd )
{
    ModernWindow * window = (ModernWindow *) p_wnd->sys;

    vlc_mutex_lock( &sEmbedLock );
    window->fVideoWnd = NULL;
    BMessenger messenger( window );
    vlc_mutex_unlock( &sEmbedLock );
    messenger.SendMessage( VIDEO_DETACHED );
}
