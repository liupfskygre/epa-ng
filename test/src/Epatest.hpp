#pragma once

#include <gtest/gtest.h>

#include "src/Model.hpp"
#include "src/Options.hpp"

// The testing environment
class Epatest : public ::testing::Environment {
public:
  // You can remove any or all of the following functions if its body
  // is empty.

  Epatest() {};

  virtual ~Epatest() {
    // You can do clean-up work that doesn't throw exceptions here.
  }

  // If the constructor and destructor are not enough for setting up
  // and cleaning up each test, you can define the following methods:

  virtual void SetUp() {
    // Code here will be called immediately after the constructor (right
    // before each test).
  }

  virtual void TearDown() {
    // Code here will be called immediately after each test (right
    // before the destructor).
  }

  // Objects declared here can be used by all tests in the test case for Foo.
  std::string data_dir;
  std::string tree_file;
  std::string reference_file;
  std::string combined_file;
  std::string query_file;
  Model model = Model("GTR");
  Options options;

};

extern Epatest* env;
