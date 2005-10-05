/*
	normalmap GIMP plugin

	Copyright (C) 2002 Shawn Kirst <skirst@fuse.net>

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

#define LERP(a,b,c) ((a) + ((b) - (a)) * (c))

void scale_pixels(unsigned char *dst, int dw, int dh,
                  unsigned char *src, int sw, int sh,
                  int bpp)
{
   int n, x, y;
   int hm1 = sh - 1;
   int wm1 = sw - 1;
   int xfl, yfl;
   float xsrc, ysrc;
   float dx, dy, val;
   unsigned char *s, *d;
   int rowbytes = sw * bpp;
      
   d = dst;
      
   for(y = 0; y < dh; ++y)
   {
      ysrc = ((float)y / (float)dh) * (float)hm1;
      yfl = (int)ysrc;
      dy = ysrc - (float)yfl;
      for(x = 0; x < dw; ++x)
      {
         xsrc = ((float)x / (float)dw) * (float)wm1;
         xfl = (int)xsrc;
         dx = xsrc - (float)xfl;
         
         s = src + ((yfl * rowbytes) + (xfl * bpp));
         for(n = 0; n < bpp; ++n)
         {
            val = LERP(LERP(s[0], s[bpp], dx),
                       LERP(s[rowbytes], s[rowbytes + bpp], dx),
                       dy);
            *d++ = (unsigned char)val;
            s++;
         }
      }
   }
}
