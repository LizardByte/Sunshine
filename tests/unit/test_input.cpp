/**
 * @file tests/test_input.cpp
 * @brief Test src/input.*.
 */
#include <src/input.h>
#include <src/platform/common.h>

#include <tests/conftest.cpp>

TEST(InputTest, MoveInputTest) {
  platf::input_t input = platf::input();

  auto old_loc = platf::get_mouse_loc(input);

  platf::move_mouse(input, 40, 40);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto new_loc = platf::get_mouse_loc(input);

  bool has_input_moved = old_loc.x != new_loc.x && old_loc.y != new_loc.y;

  if (!has_input_moved) {
    std::cout << "InputTest:: haven't moved" << std::endl;
  } else {
    std::cout << "InputTest:: moved" << std::endl;
  }

  EXPECT_TRUE(has_input_moved);

  // Verify we moved as much as we requested
  EXPECT_TRUE(new_loc.x - old_loc.x == 40);
  EXPECT_TRUE(new_loc.y - old_loc.y == 40);
}

TEST(InputTest, AbsMoveInputTest) {
  platf::input_t input = platf::input();

  auto old_loc = platf::get_mouse_loc(input);

  platf::abs_mouse(input, platf::touch_port_t {}, 40, 40);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto new_loc = platf::get_mouse_loc(input);

  bool has_input_moved = old_loc.x != new_loc.x && old_loc.y != new_loc.y;

  if (!has_input_moved) {
    std::cout << "InputTest:: haven't moved" << std::endl;
  } else {
    std::cout << "InputTest:: moved" << std::endl;
  }

  EXPECT_TRUE(has_input_moved);

  // Verify we moved to the absolute coordinate
  EXPECT_TRUE(new_loc.x == 40);
  EXPECT_TRUE(new_loc.y == 40);
}