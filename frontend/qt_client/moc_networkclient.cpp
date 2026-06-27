/****************************************************************************
** Meta object code from reading C++ file 'networkclient.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.9.5)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "networkclient.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'networkclient.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.5. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_NetworkClient_t {
    QByteArrayData data[65];
    char stringdata0[852];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_NetworkClient_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_NetworkClient_t qt_meta_stringdata_NetworkClient = {
    {
QT_MOC_LITERAL(0, 0, 13), // "NetworkClient"
QT_MOC_LITERAL(1, 14, 9), // "connected"
QT_MOC_LITERAL(2, 24, 0), // ""
QT_MOC_LITERAL(3, 25, 12), // "disconnected"
QT_MOC_LITERAL(4, 38, 9), // "errorText"
QT_MOC_LITERAL(5, 48, 7), // "message"
QT_MOC_LITERAL(6, 56, 13), // "statusMessage"
QT_MOC_LITERAL(7, 70, 14), // "loginSucceeded"
QT_MOC_LITERAL(8, 85, 8), // "username"
QT_MOC_LITERAL(9, 94, 11), // "loginFailed"
QT_MOC_LITERAL(10, 106, 17), // "registerSucceeded"
QT_MOC_LITERAL(11, 124, 14), // "registerFailed"
QT_MOC_LITERAL(12, 139, 19), // "onlineUsersReceived"
QT_MOC_LITERAL(13, 159, 5), // "users"
QT_MOC_LITERAL(14, 165, 20), // "groupMessageReceived"
QT_MOC_LITERAL(15, 186, 6), // "sender"
QT_MOC_LITERAL(16, 193, 22), // "privateMessageReceived"
QT_MOC_LITERAL(17, 216, 14), // "senderOrTarget"
QT_MOC_LITERAL(18, 231, 8), // "incoming"
QT_MOC_LITERAL(19, 240, 22), // "historyMessageReceived"
QT_MOC_LITERAL(20, 263, 4), // "kind"
QT_MOC_LITERAL(21, 268, 9), // "recipient"
QT_MOC_LITERAL(22, 278, 9), // "timestamp"
QT_MOC_LITERAL(23, 288, 16), // "bbsPostsReceived"
QT_MOC_LITERAL(24, 305, 16), // "QVector<BbsPost>"
QT_MOC_LITERAL(25, 322, 5), // "posts"
QT_MOC_LITERAL(26, 328, 21), // "bbsPostDetailReceived"
QT_MOC_LITERAL(27, 350, 7), // "BbsPost"
QT_MOC_LITERAL(28, 358, 4), // "post"
QT_MOC_LITERAL(29, 363, 17), // "QVector<BbsReply>"
QT_MOC_LITERAL(30, 381, 7), // "replies"
QT_MOC_LITERAL(31, 389, 20), // "bbsOperationFinished"
QT_MOC_LITERAL(32, 410, 2), // "ok"
QT_MOC_LITERAL(33, 413, 19), // "bbsDownloadFinished"
QT_MOC_LITERAL(34, 433, 4), // "path"
QT_MOC_LITERAL(35, 438, 15), // "connectToServer"
QT_MOC_LITERAL(36, 454, 4), // "host"
QT_MOC_LITERAL(37, 459, 4), // "port"
QT_MOC_LITERAL(38, 464, 20), // "disconnectFromServer"
QT_MOC_LITERAL(39, 485, 5), // "login"
QT_MOC_LITERAL(40, 491, 8), // "password"
QT_MOC_LITERAL(41, 500, 12), // "registerUser"
QT_MOC_LITERAL(42, 513, 18), // "requestOnlineUsers"
QT_MOC_LITERAL(43, 532, 16), // "sendGroupMessage"
QT_MOC_LITERAL(44, 549, 18), // "sendPrivateMessage"
QT_MOC_LITERAL(45, 568, 6), // "target"
QT_MOC_LITERAL(46, 575, 9), // "listPosts"
QT_MOC_LITERAL(47, 585, 8), // "viewPost"
QT_MOC_LITERAL(48, 594, 6), // "postId"
QT_MOC_LITERAL(49, 601, 10), // "createPost"
QT_MOC_LITERAL(50, 612, 5), // "title"
QT_MOC_LITERAL(51, 618, 7), // "content"
QT_MOC_LITERAL(52, 626, 14), // "attachmentPath"
QT_MOC_LITERAL(53, 641, 9), // "replyPost"
QT_MOC_LITERAL(54, 651, 23), // "uploadBbsPostAttachment"
QT_MOC_LITERAL(55, 675, 9), // "localPath"
QT_MOC_LITERAL(56, 685, 24), // "uploadBbsReplyAttachment"
QT_MOC_LITERAL(57, 710, 7), // "replyId"
QT_MOC_LITERAL(58, 718, 25), // "downloadBbsPostAttachment"
QT_MOC_LITERAL(59, 744, 13), // "saveDirectory"
QT_MOC_LITERAL(60, 758, 26), // "downloadBbsReplyAttachment"
QT_MOC_LITERAL(61, 785, 11), // "onReadyRead"
QT_MOC_LITERAL(62, 797, 13), // "onSocketError"
QT_MOC_LITERAL(63, 811, 28), // "QAbstractSocket::SocketError"
QT_MOC_LITERAL(64, 840, 11) // "socketError"

    },
    "NetworkClient\0connected\0\0disconnected\0"
    "errorText\0message\0statusMessage\0"
    "loginSucceeded\0username\0loginFailed\0"
    "registerSucceeded\0registerFailed\0"
    "onlineUsersReceived\0users\0"
    "groupMessageReceived\0sender\0"
    "privateMessageReceived\0senderOrTarget\0"
    "incoming\0historyMessageReceived\0kind\0"
    "recipient\0timestamp\0bbsPostsReceived\0"
    "QVector<BbsPost>\0posts\0bbsPostDetailReceived\0"
    "BbsPost\0post\0QVector<BbsReply>\0replies\0"
    "bbsOperationFinished\0ok\0bbsDownloadFinished\0"
    "path\0connectToServer\0host\0port\0"
    "disconnectFromServer\0login\0password\0"
    "registerUser\0requestOnlineUsers\0"
    "sendGroupMessage\0sendPrivateMessage\0"
    "target\0listPosts\0viewPost\0postId\0"
    "createPost\0title\0content\0attachmentPath\0"
    "replyPost\0uploadBbsPostAttachment\0"
    "localPath\0uploadBbsReplyAttachment\0"
    "replyId\0downloadBbsPostAttachment\0"
    "saveDirectory\0downloadBbsReplyAttachment\0"
    "onReadyRead\0onSocketError\0"
    "QAbstractSocket::SocketError\0socketError"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_NetworkClient[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      37,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      16,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  199,    2, 0x06 /* Public */,
       3,    0,  200,    2, 0x06 /* Public */,
       4,    1,  201,    2, 0x06 /* Public */,
       6,    1,  204,    2, 0x06 /* Public */,
       7,    1,  207,    2, 0x06 /* Public */,
       9,    1,  210,    2, 0x06 /* Public */,
      10,    1,  213,    2, 0x06 /* Public */,
      11,    1,  216,    2, 0x06 /* Public */,
      12,    1,  219,    2, 0x06 /* Public */,
      14,    2,  222,    2, 0x06 /* Public */,
      16,    3,  227,    2, 0x06 /* Public */,
      19,    5,  234,    2, 0x06 /* Public */,
      23,    1,  245,    2, 0x06 /* Public */,
      26,    2,  248,    2, 0x06 /* Public */,
      31,    2,  253,    2, 0x06 /* Public */,
      33,    1,  258,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      35,    2,  261,    2, 0x0a /* Public */,
      38,    0,  266,    2, 0x0a /* Public */,
      39,    2,  267,    2, 0x0a /* Public */,
      41,    2,  272,    2, 0x0a /* Public */,
      42,    0,  277,    2, 0x0a /* Public */,
      43,    1,  278,    2, 0x0a /* Public */,
      44,    2,  281,    2, 0x0a /* Public */,
      46,    0,  286,    2, 0x0a /* Public */,
      47,    1,  287,    2, 0x0a /* Public */,
      49,    3,  290,    2, 0x0a /* Public */,
      49,    2,  297,    2, 0x2a /* Public | MethodCloned */,
      53,    3,  302,    2, 0x0a /* Public */,
      53,    2,  309,    2, 0x2a /* Public | MethodCloned */,
      54,    2,  314,    2, 0x0a /* Public */,
      56,    2,  319,    2, 0x0a /* Public */,
      58,    2,  324,    2, 0x0a /* Public */,
      58,    1,  329,    2, 0x2a /* Public | MethodCloned */,
      60,    2,  332,    2, 0x0a /* Public */,
      60,    1,  337,    2, 0x2a /* Public | MethodCloned */,
      61,    0,  340,    2, 0x08 /* Private */,
      62,    1,  341,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QStringList,   13,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   15,    5,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::Bool,   17,    5,   18,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::QString,   20,   15,   21,    5,   22,
    QMetaType::Void, 0x80000000 | 24,   25,
    QMetaType::Void, 0x80000000 | 27, 0x80000000 | 29,   28,   30,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    5,   32,
    QMetaType::Void, QMetaType::QString,   34,

 // slots: parameters
    QMetaType::Void, QMetaType::QString, QMetaType::UShort,   36,   37,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    8,   40,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    8,   40,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   45,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   48,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::QString,   50,   51,   52,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   50,   51,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::QString,   48,   51,   52,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   48,   51,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   48,   55,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   57,   55,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   48,   59,
    QMetaType::Void, QMetaType::Int,   48,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,   57,   59,
    QMetaType::Void, QMetaType::Int,   57,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 63,   64,

       0        // eod
};

void NetworkClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        NetworkClient *_t = static_cast<NetworkClient *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->connected(); break;
        case 1: _t->disconnected(); break;
        case 2: _t->errorText((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->statusMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->loginSucceeded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->loginFailed((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->registerSucceeded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 7: _t->registerFailed((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 8: _t->onlineUsersReceived((*reinterpret_cast< const QStringList(*)>(_a[1]))); break;
        case 9: _t->groupMessageReceived((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 10: _t->privateMessageReceived((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 11: _t->historyMessageReceived((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5]))); break;
        case 12: _t->bbsPostsReceived((*reinterpret_cast< const QVector<BbsPost>(*)>(_a[1]))); break;
        case 13: _t->bbsPostDetailReceived((*reinterpret_cast< const BbsPost(*)>(_a[1])),(*reinterpret_cast< const QVector<BbsReply>(*)>(_a[2]))); break;
        case 14: _t->bbsOperationFinished((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 15: _t->bbsDownloadFinished((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 16: _t->connectToServer((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< quint16(*)>(_a[2]))); break;
        case 17: _t->disconnectFromServer(); break;
        case 18: _t->login((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 19: _t->registerUser((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 20: _t->requestOnlineUsers(); break;
        case 21: _t->sendGroupMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 22: _t->sendPrivateMessage((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 23: _t->listPosts(); break;
        case 24: _t->viewPost((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 25: _t->createPost((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 26: _t->createPost((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 27: _t->replyPost((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 28: _t->replyPost((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 29: _t->uploadBbsPostAttachment((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 30: _t->uploadBbsReplyAttachment((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 31: _t->downloadBbsPostAttachment((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 32: _t->downloadBbsPostAttachment((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 33: _t->downloadBbsReplyAttachment((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 34: _t->downloadBbsReplyAttachment((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 35: _t->onReadyRead(); break;
        case 36: _t->onSocketError((*reinterpret_cast< QAbstractSocket::SocketError(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 36:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAbstractSocket::SocketError >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (NetworkClient::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::connected)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::disconnected)) {
                *result = 1;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::errorText)) {
                *result = 2;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::statusMessage)) {
                *result = 3;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::loginSucceeded)) {
                *result = 4;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::loginFailed)) {
                *result = 5;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::registerSucceeded)) {
                *result = 6;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::registerFailed)) {
                *result = 7;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QStringList & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::onlineUsersReceived)) {
                *result = 8;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::groupMessageReceived)) {
                *result = 9;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & , const QString & , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::privateMessageReceived)) {
                *result = 10;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & , const QString & , const QString & , const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::historyMessageReceived)) {
                *result = 11;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QVector<BbsPost> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::bbsPostsReceived)) {
                *result = 12;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const BbsPost & , const QVector<BbsReply> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::bbsPostDetailReceived)) {
                *result = 13;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::bbsOperationFinished)) {
                *result = 14;
                return;
            }
        }
        {
            typedef void (NetworkClient::*_t)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkClient::bbsDownloadFinished)) {
                *result = 15;
                return;
            }
        }
    }
}

const QMetaObject NetworkClient::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_NetworkClient.data,
      qt_meta_data_NetworkClient,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *NetworkClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *NetworkClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_NetworkClient.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int NetworkClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 37)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 37;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 37)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 37;
    }
    return _id;
}

// SIGNAL 0
void NetworkClient::connected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void NetworkClient::disconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void NetworkClient::errorText(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void NetworkClient::statusMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void NetworkClient::loginSucceeded(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void NetworkClient::loginFailed(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void NetworkClient::registerSucceeded(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void NetworkClient::registerFailed(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void NetworkClient::onlineUsersReceived(const QStringList & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void NetworkClient::groupMessageReceived(const QString & _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void NetworkClient::privateMessageReceived(const QString & _t1, const QString & _t2, bool _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void NetworkClient::historyMessageReceived(const QString & _t1, const QString & _t2, const QString & _t3, const QString & _t4, const QString & _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)), const_cast<void*>(reinterpret_cast<const void*>(&_t4)), const_cast<void*>(reinterpret_cast<const void*>(&_t5)) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void NetworkClient::bbsPostsReceived(const QVector<BbsPost> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void NetworkClient::bbsPostDetailReceived(const BbsPost & _t1, const QVector<BbsReply> & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void NetworkClient::bbsOperationFinished(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void NetworkClient::bbsDownloadFinished(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
