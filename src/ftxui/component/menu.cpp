// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>                // for max, fill_n, reverse
#include <chrono>                   // for milliseconds
#include <ftxui/dom/direction.hpp>  // for Direction, Direction::Down, Direction::Left, Direction::Right, Direction::Up
#include <functional>               // for function
#include <string>                   // for operator+, string
#include <utility>                  // for move
#include <vector>                   // for vector, __alloc_traits<>::value_type

#include "ftxui/component/animation.hpp"  // for Animator, Linear
#include "ftxui/component/component.hpp"  // for Make, Menu, MenuEntry, Toggle
#include "ftxui/component/component_base.hpp"     // for ComponentBase
#include "ftxui/component/component_options.hpp"  // for MenuOption, MenuEntryOption, UnderlineOption, AnimatedColorOption, AnimatedColorsOption, EntryState
#include "ftxui/component/event.hpp"  // for Event, Event::ArrowDown, Event::ArrowLeft, Event::ArrowRight, Event::ArrowUp, Event::End, Event::Home, Event::PageDown, Event::PageUp, Event::Return, Event::Tab, Event::TabReverse
#include "ftxui/component/mouse.hpp"  // for Mouse, Mouse::Left, Mouse::Released, Mouse::WheelDown, Mouse::WheelUp, Mouse::None
#include "ftxui/component/screen_interactive.hpp"  // for Component
#include "ftxui/dom/elements.hpp"  // for operator|, Element, reflect, Decorator, nothing, Elements, bgcolor, color, hbox, separatorHSelector, separatorVSelector, vbox, xflex, yflex, text, bold, focus, inverted, select
#include "ftxui/dom/node_decorator.hpp"  // for NodeDecorator
#include "ftxui/screen/box.hpp"          // for Box
#include "ftxui/screen/color.hpp"        // for Color
#include "ftxui/screen/util.hpp"         // for clamp
#include "ftxui/util/ref.hpp"  // for Ref, ConstStringListRef, ConstStringRef

namespace ftxui {

namespace {

Element DefaultOptionTransform(const EntryState& state) {
  std::string label = (state.active ? "> " : "  ") + state.label;  // NOLINT
  Element e = text(std::move(label));
  if (state.focused) {
    e = e | inverted;
  }
  if (state.active) {
    e = e | bold;
  }
  return e;
}

bool IsInverted(Direction direction) {
  switch (direction) {
    case Direction::Up:
    case Direction::Left:
      return true;
    case Direction::Down:
    case Direction::Right:
      return false;
  }
  return false;  // NOT_REACHED()
}

bool IsHorizontal(Direction direction) {
  switch (direction) {
    case Direction::Left:
    case Direction::Right:
      return true;
    case Direction::Down:
    case Direction::Up:
      return false;
  }
  return false;  // NOT_REACHED()
}

}  // namespace

struct ValidCount {
  int valid = 0;
  int first_visible = -1;
  int total = 0;
};

using ValidCountCallback = std::function<ValidCount()>;
using CountItemsBefore = std::function<int64_t(int64_t)>;

class DataSourceScrollIndicator : public NodeDecorator {
 private:
  DataSource* _context;
  ValidCountCallback _valid_count_callback;
  CountItemsBefore _count_items_before;

 public:
  // using NodeDecorator::NodeDecorator;
  DataSourceScrollIndicator(Element child,
                            DataSource* context,
                            ValidCountCallback valid_count_callback,
                            CountItemsBefore count_items_before)
      : NodeDecorator(std::move(child)),
        _context(context),
        _valid_count_callback(std::move(valid_count_callback)),
        _count_items_before(std::move(count_items_before)) {}

  void ComputeRequirement() override {
    NodeDecorator::ComputeRequirement();
    requirement_ = children_[0]->requirement();
    requirement_.min_x++;
  }

  void SetBox(Box box) override {
    box_ = box;
    box.x_max--;
    children_[0]->SetBox(box);
  }

