#ifndef JS_ENGINE_H
#define JS_ENGINE_H

#include <QObject>
#include <QString>
#include <QJSEngine>

class JsEngine : public QObject {
    Q_OBJECT

public:
    explicit JsEngine(QObject *parent = nullptr);
    ~JsEngine();

    Q_INVOKABLE QString executeScript(const QString &scriptCode);

private:
    QJSEngine m_engine;
};

#endif // JS_ENGINE_H
