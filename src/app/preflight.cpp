#include "propr/app/preflight.h"

namespace propr::app {

void Preflight::add(std::string name,
                    std::function<bool()> check,
                    std::function<std::string()> diagnose) {
  gates_.push_back({std::move(name), std::move(check), std::move(diagnose)});
}

std::optional<Preflight::Failure> Preflight::run() const {
  for (const auto& g : gates_) {
    if (!g.check()) {
      Failure f;
      f.gate = g.name;
      f.detail = g.diagnose ? g.diagnose() : std::string{};
      return f;
    }
  }
  return std::nullopt;
}

}  // namespace propr::app
