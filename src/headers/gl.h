#ifndef GL_H
#define GL_H

/*
 * The only place that includes GLFW + the OpenGL core headers.
 * Every OpenGL entry point is loaded at runtime through glfwGetProcAddress,
 * so there is no GLAD/GLEW dependency and no link against libGL.
 */
#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#define GL_FUNCS(X) \
    X(PFNGLGETINTEGERVPROC,             glGetIntegerv) \
    X(PFNGLGENERATEMIPMAPPROC,          glGenerateMipmap) \
    X(PFNGLUNIFORM1FPROC,               glUniform1f) \
    X(PFNGLUNIFORM4FPROC,               glUniform4f) \
    X(PFNGLDRAWARRAYSPROC,              glDrawArrays) \
    X(PFNGLVIEWPORTPROC,                glViewport) \
    X(PFNGLCLEARCOLORPROC,              glClearColor) \
    X(PFNGLCLEARPROC,                   glClear) \
    X(PFNGLENABLEPROC,                  glEnable) \
    X(PFNGLDISABLEPROC,                 glDisable) \
    X(PFNGLBLENDFUNCPROC,               glBlendFunc) \
    X(PFNGLPIXELSTOREIPROC,             glPixelStorei) \
    X(PFNGLGENTEXTURESPROC,             glGenTextures) \
    X(PFNGLBINDTEXTUREPROC,             glBindTexture) \
    X(PFNGLDELETETEXTURESPROC,          glDeleteTextures) \
    X(PFNGLTEXPARAMETERIPROC,           glTexParameteri) \
    X(PFNGLTEXIMAGE2DPROC,              glTexImage2D) \
    X(PFNGLTEXSUBIMAGE2DPROC,           glTexSubImage2D) \
    X(PFNGLCREATESHADERPROC,            glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,            glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,           glCompileShader) \
    X(PFNGLGETSHADERIVPROC,             glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,        glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC,            glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,           glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,            glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,             glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC,            glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,       glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC,              glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC,           glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC,      glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC,               glUniform1i) \
    X(PFNGLUNIFORM2FPROC,               glUniform2f) \
    X(PFNGLGENVERTEXARRAYSPROC,         glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,         glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC,      glDeleteVertexArrays) \
    X(PFNGLGENBUFFERSPROC,              glGenBuffers) \
    X(PFNGLBINDBUFFERPROC,              glBindBuffer) \
    X(PFNGLBUFFERDATAPROC,              glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC,           glBufferSubData) \
    X(PFNGLDELETEBUFFERSPROC,           glDeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,     glVertexAttribPointer) \
    X(PFNGLVERTEXATTRIBIPOINTERPROC,    glVertexAttribIPointer) \
    X(PFNGLVERTEXATTRIBDIVISORPROC,     glVertexAttribDivisor) \
    X(PFNGLDRAWARRAYSINSTANCEDPROC,     glDrawArraysInstanced) \
    X(PFNGLACTIVETEXTUREPROC,           glActiveTexture)

#define X(type, name) extern type name;
GL_FUNCS(X)
#undef X

/* Needs a current context. Returns 0 on success, -1 if a function is missing. */
int gl_load(void);

#endif
