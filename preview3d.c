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
#define GL_GLEXT_PROTOTYPES

#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <GL/glext.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

static int _active = 0;
static int _gl_error = 0;
static gint32 bumpmap_drawable_id = -1;
static GtkWidget *window = 0;
static GtkWidget *glarea = 0;

static GLuint baseID = 0;
static GLuint bumpID = 0;

static int basemap = 0;

static float bump_s = 1;
static float bump_t = 1;
static float base_s = 1;
static float base_t = 1;

static float light_dir[3] = {0, 0, 1};

static int mx;
static int my;
static float rot[3];
static float zoom;

#ifdef _WIN32
PFNGLACTIVETEXTUREARBPROC glActiveTexture = 0;
PFNGLMULTITEXCOORD2FARBPROC glMultiTexCoord2f = 0;
#endif

#define M(r,c) m[(c << 2) + r]
#define T(r,c) t[(c << 2) + r]

static void mat_invert(float *m)
{
   float invdet;
   float t[16];
   
   invdet = (float)1.0 / (M(0, 0) * (M(1, 1) * M(2, 2) - M(1, 2) * M(2, 1)) -
                          M(0, 1) * (M(1, 0) * M(2, 2) - M(1, 2) * M(2, 0)) +
                          M(0, 2) * (M(1, 0) * M(2, 1) - M(1, 1) * M(2, 0)));
   
   T(0,0) =  invdet * (M(1, 1) * M(2, 2) - M(1, 2) * M(2, 1));
   T(0,1) = -invdet * (M(0, 1) * M(2, 2) - M(0, 2) * M(2, 1));
   T(0,2) =  invdet * (M(0, 1) * M(1, 2) - M(0, 2) * M(1, 1));
   T(0,3) = 0;
   
   T(1,0) = -invdet * (M(1, 0) * M(2, 2) - M(1, 2) * M(2, 0));
   T(1,1) =  invdet * (M(0, 0) * M(2, 2) - M(0, 2) * M(2, 0));
   T(1,2) = -invdet * (M(0, 0) * M(1, 2) - M(0, 2) * M(1, 0));
   T(1,3) = 0;
   
   T(2,0) =  invdet * (M(1, 0) * M(2, 1) - M(1, 1) * M(2, 0));
   T(2,1) = -invdet * (M(0, 0) * M(2, 1) - M(0, 1) * M(2, 0));
   T(2,2) =  invdet * (M(0, 0) * M(1, 1) - M(0, 1) * M(1, 0));
   T(2,3) = 0;
   
   T(3,0) = -(M(3, 0) * T(0, 0) + M(3, 1) * T(1, 0) + M(3, 2) * T(2, 0));
   T(3,1) = -(M(3, 0) * T(0, 1) + M(3, 1) * T(1, 1) + M(3, 2) * T(2, 1));
   T(3,2) = -(M(3, 0) * T(0, 2) + M(3, 1) * T(1, 2) + M(3, 2) * T(2, 2));
   T(3,3) = 1;
   
   memcpy(m, t, 16 * sizeof(float));
}

static void mat_transpose(float *m)
{
   float t[16];
   t[0 ] = m[0 ]; t[1 ] = m[4 ]; t[2 ] = m[8 ]; t[3 ] = m[12];
   t[4 ] = m[1 ]; t[5 ] = m[5 ]; t[6 ] = m[9 ]; t[7 ] = m[13];
   t[8 ] = m[2 ]; t[9 ] = m[6 ]; t[10] = m[10]; t[11] = m[14];
   t[12] = m[3 ]; t[13] = m[7 ]; t[14] = m[11]; t[15] = m[15];
   memcpy(m, t, 16 * sizeof(float));
}

static void mat_mult_vec(float *v, float *m)
{
   float t[3];
   t[0] = M(0, 0) * v[0] + M(0, 1) * v[1] + M(0, 2) * v[2];
   t[1] = M(1, 0) * v[0] + M(1, 1) * v[1] + M(1, 2) * v[2];
   t[2] = M(2, 0) * v[0] + M(2, 1) * v[1] + M(2, 2) * v[2];
   
   v[0] = t[0];
   v[1] = t[1];
   v[2] = t[2];
}

