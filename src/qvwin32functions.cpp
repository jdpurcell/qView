#include "qvwin32functions.h"

#include "ShlObj_core.h"
#include "winuser.h"
#include "wingdi.h"
#include "Objbase.h"
#include "appmodel.h"
#include "Shlwapi.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QCollator>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QWindow>

#include <QDebug>

QList<OpenWith::OpenWithItem> QVWin32Functions::getOpenWithItems(const QString &filePath, const bool loadIcons)
{
    QList<OpenWith::OpenWithItem> listOfOpenWithItems;

    QFileInfo info(filePath);
    QString extension = "." + info.suffix();

    // Get default program first
    WCHAR assocString[MAX_PATH];
    DWORD assocStringSize = MAX_PATH;
    AssocQueryStringW(0, ASSOCSTR_FRIENDLYAPPNAME, qUtf16Printable(extension), L"open", assocString, &assocStringSize);

    QString defaultProgramName = QString::fromWCharArray(assocString);

    // Get the big list of recommended assochandlers for this file type
    IEnumAssocHandlers *assocHandlers = 0;
    if (!SUCCEEDED(SHAssocEnumHandlers(qUtf16Printable(extension), ASSOC_FILTER_RECOMMENDED, &assocHandlers)))
    {
        qDebug() << "Failed to retrieve enum handlers";
        return listOfOpenWithItems;
    }

    ULONG retrieved = 0;
    IAssocHandler *retrievedHandlers = 0; // this is an array with one element
    while (assocHandlers->Next(1, &retrievedHandlers, &retrieved) == S_OK)
    {
        IAssocHandler &assocHandler = retrievedHandlers[0];

        // Get UI name
        WCHAR *uiName = 0;
        if (!SUCCEEDED(assocHandler.GetUIName(&uiName)))
            continue;

        // Get "name" (this is exec)
        WCHAR *name = 0;
        if (!SUCCEEDED(assocHandler.GetName(&name)))
            continue;

        // Get icon path and index (index is not used in this program)
        WCHAR *icon = 0;
        int iconIndex = 0;
        if (!SUCCEEDED(assocHandler.GetIconLocation(&icon, &iconIndex)))
            continue;

        // Set OpenWithItem fields
        OpenWith::OpenWithItem openWithItem;
        openWithItem.name = QString::fromWCharArray(uiName);
        openWithItem.exec = QString::fromWCharArray(name);
        openWithItem.isDefault = openWithItem.name == defaultProgramName;
        openWithItem.winAssocHandler = &assocHandler;


        QString iconLocation = QString::fromWCharArray(icon);
        bool isAppx = iconLocation.contains("ms-resource");

        // Don't include qView in open with menu
        if (openWithItem.name == "qView")
            continue;

        // Validity check
        if (!QFile(openWithItem.exec).exists() && !isAppx)
        {
            qDebug() << openWithItem.name << "is an invalid openwith entry";
            continue;
        }

        // Replace ampersands with escaped ampersands for menu items
        openWithItem.name.replace("&", "&&");

        if (loadIcons)
        {
            // Set an icon
            if (isAppx)
            {
                WCHAR realIconPath[MAX_PATH];
                SHLoadIndirectString(icon, realIconPath, MAX_PATH, NULL);
                openWithItem.icon = QIcon(QString::fromWCharArray(realIconPath));
            }
            else
            {
                QFileIconProvider iconProvider;
                openWithItem.icon = iconProvider.icon(QFileInfo(iconLocation));
            }
        }

        listOfOpenWithItems.append(openWithItem);
    }

    return listOfOpenWithItems;
}

void QVWin32Functions::openWithInvokeAssocHandler(const QString &filePath, void *winAssocHandler)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);

    // Create IShellItem
    IShellItem *shellItem = 0;
    HRESULT result = SHCreateItemFromParsingName(qUtf16Printable(nativeFilePath), NULL, IID_IShellItem, (void**)&shellItem);
    if (!SUCCEEDED(result))
    {
        qDebug() << "Failed to create IShellItem for " << filePath;
        return;
    }

    // Get IDataObject from that
    IDataObject *dataObject = 0;
    if (!SUCCEEDED(shellItem->BindToHandler(NULL, BHID_DataObject, IID_IDataObject, (void**)&dataObject)))
    {
        qDebug() << "Failed to get IDataObject from IShellItem";
        return;
    }

    // Cast passed IAssocHandler and invoke
    IAssocHandler *assocHandler = static_cast<IAssocHandler*>(winAssocHandler);
    assocHandler->Invoke(dataObject);
}

void QVWin32Functions::showOpenWithDialog(const QString &filePath, const QWindow *parent)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);
    const OPENASINFO info = {
        qUtf16Printable(nativeFilePath),
        0,
        OAIF_EXEC
    };

    HWND winId = reinterpret_cast<HWND>(parent->winId());
    if (!SUCCEEDED(SHOpenWithDialog(winId, &info)))
        qDebug() << "Failed launching open with dialog";
}

static QString getLongOrShortPath(const QString &path, std::function<DWORD(LPCWSTR, LPWSTR, DWORD)> getPathNameFunction)
{
    const bool isUnc = path.startsWith(R"(\\)");
    const QString inputString = isUnc ? (R"(\\?\UNC\)" + path.mid(2)) : (R"(\\?\)" + path);
    const wchar_t *input = reinterpret_cast<const wchar_t*>(inputString.utf16());
    const DWORD outputSize = getPathNameFunction(input, nullptr, 0);
    if (outputSize == 0)
        return {};
    QVarLengthArray<wchar_t, MAX_PATH> output(outputSize);
    if (getPathNameFunction(input, output.data(), output.size()) == 0)
        return {};
    const QString outputString = QString::fromWCharArray(output.data());
    return isUnc ? (R"(\\)" + outputString.mid(8)) : outputString.mid(4);
}

QString QVWin32Functions::getLongPath(const QString &path)
{
    return getLongOrShortPath(path, GetLongPathNameW);
}

QString QVWin32Functions::getShortPath(const QString &path)
{
    return getLongOrShortPath(path, GetShortPathNameW);
}

bool QVWin32Functions::showInExplorer(const QString &path)
{
    bool result = false;
    LPITEMIDLIST pIdl;
    if (SUCCEEDED(SHParseDisplayName(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr, &pIdl, 0, nullptr)))
    {
        result = SUCCEEDED(SHOpenFolderAndSelectItems(pIdl, 0, nullptr, 0));
        ILFree(pIdl);
    }
    return result;
}

QByteArray QVWin32Functions::getIccProfileForWindow(const QWindow *window)
{
    QByteArray result;
    const HWND hWnd = reinterpret_cast<HWND>(window->winId());
    const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor)
    {
        MONITORINFOEXW monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEXW);
        if (GetMonitorInfoW(hMonitor, &monitorInfo))
        {
            const HDC hDC = CreateICW(monitorInfo.szDevice, monitorInfo.szDevice, NULL, NULL);
            if (hDC)
            {
                WCHAR profilePathBuff[MAX_PATH];
                DWORD profilePathSize = MAX_PATH;
                if (GetICMProfileW(hDC, &profilePathSize, profilePathBuff))
                {
                    QString profilePath = QString::fromWCharArray(profilePathBuff);
                    QFile file(profilePath);
                    if (file.open(QIODevice::ReadOnly))
                    {
                        result = file.readAll();
                        file.close();
                    }
                }
                ReleaseDC(hWnd, hDC);
            }
        }
    }
    return result;
}
