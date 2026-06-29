#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include <QObject>
#include <QString>

class ScriptEngine : public QObject {
    Q_OBJECT

public:
    explicit ScriptEngine(QObject *parent = nullptr);
    ~ScriptEngine();

    Q_INVOKABLE QString executeScript(const QString &scriptCode);
};

#endif // SCRIPT_ENGINE_H