  void Render(Screen& screen) final {
    // TODO: visible_portion is not drawn correctly sometimes is blank char ( ),
    //       sometimes is full char (┃) when it must be half
    NodeDecorator::Render(screen);

    // Will draw only on right border of our box.
    // Each pixel allows for half of vertical line: up:╹ full:┃ down:╻
    if (_context->v.items_produced >= _context->v.items_total) {
      // no need for scroll bar
      return;
    }
    // count produced items that have height > 1
    ValidCount valid_count = _valid_count_callback();
    _context->real_start_id =
        _context->estimated_start_id + valid_count.first_visible;
    _context->items_visible = valid_count.valid;
    const int64_t items_before = _count_items_before(_context->real_start_id);

    const float items_total = _context->v.items_total;
    const float widget_height = float(box_.y_max) - box_.y_min + 1;
    const float visible_portion = float(_context->items_visible) / items_total;
    const float start_point = (items_before / items_total) * widget_height;
    const float end_point = start_point + (visible_portion * widget_height);
    const float start_y = box_.y_min + start_point;
    const float end_y = box_.y_min + end_point;

    // determine should we start half line:
    const float firstpixel_start_fraction = start_y - int(start_y);
    if (firstpixel_start_fraction < 0.25) {
      screen.PixelAt(box_.x_max, int(start_y)).character = "┃";
    } else {  // in case of shortest line, let it be half line at the top:
      screen.PixelAt(box_.x_max, int(start_y)).character = "╻";
    }
    if (int(end_y) <= box_.y_max) {
      const float lastpixel_end_fraction = end_y - int(end_y);
      if (lastpixel_end_fraction < 0.25) {
        // Test: Maybe use empty char by not rendering to a pixel
        screen.PixelAt(box_.x_max, int(end_y)).character = " ";
      } else if (lastpixel_end_fraction <
                 0.75) {  // in case of shortest line, let it be half line at
                          // the bottom:
        screen.PixelAt(box_.x_max, int(end_y)).character = "╹";
      } else {
        screen.PixelAt(box_.x_max, int(end_y)).character = "┃";
      }
    }
    int last_full_y = std::min(int(end_y) - 1, box_.y_max);
    for (int y = int(start_y) + 1; y <= last_full_y; ++y) {
      screen.PixelAt(box_.x_max, y).character = "┃";
    }
  }
};

Element filelistScrollIndicator(DataSource* context,
                                Element child,
                                ValidCountCallback valid_count_callback,
                                CountItemsBefore count_items_before) {
  //
  return std::make_shared<DataSourceScrollIndicator>(
      std::move(child), context, std::move(valid_count_callback),
      std::move(count_items_before));
}

Decorator filelist_scroll_indicator(
    DataSource* context,
    const ValidCountCallback valid_count_callback,
    const CountItemsBefore count_items_before) {
  return [context, valid_count_callback, count_items_before](Element child) {
    //
    return filelistScrollIndicator(context, std::move(child),
                                   valid_count_callback, count_items_before);
  };
}

class DataSourceReflect : public Node {
 public:
  DataSourceReflect(Element child, DataSource* context, Box* b)
      : Node(unpack(std::move(child))), _context(context), _box(b) {}

  void ComputeRequirement() final {
    Node::ComputeRequirement();
    requirement_ = children_[0]->requirement();
    requirement_.flex_grow_y = 1;
    requirement_.flex_shrink_y = 1;
    requirement_.min_y = _context->min_y;
  }

  void SetBox(Box box) final {
    *_box = box;
    Node::SetBox(box);
    children_[0]->SetBox(box);
  }

