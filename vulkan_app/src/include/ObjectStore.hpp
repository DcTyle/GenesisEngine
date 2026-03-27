#pragma once
#include <vector>
#include "Object.hpp"

class ObjectStore {
public:
    std::vector<Object> objects;
    ObjectStore() = default;
};
