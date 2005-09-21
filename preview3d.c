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
#include <ctype.h>
#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <GL/glew.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "scale.h"

#define IS_POT(x)  (((x) & ((x) - 1)) == 0)

static int _active = 0;
static int _gl_error = 0;
static gint32 normalmap_drawable_id = -1;
static GtkWidget *window = 0;
static GtkWidget *glarea = 0;
static GtkWidget *parallax_radio = 0;
static GtkWidget *relief_radio = 0;
static GtkWidget *specular_check = 0;
static GtkWidget *gloss_opt = 0;

static GLuint diffuse_tex = 0;
static GLuint gloss_tex = 0;
static GLuint normal_tex = 0;
static GLuint white_tex = 0;

static const float anisotropy = 4.0f;

static int has_glsl = 0;
static int has_npot = 0;

static GLhandleARB parallax_prog = 0;
static GLhandleARB relief_prog = 0;

static const char *parallax_vert_source =
   "varying vec2 tex;\n"
   "varying vec3 eyeVec;\n"
   "varying vec3 lightVec;\n"
   
   "uniform vec3 eyePos;\n"
   "uniform vec3 lightDir;\n\n"
   
   "void main()\n"
   "{\n"
   "   gl_Position = ftransform();\n"
   "   tex = gl_MultiTexCoord0.xy;\n"
   "   mat3 TBN;\n"
   "   TBN[0] = gl_MultiTexCoord3.xyz * gl_NormalMatrix;\n"
   "   TBN[1] = gl_MultiTexCoord4.xyz * gl_NormalMatrix;\n"
   "   TBN[2] = gl_Normal * gl_NormalMatrix;\n"
   "   lightVec = lightDir * TBN;\n"
   "   eyeVec = (eyePos - gl_Vertex.xyz) * TBN;\n"
   "}\n";
static const char *parallax_frag_source =
   "varying vec2 tex;\n"
   "varying vec3 eyeVec;\n"
   "varying vec3 lightVec;\n"
   
   "uniform sampler2D sNormal;\n"
   "uniform sampler2D sDiffuse;\n"
   "uniform sampler2D sGloss;\n\n"
   
   "uniform vec3 lightDir;\n"
   "uniform bool parallax;\n"
   "uniform bool specular;\n"
   "uniform float specular_exp;\n"
   "uniform vec3 ambient_color;\n"
   "uniform vec3 diffuse_color;\n"
   "uniform vec3 specular_color;\n\n"
      
   "void main()\n"
   "{\n"
   "   vec3 V = normalize(eyeVec);\n"
   "   vec3 L = normalize(lightVec);\n"
   "   vec2 tc = tex;\n"
   "   if(parallax)\n"
   "   {\n"
   "      float height = texture2D(sNormal, tex).a;\n"
   "      height = height * 0.04 - 0.02;\n"
   "      tc += (V.xy * height);\n"
   "   }\n"
   "   vec3 N = normalize(texture2D(sNormal, tc).rgb * 2.0 - 1.0);\n"
   "   vec3 diffuse = texture2D(sDiffuse, tc).rgb;\n"
   "   float NdotL = clamp(dot(N, L), 0.0, 1.0);\n"
   "   vec3 color = diffuse * diffuse_color * NdotL;\n"
   "   if(specular)\n"
   "   {\n"
   "      vec3 gloss = texture2D(sGloss, tc).rgb;\n"
   "      vec3 R = reflect(V, N);\n"
   "      float RdotL = clamp(dot(R, L), 0.0, 1.0);\n"
   "      color += gloss * specular_color * pow(RdotL, specular_exp);\n"
   "   }\n"
   "   gl_FragColor.rgb = ambient_color * diffuse + color;\n"
   "}\n";

static const char *relief_vert_source =
   "varying vec2 tex;\n"
   "varying vec3 vpos;\n"
   "varying vec3 normal;\n"
   "varying vec3 tangent;\n"
   "varying vec3 binormal;\n"
   "\n"
   "void main()\n"
   "{\n"
   "   gl_Position = ftransform();\n"
   "   tex = gl_MultiTexCoord0.xy;\n"
   "   vpos = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
   "   normal   = gl_NormalMatrix * gl_Normal;\n"
   "   tangent  = gl_NormalMatrix * gl_MultiTexCoord3.xyz;\n"
   "   binormal = gl_NormalMatrix * gl_MultiTexCoord4.xyz;\n"
   "}\n";
      
