# Learn Vulkan C++ 23
Base on [learn-vulkan-cpp](https://github.com/learn-vulkan-cpp/learn-vulkan-cpp).

# Build

Build artifacts are placed under `bin/`:
- apps: `bin/<app>`
- tests: `bin/tests/<test>`
- object files: `bin/obj/`
- compiled shaders: `bin/shaders/*.spv`

Building with Makefile
- Build everything (default target). This now compiles shaders as part of the `all` target:
```sh
# build shaders, apps and tests
make
```

- Build only apps or only tests:
```sh
make apps
make tests
```

- Build shaders explicitly (compiles GLSL .vert/.frag → SPIR-V):
```sh
# build all shader SPIR-V outputs
make shaders
# or build a single shader output
make bin/shaders/shader.vert.spv
```

Running tests
- Run all tests via the Makefile helper:
```sh
make run-tests
```
Test binaries are located in `bin/tests/`.

Other useful targets
- Clean build artifacts:
```sh
make clean
```

- Generate `compile_commands.json` (uses `bear`) for editor tooling / language servers:
```sh
make compile-commands
```

Notes and tips
- The `Makefile` uses `pkg-config` to populate compile/link flags for `glfw3`, `vulkan`, and `gl`.
- If you see missing packages when not using Nix, ensure system packages for GLFW3, Vulkan and OpenGL are installed and visible to `pkg-config`.
- Built apps and tests live in `bin/` — run them directly, e.g. `./bin/<app>` or `./bin/tests/<test>`.
