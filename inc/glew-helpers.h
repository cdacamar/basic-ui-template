#pragma once

#include <stdio.h>

#include <format>
#include <iterator>

#include "GL/glew.h"

#include "enum-utils.h"
#include "scope-guard.h"
#include "scoped-handle.h"

namespace Glew
{
    template <typename T, int N>
    constexpr GLsizei gl_size(T(&)[N])
    {
        return GLsizei(N);
    }

    struct ShaderDeleter
    {
        void operator()(GLuint shader_handle) const
        {
            glDeleteShader(shader_handle);
        }
    };

    using ShaderHandle = ScopedHandle<GLuint, ShaderDeleter>;

    enum class ShaderType : GLuint
    {
        Vertex = GL_VERTEX_SHADER,
        Fragment = GL_FRAGMENT_SHADER,
    };

    constexpr const char* stringify(ShaderType type)
    {
        switch (type)
        {
        case ShaderType::Fragment:
            return "Fragment";
        case ShaderType::Vertex:
            return "Vertex";
        }
        return "unknown";
    }

    template <typename Reporter>
    inline ShaderHandle compile_shader(ShaderType type, const char* src, Reporter&& reporter)
    {
        ShaderHandle handle { glCreateShader(rep(type)) };
        if (not handle)
            return { };
        glShaderSource(handle.handle(), 1, &src, nullptr);
        glCompileShader(handle.handle());

        // Check for compilation errors.
        GLint success = GL_FALSE;
        glGetShaderiv(handle.handle(), GL_COMPILE_STATUS, &success);
        if (not success)
        {
            char log[512] = { };
            GLsizei len = 0;
            glGetShaderInfoLog(handle.handle(), gl_size(log), &len, log);
            auto txt = std::format("Unable to compile shader type '{}'", stringify(type));
            reporter(txt);
            txt = std::format("{:.{}}", log, len);
            reporter(txt);
            return { };
        }
        return handle;
    }

    enum class ProgramHandle : GLuint { };

    struct ProgramDeleter
    {
        void operator()(ProgramHandle handle)
        {
            glDeleteProgram(rep(handle));
        }
    };

    using ScopedProgramHandle = ScopedHandle<ProgramHandle, ProgramDeleter>;

    enum class VertexShaderHandle : GLuint { };
    enum class FragmentShaderHandle : GLuint { };

    inline ScopedProgramHandle attach_and_create_program(VertexShaderHandle vert, FragmentShaderHandle frag)
    {
        GLuint program_handle = glCreateProgram();
        ScopedProgramHandle program { ProgramHandle(program_handle) };
        ScopeGuard g { [] { glUseProgram(0); } };

        glAttachShader(program_handle, rep(vert));
        glAttachShader(program_handle, rep(frag));
        return std::move(program);
    }

    template <typename Reporter>
    inline bool link_program(ProgramHandle prog, Reporter&& reporter)
    {
        glLinkProgram(rep(prog));

        GLint success = 0;
        glGetProgramiv(rep(prog), GL_LINK_STATUS, &success);
        if (not success)
        {
            char log[512] = { };
            GLsizei len = 0;
            glGetProgramInfoLog(rep(prog), gl_size(log), &len, log);
            auto txt = std::format("Failed to link shaders: {:.{}}\n", log, len);
            reporter(txt);
            return false;
        }
        return true;
    }

    enum class UniformHandle : GLint { };

} // namespace Glew