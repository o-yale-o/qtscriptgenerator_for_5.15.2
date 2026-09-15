// API availability probe; writes results to a file (script has no stdout binding).
var lines = [];
function p(name, v) { lines.push(name + " = " + v); }

p("QTimer.singleShot", typeof QTimer.singleShot);
p("QScreen", typeof QScreen);
p("QGuiApplication", typeof QGuiApplication);
p("QApplication.primaryScreen", typeof QApplication.primaryScreen);
p("QApplication.desktop", typeof QApplication.desktop);
p("QDesktopWidget", typeof QDesktopWidget);
p("QDesktopServices.openUrl", typeof QDesktopServices.openUrl);
p("QNetworkAccessManager", typeof QNetworkAccessManager);
p("QNetworkRequest", typeof QNetworkRequest);
p("QNetworkReply", typeof QNetworkReply);
p("QHttp", typeof QHttp);
p("QTimeLine", typeof QTimeLine);
p("QLibraryInfo.location", typeof QLibraryInfo.location);
p("QHeaderView.proto.setSectionResizeMode", typeof QHeaderView.prototype.setSectionResizeMode);
p("QHeaderView.proto.setResizeMode", typeof QHeaderView.prototype.setResizeMode);
p("QGraphicsScene.proto.addWidget", typeof QGraphicsScene.prototype.addWidget);
p("QBoxLayout.proto.addWidget", typeof QBoxLayout.prototype.addWidget);
p("QGridLayout.proto.addWidget", typeof QGridLayout.prototype.addWidget);
p("QInputDialog.getInt", typeof QInputDialog.getInt);
p("QInputDialog.getInteger", typeof QInputDialog.getInteger);
p("QInputDialog.getDouble", typeof QInputDialog.getDouble);
p("QPixmap.grabWindow", typeof QPixmap.grabWindow);
p("QPixmap.grabWidget", typeof QPixmap.grabWidget);
p("QApplication.beep", typeof QApplication.beep);
p("QFileDialog.getSaveFileName", typeof QFileDialog.getSaveFileName);
p("QFileDialog.getOpenFileNames", typeof QFileDialog.getOpenFileNames);
p("QMessageBox.StandardButtons", typeof QMessageBox.StandardButtons);
p("QErrorMessage", typeof QErrorMessage);
p("QTranslator", typeof QTranslator);
p("QLocale.system", typeof QLocale.system);
p("qApp", typeof qApp);
p("QCoreApplication.instance", typeof QCoreApplication.instance);
p("QCoreApplication.installTranslator", typeof QCoreApplication.installTranslator);
p("QFontDialog.getFont", typeof QFontDialog.getFont);
p("QColorDialog.getColor", typeof QColorDialog.getColor);
p("QSignalMapper", typeof QSignalMapper);
p("QDir.home", typeof QDir.home);
p("QXmlStreamReader.NoError", typeof QXmlStreamReader.NoError);
p("QStyle.SP_DirClosedIcon", typeof QStyle.SP_DirClosedIcon);
p("Qt.ItemFlags", typeof Qt.ItemFlags);
p("Qt.WindowFlags", typeof Qt.WindowFlags);
p("Qt.Alignment", typeof Qt.Alignment);

var f = new QFile("C:/Users/Lusp/AppData/Local/Temp/dsh-Nhq1bn/_probe_out.txt");
if (!f.open(QIODevice.OpenMode(QIODevice.WriteOnly, QIODevice.Text)))
    throw "probe cannot open output file";
var ts = new QTextStream(f);
ts.writeString(lines.join("\n"));
ts.flush();
f.close();