  void Render(Screen& screen) final {
    _context->set_screen_height(screen.dimy());
    *_box = Box::Intersection(screen.stencil, *_box);
    _context->set_component_height(_box->y_max - _box->y_min + 1);
    //
    // Redraw to allow VerticalMenu to produce more Elements
    // This action can cause a cascade of redraws.
    const bool all_items_visible =
        _context->v.items_total == _context->v.items_produced;
    const bool rowcount_larger_than_component =
        _context->v.items_total > _context->v.component_height;
    const bool menu_matched_rowcount =
        _context->v.component_height ==
        _context->v.items_produced;  // should also trigger y-shrink
    if (_context->should_redraw || !all_items_visible &&
                                       rowcount_larger_than_component &&
                                       !menu_matched_rowcount) {
      _context->should_redraw = false;
      _context->invoke_redraw();
    }
    Node::Render(screen);
  }

 private:
  DataSource* _context;
  Box* _box;
};

Decorator datasource_reflect(DataSource* context, Box* b) {
  return [context, b](Element child) -> Element {
    return std::make_shared<DataSourceReflect>(std::move(child), context, b);
  };
}

class VerticalMenu : public ComponentBase {
 public:
  explicit VerticalMenu(DataSource* dataSource) : data_(dataSource) {}

  int64_t find_start_id() {
    int64_t items_placed = 0;
    int64_t start_id = data_->focused_id;
    data_->move_id_by(start_id, -data_->v.component_height / 2);
    int64_t id = start_id;
    while (items_placed < data_->v.component_height) {
      const bool out_of_bounds = !data_->move_id_by(id, 1);
      if (out_of_bounds) {
        items_placed++;  // For reviewers: why increment here?
        // reached end of source list, now prepend items by decrementing
        // start_id
        while (items_placed < data_->v.component_height) {
          const bool out_of_bounds = !data_->move_id_by(start_id, -1);
          if (out_of_bounds) {
            break;
          }
          items_placed++;
        }
        break;
      }
      items_placed++;
    }
    return start_id;
  }

  ValidCount count_valid_boxes() {
    ValidCount valid_count;
    valid_count.total = boxes_.size();
    for (int i = 0; i < boxes_.size(); ++i) {
      auto& b = boxes_[i];
      if (b.y_max >= b.y_min) {
        if (valid_count.first_visible == -1) {
          valid_count.first_visible = i;
        }
        valid_count.valid++;
      }
    }
    return valid_count;
  }

  Element Render() override {
    boxes_.resize(data_->v.component_height);
    data_->v.items_total = data_->dataset_size().total;
    data_->estimated_start_id = find_start_id();

    DSRenderContext row_info;
    row_info.id = data_->estimated_start_id;
    row_info.component_focused = Focused();
    Elements elements;
    if(data_->v.items_total) {
      while (elements.size() < data_->v.component_height) {
        auto box_index = elements.size();
        row_info.focused = (data_->focused_id == row_info.id);
        row_info.hovered = (data_->hovered_id == row_info.id);
        elements.push_back(data_->transform(row_info) |
                          reflect(boxes_[box_index]));
        // Increment loop variables
        if (false == data_->move_id_by(row_info.id, 1)) {
          break;
        }
      }
    }
    data_->v.items_produced = elements.size();
    boxes_.resize(elements.size());
    auto reflect = datasource_reflect(data_, &box_);
    auto get_valid_boxes = [this]() { return count_valid_boxes(); };
    auto get_items_before = [this](int64_t id) {
      return data_->count_items_before(id);
    };
    auto scroll =
        filelist_scroll_indicator(data_, get_valid_boxes, get_items_before);
    return vbox(std::move(elements)) | yframe | std::move(reflect) |
           std::move(scroll);
  }

  bool mouse_wheel(Event event) {
    if (event.mouse().button == Mouse::WheelDown ||
        event.mouse().button == Mouse::WheelUp) {
      if (!box_.Contain(event.mouse().x, event.mouse().y)) {
        return false;
      }
      if (event.mouse().button == Mouse::WheelDown) {
        data_->move_id_by(data_->focused_id, 1);
      } else if (event.mouse().button == Mouse::WheelUp) {
        data_->move_id_by(data_->focused_id, -1);
      }
      return true;
    }
    return false;
  }

