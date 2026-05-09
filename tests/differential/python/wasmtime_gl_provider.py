"""PyOpenGL-backed env.gl* provider for Wasmtime differential runners."""

from __future__ import annotations

import re
import sys
import logging
import os
import ctypes
import hashlib
import json
import struct
from collections import deque
from typing import Any


class WasmtimeGLProvider:
    """Bridge Emscripten-style env.gl* imports to a real OpenGL context."""

    SUPPORTED_IMPORTS = {
        "glPixelStorei",
        "glGenTextures",
        "glTexParameteri",
        "glGetError",
        "glTexImage2D",
        "glCompressedTexImage2D",
        "glCreateProgram",
        "glAttachShader",
        "glLinkProgram",
        "glGetProgramiv",
        "glDeleteShader",
        "glGetUniformLocation",
        "glUniform1i",
        "glCreateShader",
        "glShaderSource",
        "glCompileShader",
        "glGetShaderiv",
        "glGetShaderSource",
        "glBindAttribLocation",
        "glGetActiveAttrib",
        "glGetAttribLocation",
        "glGetProgramInfoLog",
        "glGetActiveUniform",
        "glTexSubImage2D",
        "glDeleteProgram",
        "glUseProgram",
        "glActiveTexture",
        "glBindTexture",
        "glDeleteTextures",
        "glBindVertexArray",
        "glDeleteBuffers",
        "glDeleteVertexArrays",
        "glGetIntegerv",
        "glViewport",
        "glUniform1f",
        "glUniform2f",
        "glUniform3f",
        "glUniform4f",
        "glUniform1fv",
        "glUniform2fv",
        "glUniform3fv",
        "glUniform4fv",
        "glUniformMatrix3fv",
        "glUniformMatrix4fv",
        "glVertexAttribPointer",
        "glIsEnabled",
        "glGetBooleanv",
        "glEnable",
        "glDisable",
        "glCullFace",
        "glFrontFace",
        "glDepthMask",
        "glDepthFunc",
        "glGenVertexArrays",
        "glBindBuffer",
        "glEnableVertexAttribArray",
        "glDisableVertexAttribArray",
        "glBufferData",
        "glBufferSubData",
        "glDrawElements",
        "glDrawArrays",
        "glBlendFunc",
        "glBindFramebuffer",
        "glFramebufferTexture2D",
        "glFramebufferRenderbuffer",
        "glCheckFramebufferStatus",
    }

    def __init__(self) -> None:
        self._glfw = None
        self._gl = None
        self._window = None
        self._profile = ""
        self._platform = os.environ.get(
            "KRKR2_WASMTIME_GL_PLATFORM", "").strip().lower()
        self._context_api = os.environ.get(
            "KRKR2_WASMTIME_GL_CONTEXT_API", "").strip().lower()
        if self._context_api == "osmesa":
            os.environ.setdefault("PYOPENGL_PLATFORM", "osmesa")
        self._is_gles = False
        self._shader_types: dict[int, int] = {}
        self._shader_sources: dict[int, str] = {}
        self._shader_compile_logs: dict[int, str] = {}
        self._program_shaders: dict[int, set[int]] = {}
        self._program_shader_sources: dict[int, list[dict[str, Any]]] = {}
        self._program_active_attribs: dict[int, set[int]] = {}
        self._bound_buffers: dict[int, int] = {}
        self._default_vao = 0
        self._array_attribs: set[int] = set()
        self._client_attribs: dict[int, tuple[int, int, int, int, int]] = {}
        self._client_attrib_buffers: dict[int, int] = {}
        self._client_element_buffer = 0
        self._active_texture_unit = 0
        self._bound_textures: dict[tuple[int, int], int] = {}
        self._texture_info: dict[int, dict[str, Any]] = {}
        self._bound_framebuffer = 0
        self._bound_draw_framebuffer = 0
        self._framebuffer_color_texture: dict[int, int] = {}
        self._window_width = int(os.environ.get(
            "KRKR2_WASMTIME_GL_WINDOW_WIDTH", "1920"))
        self._window_height = int(os.environ.get(
            "KRKR2_WASMTIME_GL_WINDOW_HEIGHT", "1080"))
        self._default_framebuffer_viewport = (
            0, 0, self._window_width, self._window_height)
        self._pixel_store: dict[int, int] = {
            0x0CF2: 0,  # GL_UNPACK_ROW_LENGTH
            0x0D02: 0,  # GL_PACK_ROW_LENGTH
            0x0CF5: 4,  # GL_UNPACK_ALIGNMENT
            0x0D05: 4,  # GL_PACK_ALIGNMENT
        }
        self._guest_strings: dict[bytes, int] = {}
        self._recent_calls: deque[str] = deque(maxlen=80)
        self._trace_calls = os.environ.get("KRKR2_WASMTIME_GL_TRACE") == "1"
        self._readpixels_probe_path = os.environ.get(
            "KRKR2_WASMTIME_GL_READPIXELS_PROBE")
        self._readpixels_probe_limit = int(os.environ.get(
            "KRKR2_WASMTIME_GL_READPIXELS_PROBE_LIMIT", "16"))
        self._readpixels_probe_count = 0
        self._texture_probe_path = os.environ.get(
            "KRKR2_WASMTIME_GL_TEXTURE_PROBE")
        self._texture_probe_limit = int(os.environ.get(
            "KRKR2_WASMTIME_GL_TEXTURE_PROBE_LIMIT", "32"))
        self._texture_probe_count = 0
        self._draw_probe_path = os.environ.get(
            "KRKR2_WASMTIME_GL_DRAW_PROBE")
        self._draw_probe_limit = int(os.environ.get(
            "KRKR2_WASMTIME_GL_DRAW_PROBE_LIMIT", "64"))
        self._draw_probe_count = 0
        self._uniform_probe_path = os.environ.get(
            "KRKR2_WASMTIME_GL_UNIFORM_PROBE")
        self._final_read_buffer = os.environ.get(
            "KRKR2_WASMTIME_GL_FINAL_READ_BUFFER", "back").strip().lower()
        self._last_default_framebuffer_read: dict[str, Any] = {}
        self._noop = os.environ.get("KRKR2_WASMTIME_GL_MODE") == "noop"
        self._next_gl_id = 1

    def define_imports(self, linker: Any, module: Any) -> None:
        gl_imports = [
            imp for imp in module.imports
            if imp.module == "env"
            and (imp.name.startswith("gl")
                 or imp.name.startswith("emscripten_gl"))
        ]
        if not gl_imports:
            return

        for imp in gl_imports:
            linker.define_func(
                "env",
                imp.name,
                imp.type,
                self._make_import_callback(imp.name, imp.type),
                access_caller=True,
            )

    def _ensure_context(self) -> None:
        if self._window is not None:
            self._glfw.make_context_current(self._window)
            return

        try:
            import glfw  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "glfw is required for env.gl* imports; run "
                "`python3 -m pip install -r "
                "tests/differential/python/requirements-wasm.txt`"
            ) from exc
        osmesa_library = os.environ.get(
            "KRKR2_WASMTIME_OSMESA_LIBRARY", "").strip()
        if osmesa_library:
            import ctypes.util as ctypes_util

            original_find_library = ctypes_util.find_library

            def find_library(name: str) -> str | None:
                if name.lower() == "osmesa":
                    return osmesa_library
                return original_find_library(name)

            ctypes_util.find_library = find_library
            ctypes.CDLL(osmesa_library, mode=ctypes.RTLD_GLOBAL)
        try:
            logging.getLogger("OpenGL.plugins").setLevel(logging.ERROR)
            from OpenGL import GL  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "PyOpenGL is required for env.gl* imports; run "
                "`python3 -m pip install -r "
                "tests/differential/python/requirements-wasm.txt`"
            ) from exc

        errors: list[str] = []

        def error_callback(code: int, desc: bytes) -> None:
            text = desc.decode("utf-8", errors="replace")
            errors.append(f"GLFW {code}: {text}")

        glfw.set_error_callback(error_callback)
        platform_hint = self._glfw_platform_hint(glfw)
        if platform_hint is not None:
            glfw.init_hint(glfw.PLATFORM, platform_hint)
        if not glfw.init():
            raise RuntimeError(
                "glfw.init() failed: " + ("; ".join(errors) or "no detail")
            )

        attempts = self._context_attempts()
        last_error = ""
        for profile, apply_hints in attempts:
            glfw.default_window_hints()
            glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
            apply_hints(glfw)
            window = glfw.create_window(
                self._window_width, self._window_height,
                "krkr2-wasmtime-gl", None, None)
            if not window:
                last_error = "; ".join(errors) or f"{profile} create failed"
                continue

            glfw.make_context_current(window)
            self._glfw = glfw
            self._gl = GL
            self._window = window
            self._profile = profile
            self._is_gles = profile == "gles"
            try:
                self._probe_context()
                self._ensure_default_vertex_array()
                self._log_context()
                return
            except Exception as exc:
                last_error = f"{profile}: {exc}"
                glfw.destroy_window(window)
                self._window = None
                self._gl = None

        glfw.terminate()
        raise RuntimeError(
            "failed to create a usable hidden OpenGL context: " + last_error
        )

    def _glfw_platform_hint(self, glfw: Any) -> int | None:
        if self._platform in ("", "auto"):
            return None
        values = {
            "cocoa": glfw.PLATFORM_COCOA,
            "null": glfw.PLATFORM_NULL,
            "x11": glfw.PLATFORM_X11,
            "wayland": glfw.PLATFORM_WAYLAND,
        }
        if self._platform not in values:
            raise RuntimeError(
                "unsupported KRKR2_WASMTIME_GL_PLATFORM: "
                f"{self._platform!r}"
            )
        return values[self._platform]

    def _context_attempts(self):
        if self._context_api in ("", "auto"):
            return [
                ("gles", self._hint_gles),
                ("desktop_compat", self._hint_desktop_compat),
                ("desktop_core", self._hint_desktop_core),
            ]
        if self._context_api == "osmesa":
            return [("osmesa", self._hint_osmesa)]
        if self._context_api == "egl":
            return [("egl", self._hint_egl)]
        if self._context_api == "nsgl":
            return [("desktop_compat", self._hint_desktop_compat)]
        raise RuntimeError(
            "unsupported KRKR2_WASMTIME_GL_CONTEXT_API: "
            f"{self._context_api!r}"
        )

    def _ensure_window_size(self, width: int, height: int) -> None:
        self._ensure_context()
        if width <= 0 or height <= 0:
            raise RuntimeError(
                f"invalid framebuffer read size {width}x{height}")
        if self._window_width == width and self._window_height == height:
            return
        self._glfw.set_window_size(self._window, width, height)
        try:
            self._glfw.poll_events()
        except Exception:
            pass
        self._window_width = width
        self._window_height = height

    @staticmethod
    def _hint_gles(glfw: Any) -> None:
        glfw.window_hint(glfw.CLIENT_API, glfw.OPENGL_ES_API)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 2)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 0)

    @staticmethod
    def _hint_desktop_core(glfw: Any) -> None:
        glfw.window_hint(glfw.CLIENT_API, glfw.OPENGL_API)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 2)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
        if sys.platform == "darwin":
            glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, glfw.TRUE)

    @staticmethod
    def _hint_desktop_compat(glfw: Any) -> None:
        glfw.window_hint(glfw.CLIENT_API, glfw.OPENGL_API)

    @staticmethod
    def _hint_osmesa(glfw: Any) -> None:
        glfw.window_hint(glfw.CLIENT_API, glfw.OPENGL_API)
        glfw.window_hint(glfw.CONTEXT_CREATION_API, glfw.OSMESA_CONTEXT_API)

    @staticmethod
    def _hint_egl(glfw: Any) -> None:
        glfw.window_hint(glfw.CLIENT_API, glfw.OPENGL_API)
        glfw.window_hint(glfw.CONTEXT_CREATION_API, glfw.EGL_CONTEXT_API)

    def _probe_context(self) -> None:
        GL = self._gl
        tex = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
        pixel = bytes([255, 255, 255, 255])
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, 1, 1, 0,
                        GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, pixel)
        GL.glDeleteTextures([tex])

        vs = GL.glCreateShader(GL.GL_VERTEX_SHADER)
        fs = GL.glCreateShader(GL.GL_FRAGMENT_SHADER)
        program = GL.glCreateProgram()
        try:
            vs_src = (
                "attribute vec2 a_position;\n"
                "varying vec2 v_uv;\n"
                "void main(){ v_uv = a_position; "
                "gl_Position = vec4(a_position, 0.0, 1.0); }\n"
            )
            fs_src = (
                "precision mediump float;\n"
                "varying vec2 v_uv;\n"
                "void main(){ gl_FragColor = vec4(v_uv, 0.0, 1.0); }\n"
            )
            self._compile_probe_shader(vs, GL.GL_VERTEX_SHADER, vs_src)
            self._compile_probe_shader(fs, GL.GL_FRAGMENT_SHADER, fs_src)
            GL.glAttachShader(program, vs)
            GL.glAttachShader(program, fs)
            GL.glLinkProgram(program)
            if not GL.glGetProgramiv(program, GL.GL_LINK_STATUS):
                raise RuntimeError(
                    self._bytes_to_text(GL.glGetProgramInfoLog(program))
                )
        finally:
            GL.glDeleteShader(vs)
            GL.glDeleteShader(fs)
            GL.glDeleteProgram(program)

    def _ensure_default_vertex_array(self) -> None:
        GL = self._gl
        if self._is_gles:
            return
        gen_vertex_arrays = getattr(GL, "glGenVertexArrays", None)
        bind_vertex_array = getattr(GL, "glBindVertexArray", None)
        if gen_vertex_arrays is None or bind_vertex_array is None:
            return
        vao = gen_vertex_arrays(1)
        if isinstance(vao, (list, tuple)):
            vao = vao[0]
        self._default_vao = int(vao)
        bind_vertex_array(self._default_vao)

    def _compile_probe_shader(self, shader: int, shader_type: int,
                              source: str) -> None:
        GL = self._gl
        translated = self._translate_shader_source(source, shader_type)
        GL.glShaderSource(shader, [translated])
        GL.glCompileShader(shader)
        if not GL.glGetShaderiv(shader, GL.GL_COMPILE_STATUS):
            raise RuntimeError(self._bytes_to_text(GL.glGetShaderInfoLog(shader)))

    def _log_context(self) -> None:
        GL = self._gl
        vendor = self._gl_string(GL.GL_VENDOR)
        renderer = self._gl_string(GL.GL_RENDERER)
        version = self._gl_string(GL.GL_VERSION)
        shading = self._gl_string(GL.GL_SHADING_LANGUAGE_VERSION)
        print(
            "Wasmtime GL provider: "
            f"profile={self._profile}, vendor={vendor}, "
            f"renderer={renderer}, version={version}, glsl={shading}",
            file=sys.stderr,
        )

    def _gl_string(self, name: int) -> str:
        value = self._gl.glGetString(name)
        return self._bytes_to_text(value) if value else "<none>"

    def _extension_string(self) -> bytes:
        GL = self._gl
        extensions: set[str] = {"GL_EXT_unpack_subimage"}
        get_stringi = getattr(GL, "glGetStringi", None)
        if get_stringi is not None:
            try:
                count = int(GL.glGetIntegerv(GL.GL_NUM_EXTENSIONS))
                for index in range(count):
                    value = get_stringi(GL.GL_EXTENSIONS, index)
                    text = self._bytes_to_text(value)
                    if text:
                        extensions.add(text)
            except Exception:
                pass
        else:
            try:
                value = GL.glGetString(GL.GL_EXTENSIONS)
            except Exception:
                value = None
            text = self._bytes_to_text(value) if value else ""
            extensions.update(part for part in text.split() if part)
        return " ".join(sorted(extensions)).encode("utf-8")

    @staticmethod
    def _bytes_to_text(value: Any) -> str:
        if value is None:
            return ""
        if isinstance(value, bytes):
            return value.decode("utf-8", errors="replace")
        return str(value)

    def _call(self, name: str, args: tuple[Any, ...], fn: Any) -> Any:
        self._ensure_context()
        self._recent_calls.append(f"{name}{args}")
        if self._trace_calls:
            print(f"[wasmtime-gl] {name}{args}", file=sys.stderr)
        try:
            return fn()
        except Exception as exc:
            recent = "\n".join(f"  {call}" for call in self._recent_calls)
            raise RuntimeError(f"{name}{args} failed: {exc}\n{recent}") from exc

    def _make_import_callback(self, import_name: str,
                              func_type: Any) -> Any:
        if self._noop:
            return self._make_noop_callback(import_name, func_type)

        local_name = self._local_gl_name(import_name)
        method = getattr(self, local_name, None)
        if method is not None:
            return method

        def generic(caller: Any, *args: Any) -> Any:
            del caller
            self._ensure_context()
            gl_fn = getattr(self._gl, local_name, None)
            if gl_fn is None:
                recent = "\n".join(
                    f"  {call}" for call in self._recent_calls)
                raise RuntimeError(
                    f"{import_name}{tuple(args)} is not supported by the "
                    f"PyOpenGL provider\n{recent}"
                )
            return self._call(import_name, tuple(args),
                              lambda: gl_fn(*args))
        return generic

    def _alloc_noop_gl_ids(self, count: int) -> list[int]:
        count = max(0, int(count))
        ids = list(range(self._next_gl_id, self._next_gl_id + count))
        self._next_gl_id += count
        return ids

    @staticmethod
    def _default_return(func_type: Any) -> Any:
        results = getattr(func_type, "results", [])
        if not results:
            return None
        kind = str(results[0]).lower()
        if "f32" in kind or "f64" in kind or "float" in kind:
            return 0.0
        return 0

    def _noop_gl_string(self, name: int) -> bytes:
        strings = {
            0x1F00: b"krkr2-wasmtime",  # GL_VENDOR
            0x1F01: b"noop",  # GL_RENDERER
            0x1F02: b"OpenGL ES 2.0 krkr2 noop",  # GL_VERSION
            0x1F03: b"GL_EXT_unpack_subimage",  # GL_EXTENSIONS
            0x8B8C: b"OpenGL ES GLSL ES 1.00",  # GL_SHADING_LANGUAGE_VERSION
        }
        return strings.get(int(name), b"")

    def _noop_get_integer(self, pname: int) -> int:
        values = {
            0x0D33: 4096,  # GL_MAX_TEXTURE_SIZE
            0x8D57: 4096,  # GL_MAX_RENDERBUFFER_SIZE
            0x8869: 16,  # GL_MAX_VERTEX_ATTRIBS
            0x8872: 16,  # GL_MAX_TEXTURE_IMAGE_UNITS
            0x8B4C: 16,  # GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
            0x8B4D: 256,  # GL_MAX_VERTEX_UNIFORM_VECTORS
            0x8DFB: 256,  # GL_MAX_FRAGMENT_UNIFORM_VECTORS
            0x8B4B: 32,  # GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
        }
        return values.get(int(pname), 0)

    def _noop_program_param(self, pname: int) -> int:
        if int(pname) in (0x8B82, 0x8B83):  # LINK_STATUS, VALIDATE_STATUS
            return 1
        return 0

    def _noop_shader_param(self, pname: int) -> int:
        if int(pname) == 0x8B81:  # COMPILE_STATUS
            return 1
        return 0

    def _noop_readpixels_size(self, width: int, height: int,
                              fmt: int, typ: int) -> int:
        del typ
        channels = {
            0x1906: 1,  # GL_ALPHA
            0x1907: 3,  # GL_RGB
            0x1908: 4,  # GL_RGBA
            0x1909: 1,  # GL_LUMINANCE
            0x190A: 2,  # GL_LUMINANCE_ALPHA
            0x80E1: 4,  # GL_BGRA
        }.get(int(fmt), 4)
        return max(0, int(width)) * max(0, int(height)) * channels

    def _make_noop_callback(self, import_name: str,
                            func_type: Any) -> Any:
        local_name = self._local_gl_name(import_name)

        def noop(caller: Any, *args: Any) -> Any:
            if self._trace_calls:
                print(f"[wasmtime-gl-noop] {import_name}{args}",
                      file=sys.stderr)
            self._recent_calls.append(f"{import_name}{args}")

            if local_name in {
                "glGenTextures", "glGenBuffers", "glGenFramebuffers",
                "glGenRenderbuffers", "glGenVertexArrays",
            } and len(args) >= 2:
                self._write_u32_array(
                    caller, int(args[1]), self._alloc_noop_gl_ids(int(args[0])))
                return None
            if local_name in {"glCreateProgram", "glCreateShader"}:
                return self._alloc_noop_gl_ids(1)[0]
            if local_name == "glGetError":
                return 0
            if local_name == "glGetString" and args:
                return self._guest_c_string(
                    caller, self._noop_gl_string(int(args[0])))
            if local_name == "glGetIntegerv" and len(args) >= 2:
                self._write_i32(caller, int(args[1]),
                                self._noop_get_integer(int(args[0])))
                return None
            if local_name == "glGetFloatv" and len(args) >= 2:
                self._write_f32_array(caller, int(args[1]), [0.0])
                return None
            if local_name == "glGetBooleanv" and len(args) >= 2:
                self._write(caller, int(args[1]), b"\0")
                return None
            if local_name == "glGetProgramiv" and len(args) >= 3:
                self._write_i32(caller, int(args[2]),
                                self._noop_program_param(int(args[1])))
                return None
            if local_name == "glGetShaderiv" and len(args) >= 3:
                self._write_i32(caller, int(args[2]),
                                self._noop_shader_param(int(args[1])))
                return None
            if local_name in {
                "glGetShaderSource", "glGetShaderInfoLog",
                "glGetProgramInfoLog",
            } and len(args) >= 4:
                self._write_gl_string(caller, int(args[-3]), int(args[-2]),
                                      int(args[-1]), "")
                return None
            if local_name in {"glGetActiveAttrib", "glGetActiveUniform"}:
                if len(args) >= 7:
                    self._write_i32(caller, int(args[4]), 0)
                    self._write_i32(caller, int(args[5]), 0)
                    self._write_gl_string(caller, int(args[2]),
                                          int(args[3]), int(args[6]), "")
                return None
            if local_name in {"glGetUniformLocation", "glGetAttribLocation"}:
                return 0
            if local_name == "glCheckFramebufferStatus":
                return 0x8CD5  # GL_FRAMEBUFFER_COMPLETE
            if local_name == "glReadPixels" and len(args) >= 7:
                size = self._noop_readpixels_size(
                    int(args[2]), int(args[3]), int(args[4]), int(args[5]))
                if int(args[6]) and size:
                    self._write(caller, int(args[6]), b"\0" * size)
                return None
            if local_name.startswith("glIs"):
                return 0
            return self._default_return(func_type)

        return noop

    @staticmethod
    def _local_gl_name(import_name: str) -> str:
        if not import_name.startswith("emscripten_gl"):
            return import_name
        suffix = import_name[len("emscripten_gl"):]
        aliases = {
            "BindVertexArrayOES": "BindVertexArray",
            "DeleteVertexArraysOES": "DeleteVertexArrays",
            "GenVertexArraysOES": "GenVertexArrays",
            "IsVertexArrayOES": "IsVertexArray",
            "DrawArraysInstancedANGLE": "DrawArraysInstanced",
            "DrawElementsInstancedANGLE": "DrawElementsInstanced",
            "VertexAttribDivisorANGLE": "VertexAttribDivisor",
            "DrawBuffersWEBGL": "DrawBuffers",
        }
        return "gl" + aliases.get(suffix, suffix)

    @staticmethod
    def _memory(caller: Any) -> Any:
        memory = caller.get("memory")
        if memory is None:
            raise RuntimeError("guest memory export is unavailable")
        return memory

    @staticmethod
    def _memory_base(caller: Any, memory: Any) -> int:
        try:
            ptr = memory.data_ptr(caller)
        except TypeError:
            ptr = memory.data_ptr()
        return ctypes.addressof(ptr.contents)

    @staticmethod
    def _memory_len(caller: Any, memory: Any) -> int:
        try:
            return int(memory.data_len(caller))
        except TypeError:
            return int(memory.data_len())

    def _read(self, caller: Any, ptr: int, size: int) -> bytes:
        if ptr == 0 or size <= 0:
            return b""
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr + size > data_len:
            raise RuntimeError(
                f"guest memory read out of bounds: ptr={ptr} size={size}"
            )
        return ctypes.string_at(self._memory_base(caller, memory) + ptr, size)

    def _write(self, caller: Any, ptr: int, data: bytes) -> None:
        if ptr == 0:
            if data:
                raise RuntimeError("attempted to write through a null pointer")
            return
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr + len(data) > data_len:
            raise RuntimeError(
                f"guest memory write out of bounds: ptr={ptr} size={len(data)}"
            )
        ctypes.memmove(self._memory_base(caller, memory) + ptr, data,
                       len(data))

    def _read_i32(self, caller: Any, ptr: int) -> int:
        return int.from_bytes(self._read(caller, ptr, 4), "little",
                              signed=True)

    def _read_i32_array(self, caller: Any, ptr: int, count: int) -> list[int]:
        return [self._read_i32(caller, ptr + i * 4) for i in range(count)]

    def _write_i32(self, caller: Any, ptr: int, value: int) -> None:
        if ptr:
            self._write(caller, ptr,
                        int(value).to_bytes(4, "little", signed=True))

    def _write_u32_array(self, caller: Any, ptr: int,
                         values: list[int]) -> None:
        for i, value in enumerate(values):
            self._write(caller, ptr + i * 4,
                        int(value & 0xffffffff).to_bytes(
                            4, "little", signed=False))

    def _write_i32_array(self, caller: Any, ptr: int,
                         values: list[int]) -> None:
        for i, value in enumerate(values):
            self._write_i32(caller, ptr + i * 4, int(value))

    def _write_f32_array(self, caller: Any, ptr: int,
                         values: list[float]) -> None:
        if not ptr or not values:
            return
        self._write(caller, ptr,
                    struct.pack("<" + "f" * len(values), *values))

    def _read_f32_array(self, caller: Any, ptr: int, count: int) -> list[float]:
        if ptr == 0 or count <= 0:
            return []
        raw = self._read(caller, ptr, count * 4)
        return list(struct.unpack("<" + "f" * count, raw))

    def _read_c_string(self, caller: Any, ptr: int) -> bytes:
        if ptr == 0:
            return b""
        memory = self._memory(caller)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr >= data_len:
            raise RuntimeError(f"guest string pointer out of bounds: {ptr}")
        data = ctypes.string_at(self._memory_base(caller, memory) + ptr,
                                data_len - ptr)
        end = data.find(0)
        if end < 0:
            raise RuntimeError(f"unterminated string at guest pointer {ptr}")
        return bytes(data[:end])

    def _guest_c_string(self, caller: Any, value: bytes | str | None) -> int:
        if value is None:
            return 0
        raw = value.encode("utf-8") if isinstance(value, str) else bytes(value)
        raw = raw.rstrip(b"\0") + b"\0"
        cached = self._guest_strings.get(raw)
        if cached:
            return cached
        malloc = caller.get("malloc")
        if malloc is None:
            malloc = caller.get("_malloc")
        if malloc is None:
            return 0
        ptr = int(malloc(caller, len(raw)))
        if ptr == 0:
            return 0
        self._write(caller, ptr, raw)
        self._guest_strings[raw] = ptr
        return ptr

    def _read_shader_sources(self, caller: Any, count: int, strings_ptr: int,
                             lengths_ptr: int) -> list[str]:
        ptrs = self._read_i32_array(caller, strings_ptr, count)
        lengths = (
            self._read_i32_array(caller, lengths_ptr, count)
            if lengths_ptr else [-1] * count
        )
        sources: list[str] = []
        for ptr, length in zip(ptrs, lengths):
            if length is None or length < 0:
                raw = self._read_c_string(caller, ptr)
            else:
                raw = self._read(caller, ptr, length)
            sources.append(raw.decode("utf-8", errors="replace"))
        return sources

    def _write_gl_string(self, caller: Any, buf_size: int, length_ptr: int,
                         buf_ptr: int, text: str) -> None:
        raw = text.encode("utf-8")
        if length_ptr:
            self._write_i32(caller, length_ptr, min(len(raw), max(buf_size - 1, 0)))
        if buf_ptr and buf_size > 0:
            clipped = raw[:max(buf_size - 1, 0)]
            self._write(caller, buf_ptr, clipped + b"\0")

    def _translate_shader_source(self, source: str, shader_type: int) -> str:
        if self._is_gles:
            return source

        text = re.sub(r"^\s*precision\s+\w+\s+\w+\s*;\s*$", "",
                      source, flags=re.MULTILINE)
        text = re.sub(r"\b(?:lowp|mediump|highp)\s+", "", text)

        if self._profile == "desktop_core":
            text, had_version = re.subn(r"^\s*#version\s+\d+\s*$",
                                        "#version 150",
                                        text,
                                        count=1,
                                        flags=re.MULTILINE)
            if had_version == 0:
                text = "#version 150\n" + text
            if shader_type == self._gl.GL_VERTEX_SHADER:
                text = re.sub(r"\battribute\b", "in", text)
                text = re.sub(r"\bvarying\b", "out", text)
            elif shader_type == self._gl.GL_FRAGMENT_SHADER:
                text = re.sub(r"\bvarying\b", "in", text)
                text = re.sub(r"\btexture2D\b", "texture", text)
                text = re.sub(r"\btextureCube\b", "texture", text)
                if "gl_FragColor" in text:
                    lines = text.splitlines()
                    insert_at = 1 if lines and lines[0].startswith("#version") else 0
                    lines.insert(insert_at, "out vec4 fragColor;")
                    text = "\n".join(lines).replace("gl_FragColor",
                                                    "fragColor")
        else:
            text, had_version = re.subn(r"^\s*#version\s+\d+\s*$",
                                        "#version 120",
                                        text,
                                        count=1,
                                        flags=re.MULTILINE)
        return text

    def _pixel_byte_width(self, width: int, fmt: int, typ: int) -> int:
        GL = self._gl
        if typ == GL.GL_UNSIGNED_BYTE:
            channels = {
                GL.GL_RGBA: 4,
                getattr(GL, "GL_BGRA", 0x80E1): 4,
                GL.GL_RGB: 3,
                getattr(GL, "GL_BGR", 0x80E0): 3,
                GL.GL_ALPHA: 1,
                GL.GL_LUMINANCE: 1,
                GL.GL_LUMINANCE_ALPHA: 2,
            }.get(fmt)
            if channels is None:
                raise RuntimeError(
                    f"unsupported unsigned-byte texture format 0x{fmt:x}"
                )
            return width * channels
        if typ in {
            GL.GL_UNSIGNED_SHORT_4_4_4_4,
            GL.GL_UNSIGNED_SHORT_5_5_5_1,
            GL.GL_UNSIGNED_SHORT_5_6_5,
        }:
            return width * 2
        if typ in {
            getattr(GL, "GL_UNSIGNED_INT_8_8_8_8", 0x8035),
            getattr(GL, "GL_UNSIGNED_INT_8_8_8_8_REV", 0x8367),
        }:
            return width * 4
        raise RuntimeError(f"unsupported texture type 0x{typ:x}")

    @staticmethod
    def _align(value: int, alignment: int) -> int:
        if alignment <= 1:
            return value
        return (value + alignment - 1) & ~(alignment - 1)

    def _texture_byte_size(self, width: int, height: int, fmt: int,
                           typ: int, *, row_length: int = 0,
                           alignment: int = 1) -> int:
        if width <= 0 or height <= 0:
            return 0
        effective_width = row_length if row_length > 0 else width
        if effective_width < width:
            effective_width = width
        row_stride = self._align(
            self._pixel_byte_width(effective_width, fmt, typ), alignment)
        last_row = self._pixel_byte_width(width, fmt, typ)
        return (height - 1) * row_stride + last_row

    def _unpack_byte_size(self, width: int, height: int, fmt: int,
                          typ: int) -> int:
        return self._texture_byte_size(
            width, height, fmt, typ,
            row_length=self._pixel_store.get(0x0CF2, 0),
            alignment=self._pixel_store.get(0x0CF5, 4))

    def _pack_byte_size(self, width: int, height: int, fmt: int,
                        typ: int) -> int:
        return self._texture_byte_size(
            width, height, fmt, typ,
            row_length=self._pixel_store.get(0x0D02, 0),
            alignment=self._pixel_store.get(0x0D05, 4))

    def _normalize_texture_formats(self, internalformat: int,
                                   fmt: int) -> tuple[int, int]:
        if self._is_gles:
            return internalformat, fmt
        GL = self._gl
        alpha = getattr(GL, "GL_ALPHA", 0x1906)
        luminance = getattr(GL, "GL_LUMINANCE", 0x1909)
        luminance_alpha = getattr(GL, "GL_LUMINANCE_ALPHA", 0x190A)
        red = getattr(GL, "GL_RED", 0x1903)
        rg = getattr(GL, "GL_RG", 0x8227)
        r8 = getattr(GL, "GL_R8", red)
        rg8 = getattr(GL, "GL_RG8", rg)
        if fmt == alpha:
            return r8, red
        if fmt == luminance:
            return r8, red
        if fmt == luminance_alpha:
            return rg8, rg
        return internalformat, fmt

    def _pixel_buffer(self, caller: Any, ptr: int, width: int, height: int,
                      fmt: int, typ: int) -> Any:
        if ptr == 0:
            return None
        size = self._unpack_byte_size(width, height, fmt, typ)
        return self._buffer_ptr(caller, size, ptr)

    @staticmethod
    def _values_as_list(value: Any) -> list[Any]:
        if value is None:
            return []
        if isinstance(value, (bytes, bytearray)):
            return list(value)
        if isinstance(value, (list, tuple)):
            return list(value)
        if hasattr(value, "tolist"):
            listed = value.tolist()
            return listed if isinstance(listed, list) else [listed]
        try:
            return list(value)
        except TypeError:
            return [value]

    @staticmethod
    def _active_info_item_name(item: Any) -> bytes:
        if isinstance(item, (int, float)):
            return b""
        if isinstance(item, bytes):
            raw = item
        elif isinstance(item, str):
            raw = item.encode("utf-8")
        else:
            try:
                raw = bytes(item)
            except (TypeError, ValueError):
                return b""
        return raw.split(b"\0", 1)[0]

    def _active_info_parts(self, result: Any) -> tuple[bytes, int, int]:
        if result is None:
            return b"", 0, 0
        name = b""
        numeric: list[int] = []
        for item in result:
            item_name = self._active_info_item_name(item)
            if item_name:
                name = item_name
                continue
            try:
                numeric.append(int(item))
            except (TypeError, ValueError):
                continue
        if len(numeric) >= 2:
            return name, int(numeric[0]), int(numeric[1])
        return name, 0, 0

    def _active_info_name(self, result: Any) -> bytes:
        return self._active_info_parts(result)[0]

    def _active_attrib_locations(self, program: int) -> set[int]:
        cached = self._program_active_attribs.get(program)
        if cached is not None:
            return cached
        GL = self._gl
        active: set[int] = set()
        if program:
            count = int(GL.glGetProgramiv(program, GL.GL_ACTIVE_ATTRIBUTES))
            for index in range(count):
                name = self._active_info_name(
                    GL.glGetActiveAttrib(program, index))
                if not name:
                    continue
                base_name = name.split(b"[", 1)[0]
                loc = int(GL.glGetAttribLocation(program, base_name))
                if loc >= 0:
                    active.add(loc)
        self._program_active_attribs[program] = active
        return active

    def _enabled_attribs(self) -> list[int]:
        GL = self._gl
        max_attribs = max(16, int(GL.glGetIntegerv(
            GL.GL_MAX_VERTEX_ATTRIBS)))
        return [
            index for index in range(max_attribs)
            if self._attrib_enabled(index)
        ]

    def _attrib_enabled(self, index: int) -> bool:
        value = self._gl.glGetVertexAttribiv(
            index, self._gl.GL_VERTEX_ATTRIB_ARRAY_ENABLED)
        values = self._values_as_list(value)
        return bool(values and int(values[0]))

    def _suspend_inactive_attribs(self) -> list[int]:
        GL = self._gl
        program = int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM))
        active = self._active_attrib_locations(program)
        if not active:
            active = set(self._array_attribs) | set(self._client_attribs)
        disabled: list[int] = []
        for index in self._enabled_attribs():
            if index not in active:
                GL.glDisableVertexAttribArray(index)
                disabled.append(index)
        return disabled

    def _restore_attribs(self, indices: list[int]) -> None:
        for index in indices:
            self._gl.glEnableVertexAttribArray(index)

    def _pointer_or_offset(self, caller: Any, target: int, ptr: int,
                           byte_size: int) -> Any:
        if ptr == 0:
            return None
        if self._bound_buffers.get(target, 0) != 0:
            return ctypes.c_void_p(ptr)
        return self._buffer_ptr(caller, byte_size, ptr)

    def _gl_type_size(self, typ: int) -> int:
        GL = self._gl
        sizes = {
            GL.GL_BYTE: 1,
            GL.GL_UNSIGNED_BYTE: 1,
            GL.GL_SHORT: 2,
            GL.GL_UNSIGNED_SHORT: 2,
            GL.GL_INT: 4,
            GL.GL_UNSIGNED_INT: 4,
            GL.GL_FLOAT: 4,
        }
        if typ not in sizes:
            raise RuntimeError(f"unsupported GL scalar type 0x{typ:x}")
        return sizes[typ]

    def _client_buffer_id(self, index: int) -> int:
        buffer = self._client_attrib_buffers.get(index)
        if buffer:
            return buffer
        generated = self._gl.glGenBuffers(1)
        if isinstance(generated, (list, tuple)):
            generated = generated[0]
        buffer = int(generated)
        self._client_attrib_buffers[index] = buffer
        return buffer

    def _ensure_client_element_buffer(self) -> int:
        if self._client_element_buffer:
            return self._client_element_buffer
        generated = self._gl.glGenBuffers(1)
        if isinstance(generated, (list, tuple)):
            generated = generated[0]
        self._client_element_buffer = int(generated)
        return self._client_element_buffer

    def _upload_client_attribs(self, caller: Any, first: int,
                               vertex_count: int) -> None:
        if vertex_count <= 0:
            return
        GL = self._gl
        previous_array_buffer = self._bound_buffers.get(GL.GL_ARRAY_BUFFER, 0)
        try:
            for index, (size, typ, normalized, stride, pointer) in (
                    self._client_attribs.items()):
                scalar_size = self._gl_type_size(typ)
                element_size = size * scalar_size
                effective_stride = stride or element_size
                start = pointer + first * effective_stride
                byte_size = ((vertex_count - 1) * effective_stride +
                             element_size)
                data = self._read(caller, start, byte_size)
                buffer = self._client_buffer_id(index)
                GL.glBindBuffer(GL.GL_ARRAY_BUFFER, buffer)
                GL.glBufferData(GL.GL_ARRAY_BUFFER, len(data), data,
                                GL.GL_STREAM_DRAW)
                GL.glVertexAttribPointer(index, size, typ, bool(normalized),
                                         effective_stride, ctypes.c_void_p(0))
        finally:
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, previous_array_buffer)

    def _read_indices(self, caller: Any, ptr: int, count: int,
                      typ: int) -> tuple[bytes, int]:
        bytes_per_index = {
            self._gl.GL_UNSIGNED_BYTE: 1,
            self._gl.GL_UNSIGNED_SHORT: 2,
            self._gl.GL_UNSIGNED_INT: 4,
        }.get(typ)
        if bytes_per_index is None:
            raise RuntimeError(f"unsupported draw index type 0x{typ:x}")
        raw = self._read(caller, ptr, count * bytes_per_index)
        if not raw:
            return raw, 0
        if typ == self._gl.GL_UNSIGNED_BYTE:
            max_index = max(raw)
        elif typ == self._gl.GL_UNSIGNED_SHORT:
            values = struct.unpack("<" + "H" * count, raw)
            max_index = max(values) if values else 0
        else:
            values = struct.unpack("<" + "I" * count, raw)
            max_index = max(values) if values else 0
        return raw, int(max_index)

    def _buffer_ptr(self, caller: Any, size: int, ptr: int) -> Any:
        if ptr == 0:
            return None
        memory = self._memory(caller)
        get_buffer_ptr = getattr(memory, "get_buffer_ptr", None)
        if get_buffer_ptr is not None:
            try:
                return get_buffer_ptr(caller, size, ptr)
            except TypeError:
                return get_buffer_ptr(size, ptr)
        data_len = self._memory_len(caller, memory)
        if ptr < 0 or ptr + size > data_len:
            raise RuntimeError(
                f"guest buffer out of bounds: ptr={ptr} size={size}"
            )
        return (ctypes.c_ubyte * size).from_address(
            self._memory_base(caller, memory) + ptr)

    def glPixelStorei(self, caller: Any, pname: int, param: int) -> None:
        def run() -> None:
            self._pixel_store[int(pname)] = int(param)
            self._gl.glPixelStorei(pname, param)
        return self._call("glPixelStorei", (pname, param), run)

    def glGenTextures(self, caller: Any, n: int, textures: int) -> None:
        def run() -> None:
            ids = self._gl.glGenTextures(n)
            if n == 1 and isinstance(ids, int):
                values = [ids]
            else:
                values = [int(v) for v in ids]
            self._write_u32_array(caller, textures, values)
        return self._call("glGenTextures", (n, textures), run)

    def glTexParameteri(self, caller: Any, target: int, pname: int,
                        param: int) -> None:
        return self._call("glTexParameteri", (target, pname, param),
                          lambda: self._gl.glTexParameteri(target, pname,
                                                           param))

    def glGetError(self, caller: Any) -> int:
        return int(self._call("glGetError", (), self._gl.glGetError))

    def glGetIntegerv(self, caller: Any, pname: int, params: int) -> None:
        def run() -> None:
            values = [int(v) for v in self._values_as_list(
                self._gl.glGetIntegerv(pname)
            )]
            self._write_i32_array(caller, params, values)
        return self._call("glGetIntegerv", (pname, params), run)

    def glGetFloatv(self, caller: Any, pname: int, params: int) -> None:
        def run() -> None:
            values = [float(v) for v in self._values_as_list(
                self._gl.glGetFloatv(pname)
            )]
            self._write_f32_array(caller, params, values)
        return self._call("glGetFloatv", (pname, params), run)

    def glGetString(self, caller: Any, name: int) -> int:
        def run() -> int:
            try:
                value = self._gl.glGetString(name)
            except Exception:
                if name == self._gl.GL_EXTENSIONS:
                    value = self._extension_string()
                else:
                    raise
            if name == self._gl.GL_EXTENSIONS and not value:
                value = self._extension_string()
            if self._trace_calls:
                text = self._bytes_to_text(value) if value else "<none>"
                print(f"[wasmtime-gl] glGetString({name}) -> {text}",
                      file=sys.stderr)
            return self._guest_c_string(caller, value)
        return int(self._call("glGetString", (name,), run))

    def glViewport(self, caller: Any, x: int, y: int, width: int,
                   height: int) -> None:
        def run() -> None:
            if self._bound_framebuffer == 0:
                self._default_framebuffer_viewport = (
                    int(x), int(y), int(width), int(height))
            self._gl.glViewport(x, y, width, height)
        return self._call("glViewport", (x, y, width, height), run)

    def read_default_framebuffer_bgra(self, width: int, height: int) -> bytes:
        """Read the window default framebuffer as top-left-origin BGRA32."""
        width = int(width)
        height = int(height)
        self._ensure_window_size(width, height)
        GL = self._gl
        row_bytes = width * 4
        size = row_bytes * height
        read_target = getattr(GL, "GL_READ_FRAMEBUFFER", GL.GL_FRAMEBUFFER)
        read_binding_enum = getattr(
            GL, "GL_READ_FRAMEBUFFER_BINDING",
            getattr(GL, "GL_FRAMEBUFFER_BINDING", 0x8CA6))
        old_read_fb = int(GL.glGetIntegerv(read_binding_enum))
        old_pack_alignment = int(GL.glGetIntegerv(GL.GL_PACK_ALIGNMENT))
        old_read_buffer = None
        if not self._is_gles and hasattr(GL, "GL_READ_BUFFER"):
            try:
                old_read_buffer = int(GL.glGetIntegerv(GL.GL_READ_BUFFER))
            except Exception:
                old_read_buffer = None

        data = (ctypes.c_ubyte * size)()
        read_buffer_name = self._final_read_buffer or "back"
        try:
            GL.glBindFramebuffer(read_target, 0)
            selected_read_buffer = None
            if not self._is_gles and hasattr(GL, "glReadBuffer"):
                read_buffers = {
                    "back": getattr(GL, "GL_BACK", None),
                    "front": getattr(GL, "GL_FRONT", None),
                }
                selected_read_buffer = read_buffers.get(read_buffer_name)
                if selected_read_buffer is not None:
                    GL.glReadBuffer(selected_read_buffer)
            GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
            GL.glReadPixels(0, 0, width, height, GL.GL_RGBA,
                            GL.GL_UNSIGNED_BYTE, data)
            gl_error = None
            try:
                gl_error = int(GL.glGetError())
            except Exception:
                pass
            self._last_default_framebuffer_read = {
                "width": width,
                "height": height,
                "readBuffer": read_buffer_name,
                "readBufferEnum": (
                    int(selected_read_buffer)
                    if selected_read_buffer is not None else None),
                "oldReadFramebuffer": old_read_fb,
                "oldReadBuffer": old_read_buffer,
                "glError": gl_error,
            }
        finally:
            try:
                if old_read_buffer is not None:
                    GL.glReadBuffer(old_read_buffer)
            finally:
                GL.glBindFramebuffer(read_target, old_read_fb)
                GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, old_pack_alignment)

        rgba = bytes(data)
        bgra = bytearray(size)
        for dst_y, src_y in enumerate(range(height - 1, -1, -1)):
            src_off = src_y * row_bytes
            dst_off = dst_y * row_bytes
            row = rgba[src_off:src_off + row_bytes]
            bgra[dst_off:dst_off + row_bytes:4] = row[2::4]
            bgra[dst_off + 1:dst_off + row_bytes:4] = row[1::4]
            bgra[dst_off + 2:dst_off + row_bytes:4] = row[0::4]
            bgra[dst_off + 3:dst_off + row_bytes:4] = row[3::4]
        return bytes(bgra)

    def last_default_framebuffer_read_diagnostics(self) -> dict[str, Any]:
        return dict(self._last_default_framebuffer_read)

    def glTexImage2D(self, caller: Any, target: int, level: int,
                     internalformat: int, width: int, height: int,
                     border: int, fmt: int, typ: int, pixels: int) -> None:
        def run() -> None:
            size = self._unpack_byte_size(width, height, fmt, typ)
            data = self._pixel_buffer(caller, pixels, width, height, fmt, typ)
            bound_texture = self._bound_texture(target)
            self._record_texture_probe(
                "glTexImage2D", target, level, internalformat, width, height,
                fmt, typ, pixels, data, size, border=int(border),
                boundTexture=int(bound_texture))
            if bound_texture:
                raw = bytes(data) if data is not None and size > 0 else b""
                self._texture_info[bound_texture] = {
                    "width": int(width),
                    "height": int(height),
                    "format": int(fmt),
                    "internalformat": int(internalformat),
                    "type": int(typ),
                    "bytes": len(raw),
                    "sha256": (
                        hashlib.sha256(raw).hexdigest() if raw else None),
                    "prefixHex": raw[:32].hex(),
                }
            upload_internal, upload_format = self._normalize_texture_formats(
                internalformat, fmt)
            self._gl.glTexImage2D(target, level, upload_internal, width,
                                  height, border, upload_format, typ, data)
        return self._call("glTexImage2D",
                          (target, level, internalformat, width, height,
                           border, fmt, typ, pixels), run)

    def glCompressedTexImage2D(self, caller: Any, target: int, level: int,
                               internalformat: int, width: int, height: int,
                               border: int, image_size: int,
                               data_ptr: int) -> None:
        def run() -> None:
            data = (
                None if data_ptr == 0 else
                self._buffer_ptr(caller, image_size, data_ptr)
            )
            self._gl.glCompressedTexImage2D(target, level, internalformat,
                                            width, height, border,
                                            image_size, data)
        return self._call("glCompressedTexImage2D",
                          (target, level, internalformat, width, height,
                           border, image_size, data_ptr), run)

    def _record_readpixels_probe(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        fmt: int,
        typ: int,
        pixels: int,
        data: Any,
        size: int,
    ) -> None:
        path = self._readpixels_probe_path
        if not path or self._readpixels_probe_count >= self._readpixels_probe_limit:
            return
        if data is None or size <= 0:
            return
        try:
            raw = bytes(data)
        except Exception as exc:
            raw = b""
            error = str(exc)
        else:
            error = None
        record = {
            "index": self._readpixels_probe_count,
            "x": int(x),
            "y": int(y),
            "width": int(width),
            "height": int(height),
            "format": int(fmt),
            "type": int(typ),
            "guestPixels": int(pixels),
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest() if raw else None,
            "prefixHex": raw[:64].hex(),
            "suffixHex": raw[-64:].hex() if raw else "",
        }
        if error is not None:
            record["error"] = error
        with open(path, "a", encoding="utf-8") as file:
            file.write(json.dumps(record, sort_keys=True) + "\n")
        self._readpixels_probe_count += 1

    def _record_texture_probe(
        self,
        kind: str,
        target: int,
        level: int,
        internalformat: int | None,
        width: int,
        height: int,
        fmt: int,
        typ: int,
        pixels: int,
        data: Any,
        size: int,
        **extra: Any,
    ) -> None:
        path = self._texture_probe_path
        if not path or self._texture_probe_count >= self._texture_probe_limit:
            return
        raw = b""
        error = None
        if data is not None and size > 0:
            try:
                raw = bytes(data)
            except Exception as exc:
                error = str(exc)
        record = {
            "index": self._texture_probe_count,
            "kind": kind,
            "target": int(target),
            "level": int(level),
            "internalformat": (
                None if internalformat is None else int(internalformat)),
            "width": int(width),
            "height": int(height),
            "format": int(fmt),
            "type": int(typ),
            "guestPixels": int(pixels),
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest() if raw else None,
            "prefixHex": raw[:64].hex(),
            "suffixHex": raw[-64:].hex() if raw else "",
        }
        record.update(extra)
        if error is not None:
            record["error"] = error
        with open(path, "a", encoding="utf-8") as file:
            file.write(json.dumps(record, sort_keys=True) + "\n")
        self._texture_probe_count += 1

    def _bound_texture(self, target: int) -> int:
        return self._bound_textures.get((self._active_texture_unit, target), 0)

    def _texture_summary(self, texture: int) -> dict[str, Any] | None:
        info = self._texture_info.get(texture)
        if not info:
            return None
        return dict(info)

    def _record_draw_probe(self, kind: str, mode: int, first_or_count: int,
                           count: int) -> None:
        path = self._draw_probe_path
        if not path or self._draw_probe_count >= self._draw_probe_limit:
            return
        GL = self._gl
        try:
            program = int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM))
        except Exception:
            program = 0
        attached = []
        for shader in sorted(self._program_shaders.get(program, set())):
            attached.append({
                "shader": shader,
                "type": self._shader_types.get(shader, 0),
                "sourcePrefix": self._shader_sources.get(shader, "")[:320],
            })
        if not attached:
            attached = self._program_shader_sources.get(program, [])
        framebuffer = self._bound_framebuffer
        target_texture = self._framebuffer_color_texture.get(framebuffer, 0)
        textures = []
        texture_2d = GL.GL_TEXTURE_2D
        for unit in sorted({unit for unit, target in self._bound_textures
                            if target == texture_2d}):
            texture = self._bound_textures.get((unit, texture_2d), 0)
            textures.append({
                "unit": unit,
                "texture": texture,
                "info": self._texture_summary(texture),
            })
        record = {
            "index": self._draw_probe_count,
            "kind": kind,
            "mode": int(mode),
            "firstOrCount": int(first_or_count),
            "count": int(count),
            "program": program,
            "framebuffer": framebuffer,
            "targetTexture": target_texture,
            "targetInfo": self._texture_summary(target_texture),
            "textures": textures,
            "attachedShaders": attached,
        }
        with open(path, "a", encoding="utf-8") as file:
            file.write(json.dumps(record, sort_keys=True) + "\n")
        self._draw_probe_count += 1

    def glReadPixels(self, caller: Any, x: int, y: int, width: int,
                     height: int, fmt: int, typ: int, pixels: int) -> None:
        def run() -> None:
            size = self._pack_byte_size(width, height, fmt, typ)
            data = None
            if pixels:
                data = (ctypes.c_ubyte * size)()
            self._gl.glReadPixels(x, y, width, height, fmt, typ, data)
            if pixels and data is not None:
                self._write(caller, pixels, bytes(data))
            self._record_readpixels_probe(
                x, y, width, height, fmt, typ, pixels, data, size)
        return self._call("glReadPixels",
                          (x, y, width, height, fmt, typ, pixels), run)

    def glCreateProgram(self, caller: Any) -> int:
        return int(self._call("glCreateProgram", (),
                              self._gl.glCreateProgram))

    def glAttachShader(self, caller: Any, program: int, shader: int) -> None:
        def run() -> None:
            self._program_shaders.setdefault(program, set()).add(shader)
            self._gl.glAttachShader(program, shader)
        return self._call("glAttachShader", (program, shader), run)

    def glLinkProgram(self, caller: Any, program: int) -> None:
        def run() -> None:
            self._gl.glLinkProgram(program)
            self._program_active_attribs.pop(program, None)
            self._program_shader_sources[program] = [
                {
                    "shader": shader,
                    "type": self._shader_types.get(shader, 0),
                    "sourcePrefix": self._shader_sources.get(shader, "")[:320],
                }
                for shader in sorted(self._program_shaders.get(program, set()))
            ]
        return self._call("glLinkProgram", (program,), run)

    def glGetProgramiv(self, caller: Any, program: int, pname: int,
                       params: int) -> None:
        def run() -> None:
            value = self._gl.glGetProgramiv(program, pname)
            self._write_i32(caller, params, int(value))
        return self._call("glGetProgramiv", (program, pname, params), run)

    def glDeleteShader(self, caller: Any, shader: int) -> None:
        def run() -> None:
            self._gl.glDeleteShader(shader)
            self._shader_types.pop(shader, None)
            self._shader_sources.pop(shader, None)
        return self._call("glDeleteShader", (shader,), run)

    def glGetUniformLocation(self, caller: Any, program: int,
                             name_ptr: int) -> int:
        def run() -> int:
            name = self._read_c_string(caller, name_ptr)
            return int(self._gl.glGetUniformLocation(program, name))
        return int(self._call("glGetUniformLocation", (program, name_ptr),
                              run))

    def glUniform1i(self, caller: Any, location: int, v0: int) -> None:
        return self._call("glUniform1i", (location, v0),
                          lambda: self._gl.glUniform1i(location, v0))

    def glUniform1f(self, caller: Any, location: int, v0: float) -> None:
        return self._call("glUniform1f", (location, v0),
                          lambda: self._gl.glUniform1f(location, v0))

    def glUniform2f(self, caller: Any, location: int, v0: float,
                    v1: float) -> None:
        return self._call("glUniform2f", (location, v0, v1),
                          lambda: self._gl.glUniform2f(location, v0, v1))

    def glUniform3f(self, caller: Any, location: int, v0: float,
                    v1: float, v2: float) -> None:
        return self._call("glUniform3f", (location, v0, v1, v2),
                          lambda: self._gl.glUniform3f(location, v0, v1, v2))

    def glUniform4f(self, caller: Any, location: int, v0: float,
                    v1: float, v2: float, v3: float) -> None:
        return self._call("glUniform4f", (location, v0, v1, v2, v3),
                          lambda: self._gl.glUniform4f(location, v0, v1, v2,
                                                       v3))

    def _uniform_fv(self, caller: Any, name: str, location: int, count: int,
                    values_ptr: int, width: int, fn: Any) -> None:
        def run() -> None:
            values = self._read_f32_array(caller, values_ptr, count * width)
            fn(location, count, values)
        return self._call(name, (location, count, values_ptr), run)

    def glUniform1fv(self, caller: Any, location: int, count: int,
                     values: int) -> None:
        return self._uniform_fv(caller, "glUniform1fv", location, count,
                                values, 1, self._gl.glUniform1fv)

    def glUniform2fv(self, caller: Any, location: int, count: int,
                     values: int) -> None:
        return self._uniform_fv(caller, "glUniform2fv", location, count,
                                values, 2, self._gl.glUniform2fv)

    def glUniform3fv(self, caller: Any, location: int, count: int,
                     values: int) -> None:
        return self._uniform_fv(caller, "glUniform3fv", location, count,
                                values, 3, self._gl.glUniform3fv)

    def glUniform4fv(self, caller: Any, location: int, count: int,
                     values: int) -> None:
        return self._uniform_fv(caller, "glUniform4fv", location, count,
                                values, 4, self._gl.glUniform4fv)

    def glUniformMatrix3fv(self, caller: Any, location: int, count: int,
                           transpose: int, values: int) -> None:
        def run() -> None:
            data = self._read_f32_array(caller, values, count * 9)
            self._gl.glUniformMatrix3fv(location, count, bool(transpose), data)
        return self._call("glUniformMatrix3fv",
                          (location, count, transpose, values), run)

    def glUniformMatrix4fv(self, caller: Any, location: int, count: int,
                           transpose: int, values: int) -> None:
        def run() -> None:
            data = self._read_f32_array(caller, values, count * 16)
            self._gl.glUniformMatrix4fv(location, count, bool(transpose), data)
        return self._call("glUniformMatrix4fv",
                          (location, count, transpose, values), run)

    def glCreateShader(self, caller: Any, shader_type: int) -> int:
        def run() -> int:
            shader = int(self._gl.glCreateShader(shader_type))
            self._shader_types[shader] = shader_type
            return shader
        return int(self._call("glCreateShader", (shader_type,), run))

    def glShaderSource(self, caller: Any, shader: int, count: int,
                       strings: int, lengths: int) -> None:
        def run() -> None:
            sources = self._read_shader_sources(caller, count, strings,
                                                lengths)
            source = "".join(sources)
            shader_type = self._shader_types.get(shader, 0)
            translated = self._translate_shader_source(source, shader_type)
            self._shader_sources[shader] = translated
            self._gl.glShaderSource(shader, [translated])
        return self._call("glShaderSource",
                          (shader, count, strings, lengths), run)

    def glCompileShader(self, caller: Any, shader: int) -> None:
        def run() -> None:
            self._gl.glCompileShader(shader)
            status = int(self._gl.glGetShaderiv(
                shader, self._gl.GL_COMPILE_STATUS))
            if status:
                self._shader_compile_logs.pop(shader, None)
                return
            log = self._bytes_to_text(self._gl.glGetShaderInfoLog(shader))
            self._shader_compile_logs[shader] = log
            if self._trace_calls:
                source = self._shader_sources.get(shader, "")
                print(
                    f"[wasmtime-gl] shader {shader} compile failed: "
                    f"{log}\n{source}",
                    file=sys.stderr,
                )
        return self._call("glCompileShader", (shader,), run)

    def glGetShaderiv(self, caller: Any, shader: int, pname: int,
                      params: int) -> None:
        def run() -> None:
            value = self._gl.glGetShaderiv(shader, pname)
            self._write_i32(caller, params, int(value))
        return self._call("glGetShaderiv", (shader, pname, params), run)

    def glGetShaderSource(self, caller: Any, shader: int, buf_size: int,
                          length: int, source_ptr: int) -> None:
        def run() -> None:
            source = self._bytes_to_text(self._gl.glGetShaderSource(shader))
            if not source:
                source = self._shader_sources.get(shader, "")
            self._write_gl_string(caller, buf_size, length, source_ptr,
                                  source)
        return self._call("glGetShaderSource",
                          (shader, buf_size, length, source_ptr), run)

    def glGetShaderInfoLog(self, caller: Any, shader: int, buf_size: int,
                           length: int, info_log: int) -> None:
        def run() -> None:
            log = self._bytes_to_text(self._gl.glGetShaderInfoLog(shader))
            self._write_gl_string(caller, buf_size, length, info_log, log)
        return self._call("glGetShaderInfoLog",
                          (shader, buf_size, length, info_log), run)

    def glBindAttribLocation(self, caller: Any, program: int, index: int,
                             name_ptr: int) -> None:
        def run() -> None:
            name = self._read_c_string(caller, name_ptr)
            self._gl.glBindAttribLocation(program, index, name)
        return self._call("glBindAttribLocation",
                          (program, index, name_ptr), run)

    def _write_active_info(self, caller: Any, result: Any, buf_size: int,
                           length: int, size_ptr: int, type_ptr: int,
                           name_ptr: int) -> None:
        name, size, typ = self._active_info_parts(result)
        text = name.decode("utf-8", errors="replace")
        self._write_i32(caller, size_ptr, int(size))
        self._write_i32(caller, type_ptr, int(typ))
        self._write_gl_string(caller, buf_size, length, name_ptr, text)

    def _record_uniform_probe(self, program: int, index: int,
                              result: Any) -> None:
        path = self._uniform_probe_path
        if not path:
            return
        name, size, typ = self._active_info_parts(result)
        record = {
            "program": int(program),
            "index": int(index),
            "name": name.decode("utf-8", errors="replace"),
            "size": int(size),
            "type": int(typ),
            "result": repr(result),
        }
        with open(path, "a", encoding="utf-8") as file:
            file.write(json.dumps(record, sort_keys=True) + "\n")

    def glGetActiveAttrib(self, caller: Any, program: int, index: int,
                          buf_size: int, length: int, size_ptr: int,
                          type_ptr: int, name_ptr: int) -> None:
        def run() -> None:
            result = self._gl.glGetActiveAttrib(program, index)
            self._write_active_info(caller, result, buf_size, length,
                                    size_ptr, type_ptr, name_ptr)
        return self._call("glGetActiveAttrib",
                          (program, index, buf_size, length, size_ptr,
                           type_ptr, name_ptr), run)

    def glGetAttribLocation(self, caller: Any, program: int,
                            name_ptr: int) -> int:
        def run() -> int:
            name = self._read_c_string(caller, name_ptr)
            return int(self._gl.glGetAttribLocation(program, name))
        return int(self._call("glGetAttribLocation", (program, name_ptr),
                              run))

    def glGetProgramInfoLog(self, caller: Any, program: int, buf_size: int,
                            length: int, info_log: int) -> None:
        def run() -> None:
            log = self._bytes_to_text(self._gl.glGetProgramInfoLog(program))
            self._write_gl_string(caller, buf_size, length, info_log, log)
        return self._call("glGetProgramInfoLog",
                          (program, buf_size, length, info_log), run)

    def glGetActiveUniform(self, caller: Any, program: int, index: int,
                           buf_size: int, length: int, size_ptr: int,
                           type_ptr: int, name_ptr: int) -> None:
        def run() -> None:
            result = self._gl.glGetActiveUniform(program, index)
            self._record_uniform_probe(program, index, result)
            self._write_active_info(caller, result, buf_size, length,
                                    size_ptr, type_ptr, name_ptr)
        return self._call("glGetActiveUniform",
                          (program, index, buf_size, length, size_ptr,
                           type_ptr, name_ptr), run)

    def glTexSubImage2D(self, caller: Any, target: int, level: int,
                        xoffset: int, yoffset: int, width: int, height: int,
                        fmt: int, typ: int, pixels: int) -> None:
        def run() -> None:
            size = self._unpack_byte_size(width, height, fmt, typ)
            data = self._pixel_buffer(caller, pixels, width, height, fmt, typ)
            bound_texture = self._bound_texture(target)
            self._record_texture_probe(
                "glTexSubImage2D", target, level, None, width, height,
                fmt, typ, pixels, data, size, xoffset=int(xoffset),
                yoffset=int(yoffset), boundTexture=int(bound_texture))
            if bound_texture:
                raw = bytes(data) if data is not None and size > 0 else b""
                self._texture_info[bound_texture] = {
                    "width": int(width),
                    "height": int(height),
                    "format": int(fmt),
                    "internalformat": None,
                    "type": int(typ),
                    "bytes": len(raw),
                    "sha256": (
                        hashlib.sha256(raw).hexdigest() if raw else None),
                    "prefixHex": raw[:32].hex(),
                    "xoffset": int(xoffset),
                    "yoffset": int(yoffset),
                }
            _, upload_format = self._normalize_texture_formats(fmt, fmt)
            self._gl.glTexSubImage2D(target, level, xoffset, yoffset, width,
                                     height, upload_format, typ, data)
        return self._call("glTexSubImage2D",
                          (target, level, xoffset, yoffset, width, height,
                           fmt, typ, pixels), run)

    def glDeleteProgram(self, caller: Any, program: int) -> None:
        def run() -> None:
            self._program_active_attribs.pop(program, None)
            self._program_shaders.pop(program, None)
            self._program_shader_sources.pop(program, None)
            self._gl.glDeleteProgram(program)
        return self._call("glDeleteProgram", (program,), run)

    def glUseProgram(self, caller: Any, program: int) -> None:
        return self._call("glUseProgram", (program,),
                          lambda: self._gl.glUseProgram(program))

    def glActiveTexture(self, caller: Any, texture: int) -> None:
        def run() -> None:
            self._active_texture_unit = int(texture - self._gl.GL_TEXTURE0)
            self._gl.glActiveTexture(texture)
        return self._call("glActiveTexture", (texture,), run)

    def glBindTexture(self, caller: Any, target: int, texture: int) -> None:
        def run() -> None:
            self._bound_textures[(self._active_texture_unit, target)] = texture
            self._gl.glBindTexture(target, texture)
        return self._call("glBindTexture", (target, texture), run)

    def glDeleteTextures(self, caller: Any, n: int, textures: int) -> None:
        def run() -> None:
            ids = self._read_i32_array(caller, textures, n)
            self._gl.glDeleteTextures(ids)
        return self._call("glDeleteTextures", (n, textures), run)

    def glBindVertexArray(self, caller: Any, array: int) -> None:
        if array == 0 and self._default_vao:
            array = self._default_vao
        return self._call("glBindVertexArray", (array,),
                          lambda: self._gl.glBindVertexArray(array))

    def glIsVertexArray(self, caller: Any, array: int) -> int:
        return int(self._call("glIsVertexArray", (array,),
                              lambda: self._gl.glIsVertexArray(array)))

    def glGenVertexArrays(self, caller: Any, n: int, arrays: int) -> None:
        def run() -> None:
            ids = self._gl.glGenVertexArrays(n)
            if n == 1 and isinstance(ids, int):
                values = [ids]
            else:
                values = [int(v) for v in ids]
            self._write_u32_array(caller, arrays, values)
        return self._call("glGenVertexArrays", (n, arrays), run)

    def glGenBuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._gl.glGenBuffers(n)
            values = [ids] if n == 1 and isinstance(ids, int) else [
                int(v) for v in ids
            ]
            self._write_u32_array(caller, buffers, values)
        return self._call("glGenBuffers", (n, buffers), run)

    def glGenFramebuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._gl.glGenFramebuffers(n)
            values = [ids] if n == 1 and isinstance(ids, int) else [
                int(v) for v in ids
            ]
            self._write_u32_array(caller, buffers, values)
        return self._call("glGenFramebuffers", (n, buffers), run)

    def glGenRenderbuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._gl.glGenRenderbuffers(n)
            values = [ids] if n == 1 and isinstance(ids, int) else [
                int(v) for v in ids
            ]
            self._write_u32_array(caller, buffers, values)
        return self._call("glGenRenderbuffers", (n, buffers), run)

    def glDeleteBuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._read_i32_array(caller, buffers, n)
            self._gl.glDeleteBuffers(n, ids)
        return self._call("glDeleteBuffers", (n, buffers), run)

    def glDeleteVertexArrays(self, caller: Any, n: int, arrays: int) -> None:
        def run() -> None:
            ids = self._read_i32_array(caller, arrays, n)
            self._gl.glDeleteVertexArrays(n, ids)
        return self._call("glDeleteVertexArrays", (n, arrays), run)

    def glDeleteFramebuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._read_i32_array(caller, buffers, n)
            self._gl.glDeleteFramebuffers(n, ids)
        return self._call("glDeleteFramebuffers", (n, buffers), run)

    def glDeleteRenderbuffers(self, caller: Any, n: int, buffers: int) -> None:
        def run() -> None:
            ids = self._read_i32_array(caller, buffers, n)
            self._gl.glDeleteRenderbuffers(n, ids)
        return self._call("glDeleteRenderbuffers", (n, buffers), run)

    def glBindBuffer(self, caller: Any, target: int, buffer: int) -> None:
        def run() -> None:
            self._bound_buffers[target] = buffer
            self._gl.glBindBuffer(target, buffer)
        return self._call("glBindBuffer", (target, buffer), run)

    def glBufferData(self, caller: Any, target: int, size: int,
                     data_ptr: int, usage: int) -> None:
        def run() -> None:
            data = (
                None if data_ptr == 0 else
                self._buffer_ptr(caller, size, data_ptr)
            )
            self._gl.glBufferData(target, size, data, usage)
        return self._call("glBufferData", (target, size, data_ptr, usage),
                          run)

    def glBufferSubData(self, caller: Any, target: int, offset: int, size: int,
                        data_ptr: int) -> None:
        def run() -> None:
            data = (
                None if data_ptr == 0 else
                self._buffer_ptr(caller, size, data_ptr)
            )
            self._gl.glBufferSubData(target, offset, size, data)
        return self._call("glBufferSubData",
                          (target, offset, size, data_ptr), run)

    def glEnableVertexAttribArray(self, caller: Any, index: int) -> None:
        return self._call("glEnableVertexAttribArray", (index,),
                          lambda: self._gl.glEnableVertexAttribArray(index))

    def glDisableVertexAttribArray(self, caller: Any, index: int) -> None:
        return self._call("glDisableVertexAttribArray", (index,),
                          lambda: self._gl.glDisableVertexAttribArray(index))

    def glVertexAttribPointer(self, caller: Any, index: int, size: int,
                              typ: int, normalized: int, stride: int,
                              pointer: int) -> None:
        def run() -> None:
            if self._bound_buffers.get(self._gl.GL_ARRAY_BUFFER, 0) != 0:
                self._client_attribs.pop(index, None)
                self._array_attribs.add(index)
                data = ctypes.c_void_p(pointer)
            else:
                self._array_attribs.discard(index)
                self._client_attribs[index] = (
                    size, typ, normalized, stride, pointer)
                return
            self._gl.glVertexAttribPointer(index, size, typ, bool(normalized),
                                           stride, data)
        return self._call("glVertexAttribPointer",
                          (index, size, typ, normalized, stride, pointer), run)

    def glDrawElements(self, caller: Any, mode: int, count: int, typ: int,
                       indices: int) -> None:
        def run() -> None:
            self._record_draw_probe("glDrawElements", mode, count, count)
            GL = self._gl
            if self._bound_buffers.get(GL.GL_ELEMENT_ARRAY_BUFFER, 0) == 0:
                raw_indices, max_index = self._read_indices(
                    caller, indices, count, typ)
                self._upload_client_attribs(caller, 0, max_index + 1)
                previous_element_buffer = self._bound_buffers.get(
                    GL.GL_ELEMENT_ARRAY_BUFFER, 0)
                try:
                    buffer = self._ensure_client_element_buffer()
                    GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, buffer)
                    GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER,
                                    len(raw_indices), raw_indices,
                                    GL.GL_STREAM_DRAW)
                    GL.glDrawElements(mode, count, typ, ctypes.c_void_p(0))
                finally:
                    GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER,
                                    previous_element_buffer)
            else:
                self._upload_client_attribs(caller, 0, count)
                offset = None if indices == 0 else ctypes.c_void_p(indices)
                pre_error = GL.glGetError()
                if pre_error:
                    raise RuntimeError(
                        f"pre-existing GL error 0x{pre_error:x} before "
                        "glDrawElements")
                suspended = self._suspend_inactive_attribs()
                try:
                    self._gl.glDrawElements(mode, count, typ, offset)
                except Exception as exc:
                    program = int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM))
                    try:
                        GL.glValidateProgram(program)
                        validate_status = int(GL.glGetProgramiv(
                            program, GL.GL_VALIDATE_STATUS))
                    except Exception:
                        validate_status = -1
                    attached = [
                        {
                            "shader": shader,
                            "type": self._shader_types.get(shader, 0),
                            "sourcePrefix": self._shader_sources.get(
                                shader, "")[:120],
                        }
                        for shader in sorted(
                            self._program_shaders.get(program, set()))
                    ]
                    state = {
                        "program": program,
                        "linkStatus": int(GL.glGetProgramiv(
                            program, GL.GL_LINK_STATUS)),
                        "validateStatus": validate_status,
                        "programLog": self._bytes_to_text(
                            GL.glGetProgramInfoLog(program))[:240],
                        "attachedShaders": attached,
                        "activeAttribs": sorted(
                            self._active_attrib_locations(program)),
                        "configuredAttribs": sorted(
                            set(self._array_attribs) |
                            set(self._client_attribs)),
                        "arrayBuffer": int(GL.glGetIntegerv(
                            GL.GL_ARRAY_BUFFER_BINDING)),
                        "elementArrayBuffer": int(GL.glGetIntegerv(
                            GL.GL_ELEMENT_ARRAY_BUFFER_BINDING)),
                        "vertexArray": int(GL.glGetIntegerv(
                            getattr(GL, "GL_VERTEX_ARRAY_BINDING", 0x85B5))),
                        "enabledAttribs": [
                            i for i in range(8) if self._attrib_enabled(i)
                        ],
                    }
                    raise RuntimeError(
                        f"{exc}; draw state={state}") from exc
                finally:
                    self._restore_attribs(suspended)
        return self._call("glDrawElements", (mode, count, typ, indices), run)

    def glDrawArrays(self, caller: Any, mode: int, first: int,
                     count: int) -> None:
        def run() -> None:
            self._record_draw_probe("glDrawArrays", mode, first, count)
            self._upload_client_attribs(caller, first, count)
            suspended = self._suspend_inactive_attribs()
            try:
                self._gl.glDrawArrays(mode, first, count)
            finally:
                self._restore_attribs(suspended)
        return self._call("glDrawArrays", (mode, first, count), run)

    def glIsEnabled(self, caller: Any, cap: int) -> int:
        return int(bool(self._call("glIsEnabled", (cap,),
                                   lambda: self._gl.glIsEnabled(cap))))

    def glGetBooleanv(self, caller: Any, pname: int, params: int) -> None:
        def run() -> None:
            values = [1 if bool(v) else 0 for v in self._values_as_list(
                self._gl.glGetBooleanv(pname)
            )]
            self._write(caller, params, bytes(values))
        return self._call("glGetBooleanv", (pname, params), run)

    def glEnable(self, caller: Any, cap: int) -> None:
        return self._call("glEnable", (cap,), lambda: self._gl.glEnable(cap))

    def glDisable(self, caller: Any, cap: int) -> None:
        return self._call("glDisable", (cap,), lambda: self._gl.glDisable(cap))

    def glCullFace(self, caller: Any, mode: int) -> None:
        return self._call("glCullFace", (mode,),
                          lambda: self._gl.glCullFace(mode))

    def glFrontFace(self, caller: Any, mode: int) -> None:
        return self._call("glFrontFace", (mode,),
                          lambda: self._gl.glFrontFace(mode))

    def glDepthMask(self, caller: Any, flag: int) -> None:
        return self._call("glDepthMask", (flag,),
                          lambda: self._gl.glDepthMask(bool(flag)))

    def glDepthFunc(self, caller: Any, func: int) -> None:
        return self._call("glDepthFunc", (func,),
                          lambda: self._gl.glDepthFunc(func))

    def glBlendFunc(self, caller: Any, sfactor: int, dfactor: int) -> None:
        return self._call("glBlendFunc", (sfactor, dfactor),
                          lambda: self._gl.glBlendFunc(sfactor, dfactor))

    def glBindFramebuffer(self, caller: Any, target: int,
                          framebuffer: int) -> None:
        def run() -> None:
            if target in (self._gl.GL_FRAMEBUFFER,
                          getattr(self._gl, "GL_DRAW_FRAMEBUFFER",
                                  self._gl.GL_FRAMEBUFFER)):
                self._bound_framebuffer = int(framebuffer)
            self._gl.glBindFramebuffer(target, framebuffer)
        return self._call("glBindFramebuffer", (target, framebuffer), run)

    def glBindRenderbuffer(self, caller: Any, target: int,
                           renderbuffer: int) -> None:
        return self._call("glBindRenderbuffer", (target, renderbuffer),
                          lambda: self._gl.glBindRenderbuffer(target,
                                                               renderbuffer))

    def glFramebufferTexture2D(self, caller: Any, target: int,
                               attachment: int, textarget: int, texture: int,
                               level: int) -> None:
        def run() -> None:
            if attachment == self._gl.GL_COLOR_ATTACHMENT0:
                self._framebuffer_color_texture[self._bound_framebuffer] = (
                    int(texture))
            self._gl.glFramebufferTexture2D(
                target, attachment, textarget, texture, level)
        return self._call("glFramebufferTexture2D",
                          (target, attachment, textarget, texture, level),
                          run)

    def glFramebufferRenderbuffer(self, caller: Any, target: int,
                                  attachment: int, renderbuffertarget: int,
                                  renderbuffer: int) -> None:
        return self._call("glFramebufferRenderbuffer",
                          (target, attachment, renderbuffertarget,
                           renderbuffer),
                          lambda: self._gl.glFramebufferRenderbuffer(
                              target, attachment, renderbuffertarget,
                              renderbuffer))

    def glCheckFramebufferStatus(self, caller: Any, target: int) -> int:
        return int(self._call("glCheckFramebufferStatus", (target,),
                              lambda: self._gl.glCheckFramebufferStatus(
                                  target)))

    def glRenderbufferStorage(self, caller: Any, target: int,
                              internalformat: int, width: int,
                              height: int) -> None:
        return self._call(
            "glRenderbufferStorage", (target, internalformat, width, height),
            lambda: self._gl.glRenderbufferStorage(target, internalformat,
                                                   width, height))

    def glBlendEquation(self, caller: Any, mode: int) -> None:
        return self._call("glBlendEquation", (mode,),
                          lambda: self._gl.glBlendEquation(mode))

    def glBlendFuncSeparate(self, caller: Any, src_rgb: int, dst_rgb: int,
                            src_alpha: int, dst_alpha: int) -> None:
        return self._call(
            "glBlendFuncSeparate",
            (src_rgb, dst_rgb, src_alpha, dst_alpha),
            lambda: self._gl.glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha,
                                                 dst_alpha))

    def glBlendColor(self, caller: Any, red: float, green: float, blue: float,
                     alpha: float) -> None:
        return self._call("glBlendColor", (red, green, blue, alpha),
                          lambda: self._gl.glBlendColor(red, green, blue,
                                                        alpha))

    def glLineWidth(self, caller: Any, width: float) -> None:
        return self._call("glLineWidth", (width,),
                          lambda: self._gl.glLineWidth(width))

    def glScissor(self, caller: Any, x: int, y: int, width: int,
                  height: int) -> None:
        return self._call("glScissor", (x, y, width, height),
                          lambda: self._gl.glScissor(x, y, width, height))

    def glClearColor(self, caller: Any, red: float, green: float, blue: float,
                     alpha: float) -> None:
        return self._call("glClearColor", (red, green, blue, alpha),
                          lambda: self._gl.glClearColor(red, green, blue,
                                                        alpha))

    def glClear(self, caller: Any, mask: int) -> None:
        return self._call("glClear", (mask,), lambda: self._gl.glClear(mask))

    def glClearDepthf(self, caller: Any, depth: float) -> None:
        return self._call("glClearDepthf", (depth,),
                          lambda: self._gl.glClearDepth(depth))

    def glIsBuffer(self, caller: Any, buffer: int) -> int:
        return int(self._call("glIsBuffer", (buffer,),
                              lambda: self._gl.glIsBuffer(buffer)))

    def glClearStencil(self, caller: Any, stencil: int) -> None:
        return self._call("glClearStencil", (stencil,),
                          lambda: self._gl.glClearStencil(stencil))

    def glStencilMask(self, caller: Any, mask: int) -> None:
        return self._call("glStencilMask", (mask,),
                          lambda: self._gl.glStencilMask(mask))

    def glStencilFunc(self, caller: Any, func: int, ref: int,
                      mask: int) -> None:
        return self._call("glStencilFunc", (func, ref, mask),
                          lambda: self._gl.glStencilFunc(func, ref, mask))

    def glStencilOp(self, caller: Any, fail: int, zfail: int,
                    zpass: int) -> None:
        return self._call("glStencilOp", (fail, zfail, zpass),
                          lambda: self._gl.glStencilOp(fail, zfail, zpass))
