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
cp ../misc/$APP.sh "$WORK_DIR"

cd "AppDir"
if [ ! -e "./linuxdeploy-x86_64.AppImage" ]; then
    echo "going to download linux-deploy..."
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi

DESKTOP_FILE="../../misc/$APP.desktop"
ADD_DESKTOP_FILE=""
if [ -e "$DESKTOP_FILE" ]; then
    ADD_DESKTOP_FILE="-d $DESKTOP_FILE"
fi

ICON_FILE="../../misc/$APP.png"
ADD_ICON_FILE=""
if [ -e "$ICON_FILE" ]; then
    ADD_ICON_FILE="--icon-file $ICON_FILE"
fi

./linuxdeploy-x86_64.AppImage -l`g++ -print-file-name=libstdc++.so.6` --custom-apprun "../../misc/$APP.sh" -v 2 --appdir "$APP_DIR" --output appimage -e "$APP_DIR/$APP" $ADD_ICON_FILE $ADD_DESKTOP_FILE
cd ..
