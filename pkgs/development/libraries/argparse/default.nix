{ cmake, stdenv, fetchFromGitHub }:

stdenv.mkDerivation rec {
  pname = "argparse";
  version = builtins.substring 0 7 src.rev;
  src = fetchFromGitHub {
    owner = "morrisfranken";
    repo = "argparse";
    rev = "5d3710809364e694f33243810bbd1345975ae1c3";
    sha256 = "YJ0kh1tOJgBfo3v3ZoMUlMpdj6Z2BAuOXWbl52Y2J3w=";
  };
  nativeBuildInputs = [ cmake ];
}
