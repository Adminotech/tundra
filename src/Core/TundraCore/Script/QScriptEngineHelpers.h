/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   QScriptEngineHelpers.h
    @brief  QtScript conversion helpers.
    @todo   QtScript-specific functionality doesn't belong to TundraCore, move these elsewhere. */

#pragma once

#include "LoggingFunctions.h"

#include <QScriptEngine>
#include <QMetaType>
#include <QMetaEnum>

// The following functions help register a custom QObject-derived class to a QScriptEngine.
// See http://lists.trolltech.com/qt-interest/2007-12/thread00158-0.html .
template <typename Tp>
QScriptValue qScriptValueFromQObject(QScriptEngine *engine, Tp const &qobject)
{
    return engine->newQObject(qobject);
}

template <typename Tp>
void qScriptValueToQObject(const QScriptValue &value, Tp &qobject)
{
    qobject = qobject_cast<Tp>(value.toQObject());
    if (!qobject)
    {
        // qobject_cast has been observed to fail for some metatypes, such as Entity*, back in the days,
        // but this should not be case anymore so using qobject_cast. To see that there are no regressions
        // from this, check that if qobject_cast fails, so does dynamic_cast.
        Tp ptr = dynamic_cast<Tp>(value.toQObject());
        assert(!ptr);
        if (ptr)
        {
            ::LogError("qScriptValueToQObject: qobject_cast was null, but dynamic_cast was not for class!"
                + QString(ptr->metaObject()->className()));
        }
    }
}

template <typename Tp>
int qScriptRegisterQObjectMetaType(QScriptEngine *engine, const QScriptValue &prototype = QScriptValue()
#ifndef qdoc
    , Tp * = 0
#endif
    )
{
    return qScriptRegisterMetaType<Tp>(engine, qScriptValueFromQObject, qScriptValueToQObject, prototype);
}

/// Dereferences a shared_ptr<T> and converts it to a QScriptValue appropriate for the QtScript engine to access.
template<typename T>
QScriptValue qScriptValueFromBoostSharedPtr(QScriptEngine *engine, const shared_ptr<T> &ptr)
{
    return engine->newQObject(ptr.get());
}

/// Recovers the shared_ptr<T> of the given QScriptValue of an QObject that's stored in a shared_ptr.
/// For this to be safe, T must derive from enable_shared_from_this<T>.
template<typename T>
void qScriptValueToBoostSharedPtr(const QScriptValue &value, shared_ptr<T> &ptr)
{
    if (!value.isQObject())
    {
        ptr.reset();
        return;
    }

    T *obj = qobject_cast<T*>(value.toQObject());
    if (!obj)
    {
        ::LogError(QString(value.toQObject()->metaObject()->className()));
        ptr.reset();
        return;
    }
    ptr = static_pointer_cast<T>(obj->shared_from_this());
}

template<class T>
void qScriptRegisterQEnums(QScriptEngine *engine)
{
    QScriptValue enums = engine->newObject();
    const QMetaObject &mo = T::staticMetaObject;
    for(int i = mo.enumeratorOffset(); i < mo.enumeratorCount(); ++i)
    {
        const QMetaEnum enumerator = mo.enumerator(i);
        for(int j = 0 ; j < enumerator.keyCount(); ++j)
            enums.setProperty(enumerator.key(j), enumerator.value(j), QScriptValue::Undeletable | QScriptValue::ReadOnly);
    }
    engine->globalObject().setProperty(mo.className(), enums, QScriptValue::Undeletable | QScriptValue::ReadOnly);
}
