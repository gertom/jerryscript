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
#include <stdlib.h>

#ifdef __linux__
  #include <signal.h>
#endif

#ifndef FALSE
  #define FALSE 0
#endif

#ifndef TRUE
  #define TRUE !FALSE
#endif

#define DEFAULT_CAPACITY_UNIT 16
#define CHAIN_ITEM_TYPE     unsigned int
#ifdef TRACER_BINARY_CHAINS
  #define OUTPUT_FILE_NAME     "tracer.bchains"
  #define OUTPUT_FILE_MODE     "b"
  #undef  CHAIN_ITEM_FORMAT
  #undef  CHAIN_ITEM_SEPARATOR
  #define CHAIN_END_CHAIN      0ll
  #define CHAIN_START_PROGRAM  1ll
  #define CHAIN_QUIT_PROGRAM   2ll
#else
  #define OUTPUT_FILE_NAME     "tracer.chains"
  #define OUTPUT_FILE_MODE     "t"
  #define CHAIN_ITEM_FORMAT    "0x%x"
  #define CHAIN_ITEM_SEPARATOR "-->"
  #define CHAIN_END_CHAIN      "\n"
  #define CHAIN_START_PROGRAM  "## START PROGRAM\n"
  #define CHAIN_QUIT_PROGRAM   "## QUIT PROGRAM\n"
#endif

#ifndef TRACER_VERBOSE
#define TRACER_LOGM(msg)
#define TRACER_LOGS(msg,sgn)
#else
#define TRACER_LOGM(msg) fprintf(stderr, MSG "\n")
#define TRACER_LOGS(msg,sgn) fprintf(stderr, MSG "\n", signo)
#endif

#define INTENTIONALLY_UNUSED(X) (void)(X);

/**
 * address:      address of the function
 * final:        the function may call no other methods before return
 * count:        number of children
 * capacity:     allocated size of the children's array
 * parent:       pointer to the parent node
 * children:     array of pointers to the children
 */

typedef struct __cyg_profile_trace_node_type {
	void * address;
	int    final;
	size_t count;
	size_t capacity;
	struct __cyg_profile_trace_node_type *parent;
	struct __cyg_profile_trace_node_type **children;
} __cyg_profile_trace_node_type;

/**
 * Type to store chain data
 * root:                     root of the call tree
 * current:                  actual position in the call tree
 * last_action_was_an_entry: whether it is the end of a chain
 * max_depth:                maximal length of the call chain
 * act_depth:                actual length of the call chain
 */

typedef struct __cyg_profile_trace_type {
	__cyg_profile_trace_node_type * root;
	__cyg_profile_trace_node_type * current;
	int last_action_was_an_entry;
	int max_depth;
	int act_depth;
} __cyg_profile_trace_type;

/**
 * trace data
 */

static __cyg_profile_trace_type __cyg_profile_trace;
static FILE  *__cyg_profile_tracer_fp = NULL;
static CHAIN_ITEM_TYPE *__cyg_profile_tracer_chain_buffer = NULL;
#ifdef __linux__
static volatile int __cyg_profile_atomic_operation = FALSE;
static volatile int __cyg_profile_signal_occurred = 0;
#endif

/*
 * Function declaration
 */

__cyg_profile_trace_node_type *__cyg_profile_trace_node_Constructor(__cyg_profile_trace_node_type *parent, void *func) __attribute__((no_instrument_function));
void                           __cyg_profile_trace_node_Destructor(__cyg_profile_trace_node_type *this) __attribute__((no_instrument_function));
__cyg_profile_trace_node_type *__cyg_profile_trace_node_add(__cyg_profile_trace_node_type *parent, void *func) __attribute__((no_instrument_function));
void                           __cyg_profile_trace_node_collect(__cyg_profile_trace_node_type *node, int depth) __attribute__((no_instrument_function));

#ifdef __linux__
void __cyg_profile_tracer_signal_handler(int signo) __attribute__((no_instrument_function));
void __cyg_profile_tracer_ignore_signal(int signo) __attribute__((no_instrument_function));
#endif

void __cyg_profile_trace_begin(void) __attribute__((constructor,no_instrument_function));
void __cyg_profile_trace_end(void) __attribute__((destructor,no_instrument_function));
void __cyg_profile_func_enter(void *func, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *func, void *caller) __attribute__((no_instrument_function));

/**
 * Initialize a call tree node
 * @this:   pointer to the new node
 * @parent: pointer to the parent node
 * @func:   address of the called function
 */
__cyg_profile_trace_node_type *__cyg_profile_trace_node_Constructor(__cyg_profile_trace_node_type *parent, void *func) {
	__cyg_profile_trace_node_type *this = malloc(sizeof(__cyg_profile_trace_node_type));
	this->address  = func;
	this->final    = FALSE;
	this->count    = 0;
	this->capacity = 0;
	this->parent   = parent;
	this->children = NULL;
	return this;
}

