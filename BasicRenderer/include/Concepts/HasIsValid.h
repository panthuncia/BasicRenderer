#pragma once

template<class T> // Lazy buffer deletion requires the GPU to be able to check if a struct has been invalidated
concept HasIsValid = requires(T obj) {
    { obj.isValid } -> std::convertible_to<bool>;
};