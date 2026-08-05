/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.

   Minimal system headers for the wasm32 / Emscripten target.

   Every other c2mir target reads <stdio.h> and friends from the host
   filesystem.  A wasm32 build typically runs in a browser where there is
   no filesystem to read, so without these a user cannot even declare
   printf -- it would be implicitly declared with no prototype, and the
   resulting no-argument MIR proto makes the foreign call pass garbage.

   These declare only the commonly used subset.  Symbols still have to be
   supplied to MIR via MIR_load_external.  */

static char stdio_str[]
  = "#ifndef __STDIO_H\n"
    "#define __STDIO_H\n"
    "\n"
    "typedef unsigned long size_t;\n"
    "typedef struct _FILE FILE;\n"
    "\n"
    "#define EOF (-1)\n"
    "#define NULL ((void *) 0)\n"
    "\n"
    "extern FILE *stdin, *stdout, *stderr;\n"
    "\n"
    "int printf (const char *, ...);\n"
    "int fprintf (FILE *, const char *, ...);\n"
    "int sprintf (char *, const char *, ...);\n"
    "int snprintf (char *, size_t, const char *, ...);\n"
    "int scanf (const char *, ...);\n"
    "int sscanf (const char *, const char *, ...);\n"
    "int puts (const char *);\n"
    "int fputs (const char *, FILE *);\n"
    "int putchar (int);\n"
    "int fputc (int, FILE *);\n"
    "int getchar (void);\n"
    "int fflush (FILE *);\n"
    "FILE *fopen (const char *, const char *);\n"
    "int fclose (FILE *);\n"
    "size_t fread (void *, size_t, size_t, FILE *);\n"
    "size_t fwrite (const void *, size_t, size_t, FILE *);\n"
    "void perror (const char *);\n"
    "\n"
    "#endif /* #ifndef __STDIO_H */\n";

static char stdlib_str[]
  = "#ifndef __STDLIB_H\n"
    "#define __STDLIB_H\n"
    "\n"
    "typedef unsigned long size_t;\n"
    "\n"
    "#define NULL ((void *) 0)\n"
    "#define EXIT_SUCCESS 0\n"
    "#define EXIT_FAILURE 1\n"
    "#define RAND_MAX 2147483647\n"
    "\n"
    "void *malloc (size_t);\n"
    "void *calloc (size_t, size_t);\n"
    "void *realloc (void *, size_t);\n"
    "void free (void *);\n"
    "void abort (void);\n"
    "void exit (int);\n"
    "int atoi (const char *);\n"
    "long atol (const char *);\n"
    "double atof (const char *);\n"
    "long strtol (const char *, char **, int);\n"
    "double strtod (const char *, char **);\n"
    "int rand (void);\n"
    "void srand (unsigned int);\n"
    "int abs (int);\n"
    "void qsort (void *, size_t, size_t, int (*) (const void *, const void *));\n"
    "\n"
    "#endif /* #ifndef __STDLIB_H */\n";

static char string_str[]
  = "#ifndef __STRING_H\n"
    "#define __STRING_H\n"
    "\n"
    "typedef unsigned long size_t;\n"
    "\n"
    "#define NULL ((void *) 0)\n"
    "\n"
    "size_t strlen (const char *);\n"
    "char *strcpy (char *, const char *);\n"
    "char *strncpy (char *, const char *, size_t);\n"
    "char *strcat (char *, const char *);\n"
    "char *strncat (char *, const char *, size_t);\n"
    "int strcmp (const char *, const char *);\n"
    "int strncmp (const char *, const char *, size_t);\n"
    "char *strchr (const char *, int);\n"
    "char *strrchr (const char *, int);\n"
    "char *strstr (const char *, const char *);\n"
    "char *strdup (const char *);\n"
    "void *memset (void *, int, size_t);\n"
    "void *memcpy (void *, const void *, size_t);\n"
    "void *memmove (void *, const void *, size_t);\n"
    "int memcmp (const void *, const void *, size_t);\n"
    "\n"
    "#endif /* #ifndef __STRING_H */\n";

static char math_str[]
  = "#ifndef __MATH_H\n"
    "#define __MATH_H\n"
    "\n"
    "double acos (double);\n"
    "double asin (double);\n"
    "double atan (double);\n"
    "double atan2 (double, double);\n"
    "double cos (double);\n"
    "double sin (double);\n"
    "double tan (double);\n"
    "double cosh (double);\n"
    "double sinh (double);\n"
    "double tanh (double);\n"
    "double exp (double);\n"
    "double log (double);\n"
    "double log2 (double);\n"
    "double log10 (double);\n"
    "double pow (double, double);\n"
    "double sqrt (double);\n"
    "double cbrt (double);\n"
    "double ceil (double);\n"
    "double floor (double);\n"
    "double round (double);\n"
    "double trunc (double);\n"
    "double fabs (double);\n"
    "double fmod (double, double);\n"
    "double hypot (double, double);\n"
    "\n"
    "#endif /* #ifndef __MATH_H */\n";

#define WASM32_LIBC_INCLUDES                                                  \
  {"stdio.h", stdio_str}, {"stdlib.h", stdlib_str}, {"string.h", string_str}, \
  {                                                                           \
    "math.h", math_str                                                        \
  }
