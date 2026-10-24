/*****************************************************************************
 * vlc_haiku.h: Haiku application helpers
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
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

#ifndef VLC_HAIKU_H
#define VLC_HAIKU_H 1

/**
 * \file
 * Every window on Haiku needs a running BApplication (be_app). LibVLC can
 * be used inside a program that already has one; otherwise libvlccore
 * starts its own in a dedicated thread. Interface, video and audio output
 * plugins hold it for as long as they need it.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Makes sure be_app exists and is running.
 * \return VLC_SUCCESS or an error code
 */
VLC_API int vlc_haiku_app_Hold(vlc_object_t *obj);
#define vlc_haiku_app_Hold(o) vlc_haiku_app_Hold(VLC_OBJECT(o))

/**
 * Releases a reference taken with vlc_haiku_app_Hold(). The application
 * started by libvlccore quits when the last reference goes away.
 */
VLC_API void vlc_haiku_app_Release(void);

/**
 * Sets the BLooper (passed as void *) that receives the messages sent to
 * the application by the system: B_REFS_RECEIVED (files opened from
 * Tracker or dropped on the Deskbar), B_ABOUT_REQUESTED and
 * B_QUIT_REQUESTED. Pass NULL to unset it.
 */
VLC_API void vlc_haiku_app_SetTarget(void *looper);

#ifdef __cplusplus
}
#endif

#endif
