/*****************************************************************************
 * haiku.cpp: Haiku video output display module
 *****************************************************************************
 * Copyright (C) 2020-2024 Gerasim Troeglazov (3dEyes**)

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>

#include <QWidget>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QPaintEvent>
#include <QGuiApplication>

#include <SupportDefs.h>
#include <MessageQueue.h>
#include <Window.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Rect.h>
#include <Cursor.h>

extern "C" {
	static int				Open( vlc_object_t * );
	static void				Close( vlc_object_t * );
	static picture_pool_t	*Pool(vout_display_t *, unsigned count);
	static void				Display(vout_display_t *, picture_t *, subpicture_t *);
	static int				Control(vout_display_t *, int, va_list);
	static void				Manage (vout_display_t *);
}

vlc_module_begin ()
    set_shortname("Haiku")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_description("Haiku video output")
    set_capability("vout display", 100 )
    add_shortcut("haiku")
    add_obsolete_string("haiku-video-driver")
    set_callbacks(Open, Close)
vlc_module_end ()

/*****************************************************************************
 * Native classes
 *****************************************************************************/
static uint32 platformHaikuScanCodes[] = {
		KEY_ESC,		0x01,
		KEY_F1,			0x02,
		KEY_F2,			0x03,
		KEY_F3,			0x04,
		KEY_F4,			0x05,
		KEY_F5,			0x06,
		KEY_F6,			0x07,
		KEY_F7,			0x08,
		KEY_F8,			0x09,
		KEY_F9,			0x0A,
		KEY_F10,		0x0B,
		KEY_F11,		0x0C,
		KEY_F12,		0x0D,
		KEY_PAUSE,		0x22,

		'`', 			0x11,
		'1',			0x12,
		'2',			0x13,
		'3',			0x14,
		'4',			0x15,
		'5',			0x16,
		'6',			0x17,
		'7',			0x18,
		'8',			0x19,
		'9',			0x1A,
		'0',			0x1B,
		'-',			0x1C,
		'+',			0x1D,
		KEY_BACKSPACE,	0x1E,
		KEY_INSERT,		0x1F,
		KEY_HOME,		0x20,
		KEY_PAGEUP,		0x21,
		'/',			0x23,
		'*',			0x24,
		'-',			0x25,		
		                
		KEY_TAB,		0x26,        
		'q',			0x27,
		'w',			0x28,
		'e',			0x29,
		'r',			0x2A,
		't',			0x2B,
		'y',			0x2C,
		'u',			0x2D,
		'i',			0x2E,
		'o',			0x2F,
		'p',			0x30,      
		'[',			0x31,
		']',			0x32,
		'\\',			0x33,
		KEY_DELETE,		0x34,
		KEY_END,		0x35,
		KEY_PAGEDOWN,	0x36, 
		'7',			0x37,
		'8',			0x38, //numpad
		'9',			0x39, //numpad
		'+',			0x3A, //numpad

		'a',			0x3C,
		's',			0x3D,
		'd',			0x3E,
		'f',			0x3F,
		'g',			0x40,
		'h',			0x41,
		'j',			0x42,
		'k',			0x43,
		'l',			0x44,
		';',			0x45,
		'"',			0x46,
		KEY_ENTER,		0x47,              
		'4',			0x48, //numpad
		'5',			0x49, //numpad
		'6',			0x4A, //numpad

		'z',			0x4C,
		'x',			0x4D,
		'c',			0x4E,
		'v',			0x4F,
		'b',			0x50,
		'n',			0x51,
		'm',			0x52,
		',',			0x53,
		'.',			0x54,
		'/',			0x55,
		KEY_UP,			0x57,	//cursor
		'1',			0x58,	//numpad
		'2',			0x59,   //numpad
		'3',			0x5A,   //numpad
		KEY_ENTER,		0x5B,   //numpad

		' ',			0x5E,
		KEY_LEFT,		0x61,   //cursor
		KEY_DOWN,		0x62,   //cursor
		KEY_RIGHT,		0x63,   //cursor
		'0',			0x64,   //cursor
		'.',			0x65,   //numpad
		0,				0x00
	};
	