static const char *relief_frag_source =
   "varying vec2 tex;\n"
   "varying vec3 vpos;\n"
   "varying vec3 normal;\n"
   "varying vec3 tangent;\n"
   "varying vec3 binormal;\n"
   "\n"
   "uniform sampler2D sNormal;\n"
   "uniform sampler2D sDiffuse;\n"
   "uniform sampler2D sGloss;\n"
   "\n"
   "uniform vec3 lightDir;\n"
   "uniform bool specular;\n"
   "uniform vec3 ambient_color;\n"
   "uniform vec3 diffuse_color;\n"
   "uniform vec3 specular_color;\n"
   "uniform float specular_exp;\n"
   "uniform vec2 planes;\n"
   "uniform float depth_factor;\n"
   "\n"
   "float ray_intersect(sampler2D reliefMap, vec2 dp, vec2 ds)\n"
   "{\n"
   "   const int linear_search_steps = 20;\n"
   "\n"
   "   float size = 1.0 / float(linear_search_steps);\n"
   "   float depth = 0.0;\n"
   "   float best_depth = 1.0;\n"
   "\n"
   "   for(int i = 0; i < linear_search_steps - 1; ++i)\n"
   "   {\n"
   "      depth += size;\n"
   "      float t = texture2D(reliefMap, dp + ds * depth).a;\n"
   "      if(best_depth > 0.996)\n"
   "         if(depth >= t)\n"
   "            best_depth = depth;\n"
   "   }\n"
   "   depth = best_depth;\n"
   "\n"
   "   const int binary_search_steps = 6;\n"
   "\n"
   "   for(int i = 0; i < binary_search_steps; ++i)\n"
   "   {\n"
   "      size *= 0.5;\n"
   "      float t = texture2D(reliefMap, dp + ds * depth).a;\n"
   "      if(depth >= t)\n"
   "      {\n"
   "         best_depth = depth;\n"
   "         depth -= 2.0 * size;\n"
   "      }\n"
   "      depth += size;\n"
   "   }\n"
   "\n"
   "   return(best_depth);\n"
   "}\n"
   "\n"
   "void main()\n"
   "{\n"
   "\n"
   "   vec3 V = normalize(vpos);\n"
   "   float a = dot(normal, V);\n"
   "   vec2 s = vec2(dot(V, tangent), dot(V, binormal));\n"
   "   s *= depth_factor / a;\n"
   "   vec2 ds = s;\n"
   "   vec2 dp = tex;\n"
   "   float d = ray_intersect(sNormal, dp, ds);\n"
   "\n"
   "   vec2 uv = dp + ds * d;\n"
   "   vec3 N = texture2D(sNormal, uv).xyz * 2.0 - 1.0;\n"
   "   vec3 diffuse = texture2D(sDiffuse, uv).rgb;\n"
   "\n"
   "   N = normalize(-N.x * tangent + -N.y * binormal + N.z * normal);\n"
   "\n"
   "   float NdotL = clamp(dot(N, lightDir), 0.0, 1.0);\n"
   "\n"
   "   vec3 color = diffuse * diffuse_color * NdotL;\n"
   "\n"
   "   if(specular)\n"
   "   {\n"
   "      vec3 gloss = texture2D(sGloss, uv).rgb;\n"
   "      vec3 R = reflect(V, N);\n"
   "      float RdotL = clamp(dot(R, lightDir), 0.0, 1.0);\n"
   "      color += gloss * specular_color * pow(RdotL, specular_exp);\n"
   "   }\n"
   "\n"
   "   gl_FragColor.rgb = ambient_color * diffuse + color;\n"
   "}\n";
                                                    
static int bumpmapping = 0;
static int specular = 0;

static float ambient_color[3] = {0.15f, 0.15f, 0.15f};
static float diffuse_color[3] = {1, 1, 1};
static float specular_color[3] = {1, 1, 1};
static float specular_exp = 32.0f;

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

