#include "operator_validator.hpp"

#include <cstdio>

int main() {
    const EwOperatorValidationReport r = validate_operators_and_microprocessor();
    std::fwrite(r.details.data(), 1, r.details.size(), stdout);
    return r.ok ? 0 : 1;
}
