#include "headers/gl.h"

#include <stdio.h>

#define X(type, name) type name;
GL_FUNCS(X)
#undef X

int gl_load(void) {
    int missing = 0;
#define X(type, name)                                              \
    name = (type)glfwGetProcAddress(#name);                        \
    if (!name) { fprintf(stderr, "silly-term: missing GL function %s\n", #name); missing = 1; }
    GL_FUNCS(X)
#undef X
    return missing ? -1 : 0;
}
