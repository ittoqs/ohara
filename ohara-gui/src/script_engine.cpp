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
        py::dict globals = py::globals();

        // Basic sandboxing: remove dangerous builtins
        if (globals.contains("__builtins__")) {
            py::object builtins = globals["__builtins__"];
            if (py::isinstance<py::dict>(builtins)) {
                py::dict bdict = builtins.cast<py::dict>();
                if (bdict.contains("__import__")) {
                    bdict["__import__"] = py::none();
                }
                if (bdict.contains("eval")) bdict["eval"] = py::none();
                if (bdict.contains("exec")) bdict["exec"] = py::none();
                if (bdict.contains("open")) bdict["open"] = py::none();
            } else if (py::hasattr(builtins, "__import__")) {
                setattr(builtins, "__import__", py::none());
                if (py::hasattr(builtins, "eval")) setattr(builtins, "eval", py::none());
                if (py::hasattr(builtins, "exec")) setattr(builtins, "exec", py::none());
                if (py::hasattr(builtins, "open")) setattr(builtins, "open", py::none());
            }
        } else {
            py::dict bdict;
            bdict["__import__"] = py::none();
            bdict["eval"] = py::none();
            bdict["exec"] = py::none();
            bdict["open"] = py::none();
            globals["__builtins__"] = bdict;
        }

        py::dict locals;
        py::exec(scriptCode.toStdString(), globals, locals);
        return "Script executed successfully";
    } catch (std::exception &e) {
        return QString("Script Error: ") + e.what();
    }
}
