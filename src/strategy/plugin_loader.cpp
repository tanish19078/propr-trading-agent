#include "propr/strategy/plugin_loader.h"

#include "propr/log/logger.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace propr::strategy {

namespace {

void* dyn_open(const char* path, std::string& err_out) {
#if defined(_WIN32)
  HMODULE h = LoadLibraryA(path);
  if (!h) {
    char buf[256] = {0};
    DWORD ec = GetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, ec, 0, buf, sizeof(buf), nullptr);
    err_out = buf;
  }
  return reinterpret_cast<void*>(h);
#else
  ::dlerror();
  void* h = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    const char* e = ::dlerror();
    err_out = e ? e : "dlopen failed";
  }
  return h;
#endif
}

void* dyn_sym(void* handle, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
  return ::dlsym(handle, name);
#endif
}

void dyn_close(void* handle) {
  if (!handle) return;
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  ::dlclose(handle);
#endif
}

}  // namespace

PluginLoader::~PluginLoader() { unload_all(); }

namespace {

// Append the platform-specific shared-library extension if the path doesn't
// already end with one. Lets configs use a bare "build/strategies/range_mr/range_mr"
// and have it resolve to .dll on Windows, .so on Linux.
std::string with_platform_ext(const std::string& path) {
  auto ends_with = [&](std::string_view s) {
    return path.size() >= s.size() &&
           path.compare(path.size() - s.size(), s.size(), s) == 0;
  };
  if (ends_with(".so") || ends_with(".dll") || ends_with(".dylib")) return path;
#if defined(_WIN32)
  return path + ".dll";
#elif defined(__APPLE__)
  return path + ".dylib";
#else
  return path + ".so";
#endif
}

}  // namespace

bool PluginLoader::load(const std::string& plugin_path, const std::string& params_path) {
  const std::string resolved = with_platform_ext(plugin_path);
  std::string err;
  void* handle = dyn_open(resolved.c_str(), err);
  if (!handle) {
    PROPR_LOG_ERROR(std::string{R"({"plugin_dlopen_failed":")"} + resolved +
                    R"(","error":")" + err + R"("})");
    return false;
  }
  auto create = reinterpret_cast<Strategy* (*)()>(dyn_sym(handle, "create_strategy"));
  auto destroy = reinterpret_cast<void (*)(Strategy*)>(dyn_sym(handle, "destroy_strategy"));
  if (!create || !destroy) {
    PROPR_LOG_ERROR(R"({"plugin_symbol_missing":"create_strategy or destroy_strategy"})");
    dyn_close(handle);
    return false;
  }
  Strategy* s = create();
  if (!s || !s->on_init(params_path)) {
    PROPR_LOG_ERROR(R"({"plugin_init_failed":true})");
    if (s) destroy(s);
    dyn_close(handle);
    return false;
  }
  loaded_.push_back({resolved, params_path, s->name(), s, handle});
  PROPR_LOG_INFO(std::string{R"({"plugin_loaded":")"} + resolved + R"("})");
  return true;
}

void PluginLoader::unload_all() {
  for (auto& l : loaded_) {
    if (l.strategy) {
      l.strategy->on_shutdown();
      auto destroy = reinterpret_cast<void (*)(Strategy*)>(
          dyn_sym(l.handle, "destroy_strategy"));
      if (destroy) destroy(l.strategy);
    }
    dyn_close(l.handle);
  }
  loaded_.clear();
}

void PluginLoader::reload() {
  std::vector<Loaded> manifest = loaded_;
  unload_all();
  for (const auto& e : manifest) {
    load(e.plugin_path, e.params_path);
  }
}

}  // namespace propr::strategy
