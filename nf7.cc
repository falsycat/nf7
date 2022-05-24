#include "nf7.hh"

#include <algorithm>
#include <cassert>
#include <map>

#include <imgui.h>
#include <yas/serialize.hpp>
#include <yas/types/std/string.hpp>
#include <yas/types/std/vector.hpp>


using namespace std::literals;


namespace nf7 {

static std::vector<Env*> env_stack_;

// static variable in function ensures that entity is initialized before using
static inline auto& registry_() noexcept {
  static std::map<std::string, const File::TypeInfo*> registry_;
  return registry_;
}


void Exception::UpdatePanic() const noexcept {
  ImGui::TextUnformatted(msg_.c_str());
  ImGui::Indent();
  ImGui::Text("from %s:%d", srcloc_.file_name(), srcloc_.line());
  ImGui::Unindent();
}

const std::map<std::string, const File::TypeInfo*>& File::registry() noexcept {
  return registry_();
}
const File::TypeInfo& File::registry(std::string_view name) {
  const auto  sname = std::string(name);
  const auto& reg   = registry_();

  auto itr = reg.find(sname);
  if (itr == reg.end()) throw NotFoundException("missing type: "+sname);
  return *itr->second;
}
File::File(const TypeInfo& t, Env& env) noexcept : type_(&t), env_(&env) {
}
File::~File() noexcept {
  assert(id_ == 0);
}
void File::MoveUnder(File& parent, std::string_view name) noexcept {
  assert(parent_ == nullptr);
  assert(id_ == 0);
  assert(name_.empty());
  parent_ = &parent;
  name_   = name;
  id_     = env_->AddFile(*this);

  Handle({ .id = id_, .type = File::Event::kAdd });
}
void File::MakeAsRoot() noexcept {
  assert(parent_ == nullptr);
  assert(id_ == 0);
  assert(name_.empty());
  id_   = env_->AddFile(*this);
  name_ = "$";

  Handle({ .id = id_, .type = File::Event::kAdd });
}
void File::Isolate() noexcept {
  assert(id_ != 0);
  const auto pid = id_;

  env_->RemoveFile(id_);
  id_     = 0;
  parent_ = nullptr;
  name_   = "";

  Handle({ .id = pid, .type = File::Event::kRemove });
}
File& File::FindOrThrow(std::string_view name) const {
  if (auto ret = Find(name)) return *ret;
  throw NotFoundException("missing child: "+std::string(name));
}
File& File::ResolveOrThrow(const Path& p) const
try {
  assert(id_ != 0);

  auto ret = const_cast<File*>(this);
  for (const auto& term : p.terms()) {
    if (term == "..") {
      if (!parent_) throw NotFoundException("cannot go up over the root");
      ret = parent_;
    } else if (term == ".") {
      // do nothing
    } else {
      ret = &ret->FindOrThrow(term);
    }
  }
  return *ret;
} catch (Exception&) {
  throw NotFoundException("failed to resolve path: "+p.Stringify());
}
File& File::ResolveOrThrow(std::string_view p) const {
  return ResolveOrThrow(Path::Parse(p));
}
File& File::ResolveUpwardOrThrow(const Path& p) const {
  auto f = parent_;
  while (f)
  try {
    return f->ResolveOrThrow(p);
  } catch (NotFoundException&) {
    f = f->parent_;
  }
  throw NotFoundException("failed to resolve upward path: "+p.Stringify());
}
File& File::ResolveUpwardOrThrow(std::string_view p) const {
  return ResolveUpwardOrThrow(Path::Parse(p));
}
File::Interface& File::ifaceOrThrow(const std::type_info& t) {
  if (auto ret = iface(t)) return *ret;
  throw NotImplementedException(t.name()+"is not implemented"s);
}
File::Path File::abspath() const noexcept {
  std::vector<std::string> terms;
  auto f = const_cast<File*>(this);
  while (f) {
    terms.push_back(f->name());
    f = f->parent();
  }
  std::reverse(terms.begin(), terms.end());
  return {std::move(terms)};
}

File::TypeInfo::TypeInfo(const std::string&                cat,
                         const std::string&                name,
                         std::unordered_set<std::string>&& flags) noexcept :
    cat_(cat), name_(name), flags_(std::move(flags)) {
  auto& reg = registry_();
  auto [itr, uniq] = reg.emplace(std::string(name_), this);
  assert(uniq);
}
File::TypeInfo::~TypeInfo() noexcept {
  auto& reg = registry_();
  reg.erase(std::string(name_));
}

File::Path::Path(Deserializer& ar) {
  ar(terms_);
  Validate();
}
void File::Path::Serialize(Serializer& ar) const noexcept {
  ar(terms_);
}
File::Path File::Path::Parse(std::string_view p) {
  Path ret;

  auto st = p.begin(), itr = st;
  for (; itr < p.end(); ++itr) {
    if (*itr == '/') {
      if (st < itr) ret.terms_.push_back(std::string {st, itr});
      st = itr + 1;
    }
  }
  if (st < itr) ret.terms_.push_back(std::string {st, itr});

  ret.Validate();
  return ret;
}
std::string File::Path::Stringify() const noexcept {
  std::string ret;
  for (const auto& term : terms_) {
    ret += term + "/";
  }
  if (ret.size() > 0) ret.erase(ret.end()-1);
  return ret;
}
void File::Path::ValidateTerm(std::string_view term) {
  if (term.empty()) {
    throw Exception("empty term");
  }

  constexpr size_t kMaxTermSize = 256;
  if (term.size() > kMaxTermSize) {
    throw Exception("too long term (must be less than 256)");
  }

  static const char kAllowedChars[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789_";
  if (term.find_first_not_of(kAllowedChars) != std::string_view::npos) {
    throw Exception("invalid char found in term");
  }
}
void File::Path::Validate() const {
  for (const auto& term : terms_) ValidateTerm(term);
}

Context::Context(Env& env, File::Id initiator, Context::Id parent) noexcept :
    env_(&env), initiator_(initiator), id_(env_->AddContext(*this)), parent_(parent) {
}
Context::~Context() noexcept {
  env_->RemoveContext(id_);
}

void Env::Push(Env& env) noexcept {
  env_stack_.push_back(&env);
}
Env& Env::Peek() noexcept {
  return *env_stack_.back();
}
void Env::Pop() noexcept {
  env_stack_.pop_back();
}

Env::Watcher::Watcher(Env& env) noexcept : env_(&env) {
}
Env::Watcher::~Watcher() noexcept {
  env_->RemoveWatcher(*this);
}
void Env::Watcher::Watch(File::Id id) noexcept {
  env_->AddWatcher(id, *this);
}

}  // namespace nf7