static const struct {
    int haiku;
    int vlc;
} platformHaikuMouseButtons[] = {
    { B_PRIMARY_MOUSE_BUTTON, MOUSE_BUTTON_LEFT },
    { B_TERTIARY_MOUSE_BUTTON, MOUSE_BUTTON_CENTER },
    { B_SECONDARY_MOUSE_BUTTON, MOUSE_BUTTON_RIGHT },
    { -1, -1 }
};

class FrameBufferView : public BView 
{
 public:
		FrameBufferView(BRect rect, int width, int height);
		~FrameBufferView();

		virtual void 	MessageReceived(BMessage *message);
		virtual void 	Draw(BRect rect);
		
		void 			Paint();
		uint32			*GetBuffer();
		uint32			GetBufferSize();
		void			ResizeBitmap(int width, int height);
		
		int				Width() { return buffer_width; }
		int				Height() { return buffer_height; }
 private:
 		int				buffer_width;
 		int				buffer_height;
		BView			*bufferView;
		BBitmap 		*bufferBitmap;		
};

class VLCVideoWindow : public BWindow {
	public:
						VLCVideoWindow(BRect rect, const char* name);
		virtual			~VLCVideoWindow();

		virtual void 	DispatchMessage(BMessage *message, BHandler *handler);
		bool			QuitRequested();
		
		FrameBufferView* View(void) { return frameBufferView; }
		BMessageQueue*  Queue(void) { return queue; }
		void			CloneAndPushMessage(BMessage *message);

		void			SetFullscreen(bool fullscreen);
		bool			IsFullscreen(void) {return is_fullscreen; }

		void			HideCursor(bool hide);

	private:
		bool			is_fullscreen;
		BRect			lastPosition;
		BPoint			lastMouseClick;

		FrameBufferView	*frameBufferView;
		BMessageQueue 	*queue;
		BCursor 		*emptyCursor;
		bool			dragWindow;
};

FrameBufferView::FrameBufferView(BRect rect, int width, int height) : 
	BView(rect, "FrameBufferView", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS)
{
	buffer_width = width;
	buffer_height = height;
	
	BRect fbRect = BRect(0, 0, buffer_width - 1, buffer_height - 1);
	bufferBitmap = new BBitmap(fbRect, B_RGB32, true);	
}

FrameBufferView::~FrameBufferView()
{
	delete bufferBitmap;
}


void
FrameBufferView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_MOUSE_MOVED:
		case B_MOUSE_WHEEL_CHANGED:
		{
			Window()->PostMessage(message);
			break;
		}
		case B_MOUSE_DOWN:		
		case B_MOUSE_UP:
		{
			Window()->PostMessage(message);
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}

void
FrameBufferView::Draw(BRect rect)
{
	SetDrawingMode(B_OP_COPY);
	DrawBitmap(bufferBitmap, rect, rect);
}

void 
FrameBufferView::Paint()
{
	if(LockLooper())	{
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(bufferBitmap);
		UnlockLooper();
	}
}

uint32 *
FrameBufferView::GetBuffer()
{
	if(bufferBitmap == NULL)
		return NULL;
	return (uint32*)bufferBitmap->Bits();
}

uint32	
FrameBufferView::GetBufferSize()
{
	if(bufferBitmap == NULL)
		return 0;
	return bufferBitmap->BitsLength();
}

void	
FrameBufferView::ResizeBitmap(int w, int h)
{
	if(w == buffer_width && h == buffer_height && bufferBitmap != NULL)
		return;
	if(LockLooper()) {
	 	delete bufferBitmap;
		
		buffer_width = w;
		buffer_height = h;
	
		BRect	fbRect = BRect(0, 0, buffer_width - 1, buffer_height - 1);
		bufferBitmap = new BBitmap(fbRect, B_RGB32, true);	
		
	 	UnlockLooper();
	} 	 
}


VLCVideoWindow::VLCVideoWindow(BRect frame, const char* title)
	: BWindow(frame, title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, 0),
	is_fullscreen(false),
	dragWindow(false)
{
	frameBufferView = new FrameBufferView(Bounds(), frame.Width(), frame.Height());
	frameBufferView->SetViewColor(0, 0, 0);
	AddChild(frameBufferView);
	queue = new BMessageQueue();
	emptyCursor = new BCursor(B_CURSOR_ID_NO_CURSOR);
}


VLCVideoWindow::~VLCVideoWindow()
{
	delete emptyCursor;
	delete queue;
}

