/*****************************************************************************
 * ModernControls.cpp: controls of the modern (VLC 3) Haiku interface
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

#include <ControlLook.h>
#include <GradientLinear.h>
#include <LayoutBuilder.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

#include "MsgVals.h"
#include "ModernControls.h"

/* Sizes follow the font size, so that the controls scale with it */
static float Unit()
{
    return be_plain_font->Size();
}

static rgb_color Mix( rgb_color a, rgb_color b, float amount )
{
    rgb_color c;
    c.red   = a.red   + ( b.red   - a.red   ) * amount;
    c.green = a.green + ( b.green - a.green ) * amount;
    c.blue  = a.blue  + ( b.blue  - a.blue  ) * amount;
    c.alpha = 255;
    return c;
}

void format_time( char * buffer, size_t size, int64_t time )
{
    int64_t secs = time / 1000000;
    if( secs < 0 )
        secs = 0;
    if( secs >= 3600 )
        snprintf( buffer, size, "%d:%02d:%02d", (int)( secs / 3600 ),
                  (int)( secs / 60 % 60 ), (int)( secs % 60 ) );
    else
        snprintf( buffer, size, "%02d:%02d", (int)( secs / 60 ),
                  (int)( secs % 60 ) );
}

/*****************************************************************************
 * Glyphs
 *****************************************************************************/
static void Triangle( BView * v, BPoint a, BPoint b, BPoint c )
{
    BPoint p[3] = { a, b, c };
    v->FillPolygon( p, 3 );
}

static void Arrow( BView * v, BPoint tip, BPoint from, float size )
{
    /* Head of an arrow pointing from "from" to "tip" */
    float dx = tip.x - from.x, dy = tip.y - from.y;
    float len = sqrtf( dx * dx + dy * dy );
    if( len <= 0 )
        return;
    dx /= len; dy /= len;
    BPoint back( tip.x - dx * size, tip.y - dy * size );
    Triangle( v, tip, BPoint( back.x - dy * size * .6f, back.y + dx * size * .6f ),
                      BPoint( back.x + dy * size * .6f, back.y - dx * size * .6f ) );
}

