#ifndef WASM_ENGINE_H
#define WASM_ENGINE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <wasm3.h>
#include <m3_env.h>

class WasmEngine : public QObject {
    Q_OBJECT

public:
    explicit WasmEngine(QObject *parent = nullptr);
    ~WasmEngine();

    // Execute a function from a WebAssembly binary.
    // The function is expected to take no arguments and return an integer for simplicity in this basic implementation.
    Q_INVOKABLE QString executeWasm(const QByteArray &wasmBinary, const QString &functionName);

private:
    IM3Environment m_env;
};

#endif // WASM_ENGINE_H