void 
VLCVideoWindow::CloneAndPushMessage(BMessage *message)
{
	BMessage *clone = new BMessage(*message);
	queue->Lock();
	queue->AddMessage(clone);
	queue->Unlock();
}

void 
VLCVideoWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
	switch (message->what) {
		case B_MOUSE_DOWN:
		{
			uint32 buttons = 0;
			message->FindInt32("buttons", (int32 *)&buttons);

			if (buttons & B_PRIMARY_MOUSE_BUTTON) {
				frameBufferView->SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
				message->FindPoint("where", &lastMouseClick);
				dragWindow = true;
			}
			CloneAndPushMessage(message);
			break;
		}
		case B_MOUSE_UP:
		{
			frameBufferView->SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
			dragWindow = false;
			CloneAndPushMessage(message);
			BWindow::DispatchMessage(message, handler);
			break;
		}
		case B_MOUSE_MOVED:
		{
			uint32 buttons = 0;
			message->FindInt32("buttons", (int32 *)&buttons);
			BPoint where;
			message->FindPoint("where", &where);
			BPoint screenWhere;
			message->FindPoint("screen_where", &screenWhere);
			
			if (dragWindow
			&& buttons & B_PRIMARY_MOUSE_BUTTON
			&& (fabs(where.x - lastMouseClick.x) >= 0.5
			|| fabs(where.y - lastMouseClick.y) >= 0.5)) {
				BCursor cursor(B_CURSOR_ID_MOVE);
				frameBufferView->SetViewCursor(&cursor);
				MoveTo(screenWhere - lastMouseClick);
				break;
			}
			CloneAndPushMessage(message);
			break;
		}
		case B_ZOOM:
		case B_WINDOW_RESIZED:
		case B_MOUSE_WHEEL_CHANGED:
		{
			CloneAndPushMessage(message);
			break;
		}
		case B_UNMAPPED_KEY_DOWN:
		case B_KEY_DOWN:
		{
			CloneAndPushMessage(message);
			BWindow::DispatchMessage(message, handler);
			break;
		}
		default:
		{
			BWindow::DispatchMessage(message, handler);
			break;
		}
	}
}

bool
VLCVideoWindow::QuitRequested()
{
	BMessage message(B_QUIT_REQUESTED);	
	CloneAndPushMessage(&message);
	return false;
}

void
VLCVideoWindow::SetFullscreen(bool fullscreen)
{
	if (fullscreen == is_fullscreen)
		return;

	if (fullscreen) {
		BScreen screen(B_MAIN_SCREEN_ID);
		lastPosition = Frame();
		MoveTo(0,0);
		ResizeTo(screen.Frame().Width() + 1, screen.Frame().Height() + 1);
		Activate();
	} else {
		MoveTo(lastPosition.left, lastPosition.top);
		ResizeTo(lastPosition.Width(), lastPosition.Height());
	}

	is_fullscreen = fullscreen;
}

void
VLCVideoWindow::HideCursor(bool hide)
{
	if (hide && dragWindow)
		return;
	if (frameBufferView->LockLooper()) {
		frameBufferView->SetViewCursor(hide ? emptyCursor : B_CURSOR_SYSTEM_DEFAULT);
		frameBufferView->UnlockLooper();
	}
}

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
struct vout_display_sys_t {
    vout_display_place_t place;
    picture_pool_t *pool;

	/* Embedded widget */
    QLabel *videoWidget;
    QImage videoImage;
    QPixmap videoPixmap;

	/* Native window */
    VLCVideoWindow *win;
    FrameBufferView *view;
    uint32 lastMouseButtons;
    int	resizeDisplayCount;

    bool isEmbedded;

    int width;
    int height;
};

/*****************************************************************************
 * OpenVideo: activates haiku vout display method
 *****************************************************************************/
