#include "utilities.h"

#include <stdexcept>

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}