static int check_NPOT(void)
{
   const char *version;
   char *vendor;
   unsigned char dummy[3*3*3*2];
   int i;
   int major, minor, release;
   
   /* check extension */
   if(GLEW_ARB_texture_non_power_of_two)
      return(1);
   
   /* check for 2.0 and not nvidia (NV3x does NPOT in software) */
   version = glGetString(GL_VERSION);
   vendor = g_strdup(glGetString(GL_VENDOR));
   for(i = 0; i < strlen(vendor); ++i)
      vendor[i] = tolower(vendor[i]);
   sscanf(version, "%d.%d.%d", &major, &minor, &release);
   if(major == 2 && strstr(vendor, "nvidia")) return(0);
   g_free(vendor);
   
   /* try a 3x3 texture upload (ATI supports NPOT, but no extension string) */
   glBindTexture(GL_TEXTURE_2D, 0);
   glGetError();
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 3, 0, GL_RGB, GL_UNSIGNED_BYTE, dummy);
   if(glGetError() != GL_NONE) return(0);
                   
   return(1);
}

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
   
   has_npot = check_NPOT();
   
   if(_gl_error) return;

   glGenTextures(1, &diffuse_tex);
   glGenTextures(1, &gloss_tex);
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
   
   glActiveTexture(GL_TEXTURE2);
   glEnable(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D, white_tex);

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

      relief_prog = glCreateProgramObjectARB();
      
      shader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
      glShaderSourceARB(shader, 1, &relief_vert_source, 0);
      glCompileShaderARB(shader);
      glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &res);
      if(res)
         glAttachObjectARB(relief_prog, shader);
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
      glShaderSourceARB(shader, 1, &relief_frag_source, 0);
      glCompileShaderARB(shader);
      glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &res);
      if(res)
         glAttachObjectARB(relief_prog, shader);
      else
      {
         glGetObjectParameterivARB(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
         info = g_malloc(len + 1);
         glGetInfoLogARB(shader, len, 0, info);
         g_message(info);
         g_free(info);
      }
      glDeleteObjectARB(shader);
      
      glLinkProgramARB(relief_prog);
      glGetObjectParameterivARB(relief_prog, GL_OBJECT_LINK_STATUS_ARB, &res);

      if(!res)
      {
         glGetObjectParameterivARB(relief_prog, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
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
      loc = glGetUniformLocationARB(parallax_prog, "sGloss");
      glUniform1iARB(loc, 2);
      loc = glGetUniformLocationARB(parallax_prog, "lightDir");
      glUniform3fARB(loc, 0, 0, 1);

      glUseProgramObjectARB(relief_prog);
      loc = glGetUniformLocationARB(relief_prog, "sNormal");
      glUniform1iARB(loc, 0);
      loc = glGetUniformLocationARB(relief_prog, "sDiffuse");
      glUniform1iARB(loc, 1);
      loc = glGetUniformLocationARB(relief_prog, "sGloss");
      glUniform1iARB(loc, 2);
      loc = glGetUniformLocationARB(relief_prog, "lightDir");
      glUniform3fvARB(loc, 1, light_dir);
      loc = glGetUniformLocationARB(relief_prog, "depth_factor");
      glUniform1fARB(loc, 0.05f);
      loc = glGetUniformLocationARB(relief_prog, "planes");
      glUniform2fARB(loc,
                     -100.0f / (100.0f - 0.1f),
                     -100.0f * 0.1f / (100.0f - 0.1f));
         
      glUseProgramObjectARB(0);
   }
   else
   {
      gtk_widget_set_sensitive(parallax_radio, 0);
      gtk_widget_set_sensitive(relief_radio, 0);
      gtk_widget_set_sensitive(specular_check, 0);
      gtk_widget_set_sensitive(gloss_opt, 0);
   }

   rot[0] = rot[1] = rot[2] = 0;
   zoom = 2;

   gdk_gl_drawable_gl_end(gldrawable);
}

static gint expose(GtkWidget *widget, GdkEventExpose *event)
{
   float m[16];
   float l[3], c[3], mag;
   int loc;
   GdkGLContext *glcontext = gtk_widget_get_gl_context(widget);
   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
   GLhandleARB prog = 0;
   
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
   
   if(has_glsl)
   {
      prog = bumpmapping == 2 ? relief_prog : parallax_prog;

      glUseProgramObjectARB(prog);
      loc = glGetUniformLocationARB(prog, "eyePos");
      glUniform3fARB(loc, 0, 0, -zoom);
      loc = glGetUniformLocationARB(prog, "parallax");
      glUniform1iARB(loc, bumpmapping == 1);
      loc = glGetUniformLocationARB(prog, "specular");
      glUniform1iARB(loc, specular);
      loc = glGetUniformLocationARB(prog, "ambient_color");
      glUniform3fvARB(loc, 1, ambient_color);
      loc = glGetUniformLocationARB(prog, "diffuse_color");
      glUniform3fvARB(loc, 1, diffuse_color);
      loc = glGetUniformLocationARB(prog, "specular_color");
      glUniform3fvARB(loc, 1, specular_color);
      loc = glGetUniformLocationARB(prog, "specular_exp");
      glUniform1fARB(loc, specular_exp);
   }

   glBegin(GL_TRIANGLE_STRIP);
   {
      glMultiTexCoord3f(GL_TEXTURE3, -1, 0, 0);
      glMultiTexCoord3f(GL_TEXTURE4, 0, 1, 0);
      glNormal3f(0, 0, 1);
      
      glMultiTexCoord2f(GL_TEXTURE0, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE2, 0, 0);
      glVertex3f(-1, 1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 0, 1);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 1);
      glMultiTexCoord2f(GL_TEXTURE2, 0, 1);
      glVertex3f(-1, -1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 1, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 1, 0);
      glMultiTexCoord2f(GL_TEXTURE2, 1, 0);
      glVertex3f(1, 1, 0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 1, 1);
      glMultiTexCoord2f(GL_TEXTURE1, 1, 1);
      glMultiTexCoord2f(GL_TEXTURE2, 1, 1);
      glVertex3f(1, -1, 0.001f);
   }
   glEnd();

   c[1] = (-l[1] * 0.5f) + 0.5f;
   c[2] = (-l[2] * 0.5f) + 0.5f;

   glColor3fv(c);

   glBegin(GL_TRIANGLE_STRIP);
   {
      glMultiTexCoord3f(GL_TEXTURE3, -1, 0, 0);
      glMultiTexCoord3f(GL_TEXTURE4, 0, -1, 0);
      glNormal3f(0, 0, -1);

      glMultiTexCoord2f(GL_TEXTURE0, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
      glMultiTexCoord2f(GL_TEXTURE2, 0, 0);
      glVertex3f(-1,  1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 0, 1);
      glMultiTexCoord2f(GL_TEXTURE1, 0, 1);
      glMultiTexCoord2f(GL_TEXTURE2, 0, 1);
      glVertex3f(-1, -1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 1, 0);
      glMultiTexCoord2f(GL_TEXTURE1, 1, 0);
      glMultiTexCoord2f(GL_TEXTURE2, 1, 0);
      glVertex3f( 1,  1, -0.001f);

      glMultiTexCoord2f(GL_TEXTURE0, 1, 1);
      glMultiTexCoord2f(GL_TEXTURE1, 1, 1);
      glMultiTexCoord2f(GL_TEXTURE2, 1, 1);
      glVertex3f( 1, -1, -0.001f);
   }
   glEnd();

   glActiveTexture(GL_TEXTURE2);
   glDisable(GL_TEXTURE_2D);
   glActiveTexture(GL_TEXTURE1);
   glDisable(GL_TEXTURE_2D);
   glActiveTexture(GL_TEXTURE0);
   glDisable(GL_TEXTURE_2D);
   
   if(has_glsl)
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
   
   glActiveTexture(GL_TEXTURE2);
   glEnable(GL_TEXTURE_2D);
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
   //return(gimp_drawable_is_rgb(drawable_id));
   return(1);
}

static void diffusemap_callback(gint32 id, gpointer data)
{
   GimpDrawable *drawable;
   int n, w, h, bpp;
   int w_pot, h_pot;
   unsigned char *pixels, *tmp;
   GimpPixelRgn src_rgn;
   GLenum type = 0;
   
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
   
   switch(bpp)
   {
      case 1: type = GL_LUMINANCE;       break;
      case 2: type = GL_LUMINANCE_ALPHA; break;
      case 3: type = GL_RGB;             break;
      case 4: type = GL_RGBA;            break;
   }
   
   pixels = g_malloc(w * h * bpp);
   gimp_pixel_rgn_init(&src_rgn, drawable, 0, 0, w, h, 0, 0);
   gimp_pixel_rgn_get_rect(&src_rgn, pixels, 0, 0, w, h);
   
   w_pot = w;
   h_pot = h;
   
   if(!has_npot)
   {
      if(!IS_POT(w_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            w_pot = 1 << n;
            if(w_pot > w) break;
         }
      }
   
      if(!IS_POT(h_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            h_pot = 1 << n;
            if(h_pot > h) break;
         }
      }
   }
   
   if((w_pot != w) || (h_pot != h))
   {
      tmp = g_malloc(h_pot * w_pot * bpp);
      scale_pixels(tmp, w_pot, h_pot, pixels, w, h, bpp);
      g_free(pixels);
      pixels = tmp;
   }

   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, diffuse_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, type, w_pot, h_pot, 0,
                type, GL_UNSIGNED_BYTE, pixels);
   
   g_free(pixels);
   
   gimp_drawable_detach(drawable);
   
   gtk_widget_queue_draw(glarea);
}