#undef M
#undef T

static void init(GtkWidget *widget, gpointer data)
{
   GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
   const char *ext_string;
   
   if(!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
      return;
   
   glClearColor(0, 0, 0.35f, 0);
   glDepthFunc(GL_LEQUAL);
   glEnable(GL_DEPTH_TEST);
   
   glLineWidth(3);
   glEnable(GL_LINE_SMOOTH);
   
   _gl_error = 0;
   
   ext_string = (const char*)glGetString(GL_EXTENSIONS);
   
   if(!strstr(ext_string, "GL_ARB_multitexture"))
   {
      g_message("GL_ARB_multitexture is required for the 3D preview");
      _gl_error = 1;
   }
   
   if(!strstr(ext_string, "GL_ARB_texture_env_combine"))
   {
      g_message("GL_ARB_texture_env_combine is required for the 3D preview");
      _gl_error = 1;
   }

   if(!strstr(ext_string, "GL_ARB_texture_env_dot3"))
   {
      g_message("GL_ARB_texture_env_dot3 is required for the 3D preview");
      _gl_error = 1;
   }

   if(!strstr(ext_string, "GL_SGIS_generate_mipmap"))
   {
      g_message("GL_SGIS_generate_mipmap is required for the 3D preview");
      _gl_error = 1;
   }
   
#ifdef _WIN32
   glActiveTexture = (PFNGLACTIVETEXTUREARBPROC)
      wglGetProcAddress("glActiveTextureARB");
   glMultiTexCoord2f = (PFNGLMULTITEXCOORD2FARBPROC)
      wglGetProcAddress("glMultiTexCoord2fARB");
   
   if((glActiveTexture == 0) ||
      (glMultiTexCoord2f == 0))
   {
      g_message("ARB_multitexture extension initialization failed!");
      _gl_error = 1;
   }
#endif
   
   if(_gl_error) return;
   
   glActiveTexture(GL_TEXTURE0_ARB);
   glEnable(GL_TEXTURE_2D);
   glGenTextures(1, &baseID);
   glGenTextures(1, &bumpID);
   
   rot[0] = rot[1] = rot[2] = 0;
   zoom = 3;
   
   gdk_gl_drawable_gl_end(gldrawable);
}

static gint expose(GtkWidget *widget, GdkEventExpose *event)
{
   GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
   float m[16];
   float l[3], c[3], mag;
   
   if(event->count > 0) return(1);
   
   if(!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
      return(1);
   
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   
   if(_gl_error)
   {
      gdk_gl_drawable_swap_buffers(gldrawable);
      gdk_gl_drawable_gl_end(gldrawable);
      return(1);
   }
   
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0, 0, -zoom);
   glRotatef(rot[0], 1, 0, 0);
   glRotatef(rot[1], 0, 1, 0);
   glRotatef(rot[2], 0, 0, 1);
   glGetFloatv(GL_MODELVIEW_MATRIX, m);

   mat_invert(m);
   mat_transpose(m);
   l[0] = light_dir[0];
   l[1] = light_dir[1];
   l[2] = light_dir[2];
   mat_mult_vec(l, m);
   
   mag = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
   if(mag != 0)
   {
      l[0] /= mag;
      l[1] /= mag;
      l[2] /= mag;
   }
   else
      l[0] = l[1] = l[2] = 0;
      
   glActiveTexture(GL_TEXTURE0_ARB);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
   glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_DOT3_RGB_ARB);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PRIMARY_COLOR_ARB);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);

   glActiveTexture(GL_TEXTURE1_ARB);
   if(basemap)
   {
      glEnable(GL_TEXTURE_2D);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
      glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
      glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
      glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
   }
   else
      glDisable(GL_TEXTURE_2D);

   c[0] = (-l[0] * 0.5f) + 0.5f;
   c[1] = (l[1] * 0.5f) + 0.5f;
   c[2] = (l[2] * 0.5f) + 0.5f;
   
   glColor3fv(c);

   glBegin(GL_QUADS);
   {
      glNormal3f(0, 0, 1);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, 0,          0);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, 0,          0);
      glVertex3f(-1, 1, 0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, 0,     bump_t);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, 0,     base_t);
      glVertex3f(-1, -1, 0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, bump_s, bump_t);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, base_s, base_t);
      glVertex3f(1, -1, 0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, bump_s,     0);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, base_s,     0);
      glVertex3f(1, 1, 0.001f);
   }
   glEnd();
   
   c[2] = (-l[2] * 0.5f) + 0.5f;
      
   glColor3fv(c);
   
   glBegin(GL_QUADS);
   {
      glNormal3f(0, 0, -1);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, 0,           0);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, 0,           0);
      glVertex3f(-1,  1, -0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, 0,      bump_t);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, 0,      base_t);
      glVertex3f(-1, -1, -0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, bump_s, bump_t);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, base_s, base_t);
      glVertex3f( 1, -1, -0.001f);
      
      glMultiTexCoord2f(GL_TEXTURE0_ARB, bump_s,      0);
      glMultiTexCoord2f(GL_TEXTURE1_ARB, base_s,      0);
      glVertex3f( 1,  1, -0.001f);
   }
   glEnd();
   
   glActiveTexture(GL_TEXTURE1_ARB);
   glDisable(GL_TEXTURE_2D);
   glActiveTexture(GL_TEXTURE0_ARB);
   glDisable(GL_TEXTURE_2D);
   
   glColor4f(1, 1, 1, 1);
   glBegin(GL_LINE_LOOP);
   {
      glVertex3f(-1,  1, 0);
      glVertex3f(-1, -1, 0);
      glVertex3f( 1, -1, 0);
      glVertex3f( 1,  1, 0);
   }
   glEnd();
   
   glEnable(GL_TEXTURE_2D);
   
   gdk_gl_drawable_swap_buffers(gldrawable);
   gdk_gl_drawable_gl_end(gldrawable);
   
   return(1);
}

