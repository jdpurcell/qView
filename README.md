# qView
This fork of qView adds the following features:
* Option to constrain image position to keep it snapped inside the viewport.
* macOS: Option to persist session across app restarts.
* Shows zoom level in titlebar in Practical/Verbose mode.
* Custom titlebar mode based on format string.
* Option to make zoom level relative to screen pixels (for Windows/Linux users with DPI scaling enabled; not so useful in macOS due to the way it handles scaling).
* Menu toggle to preserve zoom level when changing between images.
* Ctrl/Cmd double-click viewport to hide the titlebar (macOS/Windows only), Ctrl/Cmd drag viewport to move the window.
* Random file navigation (Go -> Random File, or "r" keyboard shortcut) to browse randomly without changing sort mode.
* Option to navigate between images when scrolling sideways (e.g. two finger swipe).
* Windows: Option for different theme; non-native look but it allows for dark mode.
* Configurable window positioning behavior after matching image size.
* More accurate zoom-to-fit plus customizable overscan setting.
* Option for checkerboard background.
* Option to disable icons in "Open Recent" and "Open With" submenus (helps with memory usage (leak?) especially on macOS).
