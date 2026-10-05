#pragma once
#include "swing.hpp"
#include <cstdint>

namespace spidy::native_rays {
// Available only during the verified native collision callback. Never retain
// this object, its world, or its transient filter context beyond that callback.
class QueryContext : public WorldQueries {
  public:
    virtual uint64_t identity() const = 0;
    virtual uint32_t error() const = 0;
};
using Visitor = void (*)(const QueryContext&);
void setVisitor(Visitor);
} // namespace spidy::native_rays
