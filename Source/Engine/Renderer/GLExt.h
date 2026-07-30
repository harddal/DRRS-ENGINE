#pragma once

// Minimal loader for post-GL1.1 OpenGL entry points on Windows.
//
// The engine runs in a legacy WGL compatibility context. Modern drivers still
// expose GL 3.3+ functions through that context via wglGetProcAddress — they
// just aren't in opengl32.lib. load() must be called once after the Irrlicht
// device (and therefore the GL context) exists; all pointers are null before.
//
// Types are re-declared identically to <GL/gl.h> so this header does not drag
// in windows.h (and its min/max macros). Identical typedefs are legal C++.

#include <cstddef>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int          GLint;
typedef int          GLsizei;
typedef ptrdiff_t    GLintptr;
typedef ptrdiff_t    GLsizeiptr;
typedef char         GLchar;

// Constants missing from the GL 1.1 <GL/gl.h> shipped with the Windows SDK.
#ifndef GL_TEXTURE0
#define GL_TEXTURE0             0x84C0
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER     0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER          0x8D40
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER       0x8A11
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW         0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW          0x88E0
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F              0x8814
#endif
#ifndef GL_RGB32F
#define GL_RGB32F               0x8815
#endif
#ifndef GL_R32F
#define GL_R32F                 0x822E
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE        0x812F
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL   0x813C
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL    0x813D
#endif

namespace GLExt
{
    // --- Framebuffer objects (GL 3.0 / ARB_framebuffer_object) ---
    extern void (__stdcall* BindFramebuffer)(GLenum target, GLuint framebuffer);
    extern void (__stdcall* BlitFramebuffer)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                             GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                             GLbitfield mask, GLenum filter);

    // --- Multitexture (GL 1.3) ---
    extern void (__stdcall* ActiveTexture)(GLenum texture);

    // --- Mipmap generation (GL 3.0 / ARB_framebuffer_object) ---
    extern void (__stdcall* GenerateMipmap)(GLenum target);

    // --- Buffer objects (GL 1.5 / 3.0) ---
    extern void (__stdcall* GenBuffers)(GLsizei n, GLuint* buffers);
    extern void (__stdcall* DeleteBuffers)(GLsizei n, const GLuint* buffers);
    extern void (__stdcall* BindBuffer)(GLenum target, GLuint buffer);
    extern void (__stdcall* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
    extern void (__stdcall* BufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
    extern void (__stdcall* BindBufferBase)(GLenum target, GLuint index, GLuint buffer);

    // --- Uniform blocks (GL 3.1) ---
    extern GLuint (__stdcall* GetUniformBlockIndex)(GLuint program, const GLchar* uniformBlockName);
    extern void   (__stdcall* UniformBlockBinding)(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);

    // Load all pointers via wglGetProcAddress. Requires a current GL context.
    // Returns true when every pointer resolved; logs and returns false otherwise
    // (individual pointers may still be usable — callers must null-check any
    // pointer they rely on). Safe to call multiple times.
    bool load();
    bool isLoaded();
}