/**
 * Recursively destroy a subtree rooted at a node
 * @this:   pointer to the root of the sub-tree to be deleted
 */
void __cyg_profile_trace_node_Destructor(__cyg_profile_trace_node_type *this) {
	for (size_t i = 0; i < this->count; ++i) {
		__cyg_profile_trace_node_Destructor(this->children[i]);
	}
	free(this->children);
	free(this);
}

/**
 * Add a new call to the tree from the actual function to another one
 * @parent: pointer to a tree node that represents the caller method (and the whole call chain up to this point)
 * @func:   address of the called function
 * returns: the child node of @parent which represents the call of the given function. Creates a new child node if necessary.
 */

__cyg_profile_trace_node_type *__cyg_profile_trace_node_add(__cyg_profile_trace_node_type *parent, void *func) {
	// Check whether the node already exists
	for (size_t i = 0; i < parent->count; ++i) {
		if (parent->children[i]->address == func) {
			return parent->children[i];
		}
	}
	// Extend capacity of children's array if needed
#ifdef __linux__
	__cyg_profile_atomic_operation = TRUE;
#endif
	if(parent->capacity <= parent->count) {
		size_t newcap = parent->capacity + DEFAULT_CAPACITY_UNIT;
		void *newptr = realloc(parent->children, newcap * sizeof(__cyg_profile_trace_node_type*));
		if (!newptr) {
			TRACER_LOGM("Aborting in __cyg_profile_trace_node_add(): cannot increase node capacity");
			abort();
		}
		parent->capacity = newcap;
		parent->children = newptr;
	}
	// Create a new node and add to the parent as a child
	__cyg_profile_trace_node_type *newnode = parent->children[parent->count++] = __cyg_profile_trace_node_Constructor(parent, func);
#ifdef __linux__
	__cyg_profile_atomic_operation = FALSE;
	if (__cyg_profile_signal_occurred) {
		TRACER_LOGS("Handling signal in __cyg_profile_trace_node_add(): handling postponed signal %d", __cyg_profile_signal_occurred);
		raise(__cyg_profile_signal_occurred);
	}
#endif
	return newnode;
}

void __cyg_profile_trace_node_collect(__cyg_profile_trace_node_type *node, int depth) {
	__cyg_profile_tracer_chain_buffer[depth++] = (CHAIN_ITEM_TYPE) ((unsigned long int) (node->address));
	if (node->final) {
#ifdef TRACER_BINARY_CHAINS
		__cyg_profile_tracer_chain_buffer[depth] = CHAIN_END_CHAIN;
		fwrite(__cyg_profile_tracer_chain_buffer + 1, sizeof(CHAIN_ITEM_TYPE), depth, __cyg_profile_tracer_fp);
#else
		fprintf(__cyg_profile_tracer_fp, CHAIN_ITEM_FORMAT, __cyg_profile_tracer_chain_buffer[1]);
		for (int i = 2; i < depth; ++i) {
			fprintf(__cyg_profile_tracer_fp, CHAIN_ITEM_SEPARATOR CHAIN_ITEM_FORMAT, __cyg_profile_tracer_chain_buffer[i]);
		}
		fprintf(__cyg_profile_tracer_fp, CHAIN_END_CHAIN);
#endif
	}
	for (size_t i = 0; i < node->count; ++i) {
		__cyg_profile_trace_node_collect(node->children[i], depth);
	}
}

#ifdef __linux__
/**
 * __cyg_profile_tracer_signal_handler will help closing the trace file in
 * case of abnormal termination.
 *
 */

void __cyg_profile_tracer_signal_handler(int signo) {
	fprintf(stderr, "Signal %d received\r\n", signo);
	// if we are within the graph building
	if (__cyg_profile_atomic_operation) {
		// and we are "asked" to quit
		if (signo == SIGINT || signo == SIGTERM) {
			// then allow to finish partial memory operations before aborting
			__cyg_profile_signal_occurred = signo;
			TRACER_LOGS("Returning from __cyg_profile_tracer_signal_handler(): non-fatal signal %d occurred in atomic operation\n", signo);
			return;
		}
		// on other signals abort without dumping a (probably corrupted) trace
		TRACER_LOGS("Aborting in __cyg_profile_tracer_signal_handler(): signal %d occurred in atomic operation", signo);
		abort();
	}
	// in we are "asked" to quit, exit normally (this will automatically dump the trace)
	if (signo == SIGINT || signo == SIGTERM) {
		TRACER_LOGS("Exiting from __cyg_profile_tracer_signal_handler(): non-fatal signal %d occurred", signo);
		exit(signo);
	}
	// else try to dump trace information and abort
	TRACER_LOGS("Aborting in __cyg_profile_tracer_signal_handler(): signal %d suggests some errors", signo);
	__cyg_profile_trace_end();
	abort();
}

/**
 * __cyg_profile_tracer_ignore_signal will help supressing some signals in
 * the cleanup phase.
 *
 */

