{ stdenv
, pkg-config
, meson_0_60
, ninja
, scdoc
, python
, wayland
, wayland-protocols
, libxkbcommon
, anthy
, pango
, cairo
, systemd
, libscfg
}:

stdenv.mkDerivation {
  pname = "anthywl";
  version = "0.0.1-dev";

  src = ./.;
  postPatch = "patchShebangs .";

  nativeBuildInputs = [
    pkg-config
    meson_0_60
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
    systemd
    libscfg
  ];
}
