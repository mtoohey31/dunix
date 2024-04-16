# `dunix`

`dunix` provides disk usage breakdowns for Nix store paths.

## Installation

Using the included flake, you can install `dunix` in a couple different ways. If you just want to try it out, use `nix run` like so:

```sh
nix run github:mtoohey31/dunix# -- --help
```

For more permanent installations, add `github:mtoohey31/dunix` as a flake input then use either its default overlay or package output. If you're not using flakes, you can fetch the repository with `fetchFromGitHub` and use `callPackage` on `pkgs/tools/package-management/dunix/default.nix` directly, though this may break depending on your nixpkgs version.

## Usage

```console
$ dunix --help
Disk usage breakdowns for Nix store paths.

Usage: dunix path  [options...]
             path : The store path to display disk usage breakdown for. [default: result]

Options:
     -v,--version : Display version. [implicit: "true", default: false]
   -f,--full-path : Display full store paths. [implicit: "true", default: false]
        -s,--sort : Metric by which to sort referrers. [allowed: <nar, closure, removalimpact, references, referrers>, implicit: "true", default: 2]
        -?,--help : print help [implicit: "true", default: false]

Metrics:
         nar size : The size of the files within the store path itself. More specifically, the size of the output of nix-store --dump.
     closure size : The sum of the nar size metric for the store path's closure, which includes the store path itself, and all store paths referenced (directly or transitively) by it.
   removal impact : The space that would be saved from the root store path's closure if this store path's parent no longer depended directly on it. This is 0 if the store path has more than one referrer in the root's closure, because eliminating its parent's reference won't impact the size since the root will still depend on it through another referrer. Otherwise, it is the sum of the nar size metric for everything in the store path's closure that has no referrers outside of the closure.
       references : The number of store paths that this one references directly.
        referrers : The number of store paths that reference this one directly.
```

![`dunix` screenshot](https://github.com/mtoohey31/dunix/assets/36740602/cf430c78-2d42-48f7-91f5-1b118f91a008)

## Related

- [nix-tree](https://github.com/utdemir/nix-tree) provides similar metrics with a slightly different UI.
- [nix-du](https://github.com/symphorien/nix-du) displays relationships visually as a graph.
- See the ["Related tools" section of nix-tree's README](https://github.com/utdemir/nix-tree?tab=readme-ov-file#related-tools) for more.
