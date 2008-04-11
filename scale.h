/*
	normalmap GIMP plugin

	Copyright (C) 2002-2008 Shawn Kirst <skirst@insightbb.com>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; see the file COPYING.  If not, write to
	the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
	Boston, MA 02111-1307, USA.
*/

#ifndef __SCALE_H
#define __SCALE_H

float cubic_interpolate(float a, float b, float c, float d, float x);
void scale_pixels(unsigned char *dst, int dw, int dh,
                  unsigned char *src, int sw, int sh,
                  int bpp);

#endif
