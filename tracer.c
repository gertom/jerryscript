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
#include <memory.h>

#ifdef __linux__
#include <signal.h>
#include <stdlib.h>
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE !FALSE
#endif

#define DEFAULT_CAPACITY_UNIT 16
#define OUTPUT_FILE_NAME "tracer.chains"
#define CHAIN_ITEM_SEPARATOR "-->"

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
static void **__cyg_profile_tracer_chain_buffer = NULL;

/*
 * Function declaration
 */

__cyg_profile_trace_node_type *__cyg_profile_trace_node_Constructor(__cyg_profile_trace_node_type *parent, void *func) __attribute__((no_instrument_function));
void                           __cyg_profile_trace_node_Destructor(__cyg_profile_trace_node_type *this) __attribute__((no_instrument_function));
__cyg_profile_trace_node_type *__cyg_profile_trace_node_add(__cyg_profile_trace_node_type *parent, void *func) __attribute__((no_instrument_function));
void                           __cyg_profile_trace_node_collect(__cyg_profile_trace_node_type *node, int depth) __attribute__((no_instrument_function));

#ifdef __linux__
void __cyg_profile_tracer_signal_handler(int signo) __attribute__((no_instrument_function));
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
	for (int i = 0; i < this->count; ++i) {
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
	for (int i = 0; i < parent->count; ++i) {
		if (parent->children[i]->address == func) {
			return parent->children[i];
		}
	}
	// Extend capacity of children's array if needed
	if(parent->capacity <= parent->count) {
		size_t newcap = parent->capacity + DEFAULT_CAPACITY_UNIT;
		void *newptr = realloc(parent->children, newcap * sizeof(__cyg_profile_trace_node_type*));
		if (!newptr) {
			abort();
		}
		parent->capacity = newcap;
		parent->children = newptr;
	}
	// Create a new node and add to the parent as a child
	return parent->children[parent->count++] = __cyg_profile_trace_node_Constructor(parent, func);
}

void __cyg_profile_trace_node_collect(__cyg_profile_trace_node_type *node, int depth) {
	__cyg_profile_tracer_chain_buffer[depth++] = node->address;
	if (node->final) {
		fprintf(__cyg_profile_tracer_fp, "%p", __cyg_profile_tracer_chain_buffer[1]);
		for (int i = 2; i < depth; ++i) {
			fprintf(__cyg_profile_tracer_fp, CHAIN_ITEM_SEPARATOR "%p", __cyg_profile_tracer_chain_buffer[i]);
		}
		fprintf(__cyg_profile_tracer_fp, "\n");
	}
	for (int i = 0; i < node->count; ++i) {
		__cyg_profile_trace_node_collect(node->children[i], depth);
	}
}

#ifdef __linux__
/**
 * __cyg_profile_trace_signal_handler will help closing the trace file in
 * case of abnormal termination.
 *
 */

void __cyg_profile_tracer_signal_handler(int signo) {
	__cyg_profile_trace_end();
	abort();
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
	signal(SIGSEGV, __cyg_profile_tracer_signal_handler);
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
	signal(SIGSEGV, SIG_DFL);
#endif
	if (__cyg_profile_trace.current != __cyg_profile_trace.root) {
		__cyg_profile_trace.current->final = TRUE;
	}
	__cyg_profile_tracer_chain_buffer = malloc(__cyg_profile_trace.max_depth * sizeof(void*));
	if (__cyg_profile_tracer_chain_buffer) {
		__cyg_profile_tracer_fp = fopen(OUTPUT_FILE_NAME, "a");
		if (__cyg_profile_tracer_fp) {
			fprintf(__cyg_profile_tracer_fp, "## START PROGRAM\n");
			__cyg_profile_trace_node_collect(__cyg_profile_trace.root, 0);
			fprintf(__cyg_profile_tracer_fp, "## QUIT PROGRAM\n");
			fclose(__cyg_profile_tracer_fp);
		}
		free(__cyg_profile_tracer_chain_buffer);
	}
	__cyg_profile_trace_node_Destructor(__cyg_profile_trace.root);
}

/**
 * __cyg_profile_func_enter(void *func, void *caller) logs function calls.
 *
 */

void __cyg_profile_func_enter(void *func, void *caller) {
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
