#include "vplantumlhelper.h"

#include <QDebug>
#include <QThread>

#include "vconfigmanager.h"
#include "utils/vprocessutils.h"

extern VConfigManager *g_config;

#define TaskIdProperty "PlantUMLTaskId"
#define TaskFormatProperty "PlantUMLTaskFormat"
#define TaskTimeStampProperty "PlantUMLTaskTimeStamp"

VPlantUMLHelper::VPlantUMLHelper(QObject *p_parent)
    : QObject(p_parent)
{
    m_customCmd = g_config->getPlantUMLCmd();
    if (m_customCmd.isEmpty()) {
        prepareCommand(m_program, m_args);
    }
}

VPlantUMLHelper::VPlantUMLHelper(const QString &p_jar, QObject *p_parent)
    : QObject(p_parent)
{
    m_customCmd = g_config->getPlantUMLCmd();
    if (m_customCmd.isEmpty()) {
        prepareCommand(m_program, m_args, p_jar);
    }
}

void VPlantUMLHelper::processAsync(int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QString &p_text)
{
    QProcess *process = new QProcess(this);
    process->setProperty(TaskIdProperty, p_id);
    process->setProperty(TaskTimeStampProperty, p_timeStamp);
    process->setProperty(TaskFormatProperty, p_format);
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(handleProcessFinished(int, QProcess::ExitStatus)));

    if (m_customCmd.isEmpty()) {
        QStringList args(m_args);
        args << ("-t" + p_format);
        qDebug() << m_program << args;
        process->start(m_program, args);
    } else {
        QString cmd(m_customCmd);
        cmd.replace("%0", p_format);
        qDebug() << cmd;
        process->start(cmd);
    }

    if (process->write(p_text.toUtf8()) == -1) {
        qWarning() << "fail to write to QProcess:" << process->errorString();
    }

    process->closeWriteChannel();
}

void VPlantUMLHelper::prepareCommand(QString &p_program,
                                     QStringList &p_args,
                                     const QString &p_jar) const
{
    p_program = "java";

    p_args << "-jar" << (p_jar.isEmpty() ? g_config->getPlantUMLJar() : p_jar);
    p_args << "-charset" << "UTF-8";

    int nbthread = QThread::idealThreadCount();
    p_args << "-nbthread" << QString::number(nbthread > 0 ? nbthread : 1);

    const QString &dot = g_config->getGraphvizDot();
    if (!dot.isEmpty()) {
        p_args << "-graphvizdot";
        p_args << dot;
    }

    p_args << "-pipe";
    p_args << g_config->getPlantUMLArgs();
}

void VPlantUMLHelper::handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    QProcess *process = static_cast<QProcess *>(sender());
    int id = process->property(TaskIdProperty).toInt();
    QString format = process->property(TaskFormatProperty).toString();
    TimeStamp timeStamp = process->property(TaskTimeStampProperty).toULongLong();
    qDebug() << QString("PlantUML finished: id %1 timestamp %2 format %3 exitcode %4 exitstatus %5")
                       .arg(id)
                       .arg(timeStamp)
                       .arg(format)
                       .arg(p_exitCode)
                       .arg(p_exitStatus);
    bool failed = true;
    if (p_exitStatus == QProcess::NormalExit) {
        if (p_exitCode < 0) {
            qWarning() << "PlantUML fail" << p_exitCode;
        } else {
            failed = false;
            QByteArray outBa = process->readAllStandardOutput();
            if (format == "svg") {
                emit resultReady(id, timeStamp, format, QString::fromLocal8Bit(outBa));
            } else {
                emit resultReady(id, timeStamp, format, QString::fromLocal8Bit(outBa.toBase64()));
            }
        }
    } else {
        qWarning() << "fail to start PlantUML process" << p_exitCode << p_exitStatus;
    }

    QByteArray errBa = process->readAllStandardError();
    if (!errBa.isEmpty()) {
        QString errStr(QString::fromLocal8Bit(errBa));
        if (failed) {
            qWarning() << "PlantUML stderr:" << errStr;
        } else {
            qDebug() << "PlantUML stderr:" << errStr;
        }
    }

    if (failed) {
        emit resultReady(id, timeStamp, format, "");
    }

    process->deleteLater();
}

bool VPlantUMLHelper::testPlantUMLJar(const QString &p_jar, QString &p_msg)
{
    VPlantUMLHelper inst(p_jar);
    QStringList args(inst.m_args);
    args << "-tsvg";

    QString testGraph("VNote->Markdown : hello");

    int exitCode = -1;
    QByteArray out, err;
    int ret = VProcessUtils::startProcess(inst.m_program,
                                          args,
                                          testGraph.toUtf8(),
                                          exitCode,
                                          out,
                                          err);

    p_msg = QString("Command: %1 %2\nExitCode: %3\nOutput: %4\nError: %5")
                   .arg(inst.m_program)
                   .arg(args.join(' '))
                   .arg(exitCode)
                   .arg(QString::fromLocal8Bit(out))
                   .arg(QString::fromLocal8Bit(err));

    return ret == 0 && exitCode == 0;
}

QByteArray VPlantUMLHelper::process(const QString &p_format, const QString &p_text)
{
    VPlantUMLHelper inst;

    int exitCode = -1;
    QByteArray out, err;
    int ret = -1;
    if (inst.m_customCmd.isEmpty()) {
        QStringList args(inst.m_args);
        args << ("-t" + p_format);
        ret = VProcessUtils::startProcess(inst.m_program,
                                          args,
                                          p_text.toUtf8(),
                                          exitCode,
                                          out,
                                          err);
    } else {
        QString cmd(inst.m_customCmd);
        cmd.replace("%0", p_format);
        ret = VProcessUtils::startProcess(cmd,
                                          p_text.toUtf8(),
                                          exitCode,
                                          out,
                                          err);
    }

    if (ret != 0 || exitCode < 0) {
        qWarning() << "PlantUML fail" << ret << exitCode << QString::fromLocal8Bit(err);
    }

    return out;
}
