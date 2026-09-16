{
  description = "ingame_overlay development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/174eb786fb68e3a13e4e535a3deea479a0c07a6a";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      mingw = pkgs.pkgsCross.mingwW64;
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          # Native (host) build toolchain. Portable release builds should go
          # through the container instead (see build-container.sh), otherwise
          # the .so links against NixOS paths and glibc.
          cmake
          gcc
          gnumake
          pkg-config

          # Linux overlay + test dependencies (mirrors the CI packages in
          # .github/workflows/main.yml).
          libglvnd
          libx11
          libxrandr
          libxinerama
          libxcursor
          libxi

          # Windows cross toolchain (mingw-w64) for building
          # everyone_overlay.dll (see toolchains/mingw-w64-x86_64.cmake).
          mingw.stdenv.cc
        ];
      };
    };
}
