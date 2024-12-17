#!/usr/bin/bash

if [[ -z "$1" ]]; then
    RELEASE_VER=$(LC_ALL=C sed -n -e '/^VERSION/p' qView.pro)
    RELEASE_VER=${RELEASE_VER: -3}
fi

sudo apt update
sudo apt install libfuse2

wget -c -nv "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
wget -c -nv "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod a+x linuxdeploy-*.AppImage

mkdir -p bin/appdir/usr
make install INSTALL_ROOT=bin/appdir
cp dist/linux/hicolor/scalable/apps/com.interversehq.qView.svg bin/appdir/
cd bin
rm qview
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib/
../linuxdeploy-x86_64.AppImage \
    --plugin qt \
    --appdir appdir \
    --desktop-file appdir/usr/share/applications/com.interversehq.qView.desktop \
    --output appimage

if [ $1 != "" ]; then
    mv *.AppImage qView-JDP-$1-Linux_x86_64.AppImage
else
    mv *.AppImage qView-JDP-$RELEASE_VER-Linux_x86_64.AppImage
fi
rm -r appdir
