#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#ifdef QARVTK_HOST_GL
#include "qarvtk/host_gl.hpp"
#else
// Let VTK choose its GL loader; do not mix GLEW and GLAD in one translation unit.
#if __has_include(<vtk_glad.h>)
#include <vtk_glad.h>
#else
#include <vtk_glew.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
