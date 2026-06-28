#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

class SettingsManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString defaultSystemPrompt READ defaultSystemPrompt WRITE setDefaultSystemPrompt NOTIFY defaultSystemPromptChanged)
    Q_PROPERTY(bool firstRun READ isFirstRun NOTIFY firstRunChanged)
    Q_PROPERTY(int maxContextTokens READ maxContextTokens WRITE setMaxContextTokens NOTIFY maxContextTokensChanged)
    Q_PROPERTY(int maxResponseTokens READ maxResponseTokens WRITE setMaxResponseTokens NOTIFY maxResponseTokensChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(double topP READ topP WRITE setTopP NOTIFY topPChanged)
    Q_PROPERTY(int chatHistoryLimit READ chatHistoryLimit WRITE setChatHistoryLimit NOTIFY chatHistoryLimitChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    // Language (bilingual ID/EN)
    QString language() const;
    void setLanguage(const QString &lang);
    Q_INVOKABLE QString tr(const QString &key) const;

    // Theme
    QString theme() const;
    void setTheme(const QString &theme);

    // Font
    int fontSize() const;
    void setFontSize(int size);

    // System prompt
    QString defaultSystemPrompt() const;
    void setDefaultSystemPrompt(const QString &prompt);

    // First run
    bool isFirstRun() const;
    Q_INVOKABLE void completeFirstRun();

    // Inference settings
    int maxContextTokens() const;
    void setMaxContextTokens(int tokens);
    int maxResponseTokens() const;
    void setMaxResponseTokens(int tokens);
    double temperature() const;
    void setTemperature(double temp);
    double topP() const;
    void setTopP(double p);

    // Chat history
    int chatHistoryLimit() const;
    void setChatHistoryLimit(int limit);

    // Paths
    Q_INVOKABLE QString dataDirectory() const;
    Q_INVOKABLE QString modelsDirectory() const;
    Q_INVOKABLE QString databasePath() const;

    // Personality presets
    Q_INVOKABLE QVariantList getPersonalityPresets() const;

signals:
    void languageChanged();
    void themeChanged();
    void fontSizeChanged();
    void defaultSystemPromptChanged();
    void firstRunChanged();
    void maxContextTokensChanged();
    void maxResponseTokensChanged();
    void temperatureChanged();
    void topPChanged();
    void chatHistoryLimitChanged();

private:
    void initTranslations();
    void ensureDirectories();

    QSettings *m_settings;
    QVariantMap m_translationsId;
    QVariantMap m_translationsEn;
};

#endif // SETTINGS_MANAGER_H
