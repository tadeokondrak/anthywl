{ stdenv
, pkg-config
, meson
, ninja
, scdoc
, python
, wayland
, wayland-protocols
, libxkbcommon
, anthy
, pango
, cairo
, libscfg
, libvarlink
}:

stdenv.mkDerivation {
  pname = "anthywl";
  version = "0.0.1-dev";

  src = ./.;
  postPatch = "patchShebangs .";

  nativeBuildInputs = [
    pkg-config
    meson
    ninja
    scdoc
    python
  ];

  buildInputs = [
    wayland
    wayland-protocols
    libxkbcommon
    anthy
    pango
    cairo
    libscfg
    libvarlink
  ];
}
