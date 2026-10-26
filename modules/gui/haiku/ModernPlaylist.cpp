/*****************************************************************************
 * ModernPlaylist.cpp: the playlist of the modern Haiku interface
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

#include <Application.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <algorithm>
#include <string.h>
#include <strings.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input_item.h>

#include "MsgVals.h"
#include "ModernControls.h"
#include "ModernPlaylist.h"

/* Where the album and duration columns start, for a list this wide */
static void ColumnPositions( float width, float * album, float * duration )
{
    float d = ceilf( be_plain_font->StringWidth( "00:00:00" ) ) + 16;
    *duration = width - d;
    *album = width > 420 ? floorf( width * .58f ) : *duration;
}

/*****************************************************************************
 * PlaylistRow
 *****************************************************************************/
class PlaylistRow : public BListItem
{
 public:
    PlaylistRow( int id, const char * title, const char * album,
                 const char * duration, bool current )
        : fId( id ), fTitle( title ), fAlbum( album ), fDuration( duration ),
          fCurrent( current )
    {
    }

    int Id() const { return fId; }

    virtual void Update( BView * owner, const BFont * font )
    {
        BListItem::Update( owner, font );
        font_height fh;
        font->GetHeight( &fh );
        SetHeight( ceilf( ( fh.ascent + fh.descent + fh.leading ) * 1.45f ) );
    }

    virtual void DrawItem( BView * owner, BRect frame, bool complete )
    {
        rgb_color bg = ui_color( IsSelected() ? B_LIST_SELECTED_BACKGROUND_COLOR
                                              : B_LIST_BACKGROUND_COLOR );
        rgb_color fg = ui_color( IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR
                                              : B_LIST_ITEM_TEXT_COLOR );
        owner->SetLowColor( bg );
        owner->FillRect( frame, B_SOLID_LOW );
        (void) complete;

        BFont font( fCurrent ? be_bold_font : be_plain_font );
        owner->SetFont( &font );
        font_height fh;
        font.GetHeight( &fh );
        float base = frame.top + floorf( ( frame.Height() + fh.ascent - fh.descent ) / 2 );
        float pad = be_control_look->DefaultLabelSpacing();

        float album, duration;
        ColumnPositions( frame.Width(), &album, &duration );
        float iconSize = ceilf( fh.ascent * .8f );
        float left = frame.left + pad;

        if( fCurrent )
        {
            BRect icon( 0, 0, iconSize, iconSize );
            icon.OffsetTo( left, base - fh.ascent + ( fh.ascent - iconSize ) / 2 + 1 );
            DrawGlyph( owner, icon, GLYPH_PLAY, fg );
        }
        left += iconSize + pad;

        owner->SetHighColor( fg );
        BString text( fTitle );
        font.TruncateString( &text, B_TRUNCATE_END, album - left - pad );
        owner->DrawString( text.String(), BPoint( left, base ) );

        if( album < duration )
        {
            text = fAlbum;
            font.TruncateString( &text, B_TRUNCATE_END, duration - album - pad );
            owner->DrawString( text.String(), BPoint( frame.left + album, base ) );
        }

        float w = font.StringWidth( fDuration.String() );
        owner->DrawString( fDuration.String(),
                           BPoint( frame.right - pad - w, base ) );
        owner->SetFont( be_plain_font );
    }

 private:
    int         fId;
    BString     fTitle;
    BString     fAlbum;
    BString     fDuration;
    bool        fCurrent;
};

/*****************************************************************************
 * PlaylistHeader: the column titles
 *****************************************************************************/
class PlaylistHeader : public BView
{
 public:
    PlaylistHeader()
        : BView( "header", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE )
    {
        SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    }

    virtual void Draw( BRect update )
    {
        BRect b = Bounds();
        rgb_color base = ui_color( B_PANEL_BACKGROUND_COLOR );
        be_control_look->DrawButtonBackground( this, b, update, base, 0,
                                               BControlLook::B_ALL_BORDERS );
        SetHighColor( tint_color( base, B_DARKEN_2_TINT ) );
        StrokeLine( Bounds().LeftBottom(), Bounds().RightBottom() );

        float album, duration;
        /* The list below is narrower by its scroll bar */
        ColumnPositions( Bounds().Width() - B_V_SCROLL_BAR_WIDTH, &album, &duration );
        font_height fh;
        GetFontHeight( &fh );
        float y = floorf( ( b.Height() + fh.ascent - fh.descent ) / 2 );
        float pad = be_control_look->DefaultLabelSpacing();
        SetHighColor( ui_color( B_PANEL_TEXT_COLOR ) );
        SetDrawingMode( B_OP_OVER );
        DrawString( _("Title"), BPoint( pad + ceilf( fh.ascent * .8f ) + pad, y ) );
        if( album < duration )
        {
            DrawString( _("Album"), BPoint( album, y ) );
            StrokeLine( BPoint( album - pad, 2 ), BPoint( album - pad, b.bottom - 2 ) );
        }
        DrawString( _("Duration"), BPoint( duration, y ) );
        StrokeLine( BPoint( duration - pad, 2 ), BPoint( duration - pad, b.bottom - 2 ) );
        SetDrawingMode( B_OP_COPY );
    }

