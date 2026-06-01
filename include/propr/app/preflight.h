#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace propr::app {

// Ordered green-check list. Bootstrap MUST run this before transitioning
// RECONCILING -> LIVE. First failing gate is the failure mode; we do not continue.
class Preflight {
 public:
  struct Gate {
    std::string name;
    std::function<bool()> check;
    std::function<std::string()> diagnose;  // optional: extra detail on failure
  };

  void add(std::string name,
           std::function<bool()> check,
           std::function<std::string()> diagnose = nullptr);

  struct Failure {
    std::string gate;
    std::string detail;
  };

  // Runs every gate in order. Returns nullopt on success.
  // Stops at the first failure and returns its name + diagnostic.
  std::optional<Failure> run() const;

  const std::vector<Gate>& gates() const { return gates_; }

 private:
  std::vector<Gate> gates_;
};

}  // namespace propr::app
