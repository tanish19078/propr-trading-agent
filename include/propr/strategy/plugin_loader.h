#pragma once

#include <memory>
#include <string>
#include <vector>

#include "propr/strategy/strategy.h"

namespace propr::strategy {

// dlopen/LoadLibrary wrapper. On SIGHUP, App calls reload() to swap in updated .so.
class PluginLoader {
 public:
  struct Loaded {
    std::string plugin_path;
    std::string params_path;
    std::string name;
    Strategy* strategy{nullptr};  // owned by the .so
    void* handle{nullptr};
  };

  PluginLoader() = default;
  ~PluginLoader();

  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;

  // Load a single strategy. Returns true on success; logs and returns false on failure.
  bool load(const std::string& plugin_path, const std::string& params_path);

  // Unload all loaded plugins (calls destroy_strategy and dlclose).
  void unload_all();

  // Atomic reload: unload_all() then load(...) for every previous Loaded.
  void reload();

  const std::vector<Loaded>& loaded() const { return loaded_; }
  std::vector<Loaded>& mutable_loaded() { return loaded_; }

 private:
  std::vector<Loaded> loaded_;
};

}  // namespace propr::strategy
