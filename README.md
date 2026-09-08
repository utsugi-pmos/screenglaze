# Screenglaze

Screenshots on Plasma Mobile with **volume-down + power**, like Android, and a
sheet to share, save or crop what you just took.

Packaged for postmarketOS in
[utsugi-pmaports](https://github.com/utsugi-pmos/utsugi-pmaports):

```sh
sudo apk add screenglaze
```

## Three dependencies that are not optional, and why

- **spectacle** takes the picture. KWin authorises `org.kde.KWin.ScreenShot2`
  only for `/usr/bin/spectacle` and answers `NoAuthorized` to any other
  executable. Measured, not assumed.
- **layer-shell-qt** is what puts the sheet above everything without it showing
  up in the task switcher. Without it the application still works
  (`SCREENGLAZE_SIN_CAPA=1`), but as an ordinary window.
- **feedbackd** gives the shutter click and the vibration from the device's own
  theme, which is the only thing you actually notice at the moment you press.

## Licence

LGPL-2.0-or-later, as declared by the SPDX headers in every source file.
