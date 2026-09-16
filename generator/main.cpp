/****************************************************************************
**
** Copyright (C) 2008-2009 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the Qt Script Generator project on Qt Labs.
**
** $QT_BEGIN_LICENSE:LGPL$
** No Commercial Usage
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "main.h"
#include "asttoxml.h"
#include "reporthandler.h"
#include "typesystem.h"
#include "generatorset.h"
#include "fileout.h"

#include <QDir>

void displayHelp(GeneratorSet *generatorSet);

#include <QDebug>
#include <QRegularExpression>
int main(int argc, char *argv[])
{ 
	 // �����ȴ���ʵ����
    QCoreApplication app(argc, argv); 
    
    GeneratorSet *gs = GeneratorSet::getInstance();

    // �޸�Ϊ���ļ�ϵͳ������Դ Added at 2025-04-03 by Lusp
    //QString default_file = ":/trolltech/generator/qtscript_masterinclude.h";
    //QString default_system = ":/trolltech/generator/build_all.txt";
    
    QString appDir = QCoreApplication::applicationDirPath();
    qWarning() << "appDir=" << appDir;
    QString default_file = appDir + "/data/qtscript_masterinclude.h";
    QString default_system = appDir + "/data/build_all.txt";

    // �����Դ�ļ����
    auto checkResourceFile = [](const QString &path) -> QString {
        if (QFile::exists(path)) return path;
        
        // ���Ի��˵�Ƕ��ʽ��Դ
        QString embeddedPath = ":/trolltech/generator/" + QFileInfo(path).fileName();
        if (QFile(embeddedPath).exists()) {
            qWarning() << "Using embedded resource instead of:" << path;
            return embeddedPath;
        }
        return path;
    };
    // end 2025-04-03
    
    QString fileName;
    QString typesystemFileName;
    QString pp_file = ".preprocessed.tmp";
    QStringList rebuild_classes;
     
    QMap<QString, QString> args;

    int argNum = 0;
    for (int i=1; i<argc; ++i) {
        QString arg(argv[i]);
        arg = arg.trimmed();
        if( arg.startsWith("--") ) {
            int split = arg.indexOf("=");
            if( split > 0 )
                args[arg.mid(2).left(split-2)] = arg.mid(split + 1).trimmed();
            else
                args[arg.mid(2)] = QString();
        } else if( arg.startsWith("-")) {
            args[arg.mid(1)] = QString();
        } else {
            argNum++;
            args[QString("arg-%1").arg(argNum)] = arg;
        }
    }
    
    if (args.contains("no-suppress-warnings")) {
        TypeDatabase *db = TypeDatabase::instance();
        db->setSuppressWarnings(false);
    }
        
    if (args.contains("debug-level")) {
        QString level = args.value("debug-level");
        if (level == "sparse")
            ReportHandler::setDebugLevel(ReportHandler::SparseDebug);
        else if (level == "medium")
            ReportHandler::setDebugLevel(ReportHandler::MediumDebug);
        else if (level == "full")
            ReportHandler::setDebugLevel(ReportHandler::FullDebug);
    }      

    if (args.contains("dummy")) {
        FileOut::dummy = true;
    }

    if (args.contains("diff")) {
        FileOut::diff = true;
    }

    if (args.contains("license"))
        FileOut::license = true;

    if (args.contains("rebuild-only")) {
        QStringList classes = args.value("rebuild-only").split(",", QString::SkipEmptyParts);
        TypeDatabase::instance()->setRebuildClasses(classes);
    }

	  // Modified at 2025-04-03 by Lusp
    /* fileName = args.value("arg-1");
    typesystemFileName = args.value("arg-2");
    */
    fileName = checkResourceFile(args.value("arg-1", default_file));
    typesystemFileName = checkResourceFile(args.value("arg-2", default_system));
    if (!QFile::exists(fileName) || !QFile::exists(typesystemFileName)) {
    qCritical() << "Missing required files:";
    qCritical() << "  Header file:" << fileName;
    qCritical() << "  Typesystem file:" << typesystemFileName;
    qCritical() << "Search paths tried:";
    qCritical() << "  -" << appDir + "/data/";
    qCritical() << "  - Embedded resources";
    displayHelp(gs);
}
    // end 2025-04-03
    
    if (args.contains("arg-3"))
        displayHelp(gs);

    if (fileName.isEmpty())
        fileName = default_file;

    if (typesystemFileName.isEmpty())
        typesystemFileName = default_system;

    if (fileName.isEmpty() || typesystemFileName.isEmpty() )
        displayHelp(gs);

    if (!gs->readParameters(args))
        displayHelp(gs);

    printf("Please wait while source files are being generated...\n");

    if (!TypeDatabase::instance()->parseFile(typesystemFileName))
        qFatal("Cannot parse file: '%s'", qPrintable(typesystemFileName));

    if (!Preprocess::preprocess(fileName, pp_file, args.value("include-paths"))) {
        fprintf(stderr, "Preprocessor failed on file: '%s'\n", qPrintable(fileName));
        return 1;
    }

    // Qt 5.15 fix: the legacy rpp preprocessor does not rescan function-like
    // macros after token pasting, so QT_DEPRECATED_VERSION_X_5(15) etc.
    // survive as literal invocations inside class heads (e.g. the whole SAX
    // section of qxml.h), and the parser then silently drops those classes
    // (e.g. QXmlDefaultHandler, needed by QCAD's SVG importer). Strip the
    // leftovers from the preprocessed stream before parsing.
    {
        QFile pp(pp_file);
        if (pp.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString code = QString::fromLatin1(pp.readAll());
            pp.close();
            QRegularExpression leftover(
                "\\bQT_DEPRECATED_VERSION(_X)?_5\\s*\\(\\s*\\d+\\s*\\)\\s*");
            QString cleaned = code;
            cleaned.remove(leftover);
            if (cleaned != code) {
                QFile::remove(pp_file);
                QFile out(pp_file);
                out.open(QIODevice::WriteOnly | QIODevice::Text);
                out.write(cleaned.toLatin1());
                out.close();
            }
        }
    }

    if (args.contains("ast-to-xml")) {
	astToXML(pp_file);
	return 0;
    }

    gs->buildModel(pp_file);
    if (args.contains("dump-object-tree")) {
        gs->dumpObjectTree();
        return 0;
    }
    printf("%s\n", qPrintable(gs->generate()));

    printf("Done, %d warnings (%d known issues)\n", ReportHandler::warningCount(),
           ReportHandler::suppressedCount());
}


void displayHelp(GeneratorSet* generatorSet) {
#if defined(Q_OS_WIN32)
    char path_splitter = ';';
#else
    char path_splitter = ':';
#endif
    printf("Usage:\n  generator [options] header-file typesystem-file\n\n");
    printf("Available options:\n\n");
    printf("General:\n");
    printf("  --debug-level=[sparse|medium|full]        \n"
           "  --dump-object-tree                        \n"
           "  --help, -h or -?                          \n"
           "  --no-suppress-warnings                    \n"
           "  --output-directory=[dir]                  \n"
           "  --include-paths=<path>[%c<path>%c...]     \n"
           "  --print-stdout                            \n",
           path_splitter, path_splitter);
    
    printf("%s", qPrintable( generatorSet->usage()));
    exit(0);
}