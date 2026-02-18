#ifndef FTXUI_UTIL_SMART_PTR_HPP
#define FTXUI_UTIL_SMART_PTR_HPP

#include <utility>  // for forward

#ifdef FTXUI_USE_LOCAL_SHARED_PTR

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>

namespace ftxui {

/// @brief Non-thread-safe shared pointer (avoids atomic ref-count overhead).
template <typename T>
using shared_ptr = boost::local_shared_ptr<T>;

/// @brief Factory matching the configured shared_ptr type.
template <typename T, typename... Args>
inline shared_ptr<T> make_shared(Args&&... args) {
  return boost::make_local_shared<T>(std::forward<Args>(args)...);
}

}  // namespace ftxui

#else  // !FTXUI_USE_LOCAL_SHARED_PTR

#include <memory>  // for shared_ptr, make_shared

namespace ftxui {

/// @brief Standard thread-safe shared pointer.
template <typename T>
using shared_ptr = std::shared_ptr<T>;

/// @brief Factory matching the configured shared_ptr type.
template <typename T, typename... Args>
inline shared_ptr<T> make_shared(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

}  // namespace ftxui

#endif  // FTXUI_USE_LOCAL_SHARED_PTR

#endif  // FTXUI_UTIL_SMART_PTR_HPP