  bool mouse_click(Event event) {
    if (event.mouse().button == Mouse::Left) {
      for (int i = 0; i < boxes_.size(); ++i) {
        const bool in_box = boxes_[i].Contain(event.mouse().x, event.mouse().y);
        const bool box_out_of_screen = boxes_[i].y_min > box_.y_max;
        if (box_out_of_screen || !in_box) {
          continue;
        }
        data_->focused_id = data_->estimated_start_id;
        data_->move_id_by(data_->focused_id, i);
        TakeFocus();
        return true;
      }
    }
    return false;
  }

  bool mouse_move(Event event) {
    for (int i = 0; i < boxes_.size(); ++i) {
      const bool in_box = boxes_[i].Contain(event.mouse().x, event.mouse().y);
      const bool box_out_of_screen = boxes_[i].y_min > box_.y_max;
      if (box_out_of_screen || !in_box) {
        continue;
      }
      data_->hovered_id = data_->estimated_start_id;
      data_->move_id_by(data_->hovered_id, i);
      return true;
    }
    data_->hovered_id = -1;
    return false;
  }

  bool OnEvent(Event event) override {
    data_->move_id_by(data_->focused_id, 0);  // << Clamp()
    DSEventContext ctx{
        .event = event,
        .component_box = box_,
        .children_dimensions = &boxes_,
        .source = data_,
        .focused = Focused(),
        .mouse_ours = CaptureMouse(event) != nullptr,
        .starting_focused_id = data_->focused_id,
    };
    if (!ctx.mouse_ours) {
      return false;
    }
    if (ctx.event.is_mouse()) {
      ctx.handled = ctx.handled || mouse_wheel(event) || mouse_click(event) ||
                    mouse_move(event);
      return data_->on_event(ctx);
    }
    if (ctx.focused) {
      const int64_t height = data_->items_visible;
      if (ctx.event == Event::ArrowUp) {
        data_->move_id_by(data_->focused_id, -1);
      } else if (ctx.event == Event::ArrowDown) {
        data_->move_id_by(data_->focused_id, 1);
      } else if (ctx.event == Event::PageUp) {
        data_->move_id_by(data_->focused_id, -height);
      } else if (ctx.event == Event::PageDown) {
        data_->move_id_by(data_->focused_id, height);
      } else if (ctx.event == Event::Home) {
        data_->focused_id = data_->dataset_size().starting_id;
        data_->move_id_by(data_->focused_id, 0);
      } else if (ctx.event == Event::End) {
        data_->focused_id = data_->dataset_size().ending_id;
        data_->move_id_by(data_->focused_id, 0);
      }
    }
    ctx.handled = ctx.handled || data_->focused_id != ctx.starting_focused_id;
    return data_->on_event(ctx);
  }

  // We need to always be focusable for custom key shortcuts to work when data
  // source is empty
  bool Focusable() const final { return true; }

 protected:
  Box box_;
  DataSource* data_;
  std::vector<Box> boxes_;
};

/// @brief A list of items. The user can navigate through them.
/// @ingroup component
class MenuBase : public ComponentBase, public MenuOption {
 public:
  explicit MenuBase(const MenuOption& option) : MenuOption(option) {}

  bool IsHorizontal() { return ftxui::IsHorizontal(direction); }
  void OnChange() {
    if (on_change) {
      on_change();
    }
  }

  void OnEnter() {
    if (on_enter) {
      on_enter();
    }
  }

  void Clamp() {
    if (selected() != selected_previous_) {
      SelectedTakeFocus();
    }
    boxes_.resize(size());
    selected() = util::clamp(selected(), 0, size() - 1);
    selected_previous_ = util::clamp(selected_previous_, 0, size() - 1);
    selected_focus_ = util::clamp(selected_focus_, 0, size() - 1);
    focused_entry() = util::clamp(focused_entry(), 0, size() - 1);
  }

  void OnAnimation(animation::Params& params) override {
    animator_first_.OnAnimation(params);
    animator_second_.OnAnimation(params);
    for (auto& animator : animator_background_) {
      animator.OnAnimation(params);
    }
    for (auto& animator : animator_foreground_) {
      animator.OnAnimation(params);
    }
  }