void DrawGlyph( BView * v, BRect f, int glyph, rgb_color color )
{
    /* A square in the middle of the frame */
    float s = floorf( std::min( f.Width(), f.Height() ) );
    BRect r( 0, 0, s, s );
    r.OffsetTo( floorf( f.left + ( f.Width() - s ) / 2 ),
                floorf( f.top + ( f.Height() - s ) / 2 ) );
    float l = r.left, t = r.top, w = r.Width(), h = r.Height();
    float pen = std::max( 1.5f, roundf( w * .11f ) );

    v->PushState();
    v->SetHighColor( color );
    v->SetDrawingMode( B_OP_ALPHA );
    v->SetBlendingMode( B_PIXEL_ALPHA, B_ALPHA_OVERLAY );
    v->SetLineMode( B_ROUND_CAP, B_ROUND_JOIN );
    v->SetPenSize( pen );

    switch( glyph )
    {
        case GLYPH_PLAY:
            Triangle( v, BPoint( l + w * .18f, t ), BPoint( l + w * .98f, t + h / 2 ),
                      BPoint( l + w * .18f, t + h ) );
            break;

        case GLYPH_PAUSE:
            v->FillRect( BRect( l + w * .12f, t, l + w * .42f, t + h ) );
            v->FillRect( BRect( l + w * .58f, t, l + w * .88f, t + h ) );
            break;

        case GLYPH_STOP:
            v->FillRect( BRect( l + w * .08f, t + h * .08f,
                                l + w * .92f, t + h * .92f ) );
            break;

        case GLYPH_PREV:
            v->FillRect( BRect( l, t + h * .1f, l + w * .14f, t + h * .9f ) );
            Triangle( v, BPoint( l + w * .14f, t + h / 2 ),
                      BPoint( l + w * .57f, t + h * .1f ),
                      BPoint( l + w * .57f, t + h * .9f ) );
            Triangle( v, BPoint( l + w * .57f, t + h / 2 ),
                      BPoint( l + w, t + h * .1f ), BPoint( l + w, t + h * .9f ) );
            break;

        case GLYPH_NEXT:
            v->FillRect( BRect( l + w * .86f, t + h * .1f, l + w, t + h * .9f ) );
            Triangle( v, BPoint( l + w * .86f, t + h / 2 ),
                      BPoint( l + w * .43f, t + h * .1f ),
                      BPoint( l + w * .43f, t + h * .9f ) );
            Triangle( v, BPoint( l + w * .43f, t + h / 2 ),
                      BPoint( l, t + h * .1f ), BPoint( l, t + h * .9f ) );
            break;

        case GLYPH_FULLSCREEN:
        case GLYPH_LEAVE_FULLSCREEN:
        {
            float c = w * .32f, i = pen / 2;
            BRect b = r.InsetByCopy( i, i );
            if( glyph == GLYPH_FULLSCREEN )
            {
                v->StrokeLine( b.LeftTop(), BPoint( b.left + c, b.top ) );
                v->StrokeLine( b.LeftTop(), BPoint( b.left, b.top + c ) );
                v->StrokeLine( b.RightTop(), BPoint( b.right - c, b.top ) );
                v->StrokeLine( b.RightTop(), BPoint( b.right, b.top + c ) );
                v->StrokeLine( b.LeftBottom(), BPoint( b.left + c, b.bottom ) );
                v->StrokeLine( b.LeftBottom(), BPoint( b.left, b.bottom - c ) );
                v->StrokeLine( b.RightBottom(), BPoint( b.right - c, b.bottom ) );
                v->StrokeLine( b.RightBottom(), BPoint( b.right, b.bottom - c ) );
            }
            else
            {
                BPoint p;
                p = BPoint( b.left + c, b.top + c );
                v->StrokeLine( p, BPoint( b.left, p.y ) );
                v->StrokeLine( p, BPoint( p.x, b.top ) );
                p = BPoint( b.right - c, b.top + c );
                v->StrokeLine( p, BPoint( b.right, p.y ) );
                v->StrokeLine( p, BPoint( p.x, b.top ) );
                p = BPoint( b.left + c, b.bottom - c );
                v->StrokeLine( p, BPoint( b.left, p.y ) );
                v->StrokeLine( p, BPoint( p.x, b.bottom ) );
                p = BPoint( b.right - c, b.bottom - c );
                v->StrokeLine( p, BPoint( b.right, p.y ) );
                v->StrokeLine( p, BPoint( p.x, b.bottom ) );
            }
            break;
        }

        case GLYPH_PLAYLIST:
        {
            float d = pen;
            for( int i = 0; i < 3; i++ )
            {
                float y = t + h * ( .18f + .32f * i );
                v->FillRect( BRect( l, y - d / 2, l + d, y + d / 2 ) );
                v->StrokeLine( BPoint( l + d * 2.2f, y ), BPoint( l + w, y ) );
            }
            break;
        }

        case GLYPH_LOOP:
        case GLYPH_LOOP_ONE:
        {
            BRect b( l + pen / 2, t + h * .22f, l + w - pen / 2, t + h * .78f );
            float rad = b.Height() / 2;
            /* Top run to the right, bottom run to the left */
            v->StrokeLine( BPoint( b.left + rad, b.top ),
                           BPoint( b.right - rad - pen, b.top ) );
            v->StrokeArc( BRect( b.right - 2 * rad, b.top, b.right, b.bottom ), 270, 180 );
            v->StrokeLine( BPoint( b.right - rad, b.bottom ),
                           BPoint( b.left + rad + pen, b.bottom ) );
            v->StrokeArc( BRect( b.left, b.top, b.left + 2 * rad, b.bottom ), 90, 180 );
            Arrow( v, BPoint( b.right - rad + pen * .6f, b.top ),
                   BPoint( b.left, b.top ), pen * 2.2f );
            Arrow( v, BPoint( b.left + rad - pen * .6f, b.bottom ),
                   BPoint( b.right, b.bottom ), pen * 2.2f );
            if( glyph == GLYPH_LOOP_ONE )
            {
                BFont font( be_bold_font );
                font.SetSize( h * .5f );
                v->SetFont( &font );
                font_height fh;
                font.GetHeight( &fh );
                float sw = font.StringWidth( "1" );
                v->DrawString( "1", BPoint( l + ( w - sw ) / 2,
                               t + h / 2 + ( fh.ascent - fh.descent ) / 2 ) );
            }
            break;
        }

        case GLYPH_RANDOM:
        {
            float y1 = t + h * .22f, y2 = t + h * .78f, a = pen * 2.2f;
            float x0 = l + pen / 2, x1 = l + w * .3f, x2 = l + w * .62f,
                  x3 = l + w - pen / 2;
            /* Two crossing paths with an arrow head each */
            v->StrokeLine( BPoint( x0, y1 ), BPoint( x1, y1 ) );
            v->StrokeLine( BPoint( x1, y1 ), BPoint( x2, y2 ) );
            v->StrokeLine( BPoint( x2, y2 ), BPoint( x3 - a * .8f, y2 ) );
            Arrow( v, BPoint( x3, y2 ), BPoint( x2, y2 ), a );
            v->StrokeLine( BPoint( x0, y2 ), BPoint( x1, y2 ) );
            v->StrokeLine( BPoint( x1, y2 ), BPoint( x2, y1 ) );
            v->StrokeLine( BPoint( x2, y1 ), BPoint( x3 - a * .8f, y1 ) );
            Arrow( v, BPoint( x3, y1 ), BPoint( x2, y1 ), a );
            break;
        }

        case GLYPH_VOLUME:
        case GLYPH_MUTED:
        {
            BPoint p[4] = {
                BPoint( l + w * .26f, t + h * .34f ),
                BPoint( l + w * .52f, t + h * .1f ),
                BPoint( l + w * .52f, t + h * .9f ),
                BPoint( l + w * .26f, t + h * .66f ) };
            v->FillRect( BRect( l, t + h * .34f, l + w * .27f, t + h * .66f ) );
            v->FillPolygon( p, 4 );
            v->SetPenSize( std::max( 1.f, pen * .8f ) );
            if( glyph == GLYPH_VOLUME )
            {
                BPoint c( l + w * .5f, t + h / 2 );
                v->StrokeArc( c, w * .2f, h * .2f, -45, 90 );
                v->StrokeArc( c, w * .38f, h * .38f, -50, 100 );
            }
            else
            {
                v->StrokeLine( BPoint( l + w * .66f, t + h * .34f ),
                               BPoint( l + w * .98f, t + h * .66f ) );
                v->StrokeLine( BPoint( l + w * .66f, t + h * .66f ),
                               BPoint( l + w * .98f, t + h * .34f ) );
            }
            break;
        }
    }
    v->PopState();
}

