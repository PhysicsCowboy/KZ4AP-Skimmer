// JSON forms of the bank decoder's configuration and channel result, as the prototype writes them
// (training/kz4ap_proto: ChannelResult.to_json, runner.decode's decoded files), so that stage 1's Python tooling
// reads the C++ replay's files unchanged. Bench only: the engine library does not link nlohmann/json.
//
// Objects are nlohmann::ordered_json (insertion order, as Python dicts), and py_dumps serializes them byte for
// byte as Python's json.dumps does with its defaults: separators ", " and ": ", ASCII only, floats as repr()
// (the shortest round-trip digits), NaN and Infinity as Python writes them.
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/channel.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace kz4ap::bench {

using ordered_json = nlohmann::ordered_json;

// Python's round(x, ndigits) for a float: the double nearest the decimal rounding of x's exact value to
// ndigits places (ties to even); NaN and infinities unchanged.
double py_round(double x, int ndigits);

// Python's repr() of a float: shortest round-trip digits, positional for 1e-4 <= |x| < 1e16 (with ".0" for an
// integral value), else d.ddde+XX; "nan", "inf", "-inf" (json writes NaN, Infinity, -Infinity instead).
std::string py_float_repr(double x);

// Python's json.dumps(value) with the default arguments.
std::string py_dumps(const ordered_json& value);

// The prototype's ChannelResult.to_json(): times rounded to 4 decimals (s), T and windows to 6 (s; null for
// NaN), confidences and scores to 4; corrections unrounded (dataclasses.asdict: t_s, from_s, reach_s, old,
// new, reason; the port's from_index is not part of it).
ordered_json to_json(const bank::ChannelResult& result);

// The configuration as runner.decode stores it (dataclasses.asdict(ProtoConfig), every field in order).
ordered_json to_json(const bank::BankConfig& cfg);

// Sets one field by its ProtoConfig name from a JSON value (numbers, strings, lists of numbers); throws
// std::invalid_argument for an unknown name or a value of the wrong kind.
void set_config_value(bank::BankConfig& cfg, const std::string& name, const ordered_json& value);

// A BankConfig from defaults with every field of the object j set (the "config" of a decoded file, or the
// --set values); throws std::invalid_argument as set_config_value.
bank::BankConfig bank_config_from_json(const ordered_json& j);

}  // namespace kz4ap::bench