static int Open(vlc_object_t *object)
{
    vout_display_t *vd = (vout_display_t *)object;
    vout_display_sys_t *sys;

    vd->sys = sys = (vout_display_sys_t*)calloc(1, sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;
    
    sys->pool = NULL;
    sys->videoWidget = NULL;
    sys->isEmbedded = false;
    sys->lastMouseButtons = 0;
    sys->resizeDisplayCount = 0;

    vout_window_t* window = vout_display_NewWindow(vd, VOUT_WINDOW_TYPE_QT);
    if (window && window->handle.qtwindow) {
        sys->isEmbedded = true;
        sys->videoWidget =  static_cast<QLabel*>(window->handle.qtwindow);
    }

    video_format_t fmt;
    video_format_ApplyRotation(&fmt, &vd->fmt);

    fmt.i_chroma = VLC_CODEC_RGB32;
    fmt.i_rmask = 0x00ff0000;
    fmt.i_gmask = 0x0000ff00;
    fmt.i_bmask = 0x000000ff;

    if (sys->isEmbedded) {
		sys->width = fmt.i_width;
		sys->height = fmt.i_height;
        sys->videoImage = QImage(sys->width, sys->height, QImage::Format_RGB32);
        sys->videoImage.fill(Qt::black);
    } else {
    	BScreen screen(B_MAIN_SCREEN_ID);

		int display_margin = 128;
	    int display_width = vd->cfg->display.width;
	    int display_height = vd->cfg->display.height;
	    
	    if (display_margin + display_width > screen.Frame().Width() - display_margin) {
	    	float zoom = (screen.Frame().Width() - display_margin * 2) / (float)display_width;
	    	display_width = (float)display_width * zoom;
	    	display_height = (float)display_height * zoom;
	    }
	    if (display_margin + display_height > screen.Frame().Height() - display_margin) {
	    	float zoom = (screen.Frame().Height() - display_margin * 2) / (float)display_height;
	    	display_width = (float)display_width * zoom;
	    	display_height = (float)display_height * zoom;
	    }

	    vout_display_DeleteWindow(vd, NULL);

	    sys->win = new VLCVideoWindow(BRect(display_margin, display_margin, \
	    				display_margin + display_width, display_margin + display_height), VOUT_TITLE);
	    sys->win->Show();

	    if (vd->cfg->display.title)
	    	sys->win->SetTitle(vd->cfg->display.title);
	    else
	    	sys->win->SetTitle(VOUT_TITLE);

	    vout_display_cfg_t place_cfg = *vd->cfg;
	    place_cfg.display.width  = display_width;
	    place_cfg.display.height = display_height;
	    vout_display_PlacePicture(&sys->place, &vd->source, &place_cfg, true);	

	    fmt.i_width  = display_width;
	    fmt.i_height = display_height;
	    fmt.i_visible_width = fmt.i_width;
	    fmt.i_visible_height = fmt.i_height;

		vd->info.has_pictures_invalid = true;
    }

    vd->fmt = fmt;
    vd->info.needs_hide_mouse = true;

    vd->pool    = Pool;
    vd->prepare = NULL;
    vd->display = Display;
    vd->control = Control;
    vd->manage  = Manage;

    if (!sys->isEmbedded)
    	vout_display_SendEventDisplaySize(vd, fmt.i_width, fmt.i_height);

    return VLC_SUCCESS;
}

static void Close(vlc_object_t *object)
{
    vout_display_t *vd = (vout_display_t *)object;
    vout_display_sys_t *sys = vd->sys;

    if (!sys->isEmbedded) {
		sys->win->Lock();
		sys->win->Quit();
    }

    if (sys->pool)
        picture_pool_Release(sys->pool);

    free(sys);
}

static picture_pool_t *Pool(vout_display_t *vd, unsigned count)
{
    vout_display_sys_t *sys = vd->sys;
    if (!sys->pool) {
        picture_resource_t rsc;
        memset(&rsc, 0, sizeof(rsc));
        
        if (sys->isEmbedded) {
			memset(sys->videoImage.bits(), 0, sys->videoImage.sizeInBytes());
            rsc.p[0].p_pixels = sys->videoImage.bits();
            rsc.p[0].i_pitch = sys->videoImage.bytesPerLine();
            rsc.p[0].i_lines = sys->height;
        } else {
			memset((uint8_t*)sys->win->View()->GetBuffer(), 0, sys->win->View()->GetBufferSize());
			rsc.p[0].p_pixels = (uint8_t*)sys->win->View()->GetBuffer();
			rsc.p[0].i_pitch = sys->win->View()->Width() * 4;
			rsc.p[0].i_lines = sys->win->View()->Height();
        }

        picture_t *p_picture = picture_NewFromResource(&vd->fmt, &rsc);
        if (!p_picture)
            return NULL;

        sys->pool = picture_pool_New(1, &p_picture);
    }    
    return sys->pool;
}

static void Display(vout_display_t *vd, picture_t *picture, subpicture_t *subpicture)
{
	VLC_UNUSED(subpicture);

    vout_display_sys_t *sys = vd->sys;    

    if (sys->isEmbedded) {
    	if (sys->videoWidget && !sys->videoImage.isNull() && QGuiApplication::instance()) {
    		try {
    			sys->videoPixmap = QPixmap::fromImage(sys->videoImage);
				sys->videoWidget->setPixmap(sys->videoPixmap);
			} catch (...) {
				msg_Err(vd, "exception");
			}
		}
    } else {
    	sys->win->View()->Paint();
    }

    picture_Release(picture);
}

static int Control(vout_display_t *vd, int query, va_list args)
{
    vout_display_sys_t *sys = vd->sys;

    switch (query)
    {
		case VOUT_DISPLAY_HIDE_MOUSE:
		{
			if (sys->isEmbedded) {
				return VLC_EGENERIC;
			} else {
				sys->win->HideCursor(true);
				return VLC_SUCCESS;
			}
		}
        case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
        {
            const vout_display_cfg_t *cfg = va_arg(args, const vout_display_cfg_t *);
            if (sys->isEmbedded) {
            	if (sys->videoWidget) {
	                sys->width = cfg->display.width;
	                sys->height = cfg->display.height;
	                sys->videoImage = sys->videoImage.scaled(sys->width, sys->height);               
	                vout_display_SendEventPicturesInvalid(vd);
            	}
            } else {
		        if ((sys->resizeDisplayCount == 1 || sys->resizeDisplayCount == 2) && !cfg->is_fullscreen) {
		        	sys->resizeDisplayCount++;
		        	return VLC_EGENERIC;
		        }
				sys->win->View()->ResizeBitmap(cfg->display.width, cfg->display.height);
				if (cfg->is_fullscreen) {
					vout_display_SendEventPicturesInvalid(vd);
					return VLC_EGENERIC;
				} else {
					sys->win->ResizeTo(cfg->display.width - 1, cfg->display.height - 1);
				}
	        	vout_display_SendEventPicturesInvalid(vd);
				sys->resizeDisplayCount++;
            }
            return VLC_SUCCESS;
        }
    	case VOUT_DISPLAY_CHANGE_ZOOM:
    	case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
    	case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
    	{
        	const vout_display_cfg_t *cfg;
        	const video_format_t *source;

	        if (query == VOUT_DISPLAY_CHANGE_SOURCE_ASPECT) {
    	        source = va_arg(args, const video_format_t *);
        	    cfg = vd->cfg;
        	} else {
            	source = &vd->source;
            	cfg = va_arg(args, const vout_display_cfg_t *);
        	}
        	vout_display_SendEventPicturesInvalid(vd);
        	return VLC_SUCCESS;
    	}
		case VOUT_DISPLAY_RESET_PICTURES:
        {
            vout_display_place_t place;
            video_format_t src;

            vout_display_PlacePicture(&place, &vd->source, vd->cfg, false);
            video_format_ApplyRotation(&src, &vd->source);

            vd->fmt.i_width  = src.i_width;
            vd->fmt.i_height = src.i_height;
            vd->fmt.i_visible_width  = place.width;
            vd->fmt.i_visible_height = place.height;
            vd->fmt.i_x_offset = place.x;
            vd->fmt.i_y_offset = place.y;

			if (sys->pool)
	            picture_pool_Release(sys->pool);

	        sys->pool = NULL;
  
	        return VLC_SUCCESS;
        }
    	case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
			return VLC_EGENERIC;
    	case VOUT_DISPLAY_CHANGE_FULLSCREEN:
    	{
    		if (sys->isEmbedded) {
    			return VLC_EGENERIC;
    		} else {
	        	bool fs = va_arg(args, int);
				sys->win->SetFullscreen(fs);
		        return VLC_SUCCESS;
    		}
    	}

	    default:
    	    msg_Err(vd, "Unsupported query");
        	return VLC_EGENERIC;
    }
}

static void Manage( vout_display_t *vd )
{
	vout_display_sys_t *sys = vd->sys;

	if (!sys->isEmbedded) {
		while (BMessage* request = sys->win->Queue()->NextMessage()) {
			switch (request->what) {
				case B_QUIT_REQUESTED:
				{
					vout_display_SendEventKey(vd, KEY_MEDIA_STOP);
					break;
				}
				case B_MOUSE_WHEEL_CHANGED:
				{
					uint32 modifiers = 0;
					request->FindInt32("modifiers", (int32 *)&modifiers);
	
					int code = 0;
	
					if (modifiers & B_SHIFT_KEY)
						code |= KEY_MODIFIER_SHIFT;
					if (modifiers & B_CONTROL_KEY)
						code |= KEY_MODIFIER_ALT;
					if (modifiers & B_COMMAND_KEY)
						code |= KEY_MODIFIER_CTRL;
	
					float dx = request->FindFloat("be:wheel_delta_x");
					float dy = request->FindFloat("be:wheel_delta_y");
					if (dx > 0.1)
						vout_display_SendEventKey(vd, KEY_MOUSEWHEELRIGHT | code);
					if (dx < -0.1)
						vout_display_SendEventKey(vd, KEY_MOUSEWHEELLEFT | code);
					if (dy > 0.1)
						vout_display_SendEventKey(vd, KEY_MOUSEWHEELDOWN | code);
					if (dy < -0.1)
						vout_display_SendEventKey(vd, KEY_MOUSEWHEELUP | code);
					break;
				}
				case B_MOUSE_DOWN:
				{
					uint32 buttons = 0;
					request->FindInt32("buttons", (int32 *)&buttons);
	
		            for (int i = 0; platformHaikuMouseButtons[i].haiku != -1; i++) {
		                if (buttons & platformHaikuMouseButtons[i].haiku)
	                        vout_display_SendEventMousePressed(vd, platformHaikuMouseButtons[i].vlc);
		            }
	
					sys->lastMouseButtons = buttons;
					break;
				}
				case B_MOUSE_UP:
				{
					uint32 buttons = 0;
					request->FindInt32("buttons", (int32 *)&buttons);
	
		            for (int i = 0; platformHaikuMouseButtons[i].haiku != -1; i++) {
		                if (sys->lastMouseButtons & platformHaikuMouseButtons[i].haiku && !(buttons & platformHaikuMouseButtons[i].haiku))
	                        vout_display_SendEventMouseReleased(vd, platformHaikuMouseButtons[i].vlc);
		            }
					break;
				}
				case B_MOUSE_MOVED:
				{
					uint32 transit;
					request->FindInt32("be:transit", (int32*)&transit);
					if (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW) {
						BPoint where;
						request->FindPoint("be:view_where", &where);
			            vout_display_place_t place;
	        		    vout_display_PlacePicture(&place, &vd->source, vd->cfg, false);
						where.x = (where.x - place.x) * vd->source.i_visible_width / place.width;
						where.y = (where.y - place.y) * vd->source.i_visible_height / place.height;
						vout_display_SendEventMouseMoved(vd, where.x, where.y);
						sys->win->HideCursor(false);
					}
					break;
				}
				case B_ZOOM:
				{
	//				vout_display_SendEventFullscreen(vd, true, false);
					vout_display_SendEventKey(vd, 'f');
					break;
				}
				case B_WINDOW_RESIZED:
				{
					int width = request->FindInt32("width");
					int height = request->FindInt32("height");
					vout_display_SendEventDisplaySize(vd, width + 1, height + 1);
					break;
				}
				case B_UNMAPPED_KEY_DOWN:
				case B_KEY_DOWN:
				{
					uint32 modifiers = request->FindInt32("modifiers");
					uint32 key = request->FindInt32("key");
	
					int code = KEY_UNSET;
					int i = 0;
					while (platformHaikuScanCodes[i]) {
						if ( key == platformHaikuScanCodes[i + 1]) {
							code = platformHaikuScanCodes[i];
							break;
						}
						i += 2;
					}
					if (modifiers & B_SHIFT_KEY)
						code |= KEY_MODIFIER_SHIFT;
					if (modifiers & B_CONTROL_KEY)
						code |= KEY_MODIFIER_ALT;
					if (modifiers & B_COMMAND_KEY)
						code |= KEY_MODIFIER_CTRL;
	
					if (code != KEY_UNSET) {
						vout_display_SendEventKey(vd, code);
					}
					break;
				}
				default:
					break;
			}
			delete request;
		}
	}	
}