/*****************************************************************************
 * DrawCone: the VLC traffic cone
 *****************************************************************************/
void DrawCone( BView * v, BRect f )
{
    float s = std::min( f.Width(), f.Height() );
    if( s < 8 )
        return;
    float x0 = f.left + ( f.Width() - s ) / 2, y0 = f.top + ( f.Height() - s ) / 2;
#define P( x, y ) BPoint( x0 + (x) * s, y0 + (y) * s )
    /* Cone edges: from the tip (.5, .04) to (.17, .84) and (.83, .84) */
    const float tip = .04f, foot = .84f;
#define LEFT( y )  ( .5f - ( (y) - tip ) / ( foot - tip ) * .33f )
#define RIGHT( y ) ( .5f + ( (y) - tip ) / ( foot - tip ) * .33f )

    v->PushState();
    v->SetDrawingMode( B_OP_ALPHA );
    v->SetBlendingMode( B_PIXEL_ALPHA, B_ALPHA_OVERLAY );

    /* Base plate */
    BGradientLinear base( P( .06f, 0 ), P( .94f, 0 ) );
    base.AddColor( make_color( 0xd9, 0x5a, 0x00 ), 0 );
    base.AddColor( make_color( 0xff, 0x8c, 0x00 ), 110 );
    base.AddColor( make_color( 0xc4, 0x4c, 0x00 ), 255 );
    BPoint plate[4] = { P( .1f, .8f ), P( .9f, .8f ), P( .97f, .95f ), P( .03f, .95f ) };
    v->FillPolygon( plate, 4, base );

    /* Orange body */
    BGradientLinear body( P( LEFT( foot ), 0 ), P( RIGHT( foot ), 0 ) );
    body.AddColor( make_color( 0xff, 0x9a, 0x14 ), 0 );
    body.AddColor( make_color( 0xff, 0x80, 0x00 ), 120 );
    body.AddColor( make_color( 0xe0, 0x5c, 0x00 ), 255 );
    BPoint cone[4] = { P( .47f, tip ), P( .53f, tip ), P( RIGHT( foot ), foot ),
                       P( LEFT( foot ), foot ) };
    v->FillPolygon( cone, 4, body );

    /* White stripes */
    BGradientLinear white( P( LEFT( .6f ), 0 ), P( RIGHT( .6f ), 0 ) );
    white.AddColor( make_color( 0xff, 0xff, 0xff ), 0 );
    white.AddColor( make_color( 0xf2, 0xf2, 0xf2 ), 140 );
    white.AddColor( make_color( 0xc8, 0xc8, 0xc8 ), 255 );
    const float stripes[2][2] = { { .27f, .39f }, { .54f, .68f } };
    for( int i = 0; i < 2; i++ )
    {
        float a = stripes[i][0], b = stripes[i][1];
        BPoint band[4] = { P( LEFT( a ), a ), P( RIGHT( a ), a ),
                           P( RIGHT( b ), b ), P( LEFT( b ), b ) };
        v->FillPolygon( band, 4, white );
    }
    v->PopState();
#undef P
#undef LEFT
#undef RIGHT
}

