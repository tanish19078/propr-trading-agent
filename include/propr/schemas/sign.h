#pragma once

#include <string>
#include <string_view>

#include "propr/schemas/v1.h"

namespace propr::schemas {

// Canonical encoding used for HMAC signing. Format is stable across releases - DO NOT
// reorder fields without bumping the schema version. The hmac_hex field is excluded
// from the canonical form (otherwise it would chase its own tail).
std::string canonical_for_hmac(const v1::OrderCommandV1& c);

// Compute and set the HMAC on a command. Returns the populated command.
v1::OrderCommandV1 sign(const v1::OrderCommandV1& cmd, std::string_view secret);

// Verify a command's HMAC. Returns true iff the recomputed HMAC matches.
bool verify(const v1::OrderCommandV1& cmd, std::string_view secret);

}  // namespace propr::schemas
