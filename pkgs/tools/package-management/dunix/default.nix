{ argparse, boost, cmake, ftxui, magic-enum, stdenv, nix, pkg-config }:

stdenv.mkDerivation {
  pname = "dunix";
  version = builtins.readFile ../../../../version.txt;
  src = builtins.path { path = ../../../..; name = "dunix-src"; };
  nativeBuildInputs = [ argparse boost cmake ftxui magic-enum nix pkg-config ];
}
