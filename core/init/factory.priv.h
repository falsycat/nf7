#pragma once

#include <assert.h>

#include <uv.h>

#include "nf7.h"

#include "core/exec/entity.h"
#include "core/exec/mod.h"


struct nf7core_init_factory {
  struct nf7* nf7;

  struct nf7util_malloc* malloc;
  uv_loop_t*             uv;
  uv_idle_t              idle;

  const uint8_t* entity_name;
  size_t         entity_namelen;

  void* data;
  void (*on_created)(struct nf7core_init_factory*, struct nf7core_exec_entity*);
  // after the callback is called
};

static struct nf7core_init_factory* factory_new_(struct nf7*, const uint8_t*, size_t);
static void factory_del_(struct nf7core_init_factory*);
static void factory_on_idle_(uv_idle_t*);
static void factory_on_close_(uv_handle_t*);


static inline struct nf7core_init_factory* factory_new_(
    struct nf7* nf7, const uint8_t* entity_name, size_t entity_namelen) {
  struct nf7core_init_factory* this = nf7util_malloc_alloc(nf7->malloc, sizeof(*this));
  if (nullptr == this) {
    nf7util_log_error("failed to allocate the first factory");
    return nullptr;
  }
  *this = (struct nf7core_init_factory) {
    .nf7    = nf7,
    .malloc = nf7->malloc,
    .uv     = nf7->uv,
    .entity_name    = entity_name,
    .entity_namelen = entity_namelen,
  };

  uv_idle_init(this->uv, &this->idle);
  this->idle.data = this;

  uv_idle_start(&this->idle, factory_on_idle_);
  return this;
}

static inline void factory_del_(struct nf7core_init_factory* this) {
  if (nullptr == this) {
    return;
  }
  if (nullptr != this->idle.data) {
    uv_idle_stop(&this->idle);
    uv_close((uv_handle_t*) &this->idle, factory_on_close_);
    return;
  }
  nf7util_malloc_free(this->malloc, this);
}

static inline void factory_on_idle_(uv_idle_t* idle) {
  assert(nullptr != idle);
  struct nf7core_init_factory* this = idle->data;

  struct nf7core_exec* exec = (void*) nf7_get_mod_by_meta(this->nf7, &nf7core_exec);
  if (nullptr == exec) {
    nf7util_log_error("nf7core_exec module is not installed");
    goto EXIT;
  }

  struct nf7core_exec_entity* entity = nf7core_exec_entity_new(exec, this->entity_name, this->entity_namelen);
  if (nullptr == entity) {
    nf7util_log_error("failed to create new entity");
    goto EXIT;
  }

  assert(nullptr != this->on_created);
  this->on_created(this, entity);

EXIT:
  uv_idle_stop(&this->idle);
}
static inline void factory_on_close_(uv_handle_t* handle) {
  assert(nullptr != handle);
  struct nf7core_init_factory* this = handle->data;
  handle->data = nullptr;
  factory_del_(this);
}