/*****************************************************************************
 * GlyphButton
 *****************************************************************************/
GlyphButton::GlyphButton( const char * name, int glyph, BMessage * message,
                          float scale )
    : BControl( name, NULL, message, B_WILL_DRAW | B_NAVIGABLE ),
      fGlyph( glyph ), fScale( scale ), fPressed( false ), fInside( false )
{
    SetFlags( Flags() & ~B_NAVIGABLE );
}

void GlyphButton::SetGlyph( int glyph )
{
    if( glyph != fGlyph )
    {
        fGlyph = glyph;
        Invalidate();
    }
}

void GlyphButton::AttachedToWindow()
{
    BControl::AttachedToWindow();
    AdoptParentColors();
    SetLowColor( ViewColor() );
}

void GlyphButton::Draw( BRect updateRect )
{
    BRect r = Bounds();
    rgb_color base = LowColor();
    uint32 flags = 0;
    if( !IsEnabled() )
        flags |= BControlLook::B_DISABLED;
    if( ( fPressed && fInside ) || Value() == B_CONTROL_ON )
        flags |= BControlLook::B_ACTIVATED;
    if( fInside )
        flags |= BControlLook::B_HOVER;

    if( IsEnabled() && ( fInside || fPressed || Value() == B_CONTROL_ON ) )
    {
        be_control_look->DrawButtonFrame( this, r, updateRect, base, base, flags );
        be_control_look->DrawButtonBackground( this, r, updateRect, base, flags );
    }
    else
        FillRect( r, B_SOLID_LOW );

    rgb_color text = ui_color( B_PANEL_TEXT_COLOR );
    if( !IsEnabled() )
        text = Mix( text, base, .6f );
    float inset = floorf( Bounds().Height() * ( fScale > 1.1f ? .28f : .27f ) );
    DrawGlyph( this, Bounds().InsetByCopy( inset, inset ), fGlyph, text );
}

void GlyphButton::MouseDown( BPoint where )
{
    if( !IsEnabled() )
        return;
    fPressed = true;
    fInside = Bounds().Contains( where );
    SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
    Invalidate();
}

void GlyphButton::MouseUp( BPoint where )
{
    if( !fPressed )
        return;
    fPressed = false;
    fInside = Bounds().Contains( where );
    Invalidate();
    if( fInside && IsEnabled() )
        Invoke();
}

void GlyphButton::MouseMoved( BPoint where, uint32 transit, const BMessage * )
{
    bool inside = ( transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW )
                  && Bounds().Contains( where );
    if( inside != fInside )
    {
        fInside = inside;
        Invalidate();
    }
}

BSize GlyphButton::PreferredSize()
{
    float s = roundf( Unit() * 2.2f * fScale );
    return BSize( s, s );
}

BSize GlyphButton::MinSize()
{
    return PreferredSize();
}

BSize GlyphButton::MaxSize()
{
    return PreferredSize();
}

