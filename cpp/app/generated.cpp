#include "generated.h"

GeneratedRegistry& GeneratedRegistry::instance() {
  static GeneratedRegistry r;
  return r;
}

void GeneratedRegistry::add(std::unique_ptr<GeneratedHandler> h) {
  for (auto& x : handlers_)
    if (x->kind() == h->kind()) { x = std::move(h); return; }
  handlers_.push_back(std::move(h));
}

const GeneratedHandler* GeneratedRegistry::find(const ElementPtrs& members) const {
  for (const auto& h : handlers_)
    if (h->claims(members)) return h.get();
  return nullptr;
}

const GeneratedHandler* GeneratedRegistry::ofKind(const QString& kind) const {
  for (const auto& h : handlers_)
    if (h->kind() == kind) return h.get();
  return nullptr;
}
