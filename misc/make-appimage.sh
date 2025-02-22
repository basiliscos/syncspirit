#!/usr/bin/sh

if [ -z "$1" ]; then
    echo "Usage: make-appimage.sh path/to/app/application"
    exit 1
fi

APP_PATH=$1
APP=$(basename $APP_PATH)
APP_DIR="AppDir-$APP/usr/bin"
WORK_DIR="AppDir/$APP_DIR"
echo $WORK_DIR

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
strip --strip-all $APP_PATH
cp $APP_PATH "$WORK_DIR"

cd "AppDir"
if [ ! -e "./linuxdeploy-x86_64.AppImage" ]; then
    echo "going to download linux-deploy..."
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi

./linuxdeploy-x86_64.AppImage -v 2 --appdir "$APP_DIR" --output appimage -e "$APP_DIR/$APP" --icon-file "../../misc/$APP.png" -d "../../misc/$APP.desktop"
cd ..