  Element Render() override {
    Clamp();
    UpdateAnimationTarget();

    Elements elements;
    const bool is_menu_focused = Focused();
    if (elements_prefix) {
      elements.push_back(elements_prefix());
    }
    elements.reserve(size());
    for (int i = 0; i < size(); ++i) {
      if (i != 0 && elements_infix) {
        elements.push_back(elements_infix());
      }
      const bool is_focused = (focused_entry() == i) && is_menu_focused;
      const bool is_selected = (selected() == i);

      const EntryState state = {
          entries[i], false, is_selected, is_focused, i,
      };

      auto focus_management = (selected_focus_ != i) ? nothing
                              : is_menu_focused      ? focus
                                                     : select;

      const Element element =
          (entries_option.transform ? entries_option.transform
                                    : DefaultOptionTransform)  //
          (state);
      elements.push_back(element | AnimatedColorStyle(i) | reflect(boxes_[i]) |
                         focus_management);
    }
    if (elements_postfix) {
      elements.push_back(elements_postfix());
    }

    if (IsInverted(direction)) {
      std::reverse(elements.begin(), elements.end());
    }

    const Element bar =
        IsHorizontal() ? hbox(std::move(elements)) : vbox(std::move(elements));

    if (!underline.enabled) {
      return bar | reflect(box_);
    }

    if (IsHorizontal()) {
      return vbox({
                 bar | xflex,
                 separatorHSelector(first_, second_,  //
                                    underline.color_active,
                                    underline.color_inactive),
             }) |
             reflect(box_);
    } else {
      return hbox({
                 separatorVSelector(first_, second_,  //
                                    underline.color_active,
                                    underline.color_inactive),
                 bar | yflex,
             }) |
             reflect(box_);
    }
  }

  void SelectedTakeFocus() {
    selected_previous_ = selected();
    selected_focus_ = selected();
  }

  void OnUp() {
    switch (direction) {
      case Direction::Up:
        selected()++;
        break;
      case Direction::Down:
        selected()--;
        break;
      case Direction::Left:
      case Direction::Right:
        break;
    }
  }

  void OnDown() {
    switch (direction) {
      case Direction::Up:
        selected()--;
        break;
      case Direction::Down:
        selected()++;
        break;
      case Direction::Left:
      case Direction::Right:
        break;
    }
  }

  void OnLeft() {
    switch (direction) {
      case Direction::Left:
        selected()++;
        break;
      case Direction::Right:
        selected()--;
        break;
      case Direction::Down:
      case Direction::Up:
        break;
    }
  }

  void OnRight() {
    switch (direction) {
      case Direction::Left:
        selected()--;
        break;
      case Direction::Right:
        selected()++;
        break;
      case Direction::Down:
      case Direction::Up:
        break;
    }
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  bool OnEvent(Event event) override {
    Clamp();
    if (!CaptureMouse(event)) {
      return false;
    }

    if (event.is_mouse()) {
      return OnMouseEvent(event);
    }

    if (Focused()) {
      const int old_selected = selected();
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        OnUp();
      }
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        OnDown();
      }
      if (event == Event::ArrowLeft || event == Event::Character('h')) {
        OnLeft();
      }
      if (event == Event::ArrowRight || event == Event::Character('l')) {
        OnRight();
      }
      if (event == Event::PageUp) {
        selected() -= box_.y_max - box_.y_min;
      }
      if (event == Event::PageDown) {
        selected() += box_.y_max - box_.y_min;
      }
      if (event == Event::Home) {
        selected() = 0;
      }
      if (event == Event::End) {
        selected() = size() - 1;
      }
      if (event == Event::Tab && size()) {
        selected() = (selected() + 1) % size();
      }
      if (event == Event::TabReverse && size()) {
        selected() = (selected() + size() - 1) % size();
      }

      selected() = util::clamp(selected(), 0, size() - 1);

      if (selected() != old_selected) {
        focused_entry() = selected();
        SelectedTakeFocus();
        OnChange();
        return true;
      }
    }

