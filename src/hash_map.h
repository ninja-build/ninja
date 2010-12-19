#include <ext/hash_map>

using __gnu_cxx::hash_map;

namespace __gnu_cxx {
template<>
struct hash<std::string> {
  size_t operator()(const std::string& s) const {
    return hash<const char*>()(s.c_str());
  }
};
}
