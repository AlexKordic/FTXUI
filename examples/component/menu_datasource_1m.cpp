// Copyright 2024 Alex Kordic. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/screen_interactive.hpp"

std::vector<std::string> const phrases = {
    "May you do good and not evil.",
    "May you find forgiveness for yourself and forgive others.",
    "May you share freely, never taking more than you give.",
    "Coroutines can be viewed as a language-level construct providing a "
    "special kind of control flow.",
    "In contrast to threads, which are pre-emptive, coroutine switches are "
    "cooperative (programmer controls when a switch will happen). The kernel "
    "is not involved in the coroutine switches.",
    "In computer science routines are defined as a sequence of operations. The "
    "execution of routines forms a parent-child relationship and the child "
    "terminates always before the parent. Coroutines (the term was introduced "
    "by Melvin Conway [1]), are a generalization of routines (Donald Knuth "
    "[2]. The principal difference between coroutines and routines is that a "
    "coroutine enables explicit suspend and resume of its progress via "
    "additional operations by preserving execution state and thus provides an "
    "enhanced control flow (maintaining the execution context).",
    "Characteristics [3] of a coroutine are:\n- values of local data persist "
    "between successive calls (context switches)\n- execution is suspended as "
    "control leaves coroutine and is resumed at certain time later\n- "
    "symmetric or asymmetric control-transfer mechanism; see below\n- "
    "first-class object (can be passed as argument, returned by procedures, "
    "stored in a data structure to be used later or freely manipulated by the "
    "developer)\n- stackful or stackless"};

std::string random_phrase() {
  return phrases[std::rand() % phrases.size()];
}

struct File {
  std::string name;
  int size;

  static File random_data(int index) {
    auto name = " [" + std::to_string(index) + "] " + random_phrase();
    int size = std::rand() % 1000000;
    return File{std::move(name), size};
  }
};

int main() {
  using namespace ftxui;
  auto screen = ScreenInteractive::Fullscreen();

  // Prepare our large data set:
  int item_count = 1000000;
  std::vector<File> items;
  std::vector<std::string> old_items;
  // populate large number of data items
  items.reserve(item_count);
  for (int i = 0; i < item_count; i++) {
    File item = File::random_data(i);
    old_items.push_back(item.name);
    items.push_back(std::move(item));
  }

  // UI interface to get info from our data set
  DataSource data_source;
  // Note: Focused index is stored in data_source.focused_id
  // Note: When mouse moves over DBMenu item the data_source.hovered_id points
  // to that item

  data_source.dataset_size = [&items]() -> DataSize { return {int64_t(items.size()), 0, int64_t(items.size() - 1)}; };
  data_source.count_items_before = [&items](int64_t from_id) -> int64_t {
    // easy because our id's are indexes in items vector
    return from_id;
  };
  data_source.move_id_by = [&items](int64_t& from, int64_t offset) -> bool {
    // easy because our id's are indexes in items vector
    const int64_t initial = from;
    const int64_t size = items.size();
    from = std::max(0LL, std::min(from + offset, size - 1));
    // return false when offset would go out of bounds.
    return from != initial;
  };
  // transform is called component-height number of times in one render cycle.
  data_source.transform = [&items](DSRenderContext& c) -> Element {
    // Note: We access row text in transform callback. DBMenu doesn't access our
    // data.
    const File& file = items.at(c.id);
    Element el = hbox({text(file.name) | xflex_grow, separator(),
                       text(std::to_string(file.size))});
    if (c.focused) {
      if (c.component_focused) {
        el |= bgcolor(Color::Green);
      } else {
        el |= bgcolor(Color::GrayDark);
      }
    }
    if (c.hovered) {
      el |= inverted;
    }
    return el;
  };
  // Create our 1M long menu
  Component dynamic_menu = DBMenu(&data_source);

  // Create regular menu for comparisson
  MenuOption option;
  option.entries = &old_items;
  option.on_enter = screen.ExitLoopClosure();
  Component old_menu = Menu(option);

  // // 4ms - 12ms render time on m1-mac
  Component implementation = dynamic_menu;
  // // ~14833ms render time on m1-mac
  // Component implementation = old_menu;

  // Add render time as a title to our menu:
  Component app = Renderer(implementation, [implementation]() -> Element {
    double render_time = ScreenInteractive::Active()->LastFrameTime();
    return window(text(" Render time: " +
                       std::to_string(int(render_time * 1000.0)) + "ms ") |
                      hcenter,
                  implementation->Render());
  });
  screen.Loop(app);
}