static void glossmap_callback(gint32 id, gpointer data)
{
   GimpDrawable *drawable;
   int n, w, h, bpp;
   int w_pot, h_pot;
   unsigned char *pixels, *tmp;
   GimpPixelRgn src_rgn;
   GLenum type = 0;
   
   if(_gl_error) return;
   
   if(id == normalmap_drawable_id)
   {
      if(white_tex != 0)
      {
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, white_tex);
      }
      gtk_widget_queue_draw(glarea);
      return;
   }
   
   drawable = gimp_drawable_get(id);
   
   w = drawable->width;
   h = drawable->height;
   bpp = drawable->bpp;
   
   switch(bpp)
   {
      case 1: type = GL_LUMINANCE;       break;
      case 2: type = GL_LUMINANCE_ALPHA; break;
      case 3: type = GL_RGB;             break;
      case 4: type = GL_RGBA;            break;
   }
   
   pixels = g_malloc(w * h * bpp);
   gimp_pixel_rgn_init(&src_rgn, drawable, 0, 0, w, h, 0, 0);
   gimp_pixel_rgn_get_rect(&src_rgn, pixels, 0, 0, w, h);
   
   w_pot = w;
   h_pot = h;
   
   if(!has_npot)
   {
      if(!IS_POT(w_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            w_pot = 1 << n;
            if(w_pot > w) break;
         }
      }
   
      if(!IS_POT(h_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            h_pot = 1 << n;
            if(h_pot > h) break;
         }
      }
   }
   
   if((w_pot != w) || (h_pot != h))
   {
      tmp = g_malloc(h_pot * w_pot * bpp);
      scale_pixels(tmp, w_pot, h_pot, pixels, w, h, bpp);
      g_free(pixels);
      pixels = tmp;
   }

   glActiveTexture(GL_TEXTURE2);
   glBindTexture(GL_TEXTURE_2D, gloss_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, type, w_pot, h_pot, 0,
                type, GL_UNSIGNED_BYTE, pixels);
   
   g_free(pixels);
   
   gimp_drawable_detach(drawable);
   
   gtk_widget_queue_draw(glarea);
}

