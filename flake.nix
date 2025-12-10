{
  description = "C++ DevShell";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
      };
      llvm = pkgs.llvmPackages;
    in
    {
      devShells.${system}.default =
        pkgs.mkShell.override
          {
            stdenv = pkgs.clangStdenv;
          }
          {
            packages = with pkgs; [
              # Build
              gnumake
              bear
              pkg-config

              # Compilers
              llvm.lldb
              llvm.libcxx
              llvm.libcxxStdenv
              shaderc

              # Tools
              valgrind
              clang-tools
              cppcheck
              vulkan-tools
              nixd
              nil
              package-version-server

              # Libs
              doctest
              nanobench
              glfw
              freetype
              vulkan-headers
              vulkan-loader
              vulkan-validation-layers
            ];

            shellHook = ''
              export CXXFLAGS="-stdlib=libc++"
              export LDFLAGS="-stdlib=libc++ -lc++abi"
              echo "======== C++ DevShell ========"
            '';
          };

    };
}
