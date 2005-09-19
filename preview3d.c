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

#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <GL/glew.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

static int _active = 0;
static int _gl_error = 0;
static gint32 normalmap_drawable_id = -1;
static GtkWidget *window = 0;
static GtkWidget *glarea = 0;
static GtkWidget *parallax_check = 0;
static GtkWidget *specular_check = 0;

static GLuint diffuse_tex = 0;
static GLuint normal_tex = 0;
static GLuint white_tex = 0;

static const float anisotropy = 4.0f;

static int has_glsl = 0;
static GLhandleARB parallax_prog = 0;
static const char *parallax_vert_source =
   "varying vec2 tex0;\n"
   "varying vec2 tex1;\n"
   "varying vec3 eyeVec;\n"
   "varying vec3 lightVec;\n"
   
   "uniform vec3 eyePos;\n"
   "uniform vec3 lightDir;\n\n"
   
   "void main()\n"
   "{\n"
   "   gl_Position = ftransform();\n"
   "   tex0 = gl_MultiTexCoord0.xy;\n"
   "   tex1 = gl_MultiTexCoord1.xy;\n"
   "   mat3 TBN;\n"
   "   TBN[0] = gl_MultiTexCoord2.xyz * gl_NormalMatrix;\n"
   "   TBN[1] = gl_MultiTexCoord3.xyz * gl_NormalMatrix;\n"
   "   TBN[2] = gl_Normal * gl_NormalMatrix;\n"
   "   lightVec = lightDir * TBN;\n"
   "   eyeVec = (eyePos - gl_Vertex.xyz) * TBN;\n"
   "}\n";
static const char *parallax_frag_source =
   "varying vec2 tex0;\n"
   "varying vec2 tex1;\n"
   "varying vec3 eyeVec;\n"
   "varying vec3 lightVec;\n"
   
   "uniform sampler2D sNormal;\n\n"
   "uniform sampler2D sDiffuse;\n"
   
   "uniform bool parallax;\n"
   "uniform bool specular;\n"
   "uniform float specular_exp;\n\n"
   
   "void main()\n"
   "{\n"
   "   vec3 V = normalize(eyeVec);\n"
   "   vec3 L = normalize(lightVec);\n"
   "   vec2 offset0 = tex0;\n"
   "   vec2 offset1 = tex1;\n"
   "   if(parallax)\n"
   "   {\n"
   "      float height = texture2D(sNormal, tex0).a;\n"
   "      height = height * 0.06 - 0.03;\n"
   "      offset0 -= (V.xy * height);\n"
   "      offset1 -= (V.xy * height);\n"
   "   }\n"
   "   vec3 N = normalize(texture2D(sNormal, offset0).rgb * 2.0 - 1.0);\n"
   "   vec4 diffuse = texture2D(sDiffuse, offset1);\n"
   "   float NdotL = clamp(dot(N, L), 0.0, 1.0);\n"
   "   vec4 color = diffuse * NdotL;\n"
   "   if(specular)\n"
   "   {\n"
   "      vec3 R = reflect(V, N);\n"
   "      float RdotL = clamp(dot(R, L), 0.0, 1.0);\n"
   "      color += pow(RdotL, specular_exp);\n"
   "   }\n"
   "   gl_FragColor = color;\n"
   "}\n";

static int parallax = 0;
static int specular = 0;

static float specular_exp = 32.0f;

static float normal_s = 1;
static float normal_t = 1;
static float diffuse_s = 1;
static float diffuse_t = 1;

static float light_dir[3] = {0, 0, 1};

