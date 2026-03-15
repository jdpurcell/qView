#!/usr/bin/bash

cd bin

echo "Running macdeployqt"
macdeployqt qView.app

IMF_DIR=qView.app/Contents/PlugIns/imageformats
if [[ (-f "$IMF_DIR/kimg_heif.dylib" || -f "$IMF_DIR/kimg_heif.so") && -f "$IMF_DIR/libqmacheif.dylib" ]]; then
    # Prefer kimageformats HEIF plugin for proper color space handling
    echo "Removing duplicate HEIF plugin"
    rm "$IMF_DIR/libqmacheif.dylib"
fi
if [[ (-f "$IMF_DIR/kimg_tga.dylib" || -f "$IMF_DIR/kimg_tga.so") && -f "$IMF_DIR/libqtga.dylib" ]]; then
    # Prefer kimageformats TGA plugin which supports more formats
    echo "Removing duplicate TGA plugin"
    rm "$IMF_DIR/libqtga.dylib"
fi
if [[ (-f "$IMF_DIR/kimg_jp2.dylib" || -f "$IMF_DIR/kimg_jp2.so") && -f "$IMF_DIR/libqmacjp2.dylib" ]]; then
    # Prefer kimageformats JPEG 2000 plugin
    echo "Removing duplicate JPEG 2000 plugin"
    rm "$IMF_DIR/libqmacjp2.dylib"
fi

# OpenSSL isn't needed since we use platform-native TLS
rm -f qView.app/Contents/PlugIns/tls/libqopensslbackend.dylib

echo "Running codesign"
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    APP_IDENTIFIER=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "qView.app/Contents/Info.plist")
    codesign --sign "$APPLE_DEVID_APP_CERT_NAME" --deep --force --options runtime --timestamp "qView.app"
else
    codesign --sign - --deep --force "qView.app"
fi

echo "Creating disk image"
BUILD_NAME=qView-JDP-$1
DMG_FILENAME=$BUILD_NAME-macOS$2.dmg
hdiutil create -srcfolder "qView.app" -volname "$BUILD_NAME" -format UDSB "qView.sparsebundle"
hdiutil convert "qView.sparsebundle" -format ULFO -o "$DMG_FILENAME"
rm -r qView.sparsebundle
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    codesign --sign "$APPLE_DEVID_APP_CERT_NAME" --timestamp --identifier "$APP_IDENTIFIER.dmg" "$DMG_FILENAME"
    xcrun notarytool submit "$DMG_FILENAME" --apple-id "$APPLE_ID_USER" --password "$APPLE_ID_PASS" --team-id "${APPLE_DEVID_APP_CERT_NAME: -11:10}" --wait
    xcrun stapler staple "$DMG_FILENAME"
    xcrun stapler validate "$DMG_FILENAME"
fi

rm -r *.app
