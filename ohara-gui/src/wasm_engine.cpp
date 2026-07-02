#include "wasm_engine.h"
#include <QDebug>
#include <iostream>

WasmEngine::WasmEngine(QObject *parent) : QObject(parent) {
    m_env = m3_NewEnvironment();
}

WasmEngine::~WasmEngine() {
    if (m_env) {
        m3_FreeEnvironment(m_env);
    }
}

QString WasmEngine::executeWasm(const QByteArray &wasmBinary, const QString &functionName) {
    qDebug() << "WasmEngine: Executing WASM function:" << functionName;

    if (!m_env) {
        return "WASM Error: Environment not initialized.";
    }

    IM3Runtime runtime = m3_NewRuntime(m_env, 1024 * 64, nullptr);
    if (!runtime) {
        return "WASM Error: Failed to create runtime.";
    }

    IM3Module module;
    M3Result result = m3_ParseModule(m_env, &module, (const uint8_t*)wasmBinary.constData(), wasmBinary.size());
    if (result) {
        m3_FreeRuntime(runtime);
        return QString("WASM Error: Failed to parse module: %1").arg(result);
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        m3_FreeRuntime(runtime);
        return QString("WASM Error: Failed to load module: %1").arg(result);
    }

    IM3Function f;
    result = m3_FindFunction(&f, runtime, functionName.toStdString().c_str());
    if (result) {
        m3_FreeRuntime(runtime);
        return QString("WASM Error: Failed to find function '%1': %2").arg(functionName).arg(result);
    }

    result = m3_CallV(f);
    if (result) {
        QString errRet = QString("WASM Error: Execution failed: %1").arg(result);
        M3ErrorInfo info;
        m3_GetErrorInfo(runtime, &info);
        if (info.message) {
             errRet += QString(" - %1").arg(info.message);
        }
        m3_FreeRuntime(runtime);
        return errRet;
    }

    // Basic support for returning a single integer result if the function returns one
    uint32_t ret = 0;
    if (m3_GetRetCount(f) > 0) {
        // Retrieve result safely
        // In a real implementation we would check the return type, but for now we assume integer
        const void *ptrs[] = { &ret };
        result = m3_GetResults(f, 1, ptrs);
        if (result) {
             m3_FreeRuntime(runtime);
             return QString("WASM executed successfully, but failed to get result: %1").arg(result);
        }
        m3_FreeRuntime(runtime);
        return QString("WASM execution successful. Result: %1").arg(ret);
    }

    m3_FreeRuntime(runtime);
    return "WASM execution successful.";
}
