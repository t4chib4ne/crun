/*
 * crun - OCI runtime written in C
 *
 * Copyright (C) 2017, 2018, 2019, 2020, 2021 Giuseppe Scrivano <giuseppe@scrivano.org>
 * crun is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * crun is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with crun.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <string.h>
#define _GNU_SOURCE

#include <config.h>
#include "../custom-handler.h"
#include "../container.h"
#include "../utils.h"
#include "../linux.h"
#include "handler-utils.h"
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sched.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_DLOPEN
#  include <dlfcn.h>
#endif

#ifdef HAVE_WASMTIME
#  include <wasm.h>
#  include <wasi.h>
#  include <wasmtime.h>
#  include <wasmtime/component/component.h>
#  include <wasmtime/component/val.h>
#  include <wasmtime/component/func.h>
#  include <wasmtime/component/instance.h>
#  include <wasmtime/component/linker.h>
#  include <wasmtime/error.h>
#  include <wasmtime/store.h>
#  include <wasmtime/wasip2.h>
#endif

#if HAVE_DLOPEN && HAVE_WASMTIME
static int
libwasmtime_exec (void *cookie, libcrun_container_t *container arg_unused,
                  const char *pathname, char *const argv[])
{
  fprintf (stderr, "geladen!\n");

  size_t args_size = 0;
  char *const *arg;
  wasm_byte_vec_t error_message;
  wasm_byte_vec_t wasm_bytes;
  wasm_engine_t *(*wasm_engine_new) ();
  wasmtime_error_t *(*wasmtime_wat2wasm) (const char *wat, size_t wat_len, wasm_byte_vec_t *out);
  void (*wasm_engine_delete) (wasm_engine_t *);
  void (*wasm_byte_vec_delete) (wasm_byte_vec_t *);
  void (*wasm_byte_vec_new_uninitialized) (wasm_byte_vec_t *, size_t);
  wasi_config_t *(*wasi_config_new) (const char *);
  wasmtime_store_t *(*wasmtime_store_new) (wasm_engine_t *engine, void *data, void (*finalizer) (void *));
  wasmtime_context_t *(*wasmtime_store_context) (wasmtime_store_t *store);
  wasmtime_linker_t *(*wasmtime_linker_new) (wasm_engine_t *engine);
  wasmtime_error_t *(*wasmtime_linker_define_wasi) (wasmtime_linker_t *linker);
  wasmtime_error_t *(*wasmtime_module_new) (
      wasm_engine_t *engine,
      const uint8_t *wasm,
      size_t wasm_len,
      wasmtime_module_t **ret);
  void (*wasi_config_inherit_argv) (wasi_config_t *config);
  void (*wasi_config_inherit_env) (wasi_config_t *config);
  void (*wasi_config_set_argv) (wasi_config_t *config, int argc, const char *argv[]);
  void (*wasi_config_inherit_stdin) (wasi_config_t *config);
  void (*wasi_config_inherit_stdout) (wasi_config_t *config);
  void (*wasi_config_inherit_stderr) (wasi_config_t *config);
  wasmtime_error_t *(*wasmtime_context_set_wasi) (wasmtime_context_t *context, wasi_config_t *wasi);
  wasmtime_error_t *(*wasmtime_linker_module) (
      wasmtime_linker_t *linker,
      wasmtime_context_t *store,
      const char *name,
      size_t name_len,
      const wasmtime_module_t *module);
  wasmtime_error_t *(*wasmtime_linker_get_default) (
      const wasmtime_linker_t *linker,
      wasmtime_context_t *store,
      const char *name,
      size_t name_len,
      wasmtime_func_t *func);
  wasmtime_error_t *(*wasmtime_func_call) (
      wasmtime_context_t *store,
      const wasmtime_func_t *func,
      const wasmtime_val_t *args,
      size_t nargs,
      wasmtime_val_t *results,
      size_t nresults,
      wasm_trap_t **trap);
  void (*wasmtime_module_delete) (wasmtime_module_t *m);
  void (*wasmtime_store_delete) (wasmtime_store_t *store);
  void (*wasmtime_error_message) (const wasmtime_error_t *error, wasm_name_t *message);
  void (*wasmtime_error_delete) (wasmtime_error_t *error);
  bool (*wasi_config_preopen_dir) (wasi_config_t *config, const char *path, const char *guest_path);

  wasmtime_wat2wasm = dlsym (cookie, "wasmtime_wat2wasm");
  wasm_engine_new = dlsym (cookie, "wasm_engine_new");
  wasm_engine_delete = dlsym (cookie, "wasm_engine_delete");
  wasm_byte_vec_delete = dlsym (cookie, "wasm_byte_vec_delete");
  wasm_byte_vec_new_uninitialized = dlsym (cookie, "wasm_byte_vec_new_uninitialized");
  wasi_config_new = dlsym (cookie, "wasi_config_new");
  wasi_config_set_argv = dlsym (cookie, "wasi_config_set_argv");
  wasmtime_store_new = dlsym (cookie, "wasmtime_store_new");
  wasmtime_store_context = dlsym (cookie, "wasmtime_store_context");
  wasmtime_linker_new = dlsym (cookie, "wasmtime_linker_new");
  wasmtime_linker_define_wasi = dlsym (cookie, "wasmtime_linker_define_wasi");
  wasmtime_module_new = dlsym (cookie, "wasmtime_module_new");
  wasi_config_inherit_argv = dlsym (cookie, "wasi_config_inherit_argv");
  wasi_config_inherit_stdout = dlsym (cookie, "wasi_config_inherit_stdout");
  wasi_config_inherit_stdin = dlsym (cookie, "wasi_config_inherit_stdin");
  wasi_config_inherit_stderr = dlsym (cookie, "wasi_config_inherit_stderr");
  wasi_config_inherit_env = dlsym (cookie, "wasi_config_inherit_env");
  wasmtime_context_set_wasi = dlsym (cookie, "wasmtime_context_set_wasi");
  wasmtime_linker_module = dlsym (cookie, "wasmtime_linker_module");
  wasmtime_linker_get_default = dlsym (cookie, "wasmtime_linker_get_default");
  wasmtime_func_call = dlsym (cookie, "wasmtime_func_call");
  wasmtime_module_delete = dlsym (cookie, "wasmtime_module_delete");
  wasmtime_store_delete = dlsym (cookie, "wasmtime_store_delete");
  wasmtime_error_delete = dlsym (cookie, "wasmtime_error_delete");
  wasmtime_error_message = dlsym (cookie, "wasmtime_error_message");
  wasi_config_preopen_dir = dlsym (cookie, "wasi_config_preopen_dir");

  if (wasm_engine_new == NULL || wasm_engine_delete == NULL || wasm_byte_vec_delete == NULL
      || wasm_byte_vec_new_uninitialized == NULL || wasi_config_new == NULL || wasmtime_store_new == NULL
      || wasmtime_store_context == NULL || wasmtime_linker_new == NULL || wasmtime_linker_define_wasi == NULL
      || wasmtime_module_new == NULL || wasi_config_inherit_argv == NULL || wasi_config_inherit_stdout == NULL
      || wasi_config_inherit_stdin == NULL || wasi_config_inherit_stderr == NULL
      || wasi_config_inherit_env == NULL || wasmtime_context_set_wasi == NULL
      || wasmtime_linker_module == NULL || wasmtime_linker_get_default == NULL || wasmtime_func_call == NULL
      || wasmtime_module_delete == NULL || wasmtime_store_delete == NULL || wasi_config_set_argv == NULL
      || wasmtime_error_delete == NULL || wasmtime_error_message == NULL || wasi_config_preopen_dir == NULL
      || wasmtime_wat2wasm == NULL)
    error (EXIT_FAILURE, 0, "could not find symbol in `libwasmtime.so`");

  // Set up wasmtime context
  wasm_engine_t *engine = wasm_engine_new ();
  assert (engine != NULL);
  wasmtime_store_t *store = wasmtime_store_new (engine, NULL, NULL);
  assert (store != NULL);
  wasmtime_context_t *context = wasmtime_store_context (store);

  // Link with wasi functions defined
  wasmtime_linker_t *linker = wasmtime_linker_new (engine);
  wasmtime_error_t *err = wasmtime_linker_define_wasi (linker);
  if (err != NULL)
    {
      wasmtime_error_message (err, &error_message);
      wasmtime_error_delete (err);
      error (EXIT_FAILURE, 0, "failed to link wasi: %.*s", (int) error_message.size, error_message.data);
    }

  wasm_byte_vec_t wasm;
  // Load and parse container entrypoint
  FILE *file = fopen (pathname, "rbe");
  if (! file)
    error (EXIT_FAILURE, 0, "error loading entrypoint");
  fseek (file, 0L, SEEK_END);
  size_t file_size = ftell (file);
  wasm_byte_vec_new_uninitialized (&wasm, file_size);
  fseek (file, 0L, SEEK_SET);
  if (fread (wasm.data, file_size, 1, file) != 1)
    error (EXIT_FAILURE, 0, "error load");
  fclose (file);

  // If entrypoint contains a webassembly text format
  // compile it on the fly and convert to equivalent
  // binary format.
  if (has_suffix (pathname, "wat") > 0)
    {
      wasmtime_error_t *err = wasmtime_wat2wasm ((char *) &wasm_bytes, file_size, &wasm);
      if (err != NULL)
        {
          wasmtime_error_message (err, &error_message);
          wasmtime_error_delete (err);
          error (EXIT_FAILURE, 0, "failed while compiling wat to wasm binary : %.*s", (int) error_message.size, error_message.data);
        }
      wasm = wasm_bytes;
    }

  // Compile wasm modules
  wasmtime_module_t *module = NULL;
  err = wasmtime_module_new (engine, (uint8_t *) wasm.data, wasm.size, &module);
  if (module)
    {
      wasm_byte_vec_delete (&wasm);

      // Init WASI program
      wasi_config_t *wasi_config = wasi_config_new ("crun_wasi_program");
      assert (wasi_config);

      // Calculate argc for `wasi_config_set_argv`
      for (arg = argv; *arg != NULL; ++arg)
        args_size++;

      wasi_config_set_argv (wasi_config, args_size, (const char **) argv);
      wasi_config_inherit_env (wasi_config);
      wasi_config_inherit_stdin (wasi_config);
      wasi_config_inherit_stdout (wasi_config);
      wasi_config_inherit_stderr (wasi_config);
      wasi_config_preopen_dir (wasi_config, ".", ".");
      wasm_trap_t *trap = NULL;
      err = wasmtime_context_set_wasi (context, wasi_config);
      if (err != NULL)
        {
          wasmtime_error_message (err, &error_message);
          wasmtime_error_delete (err);
          error (EXIT_FAILURE, 0, "failed to instantiate WASI: %.*s", (int) error_message.size, error_message.data);
        }

      // Init module
      err = wasmtime_linker_module (linker, context, "", 0, module);
      if (err != NULL)
        {
          wasmtime_error_message (err, &error_message);
          wasmtime_error_delete (err);
          error (EXIT_FAILURE, 0, "failed to instantiate module: %.*s", (int) error_message.size, error_message.data);
        }

      // Actually run our .wasm
      wasmtime_func_t func;
      err = wasmtime_linker_get_default (linker, context, "", 0, &func);
      if (err != NULL)
        {
          wasmtime_error_message (err, &error_message);
          wasmtime_error_delete (err);
          error (EXIT_FAILURE, 0, "failed to locate default export for module %.*s", (int) error_message.size, error_message.data);
        }

      err = wasmtime_func_call (context, &func, NULL, 0, NULL, 0, &trap);
      if (err != NULL || trap != NULL)
        {
          wasmtime_error_message (err, &error_message);
          wasmtime_error_delete (err);
          error (EXIT_FAILURE, 0, "error calling default export: %.*s", (int) error_message.size, error_message.data);
        }

      // Clean everything
      wasmtime_module_delete (module);
      wasmtime_store_delete (store);
      wasm_engine_delete (engine);

      exit (EXIT_SUCCESS);
    }

  // Reset wasmtime context
  wasmtime_store_delete (store);
  wasm_engine_delete (engine);
  engine = wasm_engine_new ();
  assert (engine != NULL);
  store = wasmtime_store_new (engine, NULL, NULL);
  assert (store != NULL);
  context = wasmtime_store_context (store);

  // Try using Component Model
  wasmtime_error_t *(*wasmtime_component_new) (const wasm_engine_t *engine, const uint8_t *buf, size_t len, wasmtime_component_t **component_out);
  wasmtime_wasip2_config_t *(*wasmtime_wasip2_config_new) (void);
  void (*wasmtime_wasip2_config_inherit_stdin) (wasmtime_wasip2_config_t *config);
  void (*wasmtime_wasip2_config_inherit_stdout) (wasmtime_wasip2_config_t *config);
  void (*wasmtime_wasip2_config_inherit_stderr) (wasmtime_wasip2_config_t *config);
  void (*wasmtime_context_set_wasip2) (wasmtime_context_t *context, wasmtime_wasip2_config_t *config);
  wasmtime_component_linker_t *(*wasmtime_component_linker_new) (wasm_engine_t *engine);
  wasmtime_error_t *(*wasmtime_component_linker_add_wasip2) (wasmtime_component_linker_t *linker);
  wasmtime_error_t *(*wasmtime_component_linker_instantiate) (
      const wasmtime_component_linker_t *linker,
      wasmtime_context_t *context,
      const wasmtime_component_t *component,
      wasmtime_component_instance_t *instance_out);
  wasmtime_component_export_index_t *(*wasmtime_component_get_export_index) (
      wasmtime_component_t *component,
      wasmtime_component_export_index_t *index,
      const char *name,
      size_t name_len);
  wasmtime_component_export_index_t *(*wasmtime_component_instance_get_export_index) (
      const wasmtime_component_instance_t *instance,
      wasmtime_context_t *context,
      const wasmtime_component_export_index_t *instance_export_index,
      const char *name,
      size_t name_len);
  bool (*wasmtime_component_instance_get_func) (
      const wasmtime_component_instance_t *instance,
      wasmtime_context_t *context,
      const wasmtime_component_export_index_t *export_index,
      wasmtime_component_func_t *func_out);
  wasmtime_error_t *(*wasmtime_component_func_call) (
      const wasmtime_component_func_t *func,
      wasmtime_context_t *context,
      const wasmtime_component_val_t *args,
      size_t args_size,
      wasmtime_component_val_t *results,
      size_t results_size);
  void (*wasmtime_component_export_index_delete) (wasmtime_component_export_index_t *export_index);
  void (*wasmtime_component_delete) (wasmtime_component_t *c);
  void (*wasmtime_component_linker_delete) (wasmtime_component_linker_t *linker);

  wasmtime_component_new = dlsym (cookie, "wasmtime_component_new");
  wasmtime_wasip2_config_new = dlsym (cookie, "wasmtime_wasip2_config_new");
  wasmtime_wasip2_config_inherit_stdin = dlsym (cookie, "wasmtime_wasip2_config_inherit_stdin");
  wasmtime_wasip2_config_inherit_stdout = dlsym (cookie, "wasmtime_wasip2_config_inherit_stdout");
  wasmtime_wasip2_config_inherit_stderr = dlsym (cookie, "wasmtime_wasip2_config_inherit_stderr");
  wasmtime_context_set_wasip2 = dlsym (cookie, "wasmtime_context_set_wasip2");
  wasmtime_component_linker_new = dlsym (cookie, "wasmtime_component_linker_new");
  wasmtime_component_linker_add_wasip2 = dlsym (cookie, "wasmtime_component_linker_add_wasip2");
  wasmtime_component_linker_instantiate = dlsym (cookie, "wasmtime_component_linker_instantiate");
  wasmtime_component_get_export_index = dlsym (cookie, "wasmtime_component_get_export_index");
  wasmtime_component_instance_get_export_index = dlsym (cookie, "wasmtime_component_instance_get_export_index");
  wasmtime_component_instance_get_func = dlsym (cookie, "wasmtime_component_instance_get_func");
  wasmtime_component_func_call = dlsym (cookie, "wasmtime_component_func_call");
  wasmtime_component_export_index_delete = dlsym (cookie, "wasmtime_component_export_index_delete");
  wasmtime_component_delete = dlsym (cookie, "wasmtime_component_delete");
  wasmtime_component_linker_delete = dlsym (cookie, "wasmtime_component_linker_delete");

  if (wasmtime_component_new == NULL || wasmtime_wasip2_config_new == NULL || wasmtime_wasip2_config_inherit_stdin == NULL
      || wasmtime_wasip2_config_inherit_stdout == NULL || wasmtime_wasip2_config_inherit_stderr == NULL
      || wasmtime_context_set_wasip2 == NULL || wasmtime_component_linker_new == NULL || wasmtime_component_linker_add_wasip2 == NULL
      || wasmtime_component_linker_instantiate == NULL || wasmtime_component_get_export_index == NULL
      || wasmtime_component_instance_get_func == NULL || wasmtime_component_func_call == NULL || wasmtime_component_export_index_delete == NULL
      || wasmtime_component_delete == NULL || wasmtime_component_linker_delete == NULL)
    error (EXIT_FAILURE, 0, "could not find symbol in `libwasmtime.so");

  fprintf (stderr, "Loading component ...\n");
  wasmtime_component_t *component = NULL;
  err = wasmtime_component_new (engine, (uint8_t *) wasm.data, wasm.size, &component);
  if (! component || err != NULL)
    {
      wasmtime_error_message (err, &error_message);
      wasmtime_error_delete (err);
      error (EXIT_FAILURE, 0, "failed to compile component: %.*s", (int) error_message.size, error_message.data);
    }

  wasm_byte_vec_delete (&wasm);

  fprintf (stderr, "creating config\n");
  wasmtime_wasip2_config_t *wasi_config = wasmtime_wasip2_config_new ();
  assert (wasi_config != NULL);

  wasmtime_wasip2_config_inherit_stdin (wasi_config);
  wasmtime_wasip2_config_inherit_stdout (wasi_config);
  wasmtime_wasip2_config_inherit_stderr (wasi_config);
  // TODO: argv
  wasmtime_context_set_wasip2 (context, wasi_config);

  fprintf (stderr, "creating linker\n");
  wasmtime_component_linker_t *comp_linker = wasmtime_component_linker_new (engine);
  err = wasmtime_component_linker_add_wasip2 (comp_linker);
  if (err != NULL)
    {
      wasmtime_error_message (err, &error_message);
      wasmtime_error_delete (err);
      error (EXIT_FAILURE, 0, "failed to add WASIp2 to component: %.*s", (int) error_message.size, error_message.data);
    }

  // Init Component with linker
  wasmtime_component_instance_t component_inst = {};
  fprintf (stderr, "init component\n");
  err = wasmtime_component_linker_instantiate (comp_linker, context, component, &component_inst);
  if (err != NULL)
    {
      wasmtime_error_message (err, &error_message);
      wasmtime_error_delete (err);
      error (EXIT_FAILURE, 0, "failed to instantiate component: %.*s", (int) error_message.size, error_message.data);
    }

  // Get the run func
  fprintf (stderr, "get func index?!\n");
  wasmtime_component_export_index_t *run_world_index = wasmtime_component_instance_get_export_index (
      &component_inst,
      context,
      NULL,
      "wasi:cli/run@0.2.0",
      strlen ("wasi:cli/run@0.2.0"));
  assert (run_world_index != NULL);
  wasmtime_component_export_index_t *run_func_index = wasmtime_component_instance_get_export_index (
      &component_inst,
      context,
      run_world_index,
      "run",
      strlen ("run"));
  assert (run_func_index != NULL);

  fprintf (stderr, "get func itself\n");
  wasmtime_component_func_t run_func = {};
  bool found_run = wasmtime_component_instance_get_func (&component_inst, context, run_func_index, &run_func);
  if (! found_run)
    {
      error (EXIT_FAILURE, 0, "could not get func");
    }

  // Call the func
  fprintf (stderr, "call func\n");
  wasmtime_component_val_t result = {};
  err = wasmtime_component_func_call (&run_func, context, NULL, 0, &result, 1);
  if (err != NULL)
    {
      wasmtime_error_message (err, &error_message);
      wasmtime_error_delete (err);
      error (EXIT_FAILURE, 0, "could not call run export: %.*s", (int) error_message.size, error_message.data);
    }

  // Cleanup
  fprintf (stderr, "cleanup\n");
  wasmtime_component_export_index_delete (run_func_index);
  wasmtime_component_export_index_delete (run_world_index);
  wasmtime_component_delete (component);
  wasmtime_component_linker_delete (comp_linker);
  wasmtime_store_delete (store);
  wasm_engine_delete (engine);

  exit (EXIT_SUCCESS);
}

static int
libwasmtime_load (void **cookie, libcrun_error_t *err)
{
  void *handle;

  handle = dlopen ("libwasmtime.so", RTLD_NOW);
  if (handle == NULL)
    return crun_make_error (err, 0, "could not load `libwasmtime.so`: %s", dlerror ());
  *cookie = handle;

  return 0;
}

static int
libwasmtime_unload (void *cookie, libcrun_error_t *err)
{
  int r;

  if (cookie)
    {
      r = dlclose (cookie);
      if (UNLIKELY (r < 0))
        return crun_make_error (err, 0, "could not unload handle: %s", dlerror ());
    }
  return 0;
}

static int
libwasmtime_can_handle_container (libcrun_container_t *container, libcrun_error_t *err)
{
  return wasm_can_handle_container (container, err);
}

struct custom_handler_s handler_wasmtime = {
  .name = "wasmtime",
  .alias = "wasm",
  .feature_string = "WASM:wasmtime:t4chib4ne",
  .load = libwasmtime_load,
  .unload = libwasmtime_unload,
  .run_func = libwasmtime_exec,
  .can_handle_container = libwasmtime_can_handle_container,
};

#endif
