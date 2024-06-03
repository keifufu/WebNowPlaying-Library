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
    ];
    perSystem = { pkgs, system, ...}: {
      packages.default = pkgs.stdenv.mkDerivation {
        pname = "libwnp";
        version = builtins.readFile ./VERSION;
        src = ./.;
        buildInputs = with pkgs; [ clang makeWrapper ];
        buildPhase = ''
          make linux64
        '';
        installPhase = ''
          mkdir -p $out/lib $out/include
          cp build/libwnp_linux_amd64.a $out/lib/libwnp.a
          cp src/wnp.h $out/include/
        '';
      };
      devShells.default = pkgs.mkShell {
        shellHook = "exec $SHELL";
        buildInputs = with pkgs; [ clang valgrind gnumake ];
      };
    };
  };
}