    if (event == Event::Return) {
      OnEnter();
      return true;
    }

    return false;
  }

  bool OnMouseEvent(Event event) {
    if (event.mouse().button == Mouse::WheelDown ||
        event.mouse().button == Mouse::WheelUp) {
      return OnMouseWheel(event);
    }

    if (event.mouse().button != Mouse::None &&
        event.mouse().button != Mouse::Left) {
      return false;
    }
    if (!CaptureMouse(event)) {
      return false;
    }
    for (int i = 0; i < size(); ++i) {
      if (!boxes_[i].Contain(event.mouse().x, event.mouse().y)) {
        continue;
      }

      TakeFocus();
      focused_entry() = i;

      if (event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        if (selected() != i) {
          selected() = i;
          selected_previous_ = selected();
          OnChange();
        }
        return true;
      }
    }
    return false;
  }

  bool OnMouseWheel(Event event) {
    if (!box_.Contain(event.mouse().x, event.mouse().y)) {
      return false;
    }
    const int old_selected = selected();

    if (event.mouse().button == Mouse::WheelUp) {
      selected()--;
    }
    if (event.mouse().button == Mouse::WheelDown) {
      selected()++;
    }

    selected() = util::clamp(selected(), 0, size() - 1);

    if (selected() != old_selected) {
      SelectedTakeFocus();
      OnChange();
    }
    return true;
  }

  void UpdateAnimationTarget() {
    UpdateColorTarget();
    UpdateUnderlineTarget();
  }

  void UpdateColorTarget() {
    if (size() != int(animation_background_.size())) {
      animation_background_.resize(size());
      animation_foreground_.resize(size());
      animator_background_.clear();
      animator_foreground_.clear();

      const int len = size();
      animator_background_.reserve(len);
      animator_foreground_.reserve(len);
      for (int i = 0; i < len; ++i) {
        animation_background_[i] = 0.F;
        animation_foreground_[i] = 0.F;
        animator_background_.emplace_back(&animation_background_[i], 0.F,
                                          std::chrono::milliseconds(0),
                                          animation::easing::Linear);
        animator_foreground_.emplace_back(&animation_foreground_[i], 0.F,
                                          std::chrono::milliseconds(0),
                                          animation::easing::Linear);
      }
    }

    const bool is_menu_focused = Focused();
    for (int i = 0; i < size(); ++i) {
      const bool is_focused = (focused_entry() == i) && is_menu_focused;
      const bool is_selected = (selected() == i);
      float target = is_selected ? 1.F : is_focused ? 0.5F : 0.F;  // NOLINT
      if (animator_background_[i].to() != target) {
        animator_background_[i] = animation::Animator(
            &animation_background_[i], target,
            entries_option.animated_colors.background.duration,
            entries_option.animated_colors.background.function);
        animator_foreground_[i] = animation::Animator(
            &animation_foreground_[i], target,
            entries_option.animated_colors.foreground.duration,
            entries_option.animated_colors.foreground.function);
      }
    }
  }

  Decorator AnimatedColorStyle(int i) {
    Decorator style = nothing;
    if (entries_option.animated_colors.foreground.enabled) {
      style = style | color(Color::Interpolate(
                          animation_foreground_[i],
                          entries_option.animated_colors.foreground.inactive,
                          entries_option.animated_colors.foreground.active));
    }

    if (entries_option.animated_colors.background.enabled) {
      style = style | bgcolor(Color::Interpolate(
                          animation_background_[i],
                          entries_option.animated_colors.background.inactive,
                          entries_option.animated_colors.background.active));
    }
    return style;
  }