void GlyphButton::GetPreferredSize( float * width, float * height )
{
    BSize s = PreferredSize();
    *width = s.width;
    *height = s.height;
}

/*****************************************************************************
 * ModernSeekSlider
 *****************************************************************************/
ModernSeekSlider::ModernSeekSlider( const char * name, BMessage * message )
    : BControl( name, NULL, message, B_WILL_DRAW | B_FRAME_EVENTS ),
      fTracking( false ), fLastSent( 0 )
{
    SetValue( 0 );
}

void ModernSeekSlider::AttachedToWindow()
{
    BControl::AttachedToWindow();
    AdoptParentColors();
    SetLowColor( ViewColor() );
}

void ModernSeekSlider::SetPosition( float position )
{
    if( fTracking )
        return;
    int32 value = (int32)( position * 10000 );
    if( value < 0 )
        value = 0;
    if( value > 10000 )
        value = 10000;
    if( value != Value() )
        SetValue( value );
}

BRect ModernSeekSlider::_BarFrame() const
{
    BRect b = Bounds();
    float knob = floorf( b.Height() * .7f );
    float bar = std::max( 5.f, floorf( Unit() * .45f ) );
    BRect r( b.left + knob / 2, 0, b.right - knob / 2, bar - 1 );
    r.OffsetTo( r.left, floorf( ( b.Height() - bar ) / 2 ) );
    return r;
}

void ModernSeekSlider::Draw( BRect updateRect )
{
    FillRect( updateRect, B_SOLID_LOW );

    BRect bar = _BarFrame();
    rgb_color base = LowColor();
    uint32 flags = IsEnabled() ? 0 : BControlLook::B_DISABLED;
    float pos = Value() / 10000.f;
    rgb_color fill = ui_color( B_CONTROL_HIGHLIGHT_COLOR );
    rgb_color empty = tint_color( base, B_DARKEN_1_TINT );
    if( !IsEnabled() )
        fill = empty;

    BRect drawBar = bar;
    be_control_look->DrawSliderBar( this, drawBar, updateRect, base, fill, empty,
                                    pos, flags, B_HORIZONTAL );

    if( IsEnabled() )
    {
        float knob = floorf( Bounds().Height() * .7f );
        float x = bar.left + pos * bar.Width();
        BRect thumb( x - knob / 2, 0, x + knob / 2, knob );
        thumb.OffsetTo( thumb.left, floorf( ( Bounds().Height() - knob ) / 2 ) );
        be_control_look->DrawSliderThumb( this, thumb, updateRect, base,
                                          fTracking ? BControlLook::B_ACTIVATED : 0,
                                          B_HORIZONTAL );
    }
}

float ModernSeekSlider::_PositionFor( BPoint where ) const
{
    BRect bar = _BarFrame();
    float pos = ( where.x - bar.left ) / std::max( 1.f, bar.Width() );
    return std::min( 1.f, std::max( 0.f, pos ) );
}

void ModernSeekSlider::_Send( float position )
{
    BMessage message( *Message() );
    message.AddFloat( "position", position );
    Invoke( &message );
    fLastSent = system_time();
}

void ModernSeekSlider::MouseDown( BPoint where )
{
    if( !IsEnabled() )
        return;
    fTracking = true;
    SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
    float pos = _PositionFor( where );
    SetValue( (int32)( pos * 10000 ) );
    _Send( pos );
}

void ModernSeekSlider::MouseMoved( BPoint where, uint32, const BMessage * )
{
    if( !fTracking )
        return;
    float pos = _PositionFor( where );
    SetValue( (int32)( pos * 10000 ) );
    /* Not more than ten seeks a second */
    if( system_time() - fLastSent > 100000 )
        _Send( pos );
}

void ModernSeekSlider::MouseUp( BPoint where )
{
    if( !fTracking )
        return;
    float pos = _PositionFor( where );
    fTracking = false;
    SetValue( (int32)( pos * 10000 ) );
    _Send( pos );
    Invalidate();
}

BSize ModernSeekSlider::MinSize()
{
    return BSize( Unit() * 6, roundf( Unit() * 1.4f ) );
}

BSize ModernSeekSlider::MaxSize()
{
    return BSize( B_SIZE_UNLIMITED, roundf( Unit() * 1.4f ) );
}

