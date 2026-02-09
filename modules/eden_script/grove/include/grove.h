/*
 * grove.h — C header for the Grove scripting language FFI
 *
 * This header declares all functions exported by libgrove.a (Rust).
 * Include this from C++ to create/run Grove VMs and register host functions.
 */
#ifndef GROVE_H
#define GROVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque VM handle */
typedef struct GroveVm GroveVm;

/* Value tag — matches Rust GroveValueTag repr(C) */
typedef enum GroveValueTag {
    GROVE_NIL    = 0,
    GROVE_BOOL   = 1,
    GROVE_NUMBER = 2,
    GROVE_STRING = 3,
    GROVE_VEC3   = 4,
    GROVE_OBJECT = 5
} GroveValueTag;

/* String value (pointer + length, NOT null-terminated) */
typedef struct GroveStringVal {
    const char* ptr;
    uint32_t    len;
} GroveStringVal;

/* Vec3 value */
typedef struct GroveVec3Val {
    double x;
    double y;
    double z;
} GroveVec3Val;

/* Value data union — matches Rust GroveValueData repr(C) */
typedef union GroveValueData {
    int32_t        bool_val;
    double         number_val;
    GroveStringVal string_val;
    GroveVec3Val   vec3_val;
    uint64_t       object_handle;
} GroveValueData;

/* Tagged value — matches Rust GroveValue repr(C) */
typedef struct GroveValue {
    GroveValueTag  tag;
    GroveValueData data;
} GroveValue;

/*
 * Host function callback signature.
 *   args      — array of GroveValue arguments
 *   arg_count — number of arguments
 *   result    — write the return value here (defaults to Nil)
 *   userdata  — opaque pointer passed at registration time
 * Return 0 on success, non-zero on error.
 */
typedef int32_t (*GroveHostFn)(
    const GroveValue* args,
    uint32_t          arg_count,
    GroveValue*       result,
    void*             userdata
);

/* ── Lifecycle ─────────────────────────────────────── */

/* Create a new Grove VM. Returns NULL on allocation failure. */
GroveVm* grove_new(void);

/* Destroy a Grove VM. Safe to call with NULL. */
void grove_destroy(GroveVm* vm);

/* ── Evaluation ────────────────────────────────────── */

/*
 * Evaluate a null-terminated source string.
 * Returns 0 on success, -1 on error.
 * On error, call grove_last_error() / grove_last_error_line().
 */
int32_t grove_eval(GroveVm* vm, const char* source);

/* ── Host functions ────────────────────────────────── */

/*
 * Register a host function callable from Grove scripts.
 * Returns 0 on success, -1 on error.
 */
int32_t grove_register_fn(
    GroveVm*    vm,
    const char* name,
    GroveHostFn callback,
    void*       userdata
);

/* ── Globals ───────────────────────────────────────── */

int32_t grove_set_global_number(GroveVm* vm, const char* name, double value);
int32_t grove_set_global_string(GroveVm* vm, const char* name, const char* value);
int32_t grove_set_global_vec3(GroveVm* vm, const char* name, double x, double y, double z);

/* ── Error reporting ───────────────────────────────── */

/*
 * Returns the last error message, or NULL if no error.
 * The pointer is valid until the next grove_eval() call.
 */
const char* grove_last_error(const GroveVm* vm);

/* Returns the line number of the last error, or 0. */
uint32_t grove_last_error_line(const GroveVm* vm);

/* ── Configuration ─────────────────────────────────── */

/* Set the maximum number of instructions before aborting (0 = unlimited). */
void grove_set_instruction_limit(GroveVm* vm, uint64_t limit);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GROVE_H */