    virtual BSize MinSize()
    {
        font_height fh;
        be_plain_font->GetHeight( &fh );
        return BSize( 50, ceilf( ( fh.ascent + fh.descent ) * 1.6f ) );
    }

    virtual BSize MaxSize()
    {
        return BSize( B_SIZE_UNLIMITED, MinSize().height );
    }
};

/*****************************************************************************
 * PlaylistList
 *****************************************************************************/
class PlaylistList : public BListView
{
 public:
    PlaylistList( ModernPlaylist * owner )
        : BListView( "playlist", B_MULTIPLE_SELECTION_LIST,
                     B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE
                     | B_FULL_UPDATE_ON_RESIZE ),
          fOwner( owner )
    {
    }

    virtual void KeyDown( const char * bytes, int32 numBytes )
    {
        if( numBytes == 1 && ( bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE ) )
            fOwner->RemoveSelected();
        else
            BListView::KeyDown( bytes, numBytes );
    }

    virtual void MouseDown( BPoint where )
    {
        uint32 buttons = 0;
        if( Window()->CurrentMessage() )
            Window()->CurrentMessage()->FindInt32( "buttons", (int32 *) &buttons );
        if( buttons & B_SECONDARY_MOUSE_BUTTON )
        {
            int32 index = IndexOf( where );
            if( index >= 0 && !ItemAt( index )->IsSelected() )
                Select( index );
            fOwner->_ShowContextMenu( ConvertToScreen( where ) );
            return;
        }
        BListView::MouseDown( where );
    }

    virtual void FrameResized( float width, float height )
    {
        BListView::FrameResized( width, height );
        Invalidate();
    }

    virtual void MessageReceived( BMessage * message )
    {
        /* Dropped files are added to the playlist */
        if( message->what == B_SIMPLE_DATA )
        {
            BMessage copy( *message );
            copy.what = B_REFS_RECEIVED;
            copy.AddBool( "enqueue", true );
            Window()->PostMessage( &copy );
            return;
        }
        BListView::MessageReceived( message );
    }

 private:
    ModernPlaylist    * fOwner;
};

/*****************************************************************************
 * ModernPlaylist
 *****************************************************************************/
ModernPlaylist::ModernPlaylist( intf_thread_t * _p_intf )
    : BView( "playlist panel", 0 ),
      p_intf( _p_intf )
{
    SetViewUIColor( B_PANEL_BACKGROUND_COLOR );

    fFilter = new BTextControl( "filter", NULL, "", new BMessage( PLAYLIST_FILTER ) );
    fFilter->SetModificationMessage( new BMessage( PLAYLIST_FILTER ) );
    fFilter->SetToolTip( _("Search the playlist") );

    BStringView * label = new BStringView( "label", _("Playlist") );
    label->SetFont( be_bold_font );

    fHeader = new PlaylistHeader();
    fList = new PlaylistList( this );
    fList->SetInvocationMessage( new BMessage( PLAY_ITEM ) );
    BScrollView * scroll = new BScrollView( "scroll", fList, 0, false, true,
                                            B_NO_BORDER );

    float spacing = be_control_look->DefaultItemSpacing();
    BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
        .AddGroup( B_HORIZONTAL, spacing )
            .SetInsets( spacing, spacing / 2, spacing, spacing / 2 )
            .Add( label )
            .AddGlue()
            .Add( new BStringView( "search", _("Search:") ) )
            .Add( fFilter, 2.0f )
        .End()
        .Add( fHeader )
        .Add( scroll );
}

void ModernPlaylist::AttachedToWindow()
{
    BView::AttachedToWindow();
    fFilter->SetTarget( this );
    fList->SetTarget( this );
    Update();
}

void ModernPlaylist::MessageReceived( BMessage * message )
{
    switch( message->what )
    {
        case PLAY_ITEM:
        {
            int32 index;
            if( message->FindInt32( "index", &index ) == B_OK )
                _PlayItem( index );
            else
                PlaySelected();
            break;
        }
        case REMOVE_ITEMS:
            RemoveSelected();
            break;
        case CLEAR_PLAYLIST:
            playlist_Clear( pl_Get( p_intf ), pl_Unlocked );
            break;
        case B_SELECT_ALL:
            fList->Select( 0, fList->CountItems() - 1 );
            break;
        case PLAYLIST_FILTER:
            Update();
            break;
        default:
            BView::MessageReceived( message );
            break;
    }
}

static bool Matches( const char * filter, const char * a, const char * b,
                     const char * c )
{
    if( !*filter )
        return true;
    return ( a && strcasestr( a, filter ) ) || ( b && strcasestr( b, filter ) )
        || ( c && strcasestr( c, filter ) );
}

