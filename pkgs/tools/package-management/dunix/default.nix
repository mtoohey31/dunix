{ argparse, boost, cmake, ftxui, stdenv, nix, pkg-config }:

stdenv.mkDerivation {
  pname = "dunix";
  version = "0.1.0";
  src = builtins.path { path = ../../../..; name = "dunix-src"; };
  nativeBuildInputs = [ argparse boost cmake ftxui nix pkg-config ];
}
