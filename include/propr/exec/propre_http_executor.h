#pragma once

#include "propr/account/account.h"
#include "propr/core/clock.h"
#include "propr/core/ulid.h"
#include "propr/exec/order_executor.h"
#include "propr/net/http_client.h"

namespace propr::exec {

// Real executor: builds the Propr REST payload and sends it. No risk logic here
// (that all lives in RiskEngine). No retry logic either - retries are driven by
// the OrderManager replaying unresolved commands at startup with the same IDs.
class PropreHttpExecutor final : public OrderExecutor {
 public:
  PropreHttpExecutor(net::HttpClient& http,
                     const account::Account& account,
                     core::Ulid& ulid,
                     const core::Clock& clock)
      : http_(http), account_(account), ulid_(ulid), clock_(clock) {}

  schemas::v1::ExecutionReportV1 place(const schemas::v1::OrderCommandV1& cmd) override;
  schemas::v1::ExecutionReportV1 cancel_all() override;
  schemas::v1::ExecutionReportV1 close(const std::string& asset_base,
                                        const std::string& position_side,
                                        core::Qty quantity_nano) override;

 private:
  net::HttpClient& http_;
  const account::Account& account_;
  core::Ulid& ulid_;
  const core::Clock& clock_;
};

}  // namespace propr::exec
