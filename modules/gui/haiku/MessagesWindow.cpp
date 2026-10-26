/*****************************************************************************
 * MessagesWindow.cpp: Haiku interface messages window
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

/* System headers */
#include <InterfaceKit.h>
#include <SupportKit.h>

/* VLC headers */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>

/* Haiku module headers */
#include "MsgVals.h"
#include "MessagesWindow.h"

/* Keep the log from growing without bounds */
#define MAX_TEXT_LENGTH   (256 * 1024)

/*****************************************************************************
 * MessagesWindow::MessagesWindow
 *****************************************************************************/
MessagesWindow::MessagesWindow( intf_thread_t * _p_intf,
                                BRect frame, const char * name )
    : BWindow( frame, name, B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
               B_NOT_ZOOMABLE ),
    p_intf(_p_intf),
    fSubscribed( false ),
    fVerbosity( var_InheritInteger( _p_intf, "verbose" ) )
{
    /* Like the BeOS window, show everything down to debug messages
     * unless the user asked for silence (-q) */
    if( fVerbosity >= 0 && fVerbosity < 2 )
        fVerbosity = 2;

    SetSizeLimits( 400, 2000, 200, 2000 );

    BRect rect, textRect;

    rect = Bounds();
    rect.right -= B_V_SCROLL_BAR_WIDTH;
    textRect = rect;
    textRect.OffsetTo( B_ORIGIN );
    textRect.InsetBy( 5, 5 );
    fMessagesView = new BTextView( rect, "messages", textRect,
                                   B_FOLLOW_ALL, B_WILL_DRAW );
    fMessagesView->MakeEditable( false );
    fMessagesView->SetStylable( true );
    fScrollView = new BScrollView( "scrollview", fMessagesView, B_FOLLOW_ALL,
                                   B_WILL_DRAW, false, true );
    AddChild( fScrollView );

    /* start window thread in hidden state */
    Hide();
    Show();
}

/*****************************************************************************
 * MessagesWindow::~MessagesWindow
 *****************************************************************************/
MessagesWindow::~MessagesWindow()
{
}

/*****************************************************************************
 * MessagesWindow::Subscribe
 *****************************************************************************/
void MessagesWindow::Subscribe()
{
    if( fSubscribed )
        return;
    fSubscribed = true;
    vlc_LogSet( p_intf->obj.libvlc, LogCallback, this );
}

/*****************************************************************************
 * MessagesWindow::LogCallback: runs on any thread
 *****************************************************************************/
void MessagesWindow::LogCallback( void * data, int type, const vlc_log_t * item,
                                  const char * format, va_list ap )
{
    MessagesWindow * window = (MessagesWindow *) data;
    char * psz_text;

    if( window->fVerbosity < 0 || window->fVerbosity < ( type - VLC_MSG_ERR )
     || vasprintf( &psz_text, format, ap ) == -1 )
        return;

    int canc = vlc_savecancel();
    BMessage message( LOG_MESSAGE );
    message.AddInt32( "type", type );
    message.AddString( "module", item->psz_module ? item->psz_module : "" );
    message.AddString( "object", item->psz_object_type ? item->psz_object_type : "" );
    message.AddString( "text", psz_text );
    window->PostMessage( &message );
    vlc_restorecancel( canc );
    free( psz_text );
}

/*****************************************************************************
 * MessagesWindow::MessageReceived
 *****************************************************************************/
void MessagesWindow::MessageReceived( BMessage * message )
{
    if( message->what == LOG_MESSAGE )
    {
        _AddMessage( message->GetInt32( "type", VLC_MSG_INFO ),
                     message->GetString( "module", "" ),
                     message->GetString( "object", "" ),
                     message->GetString( "text", "" ) );
        return;
    }
    BWindow::MessageReceived( message );
}

/*****************************************************************************
 * MessagesWindow::_AddMessage
 *****************************************************************************/
void MessagesWindow::_AddMessage( int type, const char * module,
                                  const char * object_type, const char * text )
{
    static const rgb_color red = { 200, 0, 0, 255 };
    static const rgb_color gray = { 150, 150, 150, 255 };
    static const rgb_color green = { 0, 150, 0, 255 };
    static const rgb_color orange = { 230, 180, 0, 255 };
    rgb_color color;

    switch( type )
    {
        case VLC_MSG_INFO: color = green; break;
        case VLC_MSG_WARN: color = orange; break;
        case VLC_MSG_ERR: color = red; break;
        default: color = gray; break;
    }

    /* Scroll at the end unless the user is scrolling or selecting something */
    BScrollBar * scrollBar = fScrollView->ScrollBar( B_VERTICAL );
    float min, max;
    scrollBar->GetRange( &min, &max );
    bool isScrolling = scrollBar->Value() != max;

    if( fMessagesView->TextLength() > MAX_TEXT_LENGTH )
        fMessagesView->Delete( 0, MAX_TEXT_LENGTH / 4 );

    BString string;
    string << module << " " << object_type << ": " << text << "\n";
    int32 oldLength = fMessagesView->TextLength();
    fMessagesView->Insert( oldLength, string.String(), string.Length() );
    fMessagesView->SetFontAndColor( oldLength, fMessagesView->TextLength(),
                                    NULL, 0, &color );

    int32 start, end;
    fMessagesView->GetSelection( &start, &end );
    if( !isScrolling && start == end )
    {
        scrollBar->GetRange( &min, &max );
        scrollBar->SetValue( max );
    }
}

/*****************************************************************************
 * MessagesWindow::FrameResized
 *****************************************************************************/
void MessagesWindow::FrameResized( float width, float height )
{
    BWindow::FrameResized( width, height );
    BRect rect = fMessagesView->Bounds();
    rect.InsetBy( 5, 5 );
    fMessagesView->SetTextRect( rect );
}

/*****************************************************************************
 * MessagesWindow::QuitRequested
 *****************************************************************************/
bool MessagesWindow::QuitRequested()
{
    Hide();
    return false;
}

/*****************************************************************************
 * MessagesWindow::ReallyQuit
 *****************************************************************************/
void MessagesWindow::ReallyQuit()
{
    /* No more log messages for this window; vlc_LogSet() waits for the
     * callbacks in progress */
    if( fSubscribed )
        vlc_LogSet( p_intf->obj.libvlc, NULL, NULL );

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