static int mx;
static int my;
static float rot[3];
static float zoom;

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
   int err;
   unsigned char white[16] = {0xff, 0xff, 0xff, 0xff,
                              0xff, 0xff, 0xff, 0xff,
                              0xff, 0xff, 0xff, 0xff,
                              0xff, 0xff, 0xff, 0xff};
   GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);

   if(!gdk_gl_drawable_gl_begin(gldrawable, glcontext))
      return;
      
   err = glewInit();
   if(err != GLEW_OK)
   {
      g_message(glewGetErrorString(err));
      _gl_error = 1;
   }

   glClearColor(0, 0, 0.35f, 0);
   glDepthFunc(GL_LEQUAL);
   glEnable(GL_DEPTH_TEST);

   glLineWidth(3);
   glEnable(GL_LINE_SMOOTH);

   _gl_error = 0;

   if(!GLEW_ARB_multitexture)
   {
      g_message("GL_ARB_multitexture is required for the 3D preview");
      _gl_error = 1;
   }

   if(!GLEW_ARB_texture_env_combine)
   {
      g_message("GL_ARB_texture_env_combine is required for the 3D preview");
      _gl_error = 1;
   }

   if(!GLEW_ARB_texture_env_dot3)
   {
      g_message("GL_ARB_texture_env_dot3 is required for the 3D preview");
      _gl_error = 1;
   }

   if(!GLEW_SGIS_generate_mipmap)
   {
      g_message("GL_SGIS_generate_mipmap is required for the 3D preview");
      _gl_error = 1;
   }
   
   if(_gl_error) return;

   glGenTextures(1, &diffuse_tex);
   glGenTextures(1, &normal_tex);
   glGenTextures(1, &white_tex);

   glActiveTexture(GL_TEXTURE0);
   glEnable(GL_TEXTURE_2D);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
   glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

   glActiveTexture(GL_TEXTURE1);
   glEnable(GL_TEXTURE_2D);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
   glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
   glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
   glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
   
   glBindTexture(GL_TEXTURE_2D, white_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 4, 4, 0,
                GL_LUMINANCE, GL_UNSIGNED_BYTE, white);
   
   has_glsl = GLEW_ARB_shader_objects && GLEW_ARB_vertex_shader && 
      GLEW_ARB_fragment_shader;
   
   if(has_glsl)
   {
      GLhandleARB shader;
      int res, len, loc;
      char *info;
      
      parallax_prog = glCreateProgramObjectARB();
      
      shader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
      glShaderSourceARB(shader, 1, &parallax_vert_source, 0);
      glCompileShaderARB(shader);
      glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &res);
      if(res)
         glAttachObjectARB(parallax_prog, shader);
      else
      {
         glGetObjectParameterivARB(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
         info = g_malloc(len + 1);
         glGetInfoLogARB(shader, len, 0, info);
         g_message(info);
         g_free(info);
      }
      glDeleteObjectARB(shader);
      
      shader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
      glShaderSourceARB(shader, 1, &parallax_frag_source, 0);
      glCompileShaderARB(shader);
      glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &res);
      if(res)
         glAttachObjectARB(parallax_prog, shader);
      else
      {
         glGetObjectParameterivARB(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
         info = g_malloc(len + 1);
         glGetInfoLogARB(shader, len, 0, info);
         g_message(info);
         g_free(info);
      }
      glDeleteObjectARB(shader);
      
      glLinkProgramARB(parallax_prog);
      glGetObjectParameterivARB(parallax_prog, GL_OBJECT_LINK_STATUS_ARB, &res);

      if(!res)
      {
         glGetObjectParameterivARB(parallax_prog, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
         info = g_malloc(len + 1);
         glGetInfoLogARB(shader, len, 0, info);
         g_message(info);
         g_free(info);
      }
      
      glUseProgramObjectARB(parallax_prog);
      loc = glGetUniformLocationARB(parallax_prog, "sNormal");
      glUniform1iARB(loc, 0);
      loc = glGetUniformLocationARB(parallax_prog, "sDiffuse");
      glUniform1iARB(loc, 1);
      loc = glGetUniformLocationARB(parallax_prog, "lightDir");
      glUniform3fARB(loc, 0, 0, 1);
      glUseProgramObjectARB(0);
   }
   else
   {
      gtk_widget_set_sensitive(parallax_check, 0);
      gtk_widget_set_sensitive(specular_check, 0);
   }

   rot[0] = rot[1] = rot[2] = 0;
   zoom = 3;

   gdk_gl_drawable_gl_end(gldrawable);
}

