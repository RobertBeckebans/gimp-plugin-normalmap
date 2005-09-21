
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
