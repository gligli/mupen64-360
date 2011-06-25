#ifndef __gl_h_
#define __gl_h_

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * Datatypes
 *
 */
#ifdef CENTERLINE_CLPP
#define signed
#endif
typedef unsigned int    GLenum;
typedef unsigned char   GLboolean;
typedef unsigned int    GLbitfield;
typedef void            GLvoid;
typedef signed char     GLbyte;         /* 1-byte signed */
typedef short           GLshort;        /* 2-byte signed */
typedef int             GLint;          /* 4-byte signed */
typedef unsigned char   GLubyte;        /* 1-byte unsigned */
typedef unsigned short  GLushort;       /* 2-byte unsigned */
typedef unsigned int    GLuint;         /* 4-byte unsigned */
typedef int             GLsizei;        /* 4-byte signed */
typedef float           GLfloat;        /* single precision float */
typedef float           GLclampf;       /* single precision float in [0,1] */
typedef double          GLdouble;       /* double precision float */
typedef double          GLclampd;       /* double precision float in [0,1] */



/************************************************************************
 *
 * Constants
 *
 ************************************************************************/

/* Boolean values */
#define GL_FALSE                                0x0
#define GL_TRUE                                 0x1


#ifdef __cplusplus
}
#endif

#endif /* __gl_h_ */