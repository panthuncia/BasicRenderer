//
// Created by matth on 6/25/2024.
//
#include <windows.h>
#include <iostream>

#ifndef UTILITIES_H
#define UTILITIES_H

void ThrowIfFailed(HRESULT hr);

template<typename... Args>
void print(Args... args) {
    (std::cout << ... << args) << std::endl;
}

#endif //UTILITIES_H