BSize ModernSeekSlider::PreferredSize()
{
    return BSize( Unit() * 20, roundf( Unit() * 1.4f ) );
}

/*****************************************************************************
 * ModernVolumeSlider
 *****************************************************************************/
ModernVolumeSlider::ModernVolumeSlider( const char * name, BMessage * message )
    : BControl( name, NULL, message, B_WILL_DRAW ),
      fTracking( false )
{
    SetValue( 100 );
}

void ModernVolumeSlider::AttachedToWindow()
{
    BControl::AttachedToWindow();
    AdoptParentColors();
    SetLowColor( ViewColor() );
}

void ModernVolumeSlider::SetVolume( int32 percent )
{
    if( !fTracking && percent != Value() )
        SetValue( std::min( (int32) 125, std::max( (int32) 0, percent ) ) );
}

void ModernVolumeSlider::Draw( BRect updateRect )
{
    FillRect( updateRect, B_SOLID_LOW );

    BRect b = Bounds().InsetByCopy( 1, floorf( Bounds().Height() * .18f ) );
    rgb_color base = LowColor();
    rgb_color empty = tint_color( base, B_DARKEN_2_TINT );
    rgb_color edge = tint_color( base, B_DARKEN_3_TINT );
    rgb_color fill = ui_color( B_CONTROL_HIGHLIGHT_COLOR );
    rgb_color loud = make_color( 0xe8, 0x50, 0x20 );
    if( !IsEnabled() )
    {
        empty = tint_color( base, B_DARKEN_1_TINT );
        fill = empty;
        loud = empty;
        edge = empty;
    }

    BPoint wedge[3] = { b.LeftBottom(), b.RightBottom(), b.RightTop() };
    SetHighColor( empty );
    FillPolygon( wedge, 3 );

    /* The filled part, clipped to the wedge; the part above 100 % in a
     * warning colour, as in VLC 3 */
    float v = Value() / 125.f;
    float x = b.left + v * b.Width();
    float x100 = b.left + 100 / 125.f * b.Width();
#define WEDGE_Y( x ) ( b.bottom - ( (x) - b.left ) / b.Width() * b.Height() )
    if( v > 0 )
    {
        float xa = std::min( x, x100 );
        BPoint part[3] = { b.LeftBottom(), BPoint( xa, b.bottom ),
                           BPoint( xa, WEDGE_Y( xa ) ) };
        SetHighColor( fill );
        FillPolygon( part, 3 );
        if( x > x100 )
        {
            BPoint over[4] = { BPoint( x100, b.bottom ), BPoint( x, b.bottom ),
                               BPoint( x, WEDGE_Y( x ) ),
                               BPoint( x100, WEDGE_Y( x100 ) ) };
            SetHighColor( loud );
            FillPolygon( over, 4 );
        }
    }
    SetHighColor( edge );
    SetPenSize( 1 );
    StrokePolygon( wedge, 3 );
#undef WEDGE_Y

    /* The percentage, in the empty top left corner */
    char text[8];
    snprintf( text, sizeof( text ), "%d%%", (int) Value() );
    BFont font;
    GetFont( &font );
    font.SetSize( std::max( 8.f, Unit() * .8f ) );
    SetFont( &font );
    font_height fh;
    font.GetHeight( &fh );
    rgb_color textColor = ui_color( B_PANEL_TEXT_COLOR );
    if( !IsEnabled() )
        textColor = Mix( textColor, base, .6f );
    SetHighColor( textColor );
    DrawString( text, BPoint( Bounds().left + 1, Bounds().top + ceilf( fh.ascent ) ) );
}

void ModernVolumeSlider::_SetFromPoint( BPoint where )
{
    BRect b = Bounds().InsetByCopy( 1, 0 );
    float v = ( where.x - b.left ) / std::max( 1.f, b.Width() );
    int32 value = (int32) roundf( std::min( 1.f, std::max( 0.f, v ) ) * 125 );
    if( value != Value() )
    {
        SetValue( value );
        _Send();
    }
}

void ModernVolumeSlider::_Send()
{
    BMessage message( *Message() );
    message.AddInt32( "volume", Value() );
    Invoke( &message );
}

