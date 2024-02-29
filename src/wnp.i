%module wnp_wrapper
%{
/* Includes the header in the wrapper code */
#include "wnp.h"
%}

/* Parse the header file to generate wrappers */
%include "wnp.h"