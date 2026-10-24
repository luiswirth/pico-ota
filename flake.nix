{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    treefmt-nix.url = "github:numtide/treefmt-nix";
    treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = {
    nixpkgs,
    treefmt-nix,
    ...
  }: let
    forEachSystem = f:
      nixpkgs.lib.genAttrs ["aarch64-darwin" "x86_64-linux"]
      (system: f nixpkgs.legacyPackages.${system});
  in {
    # Every language in the tree, behind one command. The global pre-commit
    # hook checks a repository by running this, so a language left out here is
    # a language nothing checks.
    formatter = forEachSystem (pkgs:
      treefmt-nix.lib.mkWrapper pkgs {
        projectRootFile = "flake.nix";
        programs.clang-format.enable = true;
        programs.alejandra.enable = true;
        programs.just.enable = true;
      });

    devShells = forEachSystem (pkgs: let
      # The submodules bring tinyusb (USB serial), cyw43 (the radio) and lwIP (the network stack).
      pico-sdk = pkgs.pico-sdk.override {withSubmodules = true;};
    in {
      default = pkgs.mkShell {
        packages = with pkgs; [cmake ninja gcc-arm-embedded picotool python3 just tio];
        PICO_SDK_PATH = "${pico-sdk}/lib/pico-sdk";
      };
    });
  };
}
