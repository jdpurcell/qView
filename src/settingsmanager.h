#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QTranslator>
#include <QVariant>

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    struct SSetting {
        QVariant defaultValue;
        QVariant value;
    };

    explicit SettingsManager(QObject *parent = nullptr);

    void loadSettings();

    const QVariant getSetting(const QString &key, bool defaults = false) const;

    bool getBoolean(const QString &key, bool defaults = false) const;

    int getInteger(const QString &key, bool defaults = false) const;

    template <typename TEnum>
    TEnum getEnum(const QString &key, bool defaults = false) const
    {
        return static_cast<TEnum>(getInteger(key, defaults));
    }

    double getDouble(const QString &key, bool defaults = false) const;

    const QString getString(const QString &key, bool defaults = false) const;

    const QHash<QString, SSetting> &getSettings() const { return settingsLibrary; }

    bool isDefault(const QString &key) const;

    static void migrateOldSettings();

    static void copyFromOfficial();

signals:
    void settingsUpdated();

protected:
    void initializeSettingsLibrary();

private:
    QString getSystemLanguage() const;

    void loadTranslations();

    QTranslator qtTranslator;
    QTranslator appTranslator;
    QHash<QString, SSetting> settingsLibrary;
};

#endif // SETTINGSMANAGER_H
