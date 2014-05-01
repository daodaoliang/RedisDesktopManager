#include "defaulttransporter.h"
#include "abstractprotocol.h"

RedisClient::DefaultTransporter::DefaultTransporter(RedisClient::Connection *c)
    : RedisClient::AbstractTransporter(c), socket(nullptr)
{
}

void RedisClient::DefaultTransporter::init()
{
    if (!socket.isNull())
        return;

    executionTimer = QSharedPointer<QTimer>(new QTimer);
    executionTimer->setSingleShot(true);
    connect(executionTimer.data(), SIGNAL(timeout()), this, SLOT(executionTimeout()));

    socket = QSharedPointer<QTcpSocket>(new QTcpSocket());

    connect(socket.data(), SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(socket.data(), SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error(QAbstractSocket::SocketError)));

    connectToHost();
}

void RedisClient::DefaultTransporter::disconnect()
{
    if (!socket.isNull())
        socket->disconnectFromHost();
}

bool RedisClient::DefaultTransporter::connectToHost()
{
    socket->connectToHost(m_connection->config.host, m_connection->config.port);

    if (socket->waitForConnected(m_connection->config.connectionTimeout))
    {
        emit connected();
        emit logEvent(QString("%1 > connected").arg(m_connection->config.name));
        return true;
    }

    emit errorOccurred("Connection timeout");
    emit logEvent(QString("%1 > connection failed").arg(m_connection->config.name));
    return false;
}

void RedisClient::DefaultTransporter::runCommand(const Command &command)
{
    emit logEvent(QString("%1 > [runCommand] %2").arg(m_connection->config.name).arg(command.getRawString()));

    if (command.hasDbIndex()) {

        bool isDbSelected = m_connection->operations()->selectDb(command.getDbIndex());

        if (!isDbSelected) {
            emit errorOccurred("Can not select db for running command");
            return sendResponse();
        }
    }

    m_response.clear();
    m_isCommandRunning = true;
    runningCommand = command;
    executionTimer->start(m_connection->config.executeTimeout);

    // Send command
    QByteArray cmd = command.getByteRepresentation();
    socket->write(cmd);
    socket->flush();
}

void RedisClient::DefaultTransporter::readyRead()
{
    if (socket->bytesAvailable() <= 0) {
        return;
    }

    executionTimer->stop();
    readingBuffer = socket->readAll();
    m_response.appendToSource(readingBuffer);

    if (m_response.isValid()) {
        return sendResponse();
    } else {
        emit operationProgress(m_response.getLoadedItemsCount(), runningCommand.getOwner());
        executionTimer->start(m_connection->config.executeTimeout); //restart execution timer
    }
}

void RedisClient::DefaultTransporter::error(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::UnknownSocketError && connectToHost()) {
        return runCommand(runningCommand);
    }

    emit errorOccurred(
        QString("Connection error: %1").arg(socket->errorString())
        );

    return sendResponse();
}
