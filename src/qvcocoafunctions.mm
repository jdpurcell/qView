#include "actionmanager.h"
#include "qvcocoafunctions.h"

#include <QUrl>
#include <QDebug>
#include <QFileIconProvider>
#include <QCollator>

#import <Cocoa/Cocoa.h>

static void hideMenuShortcuts(NSMenu *nativeMenu)
{
    for (NSMenuItem *item in nativeMenu.itemArray)
    {
        [item setKeyEquivalent:@""];

        if (item.hasSubmenu)
            hideMenuShortcuts(item.submenu);
    }
}

void QVCocoaFunctions::showMenu(QMenu *menu, const QPoint &point, QWindow *window)
{
    NSView *view = reinterpret_cast<NSView*>(window->winId());
    NSMenu *nativeMenu = menu->toNSMenu();

    hideMenuShortcuts(nativeMenu);

    // Synthesize event to open menu
    NSPoint pointInWindow = [view convertPoint:NSMakePoint(point.x(), point.y()) toView:nil];
    NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:pointInWindow modifierFlags:0
            timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
    [NSMenu popUpContextMenu:nativeMenu withEvent:event forView:view];
}

void QVCocoaFunctions::setUserDefaults()
{
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSFullScreenMenuItemEverywhere"];
}

// This function should only be enabled once because it sets observers
void QVCocoaFunctions::setFullSizeContentView(QWidget *window, const bool enable)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());

    // Make sure the requested state isn't already in effect
    if (enable == (view.window.styleMask & NSWindowStyleMaskFullSizeContentView))
        return;

    // Enable only if this Qt and macOS version combination is already using layer-backed view
    if (enable && !view.wantsLayer)
        return;

    // Changing the style mask causes the window to resize, so snapshot the original size
    NSRect originalFrame = view.window.frame;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qv::alterWindowFlags(window, [&](Qt::WindowFlags f) { return f.setFlag(Qt::ExpandedClientAreaHint, enable); });
#else
    if (enable)
        view.window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    else
        view.window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
#endif

    // Restore original size after style mask change
    [view.window setFrame:originalFrame display:YES];
}

bool QVCocoaFunctions::getTitlebarHidden(const QWidget *window)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return window->windowFlags().testFlags(Qt::NoTitleBarBackgroundHint);
#else
    auto *view = reinterpret_cast<NSView*>(window->winId());
    return view.window.titleVisibility == NSWindowTitleHidden;
#endif
}

void QVCocoaFunctions::setTitlebarHidden(QWidget *window, const bool hide)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Qv::alterWindowFlags(window, [&](Qt::WindowFlags f) { return f.setFlag(Qt::NoTitleBarBackgroundHint, hide); });
#else
    view.window.titlebarAppearsTransparent = hide;
#endif
    view.window.titleVisibility = hide ? NSWindowTitleHidden : NSWindowTitleVisible;
}

void QVCocoaFunctions::setWindowCollectionBehaviorManaged(QWidget *window)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
    view.window.collectionBehavior =
        (view.window.collectionBehavior | NSWindowCollectionBehaviorManaged) &
        ~(NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorStationary);
}

void QVCocoaFunctions::setVibrancy(bool alwaysDark, QWindow *window)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());

    if (alwaysDark)
    {
        [view.window setAppearance: [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark]];
    }
    else
    {
        [view.window setAppearance: nil];
    }
}

int QVCocoaFunctions::getObscuredHeight(QWindow *window)
{
    if (!window)
        return 0;

    auto *view = reinterpret_cast<NSView*>(window->winId());

    if (view.window.titlebarAppearsTransparent)
        return 0;

    int visibleHeight = view.window.contentLayoutRect.size.height;
    int totalHeight = view.window.contentView.frame.size.height;

    return totalHeight - visibleHeight;
}

void QVCocoaFunctions::closeWindow(QWindow *window)
{
    auto *view = reinterpret_cast<NSView*>(window->winId());
    [view.window close];
}