void ModernVolumeSlider::MouseDown( BPoint where )
{
    if( !IsEnabled() )
        return;
    fTracking = true;
    SetMouseEventMask( B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS );
    _SetFromPoint( where );
}

void ModernVolumeSlider::MouseMoved( BPoint where, uint32, const BMessage * )
{
    if( fTracking )
        _SetFromPoint( where );
}

void ModernVolumeSlider::MouseUp( BPoint where )
{
    if( fTracking )
        _SetFromPoint( where );
    fTracking = false;
}

void ModernVolumeSlider::MessageReceived( BMessage * message )
{
    if( message->what == B_MOUSE_WHEEL_CHANGED && IsEnabled() )
    {
        float dy = 0;
        message->FindFloat( "be:wheel_delta_y", &dy );
        int32 value = Value() - (int32)( dy * 5 );
        value = std::min( (int32) 125, std::max( (int32) 0, value ) );
        if( value != Value() )
        {
            SetValue( value );
            _Send();
        }
        return;
    }
    BControl::MessageReceived( message );
}

BSize ModernVolumeSlider::MinSize()
{
    return BSize( roundf( Unit() * 6.5f ), roundf( Unit() * 2.2f ) );
}

BSize ModernVolumeSlider::MaxSize()
{
    return MinSize();
}

BSize ModernVolumeSlider::PreferredSize()
{
    return MinSize();
}

/*****************************************************************************
 * TimeLabel
 *****************************************************************************/
TimeLabel::TimeLabel( const char * name )
    : BStringView( name, "--:--" )
{
    SetAlignment( B_ALIGN_CENTER );
}

void TimeLabel::MouseDown( BPoint )
{
    fTarget.SendMessage( TOGGLE_REMAINING );
}

BSize TimeLabel::MinSize()
{
    return BSize( ceilf( StringWidth( "-0:00:00" ) ) + 4,
                  BStringView::MinSize().height );
}

/*****************************************************************************
 * ControlBar
 *****************************************************************************/
ControlBar::ControlBar( const char * name, bool fullscreen )
    : BView( name, B_WILL_DRAW ),
      fFirst( true )
{
    SetViewUIColor( B_PANEL_BACKGROUND_COLOR );
    memset( &fState, 0, sizeof( fState ) );

    fElapsed = new TimeLabel( "elapsed" );
    fSeek = new ModernSeekSlider( "seek", new BMessage( SEEK_PLAYBACK ) );
    fTotal = new TimeLabel( "total" );

    fPlay = new GlyphButton( "play", GLYPH_PLAY, new BMessage( START_PLAYBACK ), 1.3f );
    fPrev = new GlyphButton( "previous", GLYPH_PREV, new BMessage( PREV_FILE ) );
    fStop = new GlyphButton( "stop", GLYPH_STOP, new BMessage( STOP_PLAYBACK ) );
    fNext = new GlyphButton( "next", GLYPH_NEXT, new BMessage( NEXT_FILE ) );
    fFullscreen = new GlyphButton( "fullscreen",
                                   fullscreen ? GLYPH_LEAVE_FULLSCREEN : GLYPH_FULLSCREEN,
                                   new BMessage( TOGGLE_FULL_SCREEN ) );
    fPlaylist = new GlyphButton( "playlist", GLYPH_PLAYLIST,
                                 new BMessage( TOGGLE_PLAYLIST ) );
    fLoop = new GlyphButton( "loop", GLYPH_LOOP, new BMessage( TOGGLE_LOOP ) );
    fRandom = new GlyphButton( "random", GLYPH_RANDOM, new BMessage( TOGGLE_RANDOM ) );
    fMute = new GlyphButton( "mute", GLYPH_VOLUME, new BMessage( VOLUME_MUTE ) );
    fVolume = new ModernVolumeSlider( "volume", new BMessage( VOLUME_CHG ) );

    fPrev->SetToolTip( _("Previous media in the playlist") );
    fStop->SetToolTip( _("Stop playback") );
    fNext->SetToolTip( _("Next media in the playlist") );
    fFullscreen->SetToolTip( _("Toggle the video in fullscreen") );
    fPlaylist->SetToolTip( _("Toggle playlist") );
    fLoop->SetToolTip( _("Click to toggle between loop all, loop one and no loop") );
    fRandom->SetToolTip( _("Random") );
    fMute->SetToolTip( _("Mute") );
    fTotal->SetToolTip( _("Click to toggle between total and remaining time") );

    float spacing = be_control_look->DefaultItemSpacing();
    float gap = roundf( spacing * 1.5f );
    BLayoutBuilder::Group<>( this, B_VERTICAL, 0 )
        .SetInsets( spacing, spacing / 2, spacing, spacing / 2 )
        .AddGroup( B_HORIZONTAL, spacing / 2 )
            .Add( fElapsed )
            .Add( fSeek )
            .Add( fTotal )
        .End()
        .AddGroup( B_HORIZONTAL, 0 )
            .Add( fPlay )
            .AddStrut( gap )
            .Add( fPrev )
            .Add( fStop )
            .Add( fNext )
            .AddStrut( gap )
            .Add( fFullscreen )
            .Add( fPlaylist )
            .AddStrut( gap )
            .Add( fLoop )
            .Add( fRandom )
            .AddGlue()
            .Add( fMute )
            .AddStrut( spacing / 2 )
            .Add( fVolume )
        .End();

    if( fullscreen )
        fPlaylist->Hide();
}

