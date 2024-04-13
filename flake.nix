{
  description = "dunix";

  inputs = {
    nixpkgs.url = "nixpkgs/nixpkgs-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, utils }: {
    overlays.default = final: _: {
      argparse = final.callPackage ./pkgs/development/libraries/argparse { };
      dunix = final.callPackage ./pkgs/tools/package-management/dunix { };
    };
  } // utils.lib.eachDefaultSystem (system:
    let
      pkgs = import nixpkgs {
        overlays = [ self.overlays.default ];
        inherit system;
      };
      inherit (pkgs) clang-tools cmake-language-server dunix mkShell valgrind;
    in
    {
      packages.default = dunix;

      devShells.default = mkShell {
        inputsFrom = [ dunix ];
        packages = [ clang-tools cmake-language-server valgrind ];
      };
    });
}