static gint configure(GtkWidget *widget, GdkEventConfigure *event)
{
   GdkGLContext *glcontext;
   GdkGLDrawable *gldrawable;
   int w, h;
   
   g_return_val_if_fail(widget && event, FALSE);
   
   glcontext = gtk_widget_get_gl_context(widget);
   gldrawable = gtk_widget_get_gl_drawable(widget);
   
   if(!gdk_gl_drawable_gl_begin(gldrawable,glcontext))
      return(1);
   
   w = widget->allocation.width;
   h = widget->allocation.height;
      
   glViewport(0, 0, w, h);
      
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   gluPerspective(60, (float)w / (float)h, 0.5f, 100);
      
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   
   gdk_gl_drawable_gl_end(gldrawable);

   return(1);
}

static gint button_press(GtkWidget *widget, GdkEventButton *event)
{
   mx = event->x;
   my = event->y;
   return(1);
}

static gint motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
   int x, y;
   float dx, dy;
   GdkRectangle area;
   GdkModifierType state;
   
   if(event->is_hint)
   {
#ifndef _WIN32
      gdk_window_get_pointer(event->window, &x, &y, &state);
#endif
   }
   else
   {
      x = event->x;
      y = event->y;
      state = event->state;
   }
   
   area.x = 0;
   area.y = 0;
   area.width = widget->allocation.width;
   area.height = widget->allocation.height;

   dx = -0.25f * (float)(mx - x);
   dy = -0.25f * (float)(my - y);
   
   if(state & GDK_BUTTON1_MASK)
   {
      rot[1] += cosf(rot[0] / 180.0f * M_PI) * dx;
      rot[2] -= sinf(rot[0] / 180.0f * M_PI) * dx;
      rot[0] += dy;
   }
   else if(state & GDK_BUTTON3_MASK)
   {
      zoom += (-dy * 0.5f);
   }
   
   mx = x;
   my = y;

   gtk_widget_draw(widget, &area);
   
   return(1);
}

