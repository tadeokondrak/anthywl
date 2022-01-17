{
  description = "anthywl";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "i686-linux" "x86_64-darwin" ];
      overlay = final: prev: {
        anthywl = final.callPackage ./default.nix {
          python = final.python3;
          libscfg = final.callPackage
            ({ stdenv
             , fetchFromSourcehut
             , meson
             , ninja
             }:
              stdenv.mkDerivation rec {
                pname = "libscfg";
                version = "unstable-2021-12-31";
                src = fetchFromSourcehut {
                  owner = "~emersion";
                  repo = "libscfg";
                  rev = "a4f023d2e1c2c2ac71eb23a989bd58bd3f77fb2a";
                  hash = "sha256-048widtMMcgnXNUqqTAEXDdL+u+nmWb9jTEc2C5mzCo=";
                };
                nativeBuildInputs = [ meson ninja ];
              }
            )
            { };
        };
      };
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
      nixpkgsFor = forAllSystems (system:
        import nixpkgs {
          inherit system;
          overlays = [ self.overlay ];
          config.allowUnfree = true;
        }
      );
    in
    {
      inherit overlay;
      defaultPackage = forAllSystems (system: nixpkgsFor.${system}.anthywl);
      packages = forAllSystems (system: { inherit (nixpkgsFor.${system}) anthywl; });
    };
}
