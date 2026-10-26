/*****************************************************************************
 * PlayListWindow.cpp: Haiku playlist window
 *****************************************************************************
 * Copyright (C) 1999, 2000, 2001 the VideoLAN team
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Jean-Marc Dressler <polux@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
 *          Tony Castley <tony@castley.net>
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

/* System headers */
#include <InterfaceKit.h>
#include <StorageKit.h>

/* VLC headers */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input_item.h>

/* Haiku interface headers */
#include "MsgVals.h"
#include "PlayListWindow.h"

/* One playlist entry; the current one is drawn in bold */
class PlaylistItem : public BStringItem
{
 public:
    PlaylistItem( const char * text, int id, bool current )
        : BStringItem( text ), fId( id ), fCurrent( current )
    {
    }

    virtual void DrawItem( BView * owner, BRect frame, bool complete )
    {
        if( fCurrent )
            owner->SetFont( be_bold_font );
        BStringItem::DrawItem( owner, frame, complete );
        if( fCurrent )
            owner->SetFont( be_plain_font );
    }

    int Id() const { return fId; }

 private:
    int     fId;
    bool    fCurrent;
};

class PlaylistView : public BListView
{
 public:
    PlaylistView( BRect frame, BWindow * owner )
        : BListView( frame, "playlist", B_MULTIPLE_SELECTION_LIST,
                     B_FOLLOW_ALL ),
          fOwner( owner )
    {
    }

    virtual void KeyDown( const char * bytes, int32 numBytes )
    {
        if( numBytes == 1 && ( bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE ) )
            fOwner->PostMessage( REMOVE_ITEMS );
        else
            BListView::KeyDown( bytes, numBytes );
    }

    virtual void MessageReceived( BMessage * message )
    {
        /* Dropped files go to the interface, which adds them */
        if( message->what == B_SIMPLE_DATA && be_app != NULL )
        {
            BMessage copy( *message );
            copy.what = B_REFS_RECEIVED;
            be_app->PostMessage( &copy );
            return;
        }
        BListView::MessageReceived( message );
    }

 private:
    BWindow *   fOwner;
};

/*****************************************************************************
 * PlayListWindow::PlayListWindow
 *****************************************************************************/
PlayListWindow::PlayListWindow( BRect frame, const char* name,
                                intf_thread_t *p_interface )
    :   BWindow( frame, name, B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
                 B_WILL_ACCEPT_FIRST_CLICK | B_ASYNCHRONOUS_CONTROLS ),
        p_intf( p_interface )
{
    SetName( _("playlist") );

    // set up the main menu bar
    BMenuBar * menuBar = new BMenuBar( BRect(0.0, 0.0, frame.Width(), 15.0),
                                       "main menu", B_FOLLOW_NONE, B_ITEMS_IN_ROW,
                                       false );
    AddChild( menuBar );

    // Add the edit menu
    BMenu * editMenu = new BMenu( _("Edit") );
    editMenu->AddItem( new BMenuItem( _("Select All"),
                                      new BMessage( B_SELECT_ALL ), 'A' ) );
    editMenu->AddSeparatorItem();
    editMenu->AddItem( fRemoveMI = new BMenuItem( _("Remove"),
                                      new BMessage( REMOVE_ITEMS ) ) );
    editMenu->AddItem( new BMenuItem( _("Remove All"),
                                      new BMessage( CLEAR_PLAYLIST ) ) );
    menuBar->AddItem( editMenu );

    // make menu bar resize to correct height
    float menuWidth, menuHeight;
    menuBar->GetPreferredSize( &menuWidth, &menuHeight );
    menuBar->ResizeTo( frame.Width(), menuHeight );    // don't change! it's a workarround!
    menuBar->SetResizingMode( B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP );

    frame = Bounds();
    frame.top += menuBar->Bounds().IntegerHeight() + 1;
    frame.right -= B_V_SCROLL_BAR_WIDTH;

    fListView = new PlaylistView( frame, this );
    fListView->SetInvocationMessage( new BMessage( PLAY_ITEM ) );
    fListView->SetTarget( this );
    BScrollView * scrollView = new BScrollView( "scroll view", fListView,
                                                B_FOLLOW_ALL, 0, false, true,
                                                B_NO_BORDER );
    AddChild( scrollView );

    // make first window resize smaller than menubar width
    SetSizeLimits( menuWidth + 50.0, 2000.0, 100.0, 2000.0 );

    UpdatePlaylist();

    // start window thread in hidden state
    Hide();
    Show();
}

/*****************************************************************************
 * PlayListWindow::~PlayListWindow
 *****************************************************************************/
PlayListWindow::~PlayListWindow()
{
}

