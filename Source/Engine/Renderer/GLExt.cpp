#include "Engine/Renderer/GLExt.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <spdlog/spdlog.h>

namespace GLExt
{
    void (__stdcall* BindFramebuffer)(GLenum, GLuint)                                  = nullptr;
    void (__stdcall* BlitFramebuffer)(GLint, GLint, GLint, GLint,
                                      GLint, GLint, GLint, GLint,
                                      GLbitfield, GLenum)                              = nullptr;
    void (__stdcall* ActiveTexture)(GLenum)                                            = nullptr;
    void (__stdcall* GenBuffers)(GLsizei, GLuint*)                                     = nullptr;
    void (__stdcall* DeleteBuffers)(GLsizei, const GLuint*)                            = nullptr;
    void (__stdcall* BindBuffer)(GLenum, GLuint)                                       = nullptr;
    void (__stdcall* BufferData)(GLenum, GLsizeiptr, const void*, GLenum)              = nullptr;
    void (__stdcall* BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*)         = nullptr;
    void (__stdcall* BindBufferBase)(GLenum, GLuint, GLuint)                           = nullptr;
    GLuint (__stdcall* GetUniformBlockIndex)(GLuint, const GLchar*)                    = nullptr;
    void   (__stdcall* UniformBlockBinding)(GLuint, GLuint, GLuint)                    = nullptr;

    static bool s_loaded = false;

    bool load()
    {
        bool ok = true;

        auto resolve = [&ok](const char* name) -> void*
        {
            void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
            if (!p)
            {
                spdlog::warn("GLExt: failed to resolve {}", name);
                ok = false;
            }
            return p;
        };

        #define GLEXT_LOAD(fn, name) fn = reinterpret_cast<decltype(fn)>(resolve(name))
        GLEXT_LOAD(BindFramebuffer,      "glBindFramebuffer");
        GLEXT_LOAD(BlitFramebuffer,      "glBlitFramebuffer");
        GLEXT_LOAD(ActiveTexture,        "glActiveTexture");
        GLEXT_LOAD(GenBuffers,           "glGenBuffers");
        GLEXT_LOAD(DeleteBuffers,        "glDeleteBuffers");
        GLEXT_LOAD(BindBuffer,           "glBindBuffer");
        GLEXT_LOAD(BufferData,           "glBufferData");
        GLEXT_LOAD(BufferSubData,        "glBufferSubData");
        GLEXT_LOAD(BindBufferBase,       "glBindBufferBase");
        GLEXT_LOAD(GetUniformBlockIndex, "glGetUniformBlockIndex");
        GLEXT_LOAD(UniformBlockBinding,  "glUniformBlockBinding");
        #undef GLEXT_LOAD

        s_loaded = ok;
        if (ok)
            spdlog::info("GLExt: all GL 3.3-class entry points resolved");
        else
            spdlog::error("GLExt: some entry points missing — GPU/driver below the GL 3.3 feature floor");

        return ok;
    }

    bool isLoaded()
    {
        return s_loaded;
    }
}