static void window_destroy(GtkWidget *widget, gpointer data)
{
   gtk_widget_destroy(glarea);
   _active = 0;
}

static gint dialog_constrain(gint32 image_id, gint32 drawable_id,
                             gpointer data)
{
   if(drawable_id == -1) return(1);
   return(gimp_drawable_is_rgb(drawable_id));
}

static void basemap_callback(gint32 id, gpointer data)
{
   GimpDrawable *drawable;
   int x, y, n, w, h, bpp;
   int w_pot, h_pot;
   unsigned char *src, *pixels;
   unsigned char *s, *d;
   GimpPixelRgn src_rgn;
   GdkRectangle area;
   
   if(_gl_error) return;
   
   area.x = 0;
   area.y = 0;
   area.width = glarea->allocation.width;
   area.height = glarea->allocation.height;
   
   if(id == bumpmap_drawable_id)
   {
      basemap = 0;
      gtk_widget_draw(glarea, &area);
      return;
   }
   
   drawable = gimp_drawable_get(id);
   
   w = drawable->width;
   h = drawable->height;
   bpp = drawable->bpp;
   
   src = g_malloc(w * h * bpp);
   gimp_pixel_rgn_init(&src_rgn, drawable, 0, 0, w, h, 0, 0);
   gimp_pixel_rgn_get_rect(&src_rgn, src, 0, 0, w, h);
   
   w_pot = w;
   h_pot = h;
   
   if(w_pot & (w_pot - 1))
   {
      for(n = 0; n < 32; ++n)
      {
         w_pot = 1 << n;
         if(w_pot > w) break;
      }
   }
   
   if(h_pot & (h_pot - 1))
   {
      for(n = 0; n < 32; ++n)
      {
         h_pot = 1 << n;
         if(h_pot > h) break;
      }
   }
   
   if((w_pot == w)&&(h_pot == h))
      pixels = src;
   else
   {
      pixels = g_malloc(w_pot * h_pot * bpp);
      memset(pixels, 0, w_pot * h_pot * bpp);
      for(y = 0; y < h; ++y)
      {
         s = src + (y * (w * bpp));
         d = pixels + (y * (w_pot * bpp));
         for(x = 0; x < w; ++x)
         {
            for(n = 0; n < bpp; ++n)
               *d++ = *s++;
         }
      }
   }

   glActiveTexture(GL_TEXTURE1_ARB);
   glBindTexture(GL_TEXTURE_2D, baseID);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp, w_pot, h_pot, 0,
                (bpp == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels);
   
   base_s=(float)w / (float)w_pot;
   base_t=(float)h / (float)h_pot;
   
   if(pixels != src)
      g_free(pixels);
   g_free(src);
   
   basemap = 1;
   
   gimp_drawable_detach(drawable);
   
   gtk_widget_draw(glarea, &area);
}

