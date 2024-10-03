{
  description = "WebNowPlaying-Library";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };
  outputs = inputs@{ self, flake-parts, ... }: 
  flake-parts.lib.mkFlake { inherit inputs; } {
    systems = [
      "x86_64-linux"
      "aarch64-linux"
      "aarch64-darwin"
    ];
    perSystem = { pkgs, system, lib, ...}: {
      packages.default = pkgs.stdenv.mkDerivation {
        pname = "libwnp";
        version = "3.0.0";
        src = ./.;
        nativeBuildInputs = [ pkgs.cmake ]
          ++ lib.optionals pkgs.stdenv.hostPlatform.isLinux [ pkgs.pkg-config ];
        buildInputs = lib.optionals pkgs.stdenv.hostPlatform.isLinux [ pkgs.glib ];
      };
      devShells.default = pkgs.mkShell {
        shellHook = "exec $SHELL";
        buildInputs = with pkgs; [ clang cmake glib pkg-config ];
      };
    };
  };
}
