#ifndef QVNAMESPACE_H
#define QVNAMESPACE_H

#include <QColor>
#include <QSet>
#include <QString>
#include <QWidget>
#include <QWindow>

namespace Qv
{
    // Data constants

    inline constexpr int SessionStateVersion = 1;

    inline constexpr QPoint CalculateViewportCenterPos(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());

    // Settings value enums

    enum class AfterDelete
    {
        MoveBack = 0,
        DoNothing = 1,
        MoveForward = 2
    };

    enum class AfterMatchingSize
    {
        AvoidRepositioning = 0,
        CenterOnPrevious = 1,
        CenterOnScreen = 2
    };

    enum class CalculatedZoomMode
    {
        ZoomToFit = 0,
        FillWindow = 1,
        OriginalSize = 2
    };

    enum class ClickOrDrag
    {
        Click = 0,
        Drag = 1
    };

    enum class ColorSpaceConversion
    {
        Disabled = 0,
        AutoDetect = 1,
        SRgb = 2,
        DisplayP3 = 3
    };

    enum class PreloadMode
    {
        Disabled = 0,
        Adjacent = 1,
        Extended = 2
    };

    enum class SlideshowDirection
    {
        Forward = 0,
        Backward = 1,
        Random = 2
    };

    enum class SmoothScalingMode
    {
        Disabled = 0,
        Bilinear = 1,
        Expensive = 2
    };

    enum class SortMode
    {
        Name = 0,
        DateModified = 1,
        DateCreated = 2,
        Size = 3,
        Type = 4,
        Random = 5
    };

    enum class TitleBarText
    {
        Basic = 0,
        Minimal = 1,
        Practical = 2,
        Verbose = 3,
        Custom = 4
    };

    enum class ViewportClickAction
    {
        None = 0,
        ZoomToFit = 1,
        FillWindow = 2,
        OriginalSize = 3,
        ToggleFullScreen = 4,
        ToggleTitlebarHidden = 5,
        CenterImage = 6
    };

    enum class ViewportDragAction
    {
        None = 0,
        Pan = 1,
        MoveWindow = 2
    };

    enum class ViewportScrollAction
    {
        None = 0,
        Zoom = 1,
        Navigate = 2,
        Pan = 3
    };

    enum class WindowResizeMode
    {
        Never = 0,
        WhenLaunching = 1,
        WhenOpeningImages = 2
    };

    // Other enums

    enum class GoToFileMode
    {
        Constant,
        First,
        Previous,
        Next,
        Last,
        NextRandom,
        PreviousRandom
    };

    enum class MaterialIcon : quint16
    {
        Ballot = 0xE172,
        Build = 0xE869,
        CancelPresentation = 0xE0E9,
        Close = 0xE5CD,
        CloudDownload = 0xE2C0,
        ContentCopy = 0xE14D,
        ContentPaste = 0xE14F,
        Delete = 0xE872,
        DeleteForever = 0xE92B,
        Deselect = 0xEBB6,
        DisabledByDefault = 0xF230,
        DriveFileRenameOutline = 0xE9A2,
        Extension = 0xE87B,
        FastForward = 0xE01F,
        FastRewind = 0xE020,
        FileOpen = 0xEAF3,
        FirstPage = 0xE5DC,
        FitScreen = 0xEA10,
        FolderOpen = 0xE2C8,
        Fullscreen = 0xE5D0,
        FullscreenExit = 0xE5D1,
        HelpOutline = 0xE8FD,
        Image = 0xE3F4,
        Info = 0xE88E,
        Keyboard = 0xE312,
        KeyboardDoubleArrowUp = 0xEACF,
        LastPage = 0xE5DD,
        Launch = 0xE895,
        Logout = 0xE9BA,
        LooksOne = 0xE400,
        Mouse = 0xE323,
        NavigateBefore = 0xE408,
        NavigateNext = 0xE409,
        Pause = 0xE034,
        PlayArrow = 0xE037,
        PlaylistRemove = 0xEB80,
        Refresh = 0xE5D5,
        Replay = 0xE042,
        RestoreFromTrash = 0xE938,
        RotateLeft = 0xE419,
        RotateRight = 0xE41A,
        Save = 0xE161,
        Search = 0xE8B6,
        SettingsOverscan = 0xE8C4,
        Settings = 0xE8B8,
        Shuffle = 0xE043,
        SkipNext = 0xE044,
        SkipPrevious = 0xE045,
        Slideshow = 0xE41B,
        Sort = 0xE164,
        Start = 0xE089,
        SwapHoriz = 0xE8D4,
        SwapVert = 0xE0C3, // Should be 0xE8D5 but renders wrong on Windows; using identical looking one instead
        Tune = 0xE429,
        VerticalAlignTop = 0xE25A,
        Visibility = 0xE8F4,
        Wallpaper = 0xE1BC,
        WebAsset = 0xE069,
        WorkHistory = 0xEC09,
        ZoomIn = 0xE8FF,
        ZoomOut = 0xE900
    };

    // Helper functions

    inline QSet<QString> listToSet(const QStringList &list)
    {
        return QSet<QString>{list.begin(), list.end()};
    }

    inline QStringList setToList(const QSet<QString> &set)
    {
        return QStringList(set.begin(), set.end());
    }

    inline QStringList setToSortedList(const QSet<QString> &set, Qt::CaseSensitivity cs = Qt::CaseSensitive)
    {
        auto list = QStringList(set.begin(), set.end());
        list.sort(cs);
        return list;
    }

    inline bool calculatedZoomModeIsSticky(const CalculatedZoomMode mode)
    {
        return
            mode == CalculatedZoomMode::ZoomToFit ||
            mode == CalculatedZoomMode::FillWindow;
    }

    inline bool scrollActionIsSelfCompatible(const ViewportScrollAction action)
    {
        return
            action == ViewportScrollAction::None ||
            action == ViewportScrollAction::Pan;
    }

    inline qreal getPerceivedBrightness(const QColor &color)
    {
        return (color.red() * 0.299 + color.green() * 0.587 + color.blue() * 0.114) / 255.0;
    }

    inline void alterWindowFlags(QWidget *window, std::function<Qt::WindowFlags(Qt::WindowFlags)> alterFlags)
    {
        const Qt::WindowFlags newFlags = alterFlags(window->windowFlags());
        window->overrideWindowFlags(newFlags);
        window->windowHandle()->setFlags(newFlags);
    }
}

#endif // QVNAMESPACE_H
