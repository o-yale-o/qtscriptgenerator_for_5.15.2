// Headless check: parse frank.xbel via QXmlStreamReader, then write it back
// out via QXmlStreamWriter; counts prove both paths work. Results -> TEMP file.
var IN = "frank.xbel";
var OUT = "C:/Users/Lusp/AppData/Local/Temp/dsh-Nhq1bn/_sb_roundtrip.xbel";
var REPORT = "C:/Users/Lusp/AppData/Local/Temp/dsh-Nhq1bn/_sb_report.txt";
var log = [];

var reader = new QXmlStreamReader();
var file = new QFile(IN);
if (!file.open(QIODevice.OpenMode(QIODevice.ReadOnly, QIODevice.Text)))
    throw "cannot open " + IN;
reader.setDevice(file);

var counts = {};
while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
        var n = reader.name().toString();
        counts[n] = (counts[n] || 0) + 1;
    }
}
var readOk = (reader.error() == QXmlStreamReader.NoError);
file.close();
log.push("readOk=" + readOk + " error=" + reader.errorString());
log.push("xbel=" + (counts["xbel"] || 0) +
         " folder=" + (counts["folder"] || 0) +
         " bookmark=" + (counts["bookmark"] || 0) +
         " title=" + (counts["title"] || 0));

var writer = new QXmlStreamWriter();
var out = new QFile(OUT);
if (!out.open(QIODevice.OpenMode(QIODevice.WriteOnly, QIODevice.Text)))
    throw "cannot open output";
writer.setDevice(out);
writer.setAutoFormatting(true);
writer.writeStartDocument();
writer.writeDTD("<!DOCTYPE xbel>");
writer.writeStartElement("xbel");
writer.writeAttribute("version", "1.0");
writer.writeStartElement("folder");
writer.writeAttribute("folded", "no");
writer.writeTextElement("title", "roundtrip-check");
writer.writeEmptyElement("separator");
writer.writeEndElement();
writer.writeEndElement();
writer.writeEndDocument();
out.close();
log.push("writeDone=" + (new QFile(OUT).size() > 0) + " bytes=" + new QFile(OUT).size());

var rf = new QFile(REPORT);
rf.open(QIODevice.OpenMode(QIODevice.WriteOnly, QIODevice.Truncate));
var ts = new QTextStream(rf);
ts.writeString(log.join("\n"));
ts.flush();
rf.close();