/*****************************************************************************
 * PlayListWindow::QuitRequested
 *****************************************************************************/
bool
PlayListWindow::QuitRequested()
{
    Hide();
    return false;
}

/*****************************************************************************
 * PlayListWindow::MessageReceived
 *****************************************************************************/
void
PlayListWindow::MessageReceived( BMessage * p_message )
{
    switch ( p_message->what )
    {
        case PLAY_ITEM:
        {
            int32 index;
            if( p_message->FindInt32( "index", &index ) == B_OK )
                _PlayItem( index );
            break;
        }
        case B_SELECT_ALL:
            fListView->Select( 0, fListView->CountItems() - 1 );
            break;
        case REMOVE_ITEMS:
            _RemoveSelected();
            break;
        case CLEAR_PLAYLIST:
            playlist_Clear( pl_Get( p_intf ), pl_Unlocked );
            break;
        default:
            BWindow::MessageReceived( p_message );
            break;
    }
}

/*****************************************************************************
 * PlayListWindow::ReallyQuit
 *****************************************************************************/
void
PlayListWindow::ReallyQuit()
{
    /* Wait until the window is gone: with the Haiku API on Linux, Quit()
     * from another thread (and wait_for_thread()) can return before it is */
    BMessenger messenger( this );
    Lock();
    Hide();
    Quit();
    /* The looper's port goes away at the end of its destructor */
    for( int i = 0; i < 200 && messenger.IsValid(); i++ )
        snooze( 10000 );
}

/*****************************************************************************
 * PlayListWindow::UpdatePlaylist: rebuilds the list from the playing node
 *****************************************************************************/
static void AddItems( BListView * list, playlist_item_t * p_node,
                      playlist_item_t * p_current )
{
    for( int i = 0; i < p_node->i_children; i++ )
    {
        playlist_item_t * p_item = p_node->pp_children[i];
        if( p_item->i_children >= 0 )
        {
            AddItems( list, p_item, p_current );
            continue;
        }

        char * psz_name = input_item_GetTitleFbName( p_item->p_input );
        BString text;
        text << ( list->CountItems() + 1 ) << ". "
             << ( psz_name ? psz_name : "" );
        mtime_t i_duration = input_item_GetDuration( p_item->p_input );
        if( i_duration > 0 )
        {
            char psz_time[MSTRTIME_MAX_SIZE];
            secstotimestr( psz_time, i_duration / CLOCK_FREQ );
            text << "  (" << psz_time << ")";
        }
        free( psz_name );
        list->AddItem( new PlaylistItem( text.String(), p_item->i_id,
                                         p_item == p_current ) );
    }
}

void
PlayListWindow::UpdatePlaylist()
{
    playlist_t * p_playlist = pl_Get( p_intf );

    /* Keep the selection and scroll position */
    float scroll = fListView->Bounds().top;
    int32 selected = fListView->CurrentSelection();

    while( fListView->CountItems() > 0 )
        delete fListView->RemoveItem( fListView->CountItems() - 1 );

    PL_LOCK;
    AddItems( fListView, p_playlist->p_playing,
              playlist_CurrentPlayingItem( p_playlist ) );
    PL_UNLOCK;

    if( selected >= 0 && selected < fListView->CountItems() )
        fListView->Select( selected );
    fListView->ScrollTo( 0, scroll );
}

/*****************************************************************************
 * PlayListWindow::_PlayItem
 *****************************************************************************/
void
PlayListWindow::_PlayItem( int32 index )
{
    PlaylistItem * item = dynamic_cast<PlaylistItem *>( fListView->ItemAt( index ) );
    if( !item )
        return;

    playlist_t * p_playlist = pl_Get( p_intf );
    PL_LOCK;
    playlist_item_t * p_item = playlist_ItemGetById( p_playlist, item->Id() );
    if( p_item )
        playlist_ViewPlay( p_playlist, NULL, p_item );
    PL_UNLOCK;
}

/*****************************************************************************
 * PlayListWindow::_RemoveSelected
 *****************************************************************************/
void
PlayListWindow::_RemoveSelected()
{
    playlist_t * p_playlist = pl_Get( p_intf );
    int32 index;

    PL_LOCK;
    for( int32 i = 0; ( index = fListView->CurrentSelection( i ) ) >= 0; i++ )
    {
        PlaylistItem * item = dynamic_cast<PlaylistItem *>( fListView->ItemAt( index ) );
        if( !item )
            continue;
        playlist_item_t * p_item = playlist_ItemGetById( p_playlist, item->Id() );
        if( p_item )
            playlist_NodeDelete( p_playlist, p_item );
    }
    PL_UNLOCK;
}