void QVCocoaFunctions::setWindowMenu(QMenu *menu)
{
    NSMenu *nativeMenu = menu->toNSMenu();
    [nativeMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];

    [nativeMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    [[NSApplication sharedApplication] setWindowsMenu:nativeMenu];
}

void QVCocoaFunctions::setAlternate(QMenu *menu, int index)
{
    NSMenu *nativeMenu = menu->toNSMenu();
    [[nativeMenu.itemArray objectAtIndex:index] setAlternate:true];
}

void QVCocoaFunctions::setDockRecents(const QStringList &recentPathsList)
{
    NSDocumentController *documentController = [NSDocumentController sharedDocumentController];

    [documentController clearRecentDocuments:nil];

    if (recentPathsList.length() == 0)
        return;

    // The dispatch_async wrapper is a workaround for the first document not getting added after a call
    // to clear the documents. We just introduce a slight processing delay which is enough to fix it.
    const QStringList paths = recentPathsList;
    dispatch_async(dispatch_get_main_queue(), ^{
        for (int i = paths.size() - 1; i >= 0; i--)
        {
            const auto &path = paths[i];
            auto url = QUrl::fromLocalFile(path);
            [documentController noteNewRecentDocumentURL:url.toNSURL()];
        }
    });
}

QList<OpenWith::OpenWithItem> QVCocoaFunctions::getOpenWithItems(const QString &filePath, const bool loadIcons)
{
    auto fileUrl = QUrl(filePath);
    fileUrl.setScheme("file");

    NSString *utiType = nil;
    NSError *error = nil;
    BOOL success = [fileUrl.toNSURL() getResourceValue:&utiType forKey:NSURLTypeIdentifierKey error:&error];

    if (!success)
    {
        NSLog(@"getResourceValue:forKey:error: returned error == %@", error);
        return QList<OpenWith::OpenWithItem>();
    }

    NSArray *supportedApplications = [(NSArray *)LSCopyAllRoleHandlersForContentType((CFStringRef)utiType, kLSRolesAll) autorelease];
    NSString *defaultApplication = [(NSString *)LSCopyDefaultRoleHandlerForContentType((CFStringRef)utiType, kLSRolesAll) autorelease];

    QList<OpenWith::OpenWithItem> listOfOpenWithItems;
    for (NSString *appId in supportedApplications)
    {
        if ([appId isEqualToString:@"com.qview.qView"] || [appId isEqualToString:@"com.interversehq.qView"] || [appId isEqualToString:@""])
            continue;

        OpenWith::OpenWithItem openWithItem;
        openWithItem.exec = "open";
        openWithItem.args.append({"-b", QString::fromNSString(appId)});

        NSURL *appURL = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:appId];
        if (!appURL)
            continue;
        NSString *absolutePath = appURL.path;

        NSString *appName = [[NSFileManager defaultManager] displayNameAtPath:absolutePath];
        openWithItem.name = QString::fromNSString(appName);

        if (loadIcons)
        {
            QFileIconProvider fiProvider;
            QIcon icon = fiProvider.icon(QFileInfo(QString::fromNSString(absolutePath)));
            openWithItem.icon = ActionManager::getCacheableIcon("application:" + QString::fromNSString(appId), icon);
        }

        // If the program is the default program, save it to add to the beginning after sorting
        if ([appId isEqualToString:defaultApplication])
        {
            openWithItem.isDefault = true;
            openWithItem.name += QT_TR_NOOP(" (default)");
        }

        listOfOpenWithItems.append(openWithItem);
    }

    return listOfOpenWithItems;
}

QByteArray QVCocoaFunctions::getIccProfileForWindow(const QWindow *window)
{
    NSView *view = reinterpret_cast<NSView*>(window->winId());
    NSColorSpace *nsColorSpace = view.window.colorSpace;
    if (nsColorSpace)
    {
        NSData *iccProfileData = nsColorSpace.ICCProfileData;
        if (iccProfileData)
        {
            return QByteArray::fromNSData(iccProfileData);
        }
    }
    return {};
}
