/**
 * This source implements methods that can be used by the -finstrument-functions
 * feature of the gcc C/C++ compiler.
 *
 * To use the feature:
 *  - compile the target objects with the -finstrument-functions passed to gcc
 *  - compile this source normally
 *  - link the target program normally except that add the object compiled
 *    from this source to the linker's object list
 *
 * When the target program is executed it will APPEND the trace of the execution
 * to the "tracer.trc" file. The trace file contains the following entries, one
 * entry per line:
 *  - start of a new execution is denoted by a "START PROGRAM" line
 *  - end of an execution is denoted by a "QUIT PROGRAM" line
 *  - a function call line starts with the character 'E', followed by the
 *    unique identifier of the function that is called
 *  - a function return line starts with the character 'L', followed by the
 *    unique identifier of the function that is left
 *  - in this case, the unique identifier of a function is the address of it in
 *    hexadecimal form
 */

#include <stdio.h>

static FILE* __cyg_profile_tracer_fp = NULL;

/**
 * __cyg_profile_trace_begin is a `constructor' that is called before the
 * main() method of the traced program. It prepares the program elements
 * that are necessary for tracing.
 *
 * It opens the "tracer.trc" file for append and logs the start of a new
 * execution.
 */

void __attribute__((constructor,no_instrument_function)) __cyg_profile_trace_begin(void) {
	__cyg_profile_tracer_fp = fopen("tracer.trc", "a");
	if (__cyg_profile_tracer_fp) {
		fprintf(__cyg_profile_tracer_fp, "START PROGRAM\n");
	}
}

/**
 * __cyg_profile_trace_end is a `destructor' that is called right before
 * the program exits (usually after the main() returned). It finalizes the
 * program elements responsible for tracing and saves the results.
 *
 * It logs the end of an execution and closes the trace file.
 */

void __attribute__((destructor,no_instrument_function)) __cyg_profile_trace_end(void) {
	if (__cyg_profile_tracer_fp) {
		fprintf(__cyg_profile_tracer_fp, "QUIT PROGRAM\n");
		fflush(__cyg_profile_tracer_fp);
		fclose(__cyg_profile_tracer_fp);
		__cyg_profile_tracer_fp = NULL;
	}
}

/**
 * __cyg_profile_func_enter(void *func, void *caller) logs function calls.
 *
 * It prints out a single line starting with an 'E' ('Entering' a function)
 * and immediately followed by the address of the called function.
 */

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *func, void *caller) {
	if (__cyg_profile_tracer_fp) {
		fprintf(__cyg_profile_tracer_fp, "E%p\n", func);
	}
}

/**
 * __cyg_profile_func_exit(void *func, void *caller) logs function returns.
 *
 * It prints out a single line starting with an 'L' ('Leaving' a function)
 * immediately followed by the address of the function we are returning from.
 */

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void *func, void *caller) {
	if (__cyg_profile_tracer_fp) {
		fprintf(__cyg_profile_tracer_fp, "L%p\n", func);
	}
}
