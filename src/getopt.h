#ifndef GETOPT_H
#define GETOPT_H

/* include files needed by this include file */

/* macros defined by this include file */
#define no_argument       0
#define required_argument 1
#define OPTIONAL_ARG      2

/* types defined by this include file */

/* GETOPT_LONG_OPTION_T: The type of long option */
typedef struct GETOPT_LONG_OPTION_T
{
  const char *name;             /* the name of the long option */
  int has_arg;                  /* one of the above macros */
  int *flag;                    /* determines if getopt_long() returns a
                                 * value for a long option; if it is
                                 * non-NULL, 0 is returned as a function
                                 * value and the value of val is stored in
                                 * the area pointed to by flag.  Otherwise,
                                 * val is returned. */
  int val;                      /* determines the value to return if flag is
                                 * NULL. */
} GETOPT_LONG_OPTION_T;

typedef GETOPT_LONG_OPTION_T option;

#ifdef __cplusplus
extern "C"
{
#endif

  /* externally-defined variables */
  extern char *optarg;
  extern int optind;
  extern int opterr;
  extern int optopt;

  /* function prototypes */
  int getopt (int argc, char **argv, char *optstring);
  int getopt_long (int argc, char **argv, const char *shortopts,
                   const GETOPT_LONG_OPTION_T * longopts, int *longind);
  int getopt_long_only (int argc, char **argv, const char *shortopts,
                        const GETOPT_LONG_OPTION_T * longopts, int *longind);

#ifdef __cplusplus
};

#endif

#endif /* GETOPT_H */

/* END OF FILE getopt.h */
