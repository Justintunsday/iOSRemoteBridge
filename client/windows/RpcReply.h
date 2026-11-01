#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>

inline bool responseSucceeded(const nlohmann::json &reply) {
    const auto &value=reply.at("ok");
    if(value.is_boolean()) return value.get<bool>();
    // Packages through 0049e94 boxed !error as a C int. Accept only their
    // exact 0/1 representation so an action reply does not end the session.
    if(value.is_number_integer()&&(value==0||value==1)) return value==1;
    throw std::runtime_error("invalid RPC success flag");
}
