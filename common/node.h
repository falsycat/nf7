#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// DLL must have the following definition:
//   `void nf7_init(nf7_init_t*) { }`


#ifdef __cplusplus
extern "C" {
#endif


typedef struct nf7_vtable_t      nf7_vtable_t;
typedef struct nf7_init_t     nf7_init_t;
typedef struct nf7_ctx_t      nf7_ctx_t;
typedef struct nf7_node_t     nf7_node_t;
typedef struct nf7_node_msg_t nf7_node_msg_t;
typedef struct nf7_value_t    nf7_value_t;

typedef struct nf7_vtable_t {
  // ---- entrypoint methods ----
  struct {
    void (*register_node)(nf7_init_t*, const nf7_node_t*);
  } init;

  // ---- context methods ----
  struct {
    // thread-safe
    void (*exec_async)(nf7_ctx_t*, void*, void (*f)(nf7_ctx_t*, void*), uint64_t ms);
    void (*exec_emit)(nf7_ctx_t*, const char* name, const nf7_value_t*, uint64_t ms);
  } ctx;

  // ---- value accessor/mutator ----
  struct {
    nf7_value_t* (*create) (const nf7_value_t*);
    void         (*destroy)(nf7_value_t*);

    uint8_t (*get_type)(const nf7_value_t*);
#   define NF7_PULSE   UINT8_C(0)
#   define NF7_BOOLEAN UINT8_C(1)
#   define NF7_INTEGER UINT8_C(2)
#   define NF7_SCALAR  UINT8_C(3)
#   define NF7_STRING  UINT8_C(4)
#   define NF7_VECTOR  UINT8_C(5)
#   define NF7_TUPLE   UINT8_C(6)
#   define NF7_UNKNOWN UINT8_MAX

    // A result of value_get_type should be checked before calling the followings.
    bool (*get_boolean)(const nf7_value_t*, bool*);
    bool (*get_integer)(const nf7_value_t*, int64_t*);
    bool (*get_scalar) (const nf7_value_t*, double*);
    const char*        (*get_string)(const nf7_value_t*, size_t*);
    const uint8_t*     (*get_vector)(const nf7_value_t*, size_t*);
    const nf7_value_t* (*get_tuple) (const nf7_value_t*, const char*);

    void     (*set_pulse)  (nf7_value_t*);
    void     (*set_boolean)(nf7_value_t*, bool);
    void     (*set_integer)(nf7_value_t*, int64_t);
    void     (*set_scalar) (nf7_value_t*, double);
    char*    (*set_string) (nf7_value_t*, size_t);
    uint8_t* (*set_vector) (nf7_value_t*, size_t);
    void     (*set_tuple)  (nf7_value_t*, const char**, nf7_value_t**);
  } value;
} nf7_vtable_t;

typedef struct nf7_init_t {
  const nf7_vtable_t* vtable;
} nf7_init_t;

typedef struct nf7_ctx_t {
  nf7_value_t* value;
  void*        ptr;
} nf7_ctx_t;

typedef struct nf7_node_t {
  const char*  name;
  const char*  desc;
  const char** inputs;   // null terminated string array
  const char** outputs;  // null terminated string array

  void* (*init)();  // returned pointer will be set to ctx.ptr
  void  (*deinit)(void*);
  void  (*handle)(const nf7_node_msg_t*);
} nf7_node_t;

typedef struct nf7_node_msg_t {
  const char*  name;
  nf7_value_t* value;
  nf7_ctx_t*   ctx;
} nf7_node_msg_t;


#ifdef __cplusplus
}
#endif
