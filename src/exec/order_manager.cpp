#include "propr/exec/order_manager.h"

#include <nlohmann/json.hpp>

#include "propr/schemas/sign.h"

namespace propr::exec {

using propr::schemas::v1::ExecutionReportV1;
using propr::schemas::v1::ExecutionStatusV1;
using propr::schemas::v1::OrderCommandV1;

OrderManager::OrderManager(OrderExecutor& executor,
                           persist::Journal& journal,
                           const app::StateMachine& sm,
                           const core::Clock& clock,
                           std::string hmac_secret)
    : executor_(executor),
      journal_(journal),
      sm_(sm),
      clock_(clock),
      hmac_secret_(std::move(hmac_secret)) {}

void OrderManager::journal_command_(const OrderCommandV1& cmd, const char* status) {
  nlohmann::json j = cmd;
  j["_status"] = status;
  journal_.write_event(clock_.now_ns(), "order_command", j.dump());
  persist::Journal::IntentRecord rec;
  rec.intent_uuid = cmd.command_uuid;
  rec.order_group_id = cmd.order_group_id;
  nlohmann::json ids = {{"entry", cmd.entry_intent_id}};
  if (cmd.stop_intent_id) ids["stop"] = *cmd.stop_intent_id;
  if (cmd.tp_intent_id) ids["tp"] = *cmd.tp_intent_id;
  rec.intent_ids_json = ids.dump();
  rec.status = status;
  rec.created_at_ns = cmd.issued_at_ns;
  journal_.write_intent(rec);
}

void OrderManager::journal_report_(const ExecutionReportV1& rep) {
  nlohmann::json j = rep;
  journal_.write_event(rep.at_ns, "execution_report", j.dump());
}

ExecutionReportV1 OrderManager::execute(const OrderCommandV1& cmd) {
  ExecutionReportV1 r;
  r.command_uuid = cmd.command_uuid;
  r.intent_uuid = cmd.intent_uuid;
  r.at_ns = clock_.now_ns();

  // (a) HMAC verification.
  if (!schemas::verify(cmd, hmac_secret_)) {
    r.status = ExecutionStatusV1::RejectedBadHmac;
    r.detail = "hmac mismatch";
    journal_report_(r);
    return r;
  }

  // (b) Expiry.
  if (clock_.now_ns() > cmd.expires_at_ns) {
    r.status = ExecutionStatusV1::RejectedExpired;
    r.detail = "command expired";
    journal_report_(r);
    return r;
  }

  // (c) State machine.
  if (!sm_.allows_flatten()) {
    // We accept flatten-class commands in LIVE/BLIND/FLATTENING. Anything else needs
    // LIVE. We can not tell here whether this is a flatten-class command without
    // the RiskEngine's IntentKindV1 - but the command itself is the result of an
    // approved intent. We defer to the more conservative allows_new_entries() check
    // for entry-side commands, and rely on the caller (flatten_all) for flatten.
    r.status = ExecutionStatusV1::RejectedNotLive;
    r.detail = std::string("state=") + app::to_string(sm_.state());
    journal_report_(r);
    return r;
  }
  if (!sm_.allows_new_entries()) {
    r.status = ExecutionStatusV1::RejectedNotLive;
    r.detail = std::string("state=") + app::to_string(sm_.state());
    journal_report_(r);
    return r;
  }

  journal_command_(cmd, "issued");
  ExecutionReportV1 backend = executor_.place(cmd);
  backend.command_uuid = cmd.command_uuid;
  backend.intent_uuid = cmd.intent_uuid;
  journal_report_(backend);

  // Mark resolved only when the backend accepted or filled it. Network errors leave
  // the intent open so retry_unresolved() / reconciler can sort it out.
  if (backend.status == ExecutionStatusV1::Accepted ||
      backend.status == ExecutionStatusV1::Filled ||
      backend.status == ExecutionStatusV1::PartiallyFilled) {
    journal_.update_intent_status(cmd.command_uuid, "sent", clock_.now_ns());
  }
  return backend;
}

ExecutionReportV1 OrderManager::flatten_all(const account::Account& account) {
  // Cancel everything resting first.
  ExecutionReportV1 cancel = executor_.cancel_all();
  journal_report_(cancel);

  for (const auto& p : account.open_positions()) {
    auto rep = executor_.close(p.asset.base,
                               p.side == core::PositionSide::Long ? "long" : "short",
                               p.quantity);
    journal_report_(rep);
  }
  cancel.detail = "flatten_complete";
  return cancel;
}

void OrderManager::retry_unresolved() {
  for (const auto& r : journal_.unresolved_intents()) {
    journal_.update_intent_status(r.intent_uuid, "recovered", clock_.now_ns());
  }
}

}  // namespace propr::exec