void ControlBar::SetTarget( BMessenger target )
{
    fElapsed->SetTarget( target );
    fTotal->SetTarget( target );
    BControl * controls[] = { fSeek, fPlay, fPrev, fStop, fNext, fFullscreen,
                              fPlaylist, fLoop, fRandom, fMute, fVolume };
    for( size_t i = 0; i < sizeof( controls ) / sizeof( controls[0] ); i++ )
        controls[i]->SetTarget( target );
}

void ControlBar::Update( const ControlState & s )
{
    bool all = fFirst;
    fFirst = false;
#define CHANGED( field ) ( all || s.field != fState.field )

    if( CHANGED( has_input ) || CHANGED( can_seek ) )
        fSeek->SetEnabled( s.has_input && s.can_seek );
    if( CHANGED( position ) || CHANGED( has_input ) )
        fSeek->SetPosition( s.has_input ? s.position : 0 );

    if( CHANGED( time ) || CHANGED( length ) || CHANGED( remaining )
     || CHANGED( has_input ) )
    {
        char text[32];
        if( s.has_input )
            format_time( text, sizeof( text ), s.time );
        else
            strcpy( text, "--:--" );
        fElapsed->SetText( text );
        if( !s.has_input || s.length <= 0 )
            strcpy( text, "--:--" );
        else if( s.remaining )
        {
            text[0] = '-';
            format_time( text + 1, sizeof( text ) - 1, s.length - s.time );
        }
        else
            format_time( text, sizeof( text ), s.length );
        fTotal->SetText( text );
    }

    if( CHANGED( playing ) )
    {
        fPlay->SetGlyph( s.playing ? GLYPH_PAUSE : GLYPH_PLAY );
        fPlay->SetToolTip( s.playing ? _("Pause the playback")
                                     : _("Play") );
    }
    if( CHANGED( can_play ) || CHANGED( has_input ) )
    {
        fPlay->SetEnabled( s.can_play || s.has_input );
        fPrev->SetEnabled( s.can_play );
        fNext->SetEnabled( s.can_play );
        fStop->SetEnabled( s.has_input );
    }
    if( CHANGED( fullscreen ) )
        fFullscreen->SetValue( s.fullscreen ? B_CONTROL_ON : B_CONTROL_OFF );
    if( CHANGED( playlist_shown ) )
        fPlaylist->SetValue( s.playlist_shown ? B_CONTROL_ON : B_CONTROL_OFF );
    if( CHANGED( loop ) )
    {
        fLoop->SetGlyph( s.loop == 2 ? GLYPH_LOOP_ONE : GLYPH_LOOP );
        fLoop->SetValue( s.loop ? B_CONTROL_ON : B_CONTROL_OFF );
    }
    if( CHANGED( random ) )
        fRandom->SetValue( s.random ? B_CONTROL_ON : B_CONTROL_OFF );
    if( CHANGED( muted ) )
        fMute->SetGlyph( s.muted ? GLYPH_MUTED : GLYPH_VOLUME );
    if( CHANGED( volume ) )
        fVolume->SetVolume( s.volume );
#undef CHANGED

    fState = s;
}
