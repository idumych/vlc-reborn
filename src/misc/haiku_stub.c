/*****************************************************************************
 * haiku_stub.c: Haiku application helpers when there is no Haiku API
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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_haiku.h>

/* libvlccore exports these everywhere; without the Haiku API there is no
 * application to hold */

#undef vlc_haiku_app_Hold
int vlc_haiku_app_Hold(vlc_object_t *obj)
{
    VLC_UNUSED(obj);
    return VLC_EGENERIC;
}

void vlc_haiku_app_Release(void)
{
}

void vlc_haiku_app_SetTarget(void *looper)
{
    VLC_UNUSED(looper);
}