static gint expose(GtkWidget *widget, GdkEventExpose *event)
{
   float m[16];
   float l[3], c[3], mag;
   int loc;
   GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
   
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

   c[0] = (-l[0] * 0.5f) + 0.5f;
   c[1] = (l[1] * 0.5f) + 0.5f;
   c[2] = (l[2] * 0.5f) + 0.5f;
   
   glColor3fv(c);
   
   if((parallax || specular) && has_glsl)
   {
      glUseProgramObjectARB(parallax_prog);
      loc = glGetUniformLocationARB(parallax_prog, "eyePos");
      glUniform3fARB(loc, 0, 0, -zoom);
      loc = glGetUniformLocationARB(parallax_prog, "parallax");
      glUniform1iARB(loc, parallax);
      loc = glGetUniformLocationARB(parallax_prog, "specular");
      glUniform1iARB(loc, specular);
      loc = glGetUniformLocationARB(parallax_prog, "specular_exp");
      glUniform1fARB(loc, specular_exp);
   }

   glBegin(GL_TRIANGLE_STRIP);
   {
      glMultiTexCoord3f(GL_TEXTURE2, -1, 0, 0);
      glMultiTexCoord3f(GL_TEXTURE3, 0, 1, 0);
      glNormal3f(0, 0, 1);
      
      glMultiTexCoord2f(GL_TEXTURE0, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
      glVertex3f(-1, 1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 0, normal_t);
      glMultiTexCoord2f(GL_TEXTURE1, 0, diffuse_t);
      glVertex3f(-1, -1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, normal_s, 0);
      glMultiTexCoord2f(GL_TEXTURE1, diffuse_s, 0);
      glVertex3f(1, 1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, normal_s, normal_t);
      glMultiTexCoord2f(GL_TEXTURE1, diffuse_s, diffuse_t);
      glVertex3f(1, -1, 0.001f);
   }
   glEnd();

   c[1] = (-l[1] * 0.5f) + 0.5f;
   c[2] = (-l[2] * 0.5f) + 0.5f;

   glColor3fv(c);

   glBegin(GL_TRIANGLE_STRIP);
   {
      glMultiTexCoord3f(GL_TEXTURE2, -1, 0, 0);
      glMultiTexCoord3f(GL_TEXTURE3, 0, -1, 0);
      glNormal3f(0, 0, -1);

      glMultiTexCoord2f(GL_TEXTURE0, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
      glVertex3f(-1,  1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 0, normal_t);
      glMultiTexCoord2f(GL_TEXTURE1, 0, diffuse_t);
      glVertex3f(-1, -1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, normal_s, 0);
      glMultiTexCoord2f(GL_TEXTURE1, diffuse_s, 0);
      glVertex3f( 1,  1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, normal_s, normal_t);
      glMultiTexCoord2f(GL_TEXTURE1, diffuse_s, diffuse_t);
      glVertex3f( 1, -1, -0.001f);
   }
   glEnd();
   
   glActiveTexture(GL_TEXTURE1);
   glDisable(GL_TEXTURE_2D);
   glActiveTexture(GL_TEXTURE0);
   glDisable(GL_TEXTURE_2D);
   
   if((parallax || specular) && has_glsl)
      glUseProgramObjectARB(0);
   
   glColor4f(1, 1, 1, 1);
   glBegin(GL_LINE_LOOP);
   {
      glVertex3f(-1,  1, 0);
      glVertex3f(-1, -1, 0);
      glVertex3f( 1, -1, 0);
      glVertex3f( 1,  1, 0);
   }
   glEnd();
   
   glActiveTexture(GL_TEXTURE1);
   glEnable(GL_TEXTURE_2D);
   glActiveTexture(GL_TEXTURE0);
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
   gluPerspective(60, (float)w / (float)h, 0.1f, 100);
      
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
   GdkModifierType state;
   
   if(event->is_hint)
   {
#ifndef WIN32
      gdk_window_get_pointer(event->window, &x, &y, &state);
#endif
   }
   else
   {
      x = event->x;
      y = event->y;
      state = event->state;
   }

   dx = -0.25f * (float)(mx - x);
   dy = -0.25f * (float)(my - y);
   
   if(state & GDK_BUTTON1_MASK)
   {
      rot[1] += cosf(rot[0] / 180.0f * M_PI) * dx;
      //rot[2] -= sinf(rot[0] / 180.0f * M_PI) * dx;
      rot[0] += dy;
   }
   else if(state & GDK_BUTTON3_MASK)
   {
      zoom += (-dy * 0.2f);
   }
   
   mx = x;
   my = y;
   
   gtk_widget_queue_draw(widget);
   
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

static void diffusemap_callback(gint32 id, gpointer data)
{
   GimpDrawable *drawable;
   int x, y, n, w, h, bpp;
   int w_pot, h_pot;
   unsigned char *src, *pixels;
   unsigned char *s, *d;
   GimpPixelRgn src_rgn;
   
   if(_gl_error) return;
   
   if(id == normalmap_drawable_id)
   {
      if(white_tex != 0)
      {
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, white_tex);
      }
      gtk_widget_queue_draw(glarea);
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
   
   if((w_pot == w) && (h_pot == h))
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

   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, diffuse_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp, w_pot, h_pot, 0,
                (bpp == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels);
   
   diffuse_s = (float)w / (float)w_pot;
   diffuse_t = (float)h / (float)h_pot;
   
   if(pixels != src)
      g_free(pixels);
   g_free(src);
   
   gimp_drawable_detach(drawable);
   
   gtk_widget_queue_draw(glarea);
}

static void toggle_clicked(GtkWidget *widget, gpointer data)
{
   *((int*)data) = !(*((int*)data));
   gtk_widget_queue_draw(glarea);
}

static void reset_view_clicked(GtkWidget *widget, gpointer data)
{
   rot[0] = rot[1] = rot[2] = 0;
   zoom = 3;
   gtk_widget_queue_draw(glarea);
}

void show_3D_preview(GimpDrawable *drawable)
{
   GtkWidget *vbox, *vbox2;
   GtkWidget *table;
   GtkWidget *opt;
   GtkWidget *menu;
   GtkWidget *check;
   GtkWidget *btn;
   GdkGLConfig *glconfig;
   
   parallax = 0;
   specular = 0;
   
   if(_active) return;
   
   normalmap_drawable_id = drawable->drawable_id;
   
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
   
   gtk_box_pack_start(GTK_BOX(vbox), glarea, 1, 1, 0);

   vbox2 = gtk_vbox_new(0, 0);
   gtk_container_set_border_width(GTK_CONTAINER(vbox2), 10);
   gtk_box_set_spacing(GTK_BOX(vbox2), 5);
   gtk_box_pack_start(GTK_BOX(vbox), vbox2, 0, 0, 0);
   gtk_widget_show(vbox2);
   
   table = gtk_table_new(1, 2, 0);
   gtk_table_set_col_spacings(GTK_TABLE(table), 10);
   gtk_box_pack_start(GTK_BOX(vbox2), table, 0, 0, 0);
   gtk_widget_show(table);
   
   opt = gtk_option_menu_new();
   gtk_widget_show(opt);
   menu = gimp_drawable_menu_new(dialog_constrain,
                                 diffusemap_callback,
                                 0, normalmap_drawable_id);
   gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
   gimp_table_attach_aligned(GTK_TABLE(table), 0, 0,
                             "Diffuse map:", 0, 0.5,
                             opt, 2, 0);

   parallax_check = check = gtk_check_button_new_with_label("Parallax bump mapping");
   gtk_widget_show(check);
   gtk_box_pack_start(GTK_BOX(vbox2), check, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(check), "clicked",
                      GTK_SIGNAL_FUNC(toggle_clicked), &parallax);

   specular_check = check = gtk_check_button_new_with_label("Specular lighting");
   gtk_widget_show(check);
   gtk_box_pack_start(GTK_BOX(vbox2), check, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(check), "clicked",
                      GTK_SIGNAL_FUNC(toggle_clicked), &specular);
   
   btn = gtk_button_new_with_label("Reset view");
   gtk_widget_show(btn);
   gtk_box_pack_start(GTK_BOX(vbox2), btn, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(btn), "clicked",
                      GTK_SIGNAL_FUNC(reset_view_clicked), 0);
   
   gtk_widget_show(glarea);
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

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, normal_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp, w_pot, h_pot, 0,
                (bpp == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, tmp);
   
   normal_s = (float)w / (float)w_pot;
   normal_t = (float)h / (float)h_pot;
   
   if(tmp != image)
      g_free(tmp);
   
   gtk_widget_queue_draw(glarea);
}

int is_3D_preview_active(void)
{
   return(_active);
}