static void bumpmapping_clicked(GtkWidget *widget, gpointer data)
{
   if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
   {
      bumpmapping = (int)data;
      gtk_widget_queue_draw(glarea);
   }
}

static void toggle_clicked(GtkWidget *widget, gpointer data)
{
   *((int*)data) = !(*((int*)data));
   gtk_widget_queue_draw(glarea);
}

static void reset_view_clicked(GtkWidget *widget, gpointer data)
{
   rot[0] = rot[1] = rot[2] = 0;
   zoom = 2;
   gtk_widget_queue_draw(glarea);
}

void show_3D_preview(GimpDrawable *drawable)
{
   GtkWidget *vbox, *vbox2, *hbox;
   GtkWidget *table;
   GtkWidget *opt;
   GtkWidget *menu;
   GtkWidget *label;
   GtkWidget *radio;
   GtkWidget *check;
   GtkWidget *btn;
   GdkGLConfig *glconfig;
   GSList *group = 0;
   
   bumpmapping = 0;
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
   
   gtk_widget_set_usize(glarea, 600, 450);
   
   gtk_box_pack_start(GTK_BOX(vbox), glarea, 1, 1, 0);

   vbox2 = gtk_vbox_new(0, 0);
   gtk_container_set_border_width(GTK_CONTAINER(vbox2), 10);
   gtk_box_set_spacing(GTK_BOX(vbox2), 5);
   gtk_box_pack_start(GTK_BOX(vbox), vbox2, 0, 0, 0);
   gtk_widget_show(vbox2);
   
   table = gtk_table_new(2, 2, 0);
   gtk_table_set_col_spacings(GTK_TABLE(table), 10);
   gtk_table_set_row_spacings(GTK_TABLE(table), 10);
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
   
   gloss_opt = opt = gtk_option_menu_new();
   gtk_widget_show(opt);
   menu = gimp_drawable_menu_new(dialog_constrain,
                                 glossmap_callback,
                                 0, normalmap_drawable_id);
   gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
   gimp_table_attach_aligned(GTK_TABLE(table), 0, 1,
                             "Gloss map:", 0, 0.5,
                             opt, 2, 0);
   
   hbox = gtk_hbox_new(0, 0);
   gtk_box_set_spacing(GTK_BOX(hbox), 5);
   gtk_box_pack_start(GTK_BOX(vbox2), hbox, 0, 0, 0);
   gtk_widget_show(hbox);
   
   label = gtk_label_new("Bump mapping:");
   gtk_widget_show(label);
   gtk_box_pack_start(GTK_BOX(hbox), label, 0, 0, 0);
   
   radio = gtk_radio_button_new_with_label(group, "Normal");
   gtk_widget_show(radio);
   gtk_box_pack_start(GTK_BOX(hbox), radio, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(radio), "clicked",
                      GTK_SIGNAL_FUNC(bumpmapping_clicked), (gpointer)0);
   group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
   
   parallax_radio = radio = gtk_radio_button_new_with_label(group, "Parallax");
   gtk_widget_show(radio);
   gtk_box_pack_start(GTK_BOX(hbox), radio, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(radio), "clicked",
                      GTK_SIGNAL_FUNC(bumpmapping_clicked), (gpointer)1);
   group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
   
   relief_radio = radio = gtk_radio_button_new_with_label(group, "Relief");
   gtk_widget_show(radio);
   gtk_box_pack_start(GTK_BOX(hbox), radio, 0, 0, 0);
   gtk_signal_connect(GTK_OBJECT(radio), "clicked",
                      GTK_SIGNAL_FUNC(bumpmapping_clicked), (gpointer)2);
   group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));

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
   unsigned int n, w_pot = w, h_pot = h;
   unsigned char *pixels = image;
   
   if(!_active) return;
   if(_gl_error) return;
   
   if(!has_npot)
   {
      if(!IS_POT(w_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            w_pot = 1 << n;
            if(w_pot > w) break;
         }
      }
      
      if(!IS_POT(h_pot))
      {
         for(n = 0; n < 32; ++n)
         {
            h_pot = 1 << n;
            if(h_pot > h) break;
         }
      }
   }
   
   if((w_pot != w) || (h_pot != h))
   {
      pixels = g_malloc(h_pot * w_pot * bpp);
      scale_pixels(pixels, w_pot, h_pot, image, w, h, bpp);
   }

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, normal_tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
   glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp, w_pot, h_pot, 0,
                (bpp == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels);
   
   if(pixels != image)
      g_free(pixels);
   
   gtk_widget_queue_draw(glarea);
}

int is_3D_preview_active(void)
{
   return(_active);
}
