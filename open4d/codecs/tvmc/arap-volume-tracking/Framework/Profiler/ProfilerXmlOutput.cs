//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Xml;

namespace Framework
{
    public class ProfilerXmlOutput : ProfilerOutputProvider
    {
        string fileName;
        XmlWriter writer;

        public ProfilerXmlOutput(string outputDir, string fileName = "profiler")
        {
            this.fileName = outputDir + "/" + fileName + ".xml";
        }

        public override void Header()
        {
            XmlWriterSettings setting = new XmlWriterSettings();
            setting.Indent = true;
            writer = XmlWriter.Create(fileName, setting);
            writer.WriteStartDocument();
            writer.WriteStartElement("profiler");
            writer.Flush();
        }

        public override void BeginRecordPrepare(int level, string name, string param)
        {
            writer.WriteStartElement("record");
            writer.WriteAttributeString(null, "name", null, name);
            if (param != null)
                writer.WriteAttributeString(null, "param", null, name);
            writer.WriteAttributeString(null, "timestamp", null, $"{DateTime.Now:yyMMdd:HH:mm:ss.ff}");
        }

        public override void BeginRecord(ProfilerMeasurementProvider provider)
        {
            writer.WriteStartElement(null, "start", null);
            writer.WriteAttributeString(null, "provider", null, provider.GetType().Name);
            writer.WriteAttributeString(null, "value", null, provider.LastBegin);
            writer.WriteEndElement();
        }

        public override void BeginRecordFinish(int level, string name, string param)
        {
            writer.Flush();
        }

        public override void EndRecord(ProfilerMeasurementProvider provider)
        {
            writer.WriteStartElement(null, "finished", null);
            writer.WriteAttributeString(null, "provider", null, provider.GetType().Name);
            writer.WriteAttributeString(null, "value", null, provider.LastEnd);
            writer.WriteEndElement();
        }

        public override void EndRecordFinish(int level, string name, string param)
        {
            writer.WriteEndElement();
            writer.Flush();
        }

        public override void Footer()
        {
            writer.WriteEndElement();
            writer.Close();
        }
    }
}
