#!/usr/bin/sh

if [ -z "$1" ]; then
    echo "Usage: make-appimage.sh application"
    exit 1
fi

APP=$1
APP_DIR="AppDir-$APP/usr/bin"

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"
strip --strip-all "./bin/$APP"
cp "./bin/$APP" "$APP_DIR"

if [ ! -e "./linuxdeploy-x86_64.AppImage" ]; then
    echo "going to download linux-deploy..."
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi


./linuxdeploy-x86_64.AppImage -v 2 --appdir "$APP_DIR" --output appimage -e "$APP_DIR/$APP" --icon-file "../misc/$APP.png" -d "../misc/$APP.desktop"