static void AddItems( BListView * list, playlist_item_t * p_node,
                      playlist_item_t * p_current, const char * filter )
{
    for( int i = 0; i < p_node->i_children; i++ )
    {
        playlist_item_t * p_item = p_node->pp_children[i];
        if( p_item->i_children >= 0 )
        {
            AddItems( list, p_item, p_current, filter );
            continue;
        }

        input_item_t * p_input = p_item->p_input;
        char * psz_title = input_item_GetTitleFbName( p_input );
        char * psz_artist = input_item_GetArtist( p_input );
        char * psz_album = input_item_GetAlbum( p_input );

        if( Matches( filter, psz_title, psz_artist, psz_album ) )
        {
            BString title;
            if( psz_artist && *psz_artist )
                title << psz_artist << " - ";
            title << ( psz_title ? psz_title : "" );

            char psz_time[32] = "";
            mtime_t i_duration = input_item_GetDuration( p_input );
            if( i_duration > 0 )
                format_time( psz_time, sizeof( psz_time ), i_duration );

            list->AddItem( new PlaylistRow( p_item->i_id, title.String(),
                                            psz_album ? psz_album : "",
                                            psz_time, p_item == p_current ) );
        }
        free( psz_title );
        free( psz_artist );
        free( psz_album );
    }
}

void ModernPlaylist::Update()
{
    playlist_t * p_playlist = pl_Get( p_intf );

    /* Keep the selection and the scroll position */
    float scroll = fList->Bounds().top;
    int32 count = 0;
    int selected[64];
    for( int32 i = 0, index; count < 64
                             && ( index = fList->CurrentSelection( i ) ) >= 0; i++ )
        selected[count++] = ( (PlaylistRow *) fList->ItemAt( index ) )->Id();

    while( fList->CountItems() > 0 )
        delete fList->RemoveItem( fList->CountItems() - 1 );

    PL_LOCK;
    AddItems( fList, p_playlist->p_playing,
              playlist_CurrentPlayingItem( p_playlist ), fFilter->Text() );
    PL_UNLOCK;

    for( int32 i = 0; i < fList->CountItems(); i++ )
    {
        int id = ( (PlaylistRow *) fList->ItemAt( i ) )->Id();
        for( int32 j = 0; j < count; j++ )
            if( selected[j] == id )
                fList->Select( i, true );
    }
    fList->ScrollTo( 0, scroll );
}

void ModernPlaylist::_PlayItem( int32 index )
{
    PlaylistRow * row = (PlaylistRow *) fList->ItemAt( index );
    if( !row )
        return;

    playlist_t * p_playlist = pl_Get( p_intf );
    PL_LOCK;
    playlist_item_t * p_item = playlist_ItemGetById( p_playlist, row->Id() );
    if( p_item )
        playlist_ViewPlay( p_playlist, NULL, p_item );
    PL_UNLOCK;
}

void ModernPlaylist::PlaySelected()
{
    int32 index = fList->CurrentSelection();
    if( index >= 0 )
        _PlayItem( index );
}

void ModernPlaylist::RemoveSelected()
{
    playlist_t * p_playlist = pl_Get( p_intf );
    int32 index;

    PL_LOCK;
    for( int32 i = 0; ( index = fList->CurrentSelection( i ) ) >= 0; i++ )
    {
        PlaylistRow * row = (PlaylistRow *) fList->ItemAt( index );
        playlist_item_t * p_item = playlist_ItemGetById( p_playlist, row->Id() );
        if( p_item )
            playlist_NodeDelete( p_playlist, p_item );
    }
    PL_UNLOCK;
}

void ModernPlaylist::_ShowContextMenu( BPoint where )
{
    char text[256];
    BPopUpMenu * menu = new BPopUpMenu( "context", false, false );
    bool selection = fList->CurrentSelection() >= 0;

    BMenuItem * item;
    menu->AddItem( item = new BMenuItem( _("Play"), new BMessage( PLAY_ITEM ) ) );
    item->SetEnabled( selection );
    item->SetTarget( this );
    menu->AddItem( item = new BMenuItem( _("Remove Selected"),
                                         new BMessage( REMOVE_ITEMS ) ) );
    item->SetEnabled( selection );
    item->SetTarget( this );
    menu->AddItem( item = new BMenuItem( _("Clear the playlist"),
                                         new BMessage( CLEAR_PLAYLIST ) ) );
    item->SetEnabled( fList->CountItems() > 0 );
    item->SetTarget( this );
    menu->AddSeparatorItem();
    snprintf( text, sizeof( text ), "%s" B_UTF8_ELLIPSIS, _("Add File") );
    menu->AddItem( item = new BMenuItem( text, new BMessage( OPEN_FILE ) ) );
    item->SetTarget( Window() );
    snprintf( text, sizeof( text ), "%s" B_UTF8_ELLIPSIS, _("Add Folder") );
    menu->AddItem( item = new BMenuItem( text, new BMessage( OPEN_FOLDER ) ) );
    item->SetTarget( Window() );
    snprintf( text, sizeof( text ), "%s" B_UTF8_ELLIPSIS, _("Save Playlist to File") );
    menu->AddItem( item = new BMenuItem( text, new BMessage( SAVE_PLAYLIST ) ) );
    item->SetTarget( Window() );

    menu->SetAsyncAutoDestruct( true );
    menu->Go( where, true, false, true );
}