  void UpdateUnderlineTarget() {
    if (!underline.enabled) {
      return;
    }

    if (FirstTarget() == animator_first_.to() &&
        SecondTarget() == animator_second_.to()) {
      return;
    }

    if (FirstTarget() >= animator_first_.to()) {
      animator_first_ = animation::Animator(
          &first_, FirstTarget(), underline.follower_duration,
          underline.follower_function, underline.follower_delay);

      animator_second_ = animation::Animator(
          &second_, SecondTarget(), underline.leader_duration,
          underline.leader_function, underline.leader_delay);
    } else {
      animator_first_ = animation::Animator(
          &first_, FirstTarget(), underline.leader_duration,
          underline.leader_function, underline.leader_delay);

      animator_second_ = animation::Animator(
          &second_, SecondTarget(), underline.follower_duration,
          underline.follower_function, underline.follower_delay);
    }
  }

  bool Focusable() const final { return entries.size(); }
  int size() const { return int(entries.size()); }
  float FirstTarget() {
    if (boxes_.empty()) {
      return 0.F;
    }
    const int value = IsHorizontal() ? boxes_[selected()].x_min - box_.x_min
                                     : boxes_[selected()].y_min - box_.y_min;
    return float(value);
  }
  float SecondTarget() {
    if (boxes_.empty()) {
      return 0.F;
    }
    const int value = IsHorizontal() ? boxes_[selected()].x_max - box_.x_min
                                     : boxes_[selected()].y_max - box_.y_min;
    return float(value);
  }

 protected:
  int selected_previous_ = selected();
  int selected_focus_ = selected();

  // Mouse click support:
  std::vector<Box> boxes_;
  Box box_;

  // Animation support:
  float first_ = 0.F;
  float second_ = 0.F;
  animation::Animator animator_first_ = animation::Animator(&first_, 0.F);
  animation::Animator animator_second_ = animation::Animator(&second_, 0.F);
  std::vector<animation::Animator> animator_background_;
  std::vector<animation::Animator> animator_foreground_;
  std::vector<float> animation_background_;
  std::vector<float> animation_foreground_;
};

/// @brief A list of text. The focused element is selected.
/// @param option a structure containing all the paramters.
/// @ingroup component
///
/// ### Example
///
/// ```cpp
/// auto screen = ScreenInteractive::TerminalOutput();
/// std::vector<std::string> entries = {
///     "entry 1",
///     "entry 2",
///     "entry 3",
/// };
/// int selected = 0;
/// auto menu = Menu({
///   .entries = &entries,
///   .selected = &selected,
/// });
/// screen.Loop(menu);
/// ```
///
/// ### Output
///
/// ```bash
/// > entry 1
///   entry 2
///   entry 3
/// ```
// NOLINTNEXTLINE
Component Menu(MenuOption option) {
  return Make<MenuBase>(std::move(option));
}

Component DBMenu(DataSource* dataSource) {
  return Make<VerticalMenu>(dataSource);
}

/// @brief A list of text. The focused element is selected.
/// @param entries The list of entries in the menu.
/// @param selected The index of the currently selected element.
/// @param option Additional optional parameters.
/// @ingroup component
///
/// ### Example
///
/// ```cpp
/// auto screen = ScreenInteractive::TerminalOutput();
/// std::vector<std::string> entries = {
///     "entry 1",
///     "entry 2",
///     "entry 3",
/// };
/// int selected = 0;
/// auto menu = Menu(&entries, &selected);
/// screen.Loop(menu);
/// ```
///
/// ### Output
///
/// ```bash
/// > entry 1
///   entry 2
///   entry 3
/// ```
Component Menu(ConstStringListRef entries, int* selected, MenuOption option) {
  option.entries = std::move(entries);
  option.selected = selected;
  return Menu(option);
}

/// @brief An horizontal list of elements. The user can navigate through them.
/// @param entries The list of selectable entries to display.
/// @param selected Reference the selected entry.
/// See also |Menu|.
/// @ingroup component
Component Toggle(ConstStringListRef entries, int* selected) {
  return Menu(std::move(entries), selected, MenuOption::Toggle());
}

