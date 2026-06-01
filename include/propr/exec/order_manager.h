#pragma once

#include <string>

#include "propr/account/account.h"
#include "propr/app/state_machine.h"
#include "propr/core/clock.h"
#include "propr/exec/order_executor.h"
#include "propr/persist/journal.h"
#include "propr/schemas/v1.h"

namespace propr::exec {

// Verifies, journals, and submits OrderCommandV1 values. The RiskEngine has already
// decided; this layer enforces ONLY:
//   (a) the HMAC on the command matches our session secret,
//   (b) the command has not expired,
//   (c) the state machine still allows trading.
//
// All three rejections are journaled. Network errors do not consume the command -
// the journal entry stays "issued" so the next startup can retry with the same
// orderGroupId / intentId values (Propr deduplicates server-side).
class OrderManager {
 public:
  OrderManager(OrderExecutor& executor,
               persist::Journal& journal,
               const app::StateMachine& sm,
               const core::Clock& clock,
               std::string hmac_secret);

  schemas::v1::ExecutionReportV1 execute(const schemas::v1::OrderCommandV1& cmd);

  // Flatten everything we know about. Each executor call is journaled.
  // Used by the kill-switch path; safe to call from any state >= LIVE.
  schemas::v1::ExecutionReportV1 flatten_all(const account::Account& account);

  // After process restart: walk the journal and mark any "issued"-but-unresolved
  // commands as "recovered". The next reconcile pass will reflect the real state.
  void retry_unresolved();

 private:
  void journal_command_(const schemas::v1::OrderCommandV1& cmd, const char* status);
  void journal_report_(const schemas::v1::ExecutionReportV1& rep);

  OrderExecutor& executor_;
  persist::Journal& journal_;
  const app::StateMachine& sm_;
  const core::Clock& clock_;
  std::string hmac_secret_;
};

}  // namespace propr::exec
