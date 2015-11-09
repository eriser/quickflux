#include <QtCore>
#include <QtQml>
#include <QVariant>
#include <QJSValue>
#include <QPointer>
#include "qfappdispatcher.h"

QFAppDispatcher::QFAppDispatcher(QObject *parent) : QObject(parent)
{
    m_dispatching = false;
    nextListenerId = 1;
}

QFAppDispatcher::~QFAppDispatcher()
{

}

void QFAppDispatcher::dispatch(QString type, QJSValue message)
{
    if (m_dispatching) {
        m_queue.enqueue(QPair<QString,QJSValue> (type,message) );
        return;
    }

    m_dispatching = true;
    emitDispatched(type,message);

    while (m_queue.size() > 0) {
        QPair<QString,QJSValue> pair = m_queue.dequeue();
        emitDispatched(pair.first,pair.second);
    }
    m_dispatching = false;
}

void QFAppDispatcher::waitFor(QList<int> ids)
{
    if (!m_dispatching)
        return;

    bool shouldWait = false;

    for (int i = 0 ; i < ids.size() ; i++) {
        int id = ids[i];
        if (pendingListeners.contains(id)) {
            shouldWait = true;
            break;
        }
    }

    if (!shouldWait) {
        return;
    }

    waitingListeners.append(dispatchingListenerId);
    invokeListeners();
    waitingListeners.removeLast();

    for (int i = 0 ; i < ids.size() ; i++) {
        int id = ids[i];
        if (waitingListeners.indexOf(id) >= 0) {
            qWarning() << "AppDispatcher: Cyclic dependency detected";
            break;
        }
    }
}

int QFAppDispatcher::addListener(QJSValue callback)
{
    m_listeners[nextListenerId] = callback;
    return nextListenerId++;
}

void QFAppDispatcher::removeListener(int id)
{
    if (m_listeners.contains(id))
        m_listeners.remove(id);
}

QFAppDispatcher *QFAppDispatcher::instance(QQmlEngine *engine)
{
    QFAppDispatcher *dispatcher = qobject_cast<QFAppDispatcher*>(singletonObject(engine,"QuickFlux",1,0,"AppDispatcher"));

    return dispatcher;
}

QObject *QFAppDispatcher::singletonObject(QQmlEngine *engine, QString package, int versionMajor, int versionMinor, QString typeName)
{
    QString pattern  = "import QtQuick 2.0\nimport %1 %2.%3;QtObject { property var object : %4 }";

    QString qml = pattern.arg(package).arg(versionMajor).arg(versionMinor).arg(typeName);

    QObject* holder = 0;

    QQmlComponent comp (engine);
    comp.setData(qml.toUtf8(),QUrl());
    holder = comp.create();

    if (!holder) {
        qWarning() << QString("QuickFlux: Failed to gain singleton object: %1").arg(typeName);
        qWarning() << QString("Error: ") << comp.errorString();
        return 0;
    }

    QObject*object = holder->property("object").value<QObject*>();
    holder->deleteLater();

    if (!object) {
        qWarning() << QString("QuickFlux: Failed to gain singleton object: %1").arg(typeName);
        qWarning() << QString("Error: Unknown");
    }

    return object;
}

void QFAppDispatcher::emitDispatched(QString type, QJSValue message)
{
    dispatchingMessage = message;
    dispatchingMessageType = type;
    pendingListeners.clear();
    waitingListeners.clear();

    QMapIterator<int, QJSValue> iter(m_listeners);
    while (iter.hasNext()) {
        iter.next();
        pendingListeners[iter.key()] = true;
    }

    invokeListeners();

    emit dispatched(type,message);
}

void QFAppDispatcher::invokeListeners()
{
    QJSValueList args;
    args << dispatchingMessageType;
    args << dispatchingMessage;

    while (!pendingListeners.empty()) {
        int next = pendingListeners.firstKey();
        pendingListeners.remove(next);
        dispatchingListenerId = next;

        QJSValue callback = m_listeners[next];

        QJSValue ret = callback.call(args);

        if (ret.isError()) {
            QString message = QString("%1:%2: %3: %4")
                              .arg(ret.property("fileName").toString())
                              .arg(ret.property("lineNumber").toString())
                              .arg(ret.property("name").toString())
                              .arg(ret.property("message").toString());
            qWarning() << message;
        }

    }
}

static QObject *provider(QQmlEngine *engine, QJSEngine *scriptEngine) {
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);

    QFAppDispatcher* object = new QFAppDispatcher();

    return object;
}

class QFAppDispatcherRegisterHelper {

public:
    QFAppDispatcherRegisterHelper() {
        qmlRegisterSingletonType<QFAppDispatcher>("QuickFlux", 1, 0, "AppDispatcher", provider);
    }
};

static QFAppDispatcherRegisterHelper registerHelper;