void __cyg_profile_tracer_ignore_signal(int signo) {
	INTENTIONALLY_UNUSED(signo)
	return;
}
#endif

/**
 * __cyg_profile_trace_begin is a `constructor' that is called before the
 * main() method of the traced program. It prepares the program elements
 * that are necessary for tracing.
 *
 * It opens the "tracer.trc" file for append and logs the start of a new
 * execution.
 */

void __cyg_profile_trace_begin(void) {
	__cyg_profile_trace.current = __cyg_profile_trace.root = __cyg_profile_trace_node_Constructor(NULL, NULL);
	__cyg_profile_trace.last_action_was_an_entry = FALSE;
	__cyg_profile_trace.max_depth = __cyg_profile_trace.act_depth = 1;
#ifdef __linux__
	signal(SIGABRT, __cyg_profile_tracer_signal_handler);
	signal(SIGFPE, __cyg_profile_tracer_signal_handler);
	signal(SIGILL, __cyg_profile_tracer_signal_handler);
	signal(SIGINT, __cyg_profile_tracer_signal_handler);
	signal(SIGSEGV, __cyg_profile_tracer_signal_handler);
	signal(SIGTERM, __cyg_profile_tracer_signal_handler);
#endif
}

/**
 * __cyg_profile_trace_end is a `destructor' that is called right before
 * the program exits (usually after the main() returned). It finalizes the
 * program elements responsible for tracing and saves the results.
 *
 */

void __cyg_profile_trace_end(void) {
#ifdef __linux__
	signal(SIGABRT, SIG_DFL);
	signal(SIGFPE, SIG_DFL);
	signal(SIGILL, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);
	signal(SIGTERM, __cyg_profile_tracer_ignore_signal);
#endif
	if (__cyg_profile_trace.current != __cyg_profile_trace.root) {
		__cyg_profile_trace.current->final = TRUE;
	}
	__cyg_profile_tracer_chain_buffer = malloc((__cyg_profile_trace.max_depth + 1) * sizeof(CHAIN_ITEM_TYPE));
	if (__cyg_profile_tracer_chain_buffer) {
		__cyg_profile_tracer_fp = fopen(OUTPUT_FILE_NAME, "a" OUTPUT_FILE_MODE);
		if (__cyg_profile_tracer_fp) {
#ifdef TRACER_BINARY_CHAINS
			CHAIN_ITEM_TYPE startstop;
			startstop = CHAIN_START_PROGRAM;
			fwrite(&startstop, sizeof(startstop), 1, __cyg_profile_tracer_fp);
#else
			fprintf(__cyg_profile_tracer_fp, CHAIN_START_PROGRAM);
#endif
			__cyg_profile_trace_node_collect(__cyg_profile_trace.root, 0);
#ifdef TRACER_BINARY_CHAINS
			startstop = CHAIN_QUIT_PROGRAM;
			fwrite(&startstop, sizeof(startstop), 1, __cyg_profile_tracer_fp);
#else
			fprintf(__cyg_profile_tracer_fp, CHAIN_QUIT_PROGRAM);
#endif
			fclose(__cyg_profile_tracer_fp);
		}
		free(__cyg_profile_tracer_chain_buffer);
	}
#ifdef __linux__
	signal(SIGTERM, SIG_DFL);
#endif
	__cyg_profile_trace_node_Destructor(__cyg_profile_trace.root);
}

/**
 * __cyg_profile_func_enter(void *func, void *caller) logs function calls.
 *
 */

void __cyg_profile_func_enter(void *func, void *caller) {
	INTENTIONALLY_UNUSED(caller)
	++__cyg_profile_trace.act_depth;
	if (__cyg_profile_trace.max_depth < __cyg_profile_trace.act_depth) {
		__cyg_profile_trace.max_depth = __cyg_profile_trace.act_depth;
	}
	__cyg_profile_trace.current = __cyg_profile_trace_node_add(__cyg_profile_trace.current, func);
	__cyg_profile_trace.last_action_was_an_entry = TRUE;
}

/**
 * __cyg_profile_func_exit(void *func, void *caller) logs function returns.
 *
 */

void __cyg_profile_func_exit(void *func, void *caller) {
	INTENTIONALLY_UNUSED(caller)
	if (__cyg_profile_trace.last_action_was_an_entry) {
		__cyg_profile_trace.current->final = TRUE;
		__cyg_profile_trace.last_action_was_an_entry = FALSE;
	}
	while (__cyg_profile_trace.current->address && (__cyg_profile_trace.current->address != func)) {
		__cyg_profile_trace.current = __cyg_profile_trace.current->parent;
		--__cyg_profile_trace.act_depth;
	}
	if (__cyg_profile_trace.current->address) {
		__cyg_profile_trace.current = __cyg_profile_trace.current->parent;
		--__cyg_profile_trace.act_depth;
	}
}