void show_3D_preview(GimpDrawable *drawable)
{
   GtkWidget *vbox;
   GtkWidget *table;
   GtkWidget *opt;
   GtkWidget *menu;
   GdkGLConfig *glconfig;
   
   if(_active) return;
   
   bumpmap_drawable_id = drawable->drawable_id;
   
   glconfig = gdk_gl_config_new_by_mode(GDK_GL_MODE_RGBA |
                                        GDK_GL_MODE_DEPTH |
                                        GDK_GL_MODE_DOUBLE);
   if(glconfig == 0)
   {
      g_message("Could not initialize OpenGL context!");
      return;
   }

   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Normalmap - 3D Preview");
   gtk_container_set_resize_mode(GTK_CONTAINER(window), GTK_RESIZE_IMMEDIATE);
   gtk_container_set_reallocate_redraws(GTK_CONTAINER(window), TRUE);
   gtk_signal_connect(GTK_OBJECT(window), "destroy",
                      GTK_SIGNAL_FUNC(window_destroy), 0);
   
   vbox = gtk_vbox_new(0, 0);
   gtk_container_add(GTK_CONTAINER(window), vbox);
   gtk_widget_show(vbox);
   
   glarea = gtk_drawing_area_new();
   gtk_widget_set_usize(glarea, 300, 300);
   gtk_widget_set_gl_capability(glarea, glconfig, 0, 1, GDK_GL_RGBA_TYPE);
   gtk_widget_set_events(glarea, GDK_EXPOSURE_MASK |
                         GDK_BUTTON_PRESS_MASK |
                         GDK_POINTER_MOTION_MASK);
   gtk_signal_connect(GTK_OBJECT(glarea), "realize",
                      GTK_SIGNAL_FUNC(init), 0);
   gtk_signal_connect(GTK_OBJECT(glarea), "expose_event",
                      GTK_SIGNAL_FUNC(expose), 0);
   gtk_signal_connect(GTK_OBJECT(glarea), "motion_notify_event",
                      GTK_SIGNAL_FUNC(motion_notify), 0);
   gtk_signal_connect(GTK_OBJECT(glarea), "button_press_event",
                      GTK_SIGNAL_FUNC(button_press), 0);
   gtk_signal_connect(GTK_OBJECT(glarea), "configure_event",
                      GTK_SIGNAL_FUNC(configure), 0);
   
   gtk_widget_set_usize(glarea, 400, 400);
   
   gtk_box_pack_start(GTK_BOX(vbox),glarea, 1, 1, 0);
   gtk_widget_show(glarea);
   
   table = gtk_table_new(1, 2, 0);
   gtk_container_set_border_width(GTK_CONTAINER(table), 10);
   gtk_table_set_col_spacings(GTK_TABLE(table), 10);
   gtk_box_pack_start(GTK_BOX(vbox), table, 0, 0, 0);
   gtk_widget_show(table);
   
   opt = gtk_option_menu_new();
   gtk_widget_show(opt);
   menu = gimp_drawable_menu_new(dialog_constrain,
                                 basemap_callback,
                                 0, bumpmap_drawable_id);
   gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
   gimp_table_attach_aligned(GTK_TABLE(table), 0, 0,
                             "Base map:", 0, 0.5,
                             opt, 2, 0);
   
   gtk_widget_show(window);
   
   _active = 1;
}

void destroy_3D_preview(void)
{
   if(!_active) return;
   gtk_widget_destroy(window);
   _active = 0;
}

void update_3D_preview(unsigned int w, unsigned int h, int bpp,
                       unsigned char *image)
{
   unsigned int x, y, n, w_pot = w, h_pot = h;
   unsigned char *tmp, *src, *dst;
   
   if(!_active) return;
   if(_gl_error) return;
   
   if(w_pot & (w_pot - 1))
   {
      for(n = 0; n < 32; ++n)
      {
         w_pot = 1 << n;
         if(w_pot > w) break;
      }
   }
   
   if(h_pot & (h_pot - 1))
   {
      for(n = 0; n < 32; ++n)
      {
         h_pot = 1 << n;
         if(h_pot > h) break;
      }
   }
   
   if((w_pot == w) && (h_pot == h))
      tmp = image;
   else
   {
      tmp = g_malloc(w_pot * h_pot * bpp);
      for(y = 0; y < h; ++y)
      {
         src = image + (y * (w * bpp));
         dst = tmp + (y * (w_pot * bpp));
         for(x = 0; x < w; ++x)
         {
            for(n = 0; n < bpp; ++n)
               *dst++ = *src++;
         }
      }
   }

   glActiveTexture(GL_TEXTURE0_ARB);
   glBindTexture(GL_TEXTURE_2D, bumpID);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp, w_pot, h_pot, 0,
                (bpp == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, tmp);
   
   bump_s = (float)w / (float)w_pot;
   bump_t = (float)h / (float)h_pot;
   
   if(tmp != image)
      g_free(tmp);
   
   gtk_widget_queue_draw(glarea);
}

int is_3D_preview_active(void)
{
   return(_active);
}
