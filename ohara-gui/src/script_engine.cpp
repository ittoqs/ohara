#include "script_engine.h"
#include <QDebug>
#undef slots
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>

namespace py = pybind11;

ScriptEngine::ScriptEngine(QObject *parent) : QObject(parent) {
    py::initialize_interpreter();
}

ScriptEngine::~ScriptEngine() {
    py::finalize_interpreter();
}

QString ScriptEngine::executeScript(const QString &scriptCode) {
    qDebug() << "ScriptEngine: Executing script\n" << scriptCode;
    try {
        py::exec(scriptCode.toStdString());
        return "Script executed successfully";
    } catch (std::exception &e) {
        return QString("Script Error: ") + e.what();
    }
}
