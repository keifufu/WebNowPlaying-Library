%module example
%{
/* Includes the header in the wrapper code */
#include "./include/wnp.h"
%}

/* Parse the header file to generate wrappers */
%include "./include/wnp.h"