/// @brief A specific menu entry. They can be put into a Container::Vertical to
/// form a menu.
/// @param label The text drawn representing this element.
/// @param option Additional optional parameters.
/// @ingroup component
///
/// ### Example
///
/// ```cpp
/// auto screen = ScreenInteractive::TerminalOutput();
/// int selected = 0;
/// auto menu = Container::Vertical({
///    MenuEntry("entry 1"),
///    MenuEntry("entry 2"),
///    MenuEntry("entry 3"),
/// }, &selected);
/// screen.Loop(menu);
/// ```
///
/// ### Output
///
/// ```bash
/// > entry 1
///   entry 2
///   entry 3
/// ```
Component MenuEntry(ConstStringRef label, MenuEntryOption option) {
  option.label = std::move(label);
  return MenuEntry(std::move(option));
}

/// @brief A specific menu entry. They can be put into a Container::Vertical to
/// form a menu.
/// @param option The parameters.
/// @ingroup component
///
/// ### Example
///
/// ```cpp
/// auto screen = ScreenInteractive::TerminalOutput();
/// int selected = 0;
/// auto menu = Container::Vertical({
///    MenuEntry({.label = "entry 1"}),
///    MenuEntry({.label = "entry 2"}),
///    MenuEntry({.label = "entry 3"}),
/// }, &selected);
/// screen.Loop(menu);
/// ```
///
/// ### Output
///
/// ```bash
/// > entry 1
///   entry 2
///   entry 3
/// ```
Component MenuEntry(MenuEntryOption option) {
  class Impl : public ComponentBase, public MenuEntryOption {
   public:
    explicit Impl(MenuEntryOption option)
        : MenuEntryOption(std::move(option)) {}

   private:
    Element Render() override {
      const bool focused = Focused();
      UpdateAnimationTarget();

      const EntryState state{
          label(), false, hovered_, focused, Index(),
      };

      const Element element =
          (transform ? transform : DefaultOptionTransform)  //
          (state);

      auto focus_management = focused ? select : nothing;
      return element | AnimatedColorStyle() | focus_management | reflect(box_);
    }

    void UpdateAnimationTarget() {
      const bool focused = Focused();
      float target = focused ? 1.F : hovered_ ? 0.5F : 0.F;  // NOLINT
      if (target == animator_background_.to()) {
        return;
      }
      animator_background_ = animation::Animator(
          &animation_background_, target, animated_colors.background.duration,
          animated_colors.background.function);
      animator_foreground_ = animation::Animator(
          &animation_foreground_, target, animated_colors.foreground.duration,
          animated_colors.foreground.function);
    }

    Decorator AnimatedColorStyle() {
      Decorator style = nothing;
      if (animated_colors.foreground.enabled) {
        style = style |
                color(Color::Interpolate(animation_foreground_,
                                         animated_colors.foreground.inactive,
                                         animated_colors.foreground.active));
      }

      if (animated_colors.background.enabled) {
        style = style |
                bgcolor(Color::Interpolate(animation_background_,
                                           animated_colors.background.inactive,
                                           animated_colors.background.active));
      }
      return style;
    }

    bool Focusable() const override { return true; }
    bool OnEvent(Event event) override {
      if (!event.is_mouse()) {
        return false;
      }

      hovered_ = box_.Contain(event.mouse().x, event.mouse().y);

      if (!hovered_) {
        return false;
      }

      if (event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        TakeFocus();
        return true;
      }

      return false;
    }

    void OnAnimation(animation::Params& params) override {
      animator_background_.OnAnimation(params);
      animator_foreground_.OnAnimation(params);
    }

    MenuEntryOption option_;
    Box box_;
    bool hovered_ = false;

    float animation_background_ = 0.F;
    float animation_foreground_ = 0.F;
    animation::Animator animator_background_ =
        animation::Animator(&animation_background_, 0.F);
    animation::Animator animator_foreground_ =
        animation::Animator(&animation_foreground_, 0.F);
  };

  return Make<Impl>(std::move(option));
}

}  // namespace ftxui
