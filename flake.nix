{
  description = "Development environment";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    flake-utils,
    nixpkgs,
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        local-pico-sdk = pkgs.pico-sdk.override {
          withSubmodules = true;
        };
      in {
        devShell = pkgs.mkShell {
          packages = with pkgs; [
            gcc-arm-embedded-13
            picotool
            cmake
            ninja
            openocd-rp2040
            cppcheck
            tio
            python3
          ];
          buildInputs = [
            local-pico-sdk
          ];
          shellHook = ''
            export PICO_SDK_PATH=${local-pico-sdk}/lib/pico-sdk
            export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${pkgs.lib.makeLibraryPath [pkgs.udev]}" # for pico-vscode extension
            export CMAKE_PATH=${pkgs.cmake}/bin/cmake
            export NINJA_PATH=${pkgs.ninja}/bin/ninja
            export CC_PATH=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-gcc
            export CXX_PATH=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-g++

            # Source .env file if it exists
            if [ -f .env ]; then
              export $(cat .env | xargs)
            fi
          '';
        };
      }
    );
}
